"""Isolated native rosbag2 data plane; IPC carries control only, never samples."""

import multiprocessing
import signal
import time


def options(config, uri):
    import rosbag2_py
    from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy

    storage = rosbag2_py.StorageOptions(
        uri=str(uri), storage_id=config['storage_id'],
        max_bagfile_size=config['max_bagfile_size_mb'] * 1024 * 1024,
        max_bagfile_duration=config['max_bagfile_duration_s'],
        max_cache_size=config['max_cache_size_mb'] * 1024 * 1024)
    recording = rosbag2_py.RecordOptions()
    recording.topics = list(config['topics'])
    recording.all_topics = False
    recording.all_services = False
    recording.include_hidden_topics = False
    # Wait for an actual publisher to select its offered QoS. Subscribing from
    # reader-only graph entries can choose RELIABLE before a BEST_EFFORT camera
    # appears, permanently excluding that stream for this recorder instance.
    recording.include_unpublished_topics = False
    recording.disable_keyboard_controls = True
    # Subscriptions are first created after clock approval. A paused recorder
    # consumes and discards latched TF/description messages before resume.
    recording.start_paused = False
    recording.use_sim_time = False
    # Static transforms may have been published before recording starts. Use a
    # transient reader explicitly; QoS for sensor streams is adapted by rosbag.
    recording.topic_qos_profile_overrides = {
        topic: QoSProfile(depth=100, reliability=ReliabilityPolicy.RELIABLE,
                          durability=DurabilityPolicy.TRANSIENT_LOCAL)
        for topic in config['topics'] if topic.endswith((
            '/tf_static', '/robot_description', '/sensors/description', '/data_collection/status'))}
    return storage, recording


def _worker(connection, config, uri, node_name):
    """Use the same non-blocking Recorder lifecycle as Jazzy's ros2 bag CLI."""
    recorder = None
    try:
        # ros2 launch can signal the entire process group. Let the supervisor
        # perform the bounded stop/flush handshake instead of racing that signal.
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        # SIGTERM remains a hard stop: multiprocessing's atexit handler uses it
        # for daemon children and must not block forever if the parent crashes.
        signal.signal(signal.SIGTERM, signal.SIG_DFL)
        import rosbag2_py
        storage, recording = options(config, uri)
        recorder = rosbag2_py.Recorder(storage, recording, 'info', node_name)
        recorder.start_spin()
        recorder.record()
        connection.send({'ok': True, 'operation': 'start'})
        while True:
            try:
                operation = connection.recv()
            except EOFError:
                break
            if operation == 'stop':
                recorder.stop()
                recorder.stop_spin()
                recorder = None
                connection.send({'ok': True, 'operation': 'stop'})
                return
            if operation == 'pause':
                recorder.pause()
            elif operation == 'resume':
                recorder.resume()
            else:
                raise ValueError('unknown recorder operation')
            connection.send({'ok': True, 'operation': operation})
    except BaseException as error:
        try:
            connection.send({'ok': False, 'error': f'{type(error).__name__}: {error}'})
        except (EOFError, BrokenPipeError, OSError):
            pass
    finally:
        if recorder is not None:
            recorder.stop()
            recorder.stop_spin()
        connection.close()


class NativeRecorder:
    """Bounded supervision also detects a native writer crash or blocked flush."""

    def __init__(self, config, uri, node_name, *, startup_timeout_s=15.0,
                 shutdown_timeout_s=10.0):
        self._config = config
        self._uri = str(uri)
        self._node_name = node_name
        self._startup_timeout = startup_timeout_s
        self._shutdown_timeout = shutdown_timeout_s
        self._process = None
        self._connection = None
        self._paused = False

    @property
    def alive(self):
        return self._process is not None and self._process.is_alive()

    @property
    def paused(self):
        return self._paused

    def _receive(self, operation, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self._connection.poll(min(0.1, max(0, deadline - time.monotonic()))):
                try:
                    result = self._connection.recv()
                except EOFError as error:
                    raise RuntimeError('native recorder exited without acknowledgement') from error
                if not result.get('ok'):
                    raise RuntimeError('native recorder: ' + result.get('error', 'failed'))
                if result.get('operation') != operation:
                    raise RuntimeError('unexpected native recorder acknowledgement')
                return
            if not self.alive:
                raise RuntimeError(f'native recorder exited ({self._process.exitcode})')
        raise TimeoutError(f'native recorder {operation} exceeded {timeout:g}s')

    def start(self):
        if self._process is not None:
            raise RuntimeError('native recorder has already been started')
        context = multiprocessing.get_context('spawn')
        self._connection, child = context.Pipe()
        self._process = context.Process(
            target=_worker, args=(child, self._config, self._uri, self._node_name),
            name=self._node_name, daemon=True)
        try:
            self._process.start()
            child.close()
            self._receive('start', self._startup_timeout)
        except BaseException:
            child.close()
            self._kill()
            raise

    def set_paused(self, paused):
        if not self.alive:
            raise RuntimeError('native recorder is not running')
        if paused == self._paused:
            return
        operation = 'pause' if paused else 'resume'
        self._connection.send(operation)
        self._receive(operation, 2.0)
        self._paused = paused

    def _kill(self):
        if self._process is not None and self._process.pid is not None:
            if self.alive:
                self._process.terminate()
            self._process.join(timeout=1.0)
            if self.alive:
                self._process.kill()
                self._process.join(timeout=1.0)
        if self._connection is not None:
            self._connection.close()

    def stop(self):
        if self._process is None:
            return
        try:
            if not self.alive:
                raise RuntimeError(f'native recorder exited ({self._process.exitcode})')
            self._connection.send('stop')
            self._receive('stop', self._shutdown_timeout)
            self._process.join(timeout=1.0)
            if self.alive:
                raise TimeoutError('native recorder remained alive after stop')
        finally:
            self._kill()
