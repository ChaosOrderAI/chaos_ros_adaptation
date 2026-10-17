import json
from pathlib import Path
import time

import pytest

from chaos_data import native_recorder as native_module
from chaos_data.native_recorder import NativeRecorder, options
from chaos_data.recorder import CollectionController
from chaos_data.timing import TimeSample


def asset(tmp_path, *, strict=True):
    return {'id': 'vehicle_001', 'namespace': '/robots/vehicle_001',
            'data_collection': {
                'enabled': True, 'output_dir': str(tmp_path), 'storage_id': 'mcap',
                'topics': ['/camera/image', '/tf_static', '/robots/vehicle_001/robot_description'],
                'max_bagfile_duration_s': 300, 'max_bagfile_size_mb': 1024,
                'max_cache_size_mb': 64, 'min_free_space_mb': 1,
                'time_sync': {
                    'require_sync': strict, 'max_uncertainty_ms': 10,
                    'max_reference_age_s': 120, 'probe_timeout_s': 0.5,
                    'poll_interval_s': 1, 'stable_samples': 2, 'jump_threshold_ms': 100,
                }}}


class FakeRecorder:
    def __init__(self, config, path, node_name):
        self.path = Path(path)
        self.alive = False
        self.stopped = False

    def start(self):
        self.path.mkdir()
        self.alive = True

    def stop(self):
        if not self.alive:
            raise RuntimeError('child exited')
        (self.path / 'metadata.yaml').write_text('rosbag2_bagfile_information: {}\n')
        self.alive = False
        self.stopped = True


def measure(controller, second, *, quality='synced', wall_offset_s=0):
    utc = (1_700_000_000 + second + wall_offset_s) * 10**9
    steady = second * 10**9
    sample = TimeSample(quality, quality, utc, steady, offset_s=0, uncertainty_s=0.001)
    return controller.update(sample, wall_ns=utc, monotonic_ns=steady)


def test_no_subscriptions_until_stable_then_rotation_recreates_recorder(tmp_path):
    created = []

    def factory(*args):
        created.append(FakeRecorder(*args))
        return created[-1]

    controller = CollectionController(asset(tmp_path), recorder_factory=factory)
    assert measure(controller, 1)['state'] == 'waiting_for_clock'
    assert created == []
    assert measure(controller, 2)['state'] == 'recording'
    assert len(created) == 1
    assert measure(controller, 3, quality='unsynced')['state'] == 'waiting_for_clock'
    assert created[0].stopped
    row = controller.session.manifest['segments'][0]
    assert row['suspect_interval']['from']['monotonic_ns'] == 2 * 10**9
    assert measure(controller, 4)['state'] == 'waiting_for_clock'
    assert measure(controller, 5)['state'] == 'recording'
    assert len(created) == 2 and created[0].path != created[1].path
    controller.close()
    assert controller.exit_code == 0
    assert all(row['status'] == 'closed' for row in controller.session.manifest['segments'])


@pytest.mark.parametrize('offset', [-10, 10])
def test_clock_jump_separates_epochs_even_in_permissive_mode(tmp_path, offset):
    controller = CollectionController(asset(tmp_path, strict=False), recorder_factory=FakeRecorder)
    assert measure(controller, 1)['state'] == 'recording'
    assert measure(controller, 2, wall_offset_s=offset)['state'] == 'waiting_for_clock'
    assert controller.session.manifest['segments'][0]['reason'] == 'clock epoch changed'
    assert measure(controller, 3, wall_offset_s=offset)['state'] == 'recording'
    assert len(controller.session.manifest['segments']) == 2
    controller.close()


def test_permissive_recording_preserves_unknown_time_quality(tmp_path):
    controller = CollectionController(asset(tmp_path, strict=False), recorder_factory=FakeRecorder)
    snapshot = measure(controller, 1, quality='unknown')
    assert snapshot['state'] == 'recording'
    assert snapshot['clock']['time_sample']['status'] == 'unknown'
    controller.close()
    journal = (controller.session.path / 'time_sync.jsonl').read_text()
    assert 'unknown' in journal


def test_disk_limit_stops_without_deleting_existing_bag(tmp_path):
    controller = CollectionController(asset(tmp_path, strict=False), recorder_factory=FakeRecorder)
    measure(controller, 1)
    controller.session.free_bytes = lambda: 0
    snapshot = controller.poll()
    assert snapshot['state'] == 'failed' and controller.exit_code == 1
    assert (controller.session.path / 'segments/bag_0001/metadata.yaml').is_file()
    controller.close()
    assert controller.session.manifest['status'] == 'failed'


