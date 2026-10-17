import json
from types import SimpleNamespace

import pytest

from chaos_data import session as module
from chaos_data.session import Session


def asset():
    return {
        'id': 'vehicle_001', 'namespace': '/robots/vehicle_001', 'kind': 'ugv',
        'driver': {'options': {'password': 'do-not-archive'}},
        'transport': {'key': 'private-key'},
        'sensors': {'camera': {'kind': 'camera', 'driver': {'parameters': {
            'rtsp_url': 'rtsp://user:password@host'}},
            'streams': {'image': {'topic': '/camera/image', 'type': 'sensor_msgs/msg/Image',
                                  'frame_id': 'camera_link'}}}},
        'description': {'enabled': True, 'xml': '<robot secret="private"/>', 'sha256': 'hash'},
    }


def config(tmp_path):
    return {'output_dir': str(tmp_path), 'storage_id': 'mcap', 'topics': ['/tf_static']}


def test_unique_sessions_do_not_overwrite_and_omit_credentials(tmp_path):
    first = Session(asset(), config(tmp_path))
    second = Session(asset(), config(tmp_path))
    assert first.path != second.path
    data = (first.path / 'manifest.json').read_text()
    assert 'do-not-archive' not in data and 'private-key' not in data and 'rtsp://' not in data
    assert '<robot' not in data
    assert json.loads(data)['asset']['sensors'][0]['streams']['image']['frame_id'] == 'camera_link'
    first.finish('completed', 'no recording requested')
    second.finish('completed', 'no recording requested')


def test_path_collision_fails_without_overwriting(tmp_path, monkeypatch):
    monkeypatch.setattr(module.uuid, 'uuid4', lambda: SimpleNamespace(hex='fixed'))
    first = Session(asset(), config(tmp_path), wall_ns=1_700_000_000_000_000_000)
    original = (first.path / 'manifest.json').read_bytes()
    with pytest.raises(FileExistsError):
        Session(asset(), config(tmp_path), wall_ns=1_700_000_000_000_000_000)
    assert (first.path / 'manifest.json').read_bytes() == original
    first.finish('completed', 'test finished')


@pytest.mark.parametrize('identifier', ['../outside', '/tmp/other', 'a/b', 'bad-name', ''])
def test_session_rejects_path_injection(tmp_path, identifier):
    record = asset()
    record['id'] = identifier
    with pytest.raises(ValueError, match='ROS-safe'):
        Session(record, config(tmp_path))


def test_segment_close_requires_native_metadata_and_preserves_suspect_window(tmp_path):
    session = Session(asset(), config(tmp_path))
    path = session.begin_segment(clock={'epoch': 0})
    assert not path.exists(), 'native rosbag must create its own bag directory'
    assert not session.end_segment('writer crashed', suspect_interval={'from': 10, 'through': 20})
    session.finish('completed', 'attempted shutdown')
    saved = json.loads((session.path / 'manifest.json').read_text())
    assert saved['status'] == 'failed'
    assert saved['segments'][0]['status'] == 'failed'
    assert saved['segments'][0]['suspect_interval'] == {'from': 10, 'through': 20}


def test_valid_segments_and_clock_journal_are_finalized_independently(tmp_path):
    session = Session(asset(), config(tmp_path), monotonic_ns=10_000_000_000)
    for epoch in (0, 1):
        path = session.begin_segment(clock={'epoch': epoch})
        path.mkdir()
        (path / 'metadata.yaml').write_text('rosbag2_bagfile_information: {}\n')
        assert session.end_segment('rotation')
    session.append({'quality': 'unknown', 'source_header_rewritten': False})
    session.finish('completed', 'shutdown', monotonic_ns=15_000_000_000)
    saved = json.loads((session.path / 'manifest.json').read_text())
    assert saved['status'] == 'completed' and saved['duration_monotonic_s'] == 5
    assert len(saved['segments']) == 2
    journal = [json.loads(line) for line in (session.path / 'time_sync.jsonl').read_text().splitlines()]
    assert journal == [{'quality': 'unknown', 'source_header_rewritten': False}]
    with pytest.raises(RuntimeError, match='finalized'):
        session.append({})


def test_failed_atomic_finalization_leaves_running_manifest(tmp_path, monkeypatch):
    session = Session(asset(), config(tmp_path))

    def unavailable(*_):
        raise OSError('disk I/O failed')

    monkeypatch.setattr(module.os, 'replace', unavailable)
    with pytest.raises(OSError):
        session.finish('completed', 'shutdown')
    assert json.loads((session.path / 'manifest.json').read_text())['status'] == 'running'
    assert not list(session.path.glob('.manifest-*'))
