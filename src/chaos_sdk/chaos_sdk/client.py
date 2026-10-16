"""All requests return rclpy futures; the application owns node/executor lifetime."""
import math
import time
import uuid

from builtin_interfaces.msg import Time
from rclpy.action import ActionClient
from rclpy.qos import DurabilityPolicy, QoSProfile
from rclpy.task import Future
from chaos_interfaces.action import Flight, NavigatePose
from chaos_interfaces.msg import RobotState, VelocityCommand
from chaos_interfaces.srv import ControlLease, GetDescription, Stop


def _map_future(source, transform):
    result = Future()

    def completed(future):
        try:
            result.set_result(transform(future.result()))
        except Exception as error:
            result.set_exception(error)
    source.add_done_callback(completed)
    return result


class Robot:
    """Client for one asset. Tokens are never acquired or renewed implicitly.

    ``describe()`` caches the configured base frame and command timeout. A
    caller may also supply base_frame and command_timeout_s explicitly when
    constructing a client, but the runtime remains authoritative.
    ``command_ttl_s`` bounds when a service or action may start, using Unix
    time rather than the ROS simulation clock. It is not a task duration or
    client-side response timeout. Individual calls can override it with ttl_s.
    """
    def __init__(self, node, asset_id, namespace=None, base_frame=None, command_timeout_s=0.5,
                 command_ttl_s=5.0):
        self.command_ttl_s = self._validate_ttl(command_ttl_s)
        self.node = node
        self.asset_id = asset_id
        self.namespace = (namespace or f'/robots/{asset_id}').rstrip('/')
        self.base_frame = base_frame
        self.command_timeout_s = command_timeout_s
        self.lease_token = ''
        self.owner = ''
        self.last_state = None
        endpoint = lambda name: self.namespace + '/' + name
        self._description = node.create_client(GetDescription, endpoint('description'))
        self._lease = node.create_client(ControlLease, endpoint('control/lease'))
        self._stop = node.create_client(Stop, endpoint('motion/stop'))
        self._velocity = node.create_publisher(VelocityCommand, endpoint('motion/velocity'), 1)
        self._state = node.create_subscription(
            RobotState, endpoint('state'), self._on_state,
            QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
        self._navigation = ActionClient(node, NavigatePose, endpoint('navigation/to_pose'))
        self._takeoff = ActionClient(node, Flight, endpoint('flight/takeoff'))
        self._land = ActionClient(node, Flight, endpoint('flight/land'))

    def _on_state(self, message):
        self.last_state = message

    def state(self):
        """Return latest received RobotState, or None before the first sample."""
        return self.last_state

    def wait_ready(self, timeout_s=5.0):
        """Wait for description service discovery, not for robot readiness."""
        return self._description.wait_for_service(timeout_sec=timeout_s)

    def describe(self):
        def update(response):
            self.base_frame = response.base_frame
            self.command_timeout_s = response.command_timeout_s
            return response
        return _map_future(self._description.call_async(GetDescription.Request()), update)

    @staticmethod
    def _validate_ttl(value):
        if (isinstance(value, bool) or not isinstance(value, (int, float))
                or not 0 < value <= 30):
            raise ValueError('command TTL must be finite and in (0, 30] seconds')
        return float(value)

    def _deadline(self, ttl_s):
        ttl = self.command_ttl_s if ttl_s is None else self._validate_ttl(ttl_s)
        deadline_ns = time.time_ns() + max(1, int(ttl * 1_000_000_000))
        seconds, nanoseconds = divmod(deadline_ns, 1_000_000_000)
        return Time(sec=seconds, nanosec=nanoseconds)

    def _control(self, operation, duration_s=5.0, owner=None, ttl_s=None):
        request = ControlLease.Request()
        request.expires_at = self._deadline(ttl_s)
        request.operation = operation
        request.owner = owner if owner is not None else self.owner
        request.token = self.lease_token
        request.duration_s = float(duration_s)

        def update(response):
            released_with_stop_failure = (operation == 'release' and response.code == 'STOP_FAILED'
                                          and not response.token)
            if response.success or released_with_stop_failure:
                self.lease_token = response.token
                self.owner = request.owner if response.token else ''
            return response
        return _map_future(self._lease.call_async(request), update)

    def acquire(self, owner, duration_s=5.0, *, ttl_s=None):
        return self._control('acquire', duration_s, owner, ttl_s)

    def renew(self, duration_s=5.0, *, ttl_s=None):
        return self._control('renew', duration_s, ttl_s=ttl_s)

    def release(self, *, ttl_s=None):
        return self._control('release', 0.0, ttl_s=ttl_s)

    def _require_token(self):
        if not self.lease_token:
            raise ValueError('Acquire control and await its successful response first')

    def velocity(self, x=0.0, y=0.0, z=0.0, yaw=0.0, valid_for_s=None, request_id=None):
        """Publish one bounded velocity sample; returns its correlation ID.

        This is enqueueing, not completion. Subscribe to command_results when
        acceptance acknowledgement is needed. Stream fresh samples deliberately;
        missing samples cause the runtime watchdog to stop motion.
        """
        self._require_token()
        if not self.base_frame:
            raise ValueError('Call describe() first or supply base_frame')
        values = [float(v) for v in (x, y, z, yaw)]
        if not all(math.isfinite(v) for v in values):
            raise ValueError('Velocity must be finite')
        message = VelocityCommand()
        message.header.stamp = self.node.get_clock().now().to_msg()
        message.header.frame_id = self.base_frame
        message.request_id = request_id or str(uuid.uuid4())
        message.lease_token = self.lease_token
        message.valid_for_s = float(valid_for_s if valid_for_s is not None else self.command_timeout_s)
        message.velocity.linear.x, message.velocity.linear.y, message.velocity.linear.z = values[:3]
        message.velocity.angular.z = values[3]
        self._velocity.publish(message)
        return message.request_id

    def stop(self, request_id=None, *, ttl_s=None):
        self._require_token()
        request = Stop.Request()
        request.expires_at = self._deadline(ttl_s)
        request.request_id = request_id or str(uuid.uuid4())
        request.lease_token = self.lease_token
        return self._stop.call_async(request)

    def navigate(self, target, feedback_callback=None, *, ttl_s=None):
        """Return future of a goal handle; use get_result_async/cancel_goal_async."""
        self._require_token()
        goal = NavigatePose.Goal()
        goal.expires_at = self._deadline(ttl_s)
        goal.lease_token = self.lease_token
        goal.target = target
        return self._navigation.send_goal_async(goal, feedback_callback=feedback_callback)

    def takeoff(self, altitude_m, feedback_callback=None, *, ttl_s=None):
        self._require_token()
        goal = Flight.Goal()
        goal.expires_at = self._deadline(ttl_s)
        goal.lease_token = self.lease_token
        goal.altitude_m = float(altitude_m)
        return self._takeoff.send_goal_async(goal, feedback_callback=feedback_callback)

    def land(self, feedback_callback=None, *, ttl_s=None):
        self._require_token()
        goal = Flight.Goal()
        goal.expires_at = self._deadline(ttl_s)
        goal.lease_token = self.lease_token
        goal.altitude_m = 0.0
        return self._land.send_goal_async(goal, feedback_callback=feedback_callback)

    def close(self):
        """Destroy client resources. Explicitly await release() before closing."""
        for client in (self._description, self._lease, self._stop):
            self.node.destroy_client(client)
        for action in (self._navigation, self._takeoff, self._land):
            action.destroy()
        self.node.destroy_publisher(self._velocity)
        self.node.destroy_subscription(self._state)