def test_native_crash_is_fatal_and_session_cannot_claim_success(tmp_path):
    controller = CollectionController(asset(tmp_path, strict=False), recorder_factory=FakeRecorder)
    measure(controller, 1)
    controller._recorder.alive = False
    assert controller.poll()['state'] == 'failed'
    controller.close()
    assert controller.exit_code == 1
    assert controller.session.manifest['segments'][0]['status'] == 'failed'


def test_start_failure_finalizes_failed_segment(tmp_path):
    class FailingRecorder(FakeRecorder):
        def start(self):
            raise RuntimeError('mcap writer unavailable')

    class Probe:
        def sample(self):
            return TimeSample('synced', 'good', time.time_ns(), time.monotonic_ns(),
                              offset_s=0, uncertainty_s=0.001)

    controller = CollectionController(asset(tmp_path, strict=False), recorder_factory=FailingRecorder,
                                      probe=Probe())
    assert controller.poll()['state'] == 'failed'
    controller.close()
    assert controller.exit_code == 1 and controller.session.manifest['status'] == 'failed'


def test_immediate_post_start_measurement_rejects_stale_approval(tmp_path):
    class Probe:
        calls = 0

        def sample(self):
            self.calls += 1
            quality = 'synced' if self.calls == 1 else 'unsynced'
            return TimeSample(quality, quality, time.time_ns(), time.monotonic_ns(),
                              offset_s=0, uncertainty_s=0.001)

    declaration = asset(tmp_path)
    declaration['data_collection']['time_sync']['stable_samples'] = 1
    probe = Probe()
    controller = CollectionController(declaration, recorder_factory=FakeRecorder, probe=probe)
    result = controller.poll()
    assert probe.calls == 2
    assert result['state'] == 'waiting_for_clock'
    segment = controller.session.manifest['segments'][0]
    assert segment['status'] == 'closed'
    assert segment['startup_clock_check']['clock_after_start']['time_sample']['status'] == 'unsynced'
    assert segment['suspect_interval'] is not None
    controller.close()


def test_never_synchronized_run_has_zero_segments_and_honest_summary(tmp_path):
    controller = CollectionController(asset(tmp_path), recorder_factory=FakeRecorder)
    measure(controller, 1, quality='unknown')
    controller.close()
    saved = json.loads((controller.session.path / 'manifest.json').read_text())
    assert saved['status'] == 'completed' and saved['segments'] == []
    assert saved['summary']['clock']['time_sample']['status'] == 'unknown'


def test_native_options_preserve_latency_and_latched_metadata(tmp_path):
    pytest.importorskip('rosbag2_py')
    from rclpy.qos import DurabilityPolicy
    config = asset(tmp_path)['data_collection']
    storage, recording = options(config, tmp_path / 'bag')
    assert storage.max_bagfile_duration == 300
    assert storage.max_bagfile_size == 1024**3
    assert recording.topics == config['topics']
    assert not recording.start_paused and not recording.all_topics
    assert not recording.all_services and not recording.include_unpublished_topics
    assert not recording.use_sim_time
    assert '/camera/image' not in recording.topic_qos_profile_overrides
    for profile in recording.topic_qos_profile_overrides.values():
        assert profile.durability == DurabilityPolicy.TRANSIENT_LOCAL


def _blocked_stop_worker(connection, *_):
    connection.send({'ok': True, 'operation': 'start'})
    connection.recv()
    while True:
        time.sleep(1)


def _failed_start_worker(connection, *_):
    connection.send({'ok': False, 'error': 'storage plugin unavailable'})
    connection.close()


def test_native_stop_watchdog_terminates_blocked_writer(tmp_path, monkeypatch):
    monkeypatch.setattr(native_module, '_worker', _blocked_stop_worker)
    recorder = NativeRecorder(asset(tmp_path)['data_collection'], tmp_path / 'bag',
                              'test_blocked_writer', shutdown_timeout_s=0.05)
    recorder.start()
    started = time.monotonic()
    with pytest.raises(TimeoutError, match='stop exceeded'):
        recorder.stop()
    assert not recorder.alive
    assert time.monotonic() - started < 3


def test_native_failed_start_does_not_leave_child_running(tmp_path, monkeypatch):
    monkeypatch.setattr(native_module, '_worker', _failed_start_worker)
    recorder = NativeRecorder(asset(tmp_path)['data_collection'], tmp_path / 'bag', 'test_failed_writer')
    with pytest.raises(RuntimeError, match='storage plugin unavailable'):
        recorder.start()
    assert not recorder.alive
