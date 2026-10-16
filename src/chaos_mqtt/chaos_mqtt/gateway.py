"""Fixed MQTT commands translated into the asset runtime's public API."""

import json
import time

import rclpy
from action_msgs.msg import GoalStatus
from builtin_interfaces.msg import Time
from rclpy.action import ActionClient
from rclpy.node import Node
from rosidl_runtime_py.convert import message_to_ordereddict
from std_msgs.msg import String

from chaos_interfaces.action import Flight, NavigatePose
from chaos_interfaces.msg import CommandResult, RobotState, VelocityCommand
from chaos_interfaces.srv import ControlLease, GetDescription, Stop

from .protocol import (CommandError, Correlations, parse_command, recover_identity,
                       response, wire_value)


class Gateway(Node):
    def __init__(self):
        super().__init__('mqtt_gateway')
        self.declare_parameter('asset_json', '{}')
        self.declare_parameter('maximum_clock_skew_ms', 5000)
        self.declare_parameter('request_timeout_s', 5.0)
        self.declare_parameter('action_timeout_s', 300.0)
        self.asset = json.loads(self.get_parameter('asset_json').value)
        self.asset_id = self.asset.get('id', self.asset.get('asset_id'))
        if not self.asset_id or not isinstance(self.asset.get('capabilities'), list):
            raise ValueError('asset_json requires an id and capabilities')
        self.clock_skew = self.get_parameter('maximum_clock_skew_ms').value
        self.request_timeout = self.get_parameter('request_timeout_s').value
        self.action_timeout = self.get_parameter('action_timeout_s').value
        if not 100 <= self.clock_skew <= 30000:
            raise ValueError('maximum_clock_skew_ms must be in 100..30000')
        if not 0.1 <= self.request_timeout <= 600 or not 1 <= self.action_timeout <= 86400:
            raise ValueError('invalid gateway timeout')
        self.records = Correlations()
        self.result_pub = self.create_publisher(String, 'mqtt/command/result', 32)
        self.event_pub = self.create_publisher(String, 'mqtt/task_events', 32)
        self.state_pub = self.create_publisher(String, 'mqtt/state', 32)
        self.velocity_pub = self.create_publisher(VelocityCommand, 'motion/velocity', 32)
        self.create_subscription(String, 'mqtt/command', self._command, 32)
        self.create_subscription(CommandResult, 'command_results', self._velocity_result, 32)
        self.create_subscription(RobotState, 'state', self._state, 32)
        self.describe_client = self.create_client(GetDescription, 'description')
        self.lease_client = self.create_client(ControlLease, 'control/lease')
        self.stop_client = self.create_client(Stop, 'motion/stop')
        self.action_clients = {
            'navigation.to_pose': ActionClient(self, NavigatePose, 'navigation/to_pose'),
            'flight.takeoff': ActionClient(self, Flight, 'flight/takeoff'),
            'flight.land': ActionClient(self, Flight, 'flight/land'),
        }
        # Subscribe directly so a missing goal acknowledgment does not leave a
        # per-request feedback callback in rclpy's shared ActionClient forever.
        for command, action_type in (('navigation.to_pose', NavigatePose),
                                     ('flight.takeoff', Flight), ('flight.land', Flight)):
            self.create_subscription(
                action_type.Impl.FeedbackMessage,
                command.replace('.', '/') + '/_action/feedback',
                lambda message, command=command: self._feedback(command, message), 32)
        self.create_timer(0.1, self._expire)

    @staticmethod
    def _publish(publisher, value):
        publisher.publish(String(data=json.dumps(wire_value(value), separators=(',', ':'), allow_nan=False)))

    def _state(self, message):
        self._publish(self.state_pub, message_to_ordereddict(message))

    def _command(self, message):
        identity = recover_identity(message.data)
        if identity:
            request_id, _ = identity
            if request_id in self.records.completed:
                reply, event = self.records.completed[request_id]
                if reply:
                    self._publish(self.result_pub, reply)
                if event:
                    self._publish(self.event_pub, event)
                return
            if request_id in self.records.pending:
                reply = self.records.pending[request_id].reply
                if reply:
                    self._publish(self.result_pub, reply)
                return
        try:
            command = parse_command(message.data, self.asset['capabilities'],
                                    time.time_ns() // 1_000_000, self.clock_skew)
            self.records.start(command, time.monotonic(), self.request_timeout)
        except CommandError as error:
            if identity:
                reply = response(*identity, success=False, code=error.code, message=str(error))
                self._publish(self.result_pub, reply)
                self.records.finish(identity[0], reply)
            else:
                self.get_logger().warning(f'Dropped uncorrelatable MQTT command: {error.code}')
            return
        try:
            self._dispatch(command)
        except CommandError as error:
            self._finish_error(command.request_id, error.code, str(error))
        except Exception as error:
            self._finish_error(command.request_id, 'DISPATCH_ERROR', str(error))

    def _dispatch(self, command):
        name, args = command.name, command.arguments
        if command.deadline_ms <= time.time_ns() // 1_000_000:
            raise CommandError('EXPIRED', 'command expired before dispatch')
        if name == 'system.describe':
            self._service(command, self.describe_client, GetDescription.Request())
        elif name.startswith('control.'):
            request = ControlLease.Request()
            request.operation = name.split('.')[1]
            request.owner = args['owner']
            request.token = args['token']
            request.duration_s = args['duration_s']
            request.expires_at = self._deadline(command)
            self._service(command, self.lease_client, request)
        elif name == 'motion.stop':
            request = Stop.Request()
            request.request_id = command.request_id
            request.lease_token = args['lease_token']
            request.expires_at = self._deadline(command)
            self._service(command, self.stop_client, request)
        elif name == 'motion.velocity':
            remaining = (command.deadline_ms - time.time_ns() // 1_000_000) / 1000.0
            if remaining <= 0:
                raise CommandError('EXPIRED', 'velocity expired before dispatch')
            request = VelocityCommand()
            request.header.stamp = self.get_clock().now().to_msg()
            request.header.frame_id = self.asset.get('frames', {}).get('base', 'base_link')
            request.request_id = command.request_id
            request.lease_token = args['lease_token']
            timeout = self.asset.get('limits', {}).get('command_timeout_s', 0.5)
            if args['valid_for_s'] > timeout:
                raise CommandError('INVALID_ARGUMENT', 'valid_for_s exceeds asset command_timeout_s')
            request.valid_for_s = min(remaining, args['valid_for_s'])
            for kind in ('linear', 'angular'):
                for axis, value in args['velocity'][kind].items():
                    setattr(getattr(request.velocity, kind), axis, value)
            self.velocity_pub.publish(request)
        else:
            self._action(command)

    @staticmethod
    def _deadline(command):
        # Unix UTC, deliberately independent of /clock. Rounding down cannot
        # grant the remote command any additional validity.
        deadline_ns = int(command.deadline_ms) * 1_000_000
        seconds, nanoseconds = divmod(deadline_ns, 1_000_000_000)
        if not 0 < seconds < 2 ** 31:
            raise CommandError('INVALID_ARGUMENT', 'command deadline is outside ROS Time range')
        return Time(sec=seconds, nanosec=nanoseconds)

    def _service(self, command, client, request):
        if not client.service_is_ready():
            self._finish_error(command.request_id, 'UNAVAILABLE', 'runtime service is not ready')
            return
        future = client.call_async(request)
        self.records.pending[command.request_id].future = future
        future.add_done_callback(lambda done: self._service_done(command.request_id, done))

    def _service_done(self, request_id, future):
        pending = self.records.pending.get(request_id)
        if not pending:
            return
        try:
            result = future.result()
            data = dict(message_to_ordereddict(result))
            success = bool(data.pop('success', True))
            code = data.pop('code', 'OK')
            message = data.pop('message', '')
            reply = response(request_id, pending.command.name, success=success, code=code,
                             message=message, data=data)
            self._publish(self.result_pub, reply)
            self.records.finish(request_id, reply)
        except Exception as error:
            self._finish_error(request_id, 'RUNTIME_ERROR', str(error))

    def _velocity_result(self, message):
        pending = self.records.pending.get(message.request_id)
        if not pending or pending.command.name != 'motion.velocity':
            return
        reply = response(message.request_id, pending.command.name, success=message.success,
                         code=message.code, message=message.message)
        self._publish(self.result_pub, reply)
        self.records.finish(message.request_id, reply)

    def _action(self, command):
        client = self.action_clients[command.name]
        if not client.server_is_ready():
            self._finish_error(command.request_id, 'UNAVAILABLE', 'runtime action is not ready')
            return
        if command.name == 'navigation.to_pose':
            goal = NavigatePose.Goal()
            target = command.arguments['target']
            goal.target.header.stamp = self.get_clock().now().to_msg()
            goal.target.header.frame_id = target['frame_id']
            for axis, value in target['position'].items():
                setattr(goal.target.pose.position, axis, value)
            for axis, value in target['orientation'].items():
                setattr(goal.target.pose.orientation, axis, value)
        else:
            goal = Flight.Goal()
            goal.altitude_m = command.arguments['altitude_m']
        goal.lease_token = command.arguments['lease_token']
        goal.expires_at = self._deadline(command)
        future = client.send_goal_async(goal)
        self.records.pending[command.request_id].future = future
        future.add_done_callback(lambda done: self._goal_response(command.request_id, done))

    def _goal_response(self, request_id, future):
        pending = self.records.pending.get(request_id)
        if not pending:
            return
        try:
            handle = future.result()
            if not handle.accepted:
                self._finish_error(request_id, 'REJECTED', 'runtime rejected action goal')
                return
            pending.accepted = True
            pending.goal_handle = handle
            pending.expires_at = time.monotonic() + self.action_timeout
            pending.reply = response(request_id, pending.command.name, success=True, code='ACCEPTED',
                                     message='action accepted; completion arrives on task_events',
                                     phase='accepted', goal_id=bytes(handle.goal_id.uuid).hex())
            self._publish(self.result_pub, pending.reply)
            pending.future = handle.get_result_async()
            pending.future.add_done_callback(lambda done: self._action_done(request_id, done))
        except Exception as error:
            self._finish_error(request_id, 'RUNTIME_ERROR', str(error))

    def _event(self, request_id, phase, data):
        pending = self.records.pending.get(request_id)
        if not pending:
            return None
        event = {'version': 1, 'asset_id': self.asset_id, 'request_id': request_id,
                 'command': pending.command.name, 'timestamp_ms': time.time_ns() // 1_000_000,
                 'phase': phase, 'terminal': phase == 'result', **data}
        if pending.goal_handle:
            event['goal_id'] = bytes(pending.goal_handle.goal_id.uuid).hex()
        self._publish(self.event_pub, event)
        return event

    def _feedback(self, command, message):
        goal_id = bytes(message.goal_id.uuid)
        for request_id, pending in self.records.pending.items():
            if (pending.command.name == command and pending.goal_handle
                    and bytes(pending.goal_handle.goal_id.uuid) == goal_id):
                self._event(request_id, 'feedback', {'feedback': message_to_ordereddict(message.feedback)})
                break

    def _action_done(self, request_id, future):
        pending = self.records.pending.get(request_id)
        if not pending:
            return
        try:
            result = future.result()
            success = result.status == GoalStatus.STATUS_SUCCEEDED and result.result.success
            event = self._event(request_id, 'result', {
                'ok': success, 'code': result.result.code, 'message': result.result.message,
                'ros_goal_status': result.status, 'execution_state': 'known',
            })
            self._complete_action(pending, event)
        except Exception as error:
            self._finish_error(request_id, 'RUNTIME_ERROR', str(error))

    def _finish_error(self, request_id, code, message):
        pending = self.records.pending.get(request_id)
        if not pending:
            return
        if pending.accepted:
            event = self._event(request_id, 'result', {
                'ok': False, 'code': code, 'message': message, 'execution_state': 'unknown',
            })
            self._complete_action(pending, event)
        else:
            reply = response(request_id, pending.command.name, success=False, code=code,
                             message=message, execution_state='unknown')
            self._publish(self.result_pub, reply)
            self.records.finish(request_id, reply)

    def _complete_action(self, pending, event):
        # Task events remain compatible, while the correlated response path
        # also receives completion. The Rust bridge can now replace ACCEPTED
        # in its replay cache even if the task_events subscriber was offline.
        extra = {key: event[key] for key in ('goal_id', 'ros_goal_status', 'execution_state')
                 if key in event}
        reply = response(pending.command.request_id, pending.command.name,
                         success=event['ok'], code=event['code'], message=event['message'],
                         **extra)
        self._publish(self.result_pub, reply)
        self.records.finish(pending.command.request_id, reply, event)

    def _expire(self):
        for request_id in self.records.expired(time.monotonic()):
            self._finish_error(request_id, 'RESPONSE_TIMEOUT',
                               'runtime result timed out; execution state is unknown, not stopped')

    def destroy_node(self):
        for request_id in list(self.records.pending):
            if self.context.ok():
                self._finish_error(request_id, 'GATEWAY_SHUTDOWN',
                                   'gateway shutting down; execution state is unknown')
            else:
                self.records.finish(request_id, None)
        for client in self.action_clients.values():
            client.destroy()
        return super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    node = None
    try:
        node = Gateway()
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if node:
            node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
