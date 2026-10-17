import hashlib
import json
from types import SimpleNamespace as NS

import numpy as np
import pytest
import yaml

from chaos_data import dataset


START = 10_000_000_000
END = START + 1_000_000_000


def stamp(value):
    return NS(sec=value // 1_000_000_000, nanosec=value % 1_000_000_000)


def pose(value, x=1.0, frame='odom'):
    return NS(header=NS(stamp=stamp(value), frame_id=frame), pose=NS(position=NS(x=x)))


def trace(offset, sequence, *, stage='issued', command='motion.velocity', ttl=1.0,
          x=1.0, frame='base_link', asset_id='vehicle_001'):
    value = START + offset
    return NS(header=NS(stamp=stamp(value), frame_id=frame), asset_id=asset_id,
              sequence=sequence, monotonic_ns=value, request_id=f'command_{sequence}',
              stage=stage, command=command,
              valid_for_s=ttl if stage == 'issued' and command == 'motion.velocity' else 0.0, code='ok',
              velocity_valid=stage == 'issued' and command == 'motion.velocity',
              velocity=NS(linear=NS(x=x, y=0.0, z=0.0), angular=NS(x=0.0, y=0.0, z=0.0)))


def image(value, *, encoding='rgb8', endian=0, height=1, width=2, step=8,
          data=bytes([1, 2, 3, 4, 5, 6, 99, 98])):
    return NS(header=NS(stamp=stamp(value), frame_id='camera'), encoding=encoding,
              is_bigendian=endian, height=height, width=width, step=step, data=data)


def profile(root, *, images=False):
    features = {'observation.state': {
        'kind': 'vector', 'topic': '/pose', 'type': 'geometry_msgs/msg/PoseStamped',
        'fields': ['pose.position.x'], 'units': ['m'], 'stamp_field': 'header.stamp',
        'frame_field': 'header.frame_id', 'frame': 'odom', 'clock_domain': 'system_utc',
        'max_age_ms': 500,
    }}
    if images:
        features['observation.images.front'] = {
            'kind': 'image', 'topic': '/image', 'type': 'sensor_msgs/msg/Image',
            'units': 'intensity', 'stamp_field': 'header.stamp', 'frame_field': 'header.frame_id',
            'frame': 'camera', 'clock_domain': 'system_utc', 'max_age_ms': 500,
        }
    return {'schema_version': 1, 'fps': 10, 'allow_unverified_time': False, 'features': features,
            'action': {'topic': '/control_trace', 'fields': ['velocity.linear.x', 'velocity.angular.z'],
                       'units': ['m/s', 'rad/s'], 'frame': 'base_link', 'space': 'base_velocity_2d',
                       'clock_domain': 'system_utc', 'max_age_ms': 1000},
            'episodes': [{'id': 'pick_001', 'session': str(root), 'segment': 1,
                          'start_utc_ns': START, 'end_utc_ns': END,
                          'task': 'drive to marker', 'outcome': 'success'}]}


def session(tmp_path, *, real=False):
    root = tmp_path / 'session'
    root.mkdir()
    bag = root / 'segments' / 'bag_0001'
    if not real:
        bag.mkdir(parents=True)
        (bag / 'test.mcap').write_bytes(b'fixture source never modified')
        (bag / 'metadata.yaml').write_text(yaml.safe_dump({'rosbag2_bagfile_information': {
            'storage_identifier': 'mcap', 'relative_file_paths': ['test.mcap']}}))
    else:
        bag.parent.mkdir()
    clock = {'epoch': 0, 'allowed': True,
             'time_sample': {'status': 'synced', 'uncertainty_s': 0.001}}
    manifest = {'schema_version': 1, 'session_id': root.name, 'status': 'completed',
                'asset': {'id': 'vehicle_001', 'mode': 'real', 'model': 'vehicle'},
                'recording': {'time_sync': {'require_sync': True}},
                'segments': [{'index': 1, 'relative_path': 'segments/bag_0001', 'status': 'closed',
                              'started_utc_ns': START - 1_000_000_000,
                              'finished_utc_ns': END + 1_000_000_000,
                              'suspect_interval': None, 'clock_at_start': clock,
                              'startup_clock_check': {'clock_after_start': clock}}]}
    (root / 'manifest.json').write_text(json.dumps(manifest))
    journal = [dict(event='clock_sample', session_id=root.name, asset_id='vehicle_001',
                    segment_count=1, state='recording', clock=clock),
               dict(event='session_end', session_id=root.name, asset_id='vehicle_001')]
    (root / 'time_sync.jsonl').write_text(''.join(json.dumps(row) + '\n' for row in journal))
    return root, manifest, bag


def save_profile(tmp_path, value):
    path = tmp_path / 'export.yaml'
    path.write_text(yaml.safe_dump(value))
    return path


def records(*, images=False):
    result = [('/pose', pose(START + i * 100_000_000, float(i)), START + i * 100_000_000)
              for i in range(10)]
    result += [('/control_trace', trace(0, 1), START),
               ('/control_trace', trace(200_000_000, 2, stage='rejected'), START + 200_000_000),
               # A stop is effective before reception; treating reception as action time is wrong.
               ('/control_trace', trace(400_000_000, 3, stage='invalidated'), START + 650_000_000),
               ('/control_trace', trace(600_000_000, 4, x=2.0, ttl=0.4), START + 650_000_000),
               ('/control_trace', trace(1_000_000_000, 5, stage='invalidated'), END + 100_000_000)]
    if images:
        result += [('/image', image(START + i * 100_000_000), START + i * 100_000_000)
                   for i in range(10)]
    return sorted(result, key=lambda item: item[2])


def fake_records(monkeypatch, values):
    def read(_source, expected):
        for index, (topic, message, receipt) in enumerate(values, 1):
            if topic in expected:
                yield topic, message, receipt, index
    monkeypatch.setattr(dataset, '_records', read)


def rows(output):
    return [json.loads(line) for line in (output / 'episodes/pick_001/steps.jsonl').read_text().splitlines()]


def test_late_stop_rejected_command_and_original_grid_are_preserved(tmp_path, monkeypatch):
    root, _, _ = session(tmp_path)
    fake_records(monkeypatch, records(images=True))
    output = tmp_path / 'export'
    report = dataset.export_dataset(save_profile(tmp_path, profile(root, images=True)), output)
    data = rows(output)
    assert report['time_verified'] and not report['synthetic']
    assert len(data) == 10 and [row['grid_index'] for row in data] == list(range(10))
    assert data[2]['action']['values'] == [1.0, 0.0], 'rejected velocity must not become a label'
    assert not data[4]['action']['valid'] and not data[5]['action']['valid']
    assert data[6]['action']['values'] == [2.0, 0.0]
    assert data[6]['action']['receive_ns'] > data[6]['timestamp_ns'], 'offline action labels may arrive late'
    saved = data[0]['features']['observation.images.front']
    assert saved['encoding'] == 'rgb8' and saved['original_step'] == 8
    pixels = np.load(output / 'episodes/pick_001' / saved['path'], allow_pickle=False)
    assert pixels.tolist() == [[[1, 2, 3], [4, 5, 6]]]
    assert (output / 'episodes/pick_001/control_events.jsonl').is_file()
    episode = json.loads((output / 'episodes/pick_001/episode.json').read_text())
    assert episode['valid_rows'] == 8 and episode['invalid_rows'] == 2
    assert all(len(item['sha256']) == 64 for item in episode['source_files'])


def test_observations_need_both_source_and_receive_causality(tmp_path, monkeypatch):
    root, _, _ = session(tmp_path)
    data = [row for row in records(images=True) if row[0] not in ('/pose', '/image')]
    data += [('/pose', pose(START, 1.0), START),
             ('/pose', pose(START + 100_000_000, 2.0), START + 250_000_000),
             ('/image', image(START), START),
             # Source time is ahead of receipt; it must wait until grid reaches source.
             ('/image', image(START + 200_000_000, data=bytes([7, 8, 9, 0, 1, 2, 0, 0])), START + 100_000_000)]
    fake_records(monkeypatch, sorted(data, key=lambda item: item[2]))
    output = tmp_path / 'export'
    dataset.export_dataset(save_profile(tmp_path, profile(root, images=True)), output)
    result = rows(output)
    assert result[2]['features']['observation.state']['values'] == [1.0]
    assert result[3]['features']['observation.state']['values'] == [2.0]
    assert result[1]['features']['observation.images.front']['source_ns'] == START
    assert result[2]['features']['observation.images.front']['source_ns'] == START + 200_000_000
    assert result[9]['features']['observation.state']['reason'] == 'sample_too_old'


def test_action_ttl_and_unclosed_tail_are_invalid(tmp_path, monkeypatch):
    root, _, _ = session(tmp_path)
    data = [row for row in records() if row[0] != '/control_trace']
    data += [('/control_trace', trace(0, 1, ttl=0.2), START),
             ('/control_trace', trace(500_000_000, 2, ttl=1.0), START + 500_000_000)]
    fake_records(monkeypatch, sorted(data, key=lambda item: item[2]))
    output = tmp_path / 'export'
    dataset.export_dataset(save_profile(tmp_path, profile(root)), output)
    result = rows(output)
    assert result[1]['action']['valid']
    assert result[2]['action']['reason'] == 'velocity_expired'
    assert result[5]['action']['reason'] == 'action_unclosed'
    assert not any(row['action']['valid'] for row in result[5:])


@pytest.mark.parametrize('sequences', [[1, 3], [1, 1], [2, 1]])
def test_control_sequence_loss_fails_without_publishing_partial_dataset(tmp_path, monkeypatch, sequences):
    root, _, _ = session(tmp_path)
    data = [('/control_trace', trace(0, sequences[0]), START),
            ('/control_trace', trace(900_000_000, sequences[1]), START + 900_000_000)]
    fake_records(monkeypatch, data)
    with pytest.raises(ValueError, match='sequence gap'):
        dataset.export_dataset(save_profile(tmp_path, profile(root)), tmp_path / 'export')
    assert not (tmp_path / 'export').exists()
    assert not list(tmp_path.glob('.*.partial-*'))


@pytest.mark.parametrize('mutation,error', [
    (lambda m: m.update(status='running'), 'completed'),
    (lambda m: m['segments'][0].update(status='recording'), 'closed'),
    (lambda m: m['segments'][0].update(suspect_interval={'reason': 'clock jump'}), 'suspect_interval'),
    (lambda m: m['recording']['time_sync'].update(require_sync=False), 'require_sync'),
    (lambda m: m['segments'][0]['clock_at_start'].update(epoch=1), 'one epoch'),
    (lambda m: m['segments'][0]['startup_clock_check'].clear(), 'startup'),
])
def test_untrusted_source_quality_is_rejected_before_output(tmp_path, mutation, error):
    root, manifest, _ = session(tmp_path)
    mutation(manifest)
    (root / 'manifest.json').write_text(json.dumps(manifest))
    with pytest.raises(ValueError, match=error):
        dataset.export_dataset(save_profile(tmp_path, profile(root)), tmp_path / 'export')
    assert not (tmp_path / 'export').exists()


def test_unverified_time_override_is_only_mock_and_labelled(tmp_path, monkeypatch):
    root, manifest, _ = session(tmp_path)
    settings = profile(root)
    settings['allow_unverified_time'] = True
    with pytest.raises(ValueError, match='restricted to mock'):
        dataset.export_dataset(save_profile(tmp_path, settings), tmp_path / 'export')
    manifest['asset']['mode'] = 'mock'
    manifest['recording']['time_sync']['require_sync'] = False
    manifest['segments'][0]['clock_at_start']['time_sample']['status'] = 'unknown'
    (root / 'manifest.json').write_text(json.dumps(manifest))
    fake_records(monkeypatch, records())
    result = dataset.export_dataset(save_profile(tmp_path, settings), tmp_path / 'export')
    assert result['synthetic'] and not result['time_verified']


@pytest.mark.parametrize('modification,error', [
    (lambda p: p['features']['observation.state'].update(clock_domain='device'), 'system_utc'),
    (lambda p: p['features']['observation.state'].update(fields=[['bad']]), 'field path'),
    (lambda p: p.update(fps=10**500), 'finite'),
    (lambda p: p['episodes'][0].update(end_utc_ns=START), 'interval'),
    (lambda p: p['episodes'][0].update(end_utc_ns=START + 86_400 * 1_000_000_000), 'step limit'),
    (lambda p: p['action'].update(fields=['pose.position.x']), 'field count'),
    (lambda p: p['features'].update({'../escape': p['features']['observation.state']}), 'feature name'),
])
def test_profile_validation(tmp_path, modification, error):
    root, _, _ = session(tmp_path)
    settings = profile(root)
    if error == 'step limit':
        settings['fps'] = 240
    modification(settings)
    with pytest.raises(ValueError, match=error):
        dataset.load_profile(save_profile(tmp_path, settings))


def test_robot_state_requires_pose_source_clock_validity_and_pose_only_fields(tmp_path):
    root, _, _ = session(tmp_path)
    settings = profile(root)
    feature = settings['features']['observation.state']
    feature.update(type='chaos_interfaces/msg/RobotState', fields=['pose.pose.position.x'],
                   stamp_field='stamp', frame_field='pose.header.frame_id', validity_fields=['pose_valid'])
    with pytest.raises(ValueError, match='pose.header'):
        dataset.load_profile(save_profile(tmp_path, settings))
    feature['stamp_field'] = 'pose.header.stamp'
    with pytest.raises(ValueError, match='connected'):
        dataset.load_profile(save_profile(tmp_path, settings))
    feature['validity_fields'].append('connected')
    feature['fields'] = ['twist.twist.linear.x']
    with pytest.raises(ValueError, match='only pose.pose'):
        dataset.load_profile(save_profile(tmp_path, settings))


@pytest.mark.parametrize('encoding,endian,raw,step,expected', [
    ('rgb8', 0, bytes([1, 2, 3, 4, 5, 6, 99, 98]), 8, [[[1, 2, 3], [4, 5, 6]]]),
    ('bgr8', 0, bytes([6, 5, 4, 3, 2, 1]), 6, [[[6, 5, 4], [3, 2, 1]]]),
    ('mono8', 0, bytes([7, 9, 99]), 3, [[7, 9]]),
    ('16UC1', 1, b'\x01\x02\x03\x04\xaa\xbb', 6, [[258, 772]]),
    ('16UC1', 0, b'\x02\x01\x04\x03', 4, [[258, 772]]),
    ('32FC1', 1, np.array([1.25, 2.5], dtype='>f4').tobytes(), 8, [[1.25, 2.5]]),
])
def test_image_pixels_are_lossless_with_padding_and_endian(encoding, endian, raw, step, expected):
    pixels = dataset.image_array(image(START, encoding=encoding, endian=endian, data=raw, step=step))
    assert pixels.tolist() == expected
    assert pixels.flags.c_contiguous
    if encoding in ('16UC1', '32FC1') and endian:
        assert pixels.dtype.byteorder == '>'


@pytest.mark.parametrize('changes', [{'encoding': 'yuv422'}, {'step': 5}, {'data': b'123'}])
def test_bad_image_layout_fails_instead_of_silent_conversion(changes):
    with pytest.raises(ValueError):
        dataset.image_array(image(START, **changes))


def test_invalid_state_is_not_filled_with_previous_valid_pose(tmp_path, monkeypatch):
    root, _, _ = session(tmp_path)
    settings = profile(root)
    settings['features']['observation.state']['validity_fields'] = ['connected']
    values = records()
    for topic, message, _ in values:
        if topic == '/pose':
            message.connected = message.header.stamp.nanosec < 300_000_000
    fake_records(monkeypatch, values)
    dataset.export_dataset(save_profile(tmp_path, settings), tmp_path / 'export')
    result = rows(tmp_path / 'export')
    assert result[2]['features']['observation.state']['valid']
    assert result[3]['features']['observation.state']['reason'] == 'invalid_source_flag'


def test_source_time_reversal_does_not_hide_invalid_new_state(tmp_path, monkeypatch):
    root, _, _ = session(tmp_path)
    values = records()
    for topic, message, receipt in values:
        if topic == '/pose' and receipt == START + 200_000_000:
            message.header.stamp = stamp(START + 50_000_000)
    fake_records(monkeypatch, values)
    with pytest.raises(ValueError, match='observation source timestamps reversed'):
        dataset.export_dataset(save_profile(tmp_path, profile(root)), tmp_path / 'export')


def test_no_overwrite_and_source_output_rejected(tmp_path, monkeypatch):
    root, _, _ = session(tmp_path)
    fake_records(monkeypatch, records())
    configuration = save_profile(tmp_path, profile(root))
    output = tmp_path / 'existing'
    output.mkdir()
    marker = output / 'keep'
    marker.write_text('unchanged')
    with pytest.raises(FileExistsError):
        dataset.export_dataset(configuration, output)
    assert marker.read_text() == 'unchanged'
    with pytest.raises(ValueError, match='outside'):
        dataset.export_dataset(configuration, root / 'derived')


def test_storage_metadata_cannot_import_another_clock_segment(tmp_path):
    root, _, bag = session(tmp_path)
    other = bag.parent / 'bag_0002'
    other.mkdir()
    (other / 'other.mcap').write_bytes(b'different clock segment')
    path = bag / 'metadata.yaml'
    metadata = yaml.safe_load(path.read_text())
    metadata['rosbag2_bagfile_information']['relative_file_paths'] = ['../bag_0002/other.mcap']
    path.write_text(yaml.safe_dump(metadata))
    with pytest.raises(ValueError, match='inside'):
        dataset.export_dataset(save_profile(tmp_path, profile(root)), tmp_path / 'export')
    assert not (tmp_path / 'export').exists()


def test_atomic_publication_will_not_replace_racing_empty_directory(tmp_path):
    source = tmp_path / 'staged'
    source.mkdir()
    (source / 'artifact').write_text('retained')
    destination = tmp_path / 'created_by_someone_else'
    destination.mkdir()
    with pytest.raises(FileExistsError):
        dataset._publish_directory(source, destination)
    assert not list(destination.iterdir()) and (source / 'artifact').is_file()


def test_clock_journal_must_belong_to_session_and_be_finalized(tmp_path):
    root, _, _ = session(tmp_path)
    settings = save_profile(tmp_path, profile(root))
    path = root / 'time_sync.jsonl'
    original = path.read_text()
    path.write_text(original.replace('vehicle_001', 'other_robot'))
    with pytest.raises(ValueError, match='different session or asset'):
        dataset.export_dataset(settings, tmp_path / 'export')
    path.write_text(original.splitlines()[0] + '\n')
    with pytest.raises(ValueError, match='missing session_end'):
        dataset.export_dataset(settings, tmp_path / 'export')


@pytest.mark.parametrize('text', ['schema_version: 1\nfps: 10\nfps: 20\n',
                                 'features:\n  state: {}\n  state: {}\n', '42: value\n'])
def test_yaml_duplicate_or_nonstring_keys_are_rejected(tmp_path, text):
    path = tmp_path / 'profile.yaml'
    path.write_text(text)
    with pytest.raises(ValueError, match='unique strings'):
        dataset.load_profile(path)


@pytest.mark.parametrize('change,error', [
    (lambda m: setattr(m, 'command', 'unknown.control'), 'unknown ControlTrace command'),
    (lambda m: setattr(m, 'stage', 'accepted'), 'unknown ControlTrace stage'),
    (lambda m: setattr(m, 'asset_id', 'other'), 'asset_id'),
    (lambda m: setattr(m.header, 'frame_id', 'other'), 'frame'),
    (lambda m: setattr(m, 'sequence', 0), 'sequence'),
    (lambda m: setattr(m.header, 'stamp', stamp(0)), 'nonzero'),
    (lambda m: setattr(m, 'velocity_valid', False), 'must not carry'),
    (lambda m: setattr(m, 'valid_for_s', float('nan')), 'finite'),
])
def test_malformed_control_events_fail_closed(tmp_path, monkeypatch, change, error):
    root, _, _ = session(tmp_path)
    values = records()
    first = next(message for topic, message, _ in values if topic == '/control_trace')
    change(first)
    fake_records(monkeypatch, values)
    with pytest.raises(ValueError, match=error):
        dataset.export_dataset(save_profile(tmp_path, profile(root)), tmp_path / 'export')


def test_only_final_issued_event_never_becomes_unconfirmed_tail_labels(tmp_path, monkeypatch):
    root, _, _ = session(tmp_path)
    values = [row for row in records() if row[0] != '/control_trace']
    values.append(('/control_trace', trace(0, 1), START))
    fake_records(monkeypatch, sorted(values, key=lambda row: row[2]))
    with pytest.raises(ValueError, match='action_unclosed'):
        dataset.export_dataset(save_profile(tmp_path, profile(root)), tmp_path / 'export')
    assert not (tmp_path / 'export').exists()


def test_episode_does_not_reuse_prior_task_action_or_pose(tmp_path, monkeypatch):
    root, _, _ = session(tmp_path)
    values = [('/pose', pose(START - 1), START - 1),
              ('/control_trace', trace(-1, 1), START - 1),
              ('/pose', pose(START + 200_000_000), START + 200_000_000),
              ('/control_trace', trace(200_000_000, 2), START + 200_000_000),
              ('/control_trace', trace(800_000_000, 3, stage='invalidated'), START + 800_000_000)]
    fake_records(monkeypatch, values)
    dataset.export_dataset(save_profile(tmp_path, profile(root)), tmp_path / 'export')
    result = rows(tmp_path / 'export')
    assert not result[0]['action']['valid'] and not result[0]['features']['observation.state']['valid']
    assert result[2]['valid']


def test_image_memory_limit_fails_before_publishing(tmp_path, monkeypatch):
    root, _, _ = session(tmp_path)
    fake_records(monkeypatch, records(images=True))
    monkeypatch.setattr(dataset, '_MAX_RETAINED_IMAGE_BYTES', 7)
    with pytest.raises(ValueError, match='image buffers'):
        dataset.export_dataset(save_profile(tmp_path, profile(root, images=True)), tmp_path / 'export')
    assert not (tmp_path / 'export').exists()


@pytest.mark.parametrize('storage_id', ['mcap', 'sqlite3'])
def test_real_rosbag_export_preserves_source_and_original_images(tmp_path, storage_id):
    rosbag2_py = pytest.importorskip('rosbag2_py')
    try:
        from chaos_interfaces.msg import ControlTrace
    except ImportError:
        pytest.skip('ControlTrace generated interface must be built for real bag integration')
    from geometry_msgs.msg import PoseStamped
    from sensor_msgs.msg import Image
    from rclpy.serialization import serialize_message

    root, _, bag = session(tmp_path, real=True)
    writer = rosbag2_py.SequentialWriter()
    writer.open(rosbag2_py.StorageOptions(uri=str(bag), storage_id=storage_id),
                rosbag2_py.ConverterOptions('', ''))
    for index, (topic, kind) in enumerate([('/pose', 'geometry_msgs/msg/PoseStamped'),
                                          ('/image', 'sensor_msgs/msg/Image'),
                                          ('/control_trace', dataset._TRACE_TYPE)]):
        writer.create_topic(rosbag2_py.TopicMetadata(id=index, name=topic, type=kind,
                                                    serialization_format='cdr'))
    for topic, source, receipt in records(images=True):
        if topic == '/pose':
            message = PoseStamped()
            message.pose.position.x = source.pose.position.x
        elif topic == '/image':
            message = Image()
            for name in ('height', 'width', 'step', 'encoding', 'is_bigendian'):
                setattr(message, name, getattr(source, name))
            message.data = source.data
        else:
            message = ControlTrace()
            for name in ('asset_id', 'sequence', 'monotonic_ns', 'request_id', 'stage', 'command',
                         'valid_for_s', 'code', 'velocity_valid'):
                setattr(message, name, getattr(source, name))
            message.velocity.linear.x = source.velocity.linear.x
        message.header.frame_id = source.header.frame_id
        message.header.stamp.sec = source.header.stamp.sec
        message.header.stamp.nanosec = source.header.stamp.nanosec
        writer.write(topic, serialize_message(message), receipt)
    writer.close()
    before = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in root.rglob('*') if path.is_file()}
    output = tmp_path / 'export'
    dataset.export_dataset(save_profile(tmp_path, profile(root, images=True)), output)
    assert len(rows(output)) == 10
    assert rows(output)[4]['action']['valid'] is False
    assert rows(output)[6]['action']['values'] == [2.0, 0.0]
    assert before == {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                      for path in root.rglob('*') if path.is_file()}

