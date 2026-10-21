"""Clock-gated collection supervisor; native rosbag handles all sensor payloads."""

import json
import signal
import time

from .native_recorder import NativeRecorder
from .session import Session
from .timing import ChronyProbe, ClockGuard


class CollectionController:
    """Rotate subscriptions after clock loss so latched messages are reacquired.

    Polling can detect a clock fault only after it happened. A segment closed for
    bad clock quality is explicitly marked with the potentially affected tail;
    neither a successful NTP sample nor this gate proves hardware timestamps.
    """

    def __init__(self, asset, *, session=None, recorder_factory=NativeRecorder,
                 probe=None, guard=None):
        self.asset_id = asset['id']
        self.config = asset['data_collection']
        self.session = session or Session(asset, self.config)
        settings = self.config['time_sync']
        self.probe = probe or ChronyProbe(
            max_uncertainty_s=settings['max_uncertainty_ms'] / 1000,
            max_reference_age_s=settings['max_reference_age_s'],
            timeout_s=settings['probe_timeout_s'])
        self.guard = guard or ClockGuard(
            required_samples=settings['stable_samples'],
            jump_threshold_s=settings['jump_threshold_ms'] / 1000,
            max_sample_age_s=max(2.0, 2 * settings['poll_interval_s'] + settings['probe_timeout_s']),
            strict=settings['require_sync'])
        self._recorder_factory = recorder_factory
        self._recorder = None
        self._epoch = None
        self._last_good = None
        self._last_decision = None
        self._pending_startup = None
        self._samples = 0
        self._closed = False
        self._free_bytes = None
        self.state = 'waiting_for_clock'
        self.reason = 'waiting for clock quality samples'
        self.exit_code = 0

    def _close_segment(self, reason, *, suspect=False, failed=False):
        if self._recorder is None:
            return
        recorder, self._recorder = self._recorder, None
        error = None
        try:
            recorder.stop()
        except Exception as caught:
            error = caught
            failed = True
            reason += f'; recorder shutdown failed: {caught}'
        utc, steady = time.time_ns(), time.monotonic_ns()
        interval = None
        if suspect:
            interval = {
                'from': self._last_good,
                'through': {'utc_ns': utc, 'monotonic_ns': steady},
                'reason': reason,
                'meaning': 'samples in this tail may have an untrusted receipt clock; '
                           'UTC bounds can cross a clock step, use monotonic bounds',
            }
        clean = self.session.end_segment(
            reason, failed=failed, suspect_interval=interval,
            wall_ns=utc, monotonic_ns=steady)
        if not clean or error is not None:
            raise RuntimeError(reason if error is not None else 'recording segment did not finalize')

    def fail(self, reason):
        self.state, self.reason, self.exit_code = 'failed', reason, 1
        try:
            self._close_segment(reason, failed=True, suspect=True)
        except Exception as error:
            self.reason += f'; {error}'

    def poll(self):
        if self._closed or self.state == 'failed':
            return self.snapshot()
        try:
            if self._recorder is not None and not self._recorder.alive:
                raise RuntimeError('native recorder exited unexpectedly')
            self._free_bytes = self.session.free_bytes()
            if self._free_bytes < self.config['min_free_space_mb'] * 1024 * 1024:
                raise RuntimeError('free disk space is below min_free_space_mb; recordings retained')
            sample = self.probe.sample()
            result = self.update(sample, wall_ns=time.time_ns(), monotonic_ns=time.monotonic_ns())
            if self._pending_startup is not None:
                # Process creation/DDS/storage startup can take longer than one
                # polling interval. Recheck immediately after the native ACK;
                # never silently reuse the pre-start measurement as fresh proof.
                evidence, self._pending_startup = self._pending_startup, None
                sample = self.probe.sample()
                result = self.update(sample, wall_ns=time.time_ns(), monotonic_ns=time.monotonic_ns())
                evidence.update({
                    'checked_utc_ns': time.time_ns(), 'checked_monotonic_ns': time.monotonic_ns(),
                    'clock_after_start': self._last_decision,
                    'meaning': 'recorder startup is a measurement gap; these bounds do not '
                               'prove clock quality for every sample inside that gap',
                })
                self.session.record_startup_check(evidence['segment_index'], evidence)
                self.session.append(dict(evidence, event='startup_clock_check'))
            return result
        except Exception as error:
            self.fail(f'{type(error).__name__}: {error}')
            return self.snapshot()

    def update(self, sample, *, wall_ns, monotonic_ns):
        """Apply a measured sample; explicit clocks also make epoch behavior testable."""
        decision = self.guard.update(sample, wall_ns=wall_ns, monotonic_ns=monotonic_ns)
        self._last_decision = decision.to_dict()
        self._samples += 1
        jumped = self._epoch is not None and decision.epoch != self._epoch
        self._epoch = decision.epoch
        if jumped or not decision.allowed:
            close_reason = 'clock epoch changed' if jumped else 'clock quality lost: ' + decision.reason
            self._close_segment(close_reason, suspect=True)
            self.state, self.reason = 'waiting_for_clock', decision.reason
            if jumped:
                # Even permissive mode separates wall-clock epochs. Waiting for
                # another sample avoids immediately starting on the detected step.
                self.guard.reset(reason='clock epoch changed')
        else:
            if self._recorder is None:
                path = self.session.begin_segment(
                    clock=self._last_decision, wall_ns=wall_ns, monotonic_ns=monotonic_ns)
                self._recorder = self._recorder_factory(
                    self.config, path, self.asset_id + '_data_recorder')
                self._recorder.start()
                self._pending_startup = {
                    'segment_index': len(self.session.manifest['segments']),
                    'approved_utc_ns': wall_ns, 'approved_monotonic_ns': monotonic_ns,
                    'recorder_ready_utc_ns': time.time_ns(),
                    'recorder_ready_monotonic_ns': time.monotonic_ns(),
                }
            self.state, self.reason = 'recording', decision.reason
            # Only record evidence judged synchronized as a trusted bound. With
            # require_sync=false, a null bound explicitly means no trusted sample.
            if getattr(sample, 'status', None) == 'synced':
                self._last_good = {'utc_ns': wall_ns, 'monotonic_ns': monotonic_ns}
        event = self.snapshot()
        event.update({'event': 'clock_sample', 'utc_ns': wall_ns, 'monotonic_ns': monotonic_ns})
        self.session.append(event)
        return event

    def snapshot(self):
        return {
            'schema_version': 1, 'asset_id': self.asset_id,
            'session_id': self.session.path.name, 'state': self.state,
            'reason': self.reason, 'exit_code': self.exit_code,
            'clock': self._last_decision, 'clock_samples': self._samples,
            'free_bytes': self._free_bytes,
            'segment_count': len(self.session.manifest['segments']),
            'require_sync': self.config['time_sync']['require_sync'],
        }

    def close(self):
        if self._closed:
            return
        try:
            self._close_segment('collector shutdown')
        except Exception as error:
            self.fail(f'recorder shutdown failed: {error}')
        if self.state != 'failed':
            self.state, self.reason = 'stopped', 'collector shutdown'
        try:
            self.session.append(dict(self.snapshot(), event='session_end',
                                     utc_ns=time.time_ns(), monotonic_ns=time.monotonic_ns()))
            self.session.finish(
                'failed' if self.exit_code else 'completed', self.reason, summary=self.snapshot())
            if self.session.manifest['status'] == 'failed':
                self.exit_code = 1
        except Exception as error:
            # Metadata I/O failure leaves the original running manifest as
            # interruption evidence and must never be reported as success.
            self.state, self.reason, self.exit_code = 'failed', str(error), 1
        finally:
            self._closed = True


