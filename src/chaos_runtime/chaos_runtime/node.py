"""ROS-facing runtime. No adapter is allowed to bypass shared control validation."""
import functools
import json
import math
import re
import signal
import threading
import time

import rclpy
from rclpy.action import CancelResponse, GoalResponse
from rclpy.callback_groups import ReentrantCallbackGroup
from rclpy.clock import Clock, ClockType
from rclpy.executors import MultiThreadedExecutor
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy
from rclpy.task import Future
from rclpy.signals import SignalHandlerOptions
from geometry_msgs.msg import PoseStamped
from chaos_interfaces.action import Flight, NavigatePose
from chaos_interfaces.msg import CommandResult, ControlTrace, RobotState, VelocityCommand
from chaos_interfaces.srv import ControlLease, GetDescription, Stop

from chaos_runtime.adapters.base import load_adapter
from chaos_runtime.action_server import RecoveringActionServer
from chaos_runtime.control import (CommandDeadline, ControlGuard, Limits, Rejected,
                                   finite_positive, validate_pose)


def _emit_control_trace(runtime, command, stage, code, *, request_id='', velocity=None):
    """Best-effort evidence, isolated from control decisions and driver outcomes.

    The sequence advances before construction/publication so a missing event is
    visible to an offline consumer. No authorization values or error text are
    copied. Runtime callers hold adapter_lock, which also serializes the stream.
    """
    runtime.trace_sequence = getattr(runtime, 'trace_sequence', 0) + 1
    try:
        trace = ControlTrace()
        trace.header.stamp = runtime.get_clock().now().to_msg()
        trace.header.frame_id = runtime.asset['frames']['base']
        trace.asset_id = runtime.asset['id']
        trace.sequence = runtime.trace_sequence
        trace.monotonic_ns = time.monotonic_ns()
        # Correlation IDs are metadata, never free-form input or credentials.
        trace.request_id = request_id if (
            isinstance(request_id, str)
            and re.fullmatch(r'[A-Za-z0-9_.:-]{1,128}', request_id)) else ''
        trace.command, trace.stage = command, stage
        trace.code = code if (
            isinstance(code, str) and re.fullmatch(r'[A-Z][A-Z0-9_]{0,63}', code)) else 'UNKNOWN'
        if velocity is not None and command == 'motion.velocity' and stage == 'issued':
            trace.velocity_valid = True
            (trace.velocity.linear.x, trace.velocity.linear.y, trace.velocity.linear.z,
             trace.velocity.angular.x, trace.velocity.angular.y,
             trace.velocity.angular.z) = velocity
            # Sample the guard after the event timestamp: any sampling delay
            # shortens the exported interval, rather than extending its lease.
            trace.valid_for_s = max(0.0, min(
                runtime.guard.velocity_expires, runtime.guard.expires) - runtime.guard.clock()
            ) if runtime.guard.token else 0.0
        runtime.trace_pub.publish(trace)
    except Exception:
        # A failed observation channel must not turn an accepted command into
        # a rejected one, or suppress watchdog/stop output. Avoid exception text.
        try:
            runtime.get_logger().warning('Control trace publication failed; event dropped')
        except Exception:
            pass


class RobotRuntime(Node):
    def __init__(self, **kwargs):
        super().__init__('robot_runtime', **kwargs)
        self.declare_parameter('asset_json', '{}')
        self.asset = json.loads(self.get_parameter('asset_json').value)
        self.limits = Limits.from_dict(self.asset.get('limits', {}))
        self.capabilities = frozenset(self.asset['capabilities'])
        self.adapter_lock = threading.RLock()
        self.io_group = ReentrantCallbackGroup()
        self.guard = ControlGuard(self.limits)
        self.operation = None
        self.driving = False
        self.stop_pending = None
        self.closing = False
        self.fault = ''
        self.waiters = []
        self.adapter = load_adapter(self.asset['driver']['plugin'], self, self.asset, self.limits)
        prefix = self.asset['namespace'].rstrip('/')
        self._endpoint = lambda name: prefix + '/' + name
        state_qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE,
                               durability=DurabilityPolicy.TRANSIENT_LOCAL)
        self.state_pub = self.create_publisher(RobotState, self._endpoint('state'), state_qos)
        self.results_pub = self.create_publisher(CommandResult, self._endpoint('command_results'), 50)
        self.trace_sequence = 0
        self.trace_pub = self.create_publisher(ControlTrace, self._endpoint('control_trace'), 100)
        self.create_service(GetDescription, self._endpoint('description'), self._describe,
                            callback_group=self.io_group)
        self.create_service(ControlLease, self._endpoint('control/lease'), self._lease,
                            callback_group=self.io_group)
        self.create_service(Stop, self._endpoint('motion/stop'), self._stop,
                            callback_group=self.io_group)
        self.create_subscription(VelocityCommand, self._endpoint('motion/velocity'), self._velocity,
                                 QoSProfile(depth=1), callback_group=self.io_group)
        self.action_servers = [
            self._action_server(NavigatePose, 'navigation/to_pose', 'navigation.pose'),
            self._action_server(Flight, 'flight/takeoff', 'flight.takeoff'),
            self._action_server(Flight, 'flight/land', 'flight.land'),
        ]
        self.last_tick = time.monotonic()
        self.last_state = 0.0
        # Control expiry must run even when /clock is paused or jumps backwards.
        self.timer = self.create_timer(0.05, self._tick, callback_group=self.io_group,
                                       clock=Clock(clock_type=ClockType.STEADY_TIME))
        self.get_logger().info(f'{self.asset["id"]}: adapter={self.asset["driver"]["plugin"]}, capabilities={sorted(self.capabilities)}')

    def _action_server(self, action_type, name, kind):
        return RecoveringActionServer(
            self, action_type, self._endpoint(name),
            execute_callback=functools.partial(self._execute, kind, action_type),
            goal_callback=functools.partial(self._goal, kind),
            cancel_callback=self._cancel,
            handle_accepted_callback=self._accepted,
            acceptance_failed_callback=self._acceptance_failed,
            callback_group=self.io_group,
        )

    def _require_capability(self, capability):
        if capability not in self.capabilities:
            raise Rejected('UNSUPPORTED', f'Capability {capability} is not enabled')
        if self.closing:
            raise Rejected('NOT_READY', 'Runtime is shutting down')

    def _require_ready(self):
        if self.stop_pending is not None:
            raise Rejected('STOP_FAILED', 'Stop output is pending; motion is inhibited')
        if self.fault:
            raise Rejected('DRIVER_ERROR', self.fault)
        snapshot = self.adapter.snapshot()
        if not snapshot.connected or not snapshot.ready:
            raise Rejected('NOT_READY', snapshot.detail)

    @staticmethod
    def _response(response, success, code, message):
        response.success, response.code, response.message = success, code, message
        return response

    def _result(self, request_id, success, code, message):
        result = CommandResult()
        result.stamp = self.get_clock().now().to_msg()
        result.request_id = request_id
        result.success, result.code, result.message = success, code, message
        self.results_pub.publish(result)

    def _halt(self, code, message, explicit=False, request_id=''):
        if self.operation is not None and self.operation['interrupted'] is None:
            self.operation['interrupted'] = (code, message)
        if self.stop_pending is None:
            _emit_control_trace(self, 'motion.stop', 'invalidated', code, request_id=request_id)
            self.stop_pending = {'explicit': explicit, 'failures': 0, 'next_attempt': 0.0,
                                 'request_id': request_id}
        elif explicit:
            self.stop_pending['explicit'] = True
            self.stop_pending['request_id'] = request_id
            _emit_control_trace(self, 'motion.stop', 'invalidated', code, request_id=request_id)
        return self._retry_stop(force=explicit)

    def _retry_stop(self, force=False):
        pending = self.stop_pending
        if pending is None:
            return True
        now = time.monotonic()
        if not force and now < pending['next_attempt']:
            return False
        try:
            if pending['explicit']:
                self.adapter.controlled_stop()
            else:
                self.adapter.stop()
        except Exception as error:
            self.fault = f'Adapter stop failed: {error}'
            # Keep retry state independent of the lease/command that caused it.
            # Bounded backoff and counter prevent a busy loop or growing queue.
            pending['failures'] = min(pending['failures'] + 1, 5)
            pending['next_attempt'] = now + min(1.0, 0.1 * 2 ** (pending['failures'] - 1))
            if pending['failures'] == 1:
                self.get_logger().error(self.fault)
            return False
        self.driving = False
        self.guard.velocity_expires = 0.0
        self.stop_pending = None
        _emit_control_trace(self, 'motion.stop', 'issued', 'STOP_OUTPUT_ISSUED',
                            request_id=pending.get('request_id', ''))
        # Preserve the fault latch: a recovered stop must not resume motion.
        return True

    def _expire(self):
        if self.guard.expire() or (self.driving and self.guard.remaining <= 0):
            self._halt('LEASE_EXPIRED', 'Control lease expired')

    def _describe(self, request, response):
        response.asset_id = self.asset['id']
        response.model = self.asset['model']
        response.model_version = str(self.asset.get('model_version', '1'))
        response.capabilities = sorted(self.capabilities)
        response.base_frame, response.odom_frame, response.map_frame = (
            self.asset['frames'][key] for key in ('base', 'odom', 'map'))
        response.linear_limit_mps = self.limits.linear_mps
        response.angular_limit_rps = self.limits.angular_rps
        response.command_timeout_s = self.limits.command_timeout_s
        response.lease_max_s = self.limits.lease_max_s
        return response

    def _lease(self, request, response):
        deadline = CommandDeadline.capture(request.expires_at)
        with self.adapter_lock:
            try:
                deadline.require()
                self._expire()
                if request.operation == 'acquire':
                    if not self.capabilities - {'state'}:
                        raise Rejected('UNSUPPORTED', 'This asset is read-only')
                    self._require_ready()
                deadline.require()
                token, remaining = self.guard.lease(
                    request.operation, request.owner, request.token, request.duration_s)
                response.token, response.remaining_s = token, remaining
                if request.operation == 'release':
                    if not self._halt('CONTROL_RELEASED', 'Control lease released'):
                        return self._response(response, False, 'STOP_FAILED',
                                              'Control lease released; stop output failed and will be retried')
                return self._response(response, True, 'OK', 'Control lease updated')
            except Rejected as error:
                return self._response(response, False, error.code, str(error))
            except Exception as error:
                self.fault = str(error)
                self._halt('DRIVER_ERROR', self.fault)
                return self._response(response, False, 'DRIVER_ERROR', self.fault)

    def _stop(self, request, response):
        deadline = CommandDeadline.capture(request.expires_at)
        with self.adapter_lock:
            try:
                deadline.require()
                self._expire()
                self._require_capability('motion.stop')
                self.guard.require(request.lease_token)
                deadline.require()
                if not self._halt('STOPPED', 'Stopped by command', explicit=True,
                                  request_id=request.request_id):
                    raise Rejected('STOP_FAILED', self.fault)
                return self._response(response, True, 'OK', 'Controlled stop output issued')
            except Rejected as error:
                _emit_control_trace(self, 'motion.stop', 'rejected', error.code,
                                    request_id=request.request_id)
                return self._response(response, False, error.code, str(error))
            except Exception as error:
                self.fault = str(error)
                self._halt('DRIVER_ERROR', self.fault)
                _emit_control_trace(self, 'motion.stop', 'rejected', 'DRIVER_ERROR',
                                    request_id=request.request_id)
                return self._response(response, False, 'DRIVER_ERROR', self.fault)

    def _velocity(self, request):
        with self.adapter_lock:
            try:
                self._expire()
                self._require_capability('motion.velocity')
                self._require_ready()
                if self.operation is not None:
                    raise Rejected('BUSY', 'An action is active; stop or cancel it first')
                t = request.velocity
                values = [t.linear.x, t.linear.y, t.linear.z, t.angular.x, t.angular.y, t.angular.z]
                self.adapter.validate_velocity(values)
                self.guard.velocity(
                    request.lease_token,
                    request.header.stamp.sec + request.header.stamp.nanosec / 1e9,
                    self.get_clock().now().nanoseconds / 1e9,
                    request.valid_for_s, request.header.frame_id, self.asset['frames']['base'], values)
                self.adapter.set_velocity(values)
                self.driving = True
                _emit_control_trace(self, 'motion.velocity', 'issued', 'RUNTIME_ISSUED',
                                    request_id=request.request_id, velocity=values)
                self._result(request.request_id, True, 'ACCEPTED', 'Velocity accepted until command expiry')
            except Rejected as error:
                _emit_control_trace(self, 'motion.velocity', 'rejected', error.code,
                                    request_id=request.request_id)
                self._result(request.request_id, False, error.code, str(error))
            except Exception as error:
                self.fault = str(error)
                self._halt('DRIVER_ERROR', self.fault)
                _emit_control_trace(self, 'motion.velocity', 'rejected', 'DRIVER_ERROR',
                                    request_id=request.request_id)
                self._result(request.request_id, False, 'DRIVER_ERROR', self.fault)

    def _goal(self, kind, request):
        deadline = CommandDeadline.capture(request.expires_at)
        with self.adapter_lock:
            try:
                deadline.require()
                self._expire()
                self._require_capability(kind)
                self.guard.require(request.lease_token)
                self._require_ready()
                if self.operation is not None:
                    raise Rejected('BUSY', 'Another operation is active')
                if kind == 'navigation.pose':
                    p, q = request.target.pose.position, request.target.pose.orientation
                    validate_pose(request.target.header.frame_id, self.asset['frames']['map'],
                                  (p.x, p.y, p.z), (q.x, q.y, q.z, q.w),
                                  self.asset.get('kind', 'ugv'), self.limits.max_altitude_m)
                    target = [p.x, p.y, p.z, 2 * math.atan2(q.z, q.w)]
                elif kind == 'flight.takeoff':
                    target = finite_positive(request.altitude_m, 'altitude_m')
                    if target > self.limits.max_altitude_m:
                        raise Rejected('OUT_OF_RANGE', 'Takeoff altitude exceeds max_altitude_m')
                else:
                    if not math.isfinite(request.altitude_m) or request.altitude_m != 0:
                        raise Rejected('INVALID_ARGUMENT', 'Landing altitude_m must be zero')
                    target = 0.0
                self._halt('PREEMPTED', 'Velocity replaced by action')
                if self.fault:
                    raise Rejected('DRIVER_ERROR', self.fault)
                deadline.require()
                self.operation = {'kind': kind, 'target': target, 'interrupted': None,
                                  'start': time.monotonic(), 'request': request, 'deadline': deadline}
                return GoalResponse.ACCEPT
            except Rejected as error:
                self.get_logger().warning(f'Rejected {kind}: {error.code}: {error}')
                return GoalResponse.REJECT
            except Exception as error:
                self.get_logger().error(f'Failed to validate {kind}: {error}')
                return GoalResponse.REJECT

    def _accepted(self, goal_handle):
        with self.adapter_lock:
            if self.operation is not None and self.operation['request'] is goal_handle.request:
                self.operation['handle'] = goal_handle
        # Always execute to send a typed aborted result for an expired reservation.
        goal_handle.execute()

    def _acceptance_failed(self, goal_handle):
        with self.adapter_lock:
            if self.operation is not None and self.operation['request'] is goal_handle.request:
                self._halt('ACCEPTANCE_FAILED', 'Action acceptance response could not be delivered')
                self.operation = None

    def _cancel(self, goal_handle):
        with self.adapter_lock:
            # The executor advances goal status after this callback returns.
            # Halting here prevents a final movement before execute sees it.
            if self.operation is None or not goal_handle.is_active:
                return CancelResponse.REJECT
            if self.operation['request'] is not goal_handle.request:
                return CancelResponse.REJECT
            bound_handle = self.operation.get('handle')
            if bound_handle is not None and bound_handle is not goal_handle:
                return CancelResponse.REJECT
            self._halt('CANCELED', 'Action canceled by client')
            return CancelResponse.ACCEPT

    async def _next_tick(self):
        future = Future()
        with self.adapter_lock:
            if self.closing:
                return
            self.waiters.append(future)
        await future

    def _finish_action(self, operation, goal_handle, result, terminal):
        with self.adapter_lock:
            if operation is not None and self.operation is operation:
                if not self._halt('FINISHED', 'Operation finished'):
                    terminal = 'aborted'
                    self._response(result, False, 'STOP_FAILED',
                                   'Operation ended but stop output failed and will be retried')
                self.operation = None
            if goal_handle.is_active:
                if terminal == 'succeeded':
                    goal_handle.succeed()
                elif terminal == 'canceled':
                    goal_handle.canceled()
                else:
                    goal_handle.abort()

    async def _execute(self, kind, action_type, goal_handle):
        result = action_type.Result()
        operation = None
        terminal = 'aborted'
        try:
            with self.adapter_lock:
                candidate = self.operation
                if (candidate is None or candidate['kind'] != kind
                        or candidate['request'] is not goal_handle.request):
                    raise Rejected('INTERNAL_ERROR', 'Action reservation was lost')
                operation = candidate
                operation['handle'] = goal_handle
                if operation['interrupted'] is None:
                    operation['deadline'].require()
                    self.guard.require(goal_handle.request.lease_token)
                    self._require_capability(kind)
                    self._require_ready()
                    operation['deadline'].require()
                    _emit_control_trace(self, kind, 'invalidated', 'OPERATION_STARTING')
                    self.adapter.start_operation(kind, operation['target'])
            while rclpy.ok(context=self.context):
                with self.adapter_lock:
                    if goal_handle.is_cancel_requested:
                        self._halt('CANCELED', 'Action canceled by client')
                        terminal = 'canceled'
                        self._response(result, False, 'CANCELED', 'Action canceled by client')
                        self._finish_action(operation, goal_handle, result, terminal)
                        return result
                    if operation['interrupted']:
                        code, message = operation['interrupted']
                        # Cancellation is accepted before ROS exposes CANCELING.
                        # Wait one iteration for the accepted transition.
                        if code != 'CANCELED':
                            raise Rejected(code, message)
                    else:
                        self.guard.require(goal_handle.request.lease_token)
                        self._require_ready()
                        if time.monotonic() - operation['start'] > self.limits.action_timeout_s:
                            raise Rejected('TIMEOUT', 'Action exceeded action_timeout_s')
                        done, success, code, message, progress = self.adapter.poll_operation()
                        feedback = action_type.Feedback()
                        snapshot = self.adapter.snapshot()
                        if kind == 'navigation.pose':
                            feedback.current_pose = self._pose(snapshot)
                        else:
                            feedback.altitude_m = snapshot.position[2]
                        feedback.progress = float(progress)
                        goal_handle.publish_feedback(feedback)
                        if done:
                            terminal = 'succeeded' if success else 'aborted'
                            self._response(result, success, code, message)
                            # Finalize under the same lock as poll_operation so
                            # a new cancellation cannot slip between completion
                            # and the terminal state transition.
                            self._finish_action(operation, goal_handle, result, terminal)
                            return result
                await self._next_tick()
            raise Rejected('SHUTDOWN', 'Runtime stopped')
        except Rejected as error:
            return self._response(result, False, error.code, str(error))
        except Exception as error:
            return self._response(result, False, 'DRIVER_ERROR', str(error))
        finally:
            # Also finish failures raised outside the polling loop. Identity
            # and active-state checks make repeated finalization harmless.
            self._finish_action(operation, goal_handle, result, terminal)

    def _pose(self, snapshot):
        pose = PoseStamped()
        pose.header.stamp = snapshot.source_stamp or self.get_clock().now().to_msg()
        pose.header.frame_id = snapshot.pose_frame
        pose.pose.position.x, pose.pose.position.y, pose.pose.position.z = snapshot.position
        (pose.pose.orientation.x, pose.pose.orientation.y,
         pose.pose.orientation.z, pose.pose.orientation.w) = snapshot.quaternion
        return pose

    def _publish_state(self, snapshot):
        state = RobotState()
        state.stamp = self.get_clock().now().to_msg()
        state.asset_id, state.model = self.asset['id'], self.asset['model']
        state.connected = snapshot.connected
        state.ready = snapshot.ready and not bool(self.fault)
        state.pose_valid = snapshot.pose_valid and snapshot.connected
        if self.fault:
            state.operational_state = 'fault'
        elif not snapshot.connected:
            state.operational_state = 'offline'
        elif not snapshot.ready:
            state.operational_state = 'read_only'
        elif self.operation:
            state.operational_state = 'executing'
        elif self.driving:
            state.operational_state = 'velocity'
        else:
            state.operational_state = 'idle'
        state.pose = self._pose(snapshot)
        state.twist.header.stamp = state.pose.header.stamp
        state.twist.header.frame_id = snapshot.twist_frame
        v = snapshot.velocity
        state.twist.twist.linear.x, state.twist.twist.linear.y, state.twist.twist.linear.z = v[:3]
        state.twist.twist.angular.x, state.twist.twist.angular.y, state.twist.twist.angular.z = v[3:]
        state.battery_percentage = snapshot.battery
        state.detail = self.fault or snapshot.detail
        self.state_pub.publish(state)

    def _tick(self):
        with self.adapter_lock:
            if self.closing:
                return
            now = time.monotonic()
            try:
                self._expire()
                if (self.operation is not None and 'handle' not in self.operation
                        and (self.operation['interrupted'] is not None
                             or now - self.operation['start'] > 2.0)):
                    # Jazzy may fail to send the acceptance response if the
                    # client disappears, skipping handle_accepted_callback.
                    self._halt('TIMEOUT', 'Action acceptance handshake expired')
                    self.operation = None
                if self.driving and self.guard.velocity_timed_out():
                    self._halt('COMMAND_EXPIRED', 'Velocity command expired')
                self._retry_stop()
                snapshot = self.adapter.snapshot()
                if (self.driving or self.operation) and (not snapshot.connected or not snapshot.ready):
                    self._halt('NOT_READY', 'Adapter telemetry lost or no longer ready')
                self.adapter.update(now - self.last_tick)
                if now - self.last_state >= 0.1:
                    self._publish_state(self.adapter.snapshot())
                    self.last_state = now
            except Exception as error:
                self.fault = str(error)
                self._halt('DRIVER_ERROR', self.fault)
                self.get_logger().error(f'Adapter error: {error}')
            finally:
                self.last_tick = now
                waiters, self.waiters = self.waiters, []
                for future in waiters:
                    if not future.done():
                        future.set_result(None)

    def close(self):
        with self.adapter_lock:
            if self.closing:
                return
            self.closing = True
            self.timer.cancel()
            self._halt('SHUTDOWN', 'Runtime shutting down')
            if self.stop_pending is not None:
                self._retry_stop(force=True)
            try:
                self.adapter.close()
            except Exception as error:
                self.get_logger().error(f'Adapter close failed: {error}')
            for future in self.waiters:
                if not future.done():
                    future.set_result(None)
            self.waiters = []


def main(args=None):
    # Keep the ROS context alive while emitting the final controlled stop.
    rclpy.init(args=args, signal_handler_options=SignalHandlerOptions.NO)
    node = None
    executor = MultiThreadedExecutor(num_threads=4)
    previous_handlers = {}

    def stop_signal(signum, frame):
        if node is not None:
            node.close()
        raise KeyboardInterrupt

    for signum in (signal.SIGINT, signal.SIGTERM):
        previous_handlers[signum] = signal.signal(signum, stop_signal)
    try:
        node = RobotRuntime()
        executor.add_node(node)
        executor.spin()
    except KeyboardInterrupt:
        pass
    finally:
        if node is not None:
            node.close()
        executor.shutdown()
        if node is not None:
            node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
        for signum, handler in previous_handlers.items():
            signal.signal(signum, handler)