def main(args=None):
    import rclpy
    from diagnostic_msgs.msg import DiagnosticArray, DiagnosticStatus, KeyValue
    from rclpy.clock import Clock, ClockType
    from rclpy.executors import ExternalShutdownException
    from rclpy.node import Node
    from rclpy.qos import DurabilityPolicy, QoSProfile
    from rclpy.signals import SignalHandlerOptions
    from std_msgs.msg import String

    class Collector(Node):
        def __init__(self):
            super().__init__('data_collector')
            self.declare_parameter('asset_json', '{}')
            if not self.has_parameter('use_sim_time'):
                self.declare_parameter('use_sim_time', False)
            if self.get_parameter('use_sim_time').value:
                raise ValueError('data collection currently requires the system clock')
            raw = self.get_parameter('asset_json').value
            if not isinstance(raw, str) or len(raw) > 1_048_576:
                raise ValueError('asset_json must be a JSON string of at most 1 MiB')
            asset = json.loads(raw)
            if not asset.get('data_collection', {}).get('enabled', False):
                raise ValueError('data_collection must be explicitly enabled')
            namespace = asset['namespace'].rstrip('/')
            self.status = self.create_publisher(
                String, namespace + '/data_collection/status',
                QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
            self.diagnostics = self.create_publisher(
                DiagnosticArray, namespace + '/data_collection/diagnostics', 10)
            self.steady = Clock(clock_type=ClockType.STEADY_TIME)
            self.controller = CollectionController(asset)
            try:
                self.timer = self.create_timer(
                    asset['data_collection']['time_sync']['poll_interval_s'],
                    self.tick, clock=self.steady)
            except BaseException:
                self.controller.fail('collector initialization interrupted')
                self.controller.close()
                self.destroy_node()
                raise
            self.get_logger().info(f'Data collection session: {self.controller.session.path}')

        def tick(self):
            snapshot = self.controller.poll()
            if not self.context.ok():
                return
            self.status.publish(String(data=json.dumps(snapshot, allow_nan=False)))
            clock = snapshot['clock'] or {}
            synchronized = (clock.get('time_sample', {}).get('status') == 'synced'
                            and clock.get('reason') == 'ready')
            level = 2 if snapshot['state'] == 'failed' else (
                0 if snapshot['state'] == 'recording' and synchronized else 1)
            diagnostics = DiagnosticArray()
            diagnostics.header.stamp = self.get_clock().now().to_msg()
            diagnostics.status.append(DiagnosticStatus(
                level=bytes([level]), name=self.controller.asset_id + '/data_collection',
                hardware_id=self.controller.asset_id, message=snapshot['reason'],
                values=[KeyValue(key=key, value=str(snapshot[key]))
                        for key in ('state', 'session_id', 'segment_count', 'free_bytes')]))
            self.diagnostics.publish(diagnostics)
            if self.controller.exit_code:
                self.get_logger().error(self.controller.reason)

    shutdown_requested = False

    def request_shutdown(_signum, _frame):
        nonlocal shutdown_requested
        shutdown_requested = True
        # A terminal/process-group SIGINT can be followed by launch forwarding
        # another SIGINT. Never raise KeyboardInterrupt into a native flush or
        # an atomic manifest update. SIGTERM retains its hard-stop semantics.
        signal.signal(signal.SIGINT, signal.SIG_IGN)

    previous_sigint = signal.signal(signal.SIGINT, request_shutdown)
    node = None
    code = 1
    try:
        # rclpy's default shutdown restores Python's SIGINT handler; a second
        # signal can then interrupt the recorder's finally block. This process
        # owns SIGINT throughout its lifecycle, leaving the context alive until
        # the writer and durable metadata have both been finalized.
        rclpy.init(args=args, signal_handler_options=SignalHandlerOptions.NO)
        if not shutdown_requested:
            node = Collector()
            while rclpy.ok() and not shutdown_requested and not node.controller.exit_code:
                rclpy.spin_once(node, timeout_sec=0.2)
        else:
            code = 0
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    except Exception as error:
        if node is not None:
            node.controller.fail(f'collector exception: {type(error).__name__}: {error}')
        raise
    finally:
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        try:
            if node is not None:
                node.controller.close()
                code = node.controller.exit_code
                node.destroy_node()
            if rclpy.ok():
                rclpy.shutdown()
        finally:
            signal.signal(signal.SIGINT, previous_sigint)
    if code:
        raise SystemExit(code)


if __name__ == '__main__':
    main()
