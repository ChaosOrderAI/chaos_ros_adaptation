import hashlib
import json
from types import SimpleNamespace

import pytest

from chaos_data.audit import AuditTracker, audit_bag, source_stamp


def message(ns=1_000_000_001, frame='camera'):
    sec, nanosec = divmod(ns, 1_000_000_000)
    return {'header': {'stamp': {'sec': sec, 'nanosec': nanosec}, 'frame_id': frame}}


def topic(tracker, name='/camera'):
    return tracker.snapshot()['topics'][name]


def test_header_top_level_and_ros_object_stamp_extraction():
    assert source_stamp(message()) == (1_000_000_001, 'valid', 'camera')
    assert source_stamp({'stamp': {'sec': 1, 'nanosec': 2}}) == (1_000_000_002, 'valid', None)
    ros = SimpleNamespace(header=SimpleNamespace(stamp=SimpleNamespace(sec=1, nanosec=3), frame_id='imu'))
    assert source_stamp(ros) == (1_000_000_003, 'valid', 'imu')


@pytest.mark.parametrize('msg,kind', [
    ({'data': 'payload'}, 'missing'), ({'header': {}}, 'invalid'),
    (message(0), 'zero'), (message(-1), 'invalid'),
    ({'stamp': {'sec': 1, 'nanosec': 1_000_000_000}}, 'invalid'),
    ({'stamp': {'sec': 1.0, 'nanosec': 0}}, 'invalid'),
    ({'stamp': {'sec': True, 'nanosec': 0}}, 'invalid'),
])
def test_unknown_and_invalid_source_stamps_are_never_fabricated(msg, kind):
    assert source_stamp(msg)[:2] == (None, kind)


def test_tf_multi_stamp_is_explicitly_excluded():
    transforms = {'transforms': [message(1_000_000_000), message(2_000_000_000)]}
    tracker = AuditTracker()
    tracker.observe('/tf', transforms, 3_000_000_000, type_name='tf2_msgs/msg/TFMessage')
    values = topic(tracker, '/tf')
    assert values['excluded_multi_stamp_messages'] == 1
    assert values['valid_source_stamps'] == 0
    assert values['difference_samples'] == 0


def test_source_reordering_duplicates_and_receipt_differences_are_visible():
    tracker = AuditTracker(max_difference_s=.1)
    tracker.observe('/camera', message(1_000_000_000), 1_010_000_000)
    tracker.observe('/camera', message(1_000_000_000), 1_500_000_000)
    tracker.observe('/camera', message(900_000_000), 800_000_000)
    values = topic(tracker)
    assert values['source_duplicates'] == 1
    assert values['source_reversals'] == 1
    assert values['receipt_reversals'] == 1
    assert values['difference_min_ns'] == -100_000_000
    assert values['difference_max_ns'] == 500_000_000
    assert values['difference_abs_exceeded'] == 1
    assert values['source_ahead_of_receipt'] == 1
    assert values['difference_mean_ns'] == pytest.approx(410_000_000 / 3)
    assert 'not device clock offset' in tracker.snapshot()['timestamp_difference']


def test_epoch_boundaries_do_not_create_false_reversals():
    tracker = AuditTracker()
    tracker.observe('/camera', message(5_000_000_000), 5_000_000_001, epoch=0)
    tracker.observe('/camera', message(1_000_000_000), 1_000_000_001, epoch=1)
    values = topic(tracker)
    assert values['epoch_changes'] == 1
    assert values['source_reversals'] == values['receipt_reversals'] == 0


def test_memory_is_bounded_by_topics_and_frames():
    tracker = AuditTracker(max_topics=2, max_frames_per_topic=2)
    for number in range(100):
        tracker.observe('/camera', message(frame=f'frame_{number}'), number)
        tracker.observe(f'/other_{number}', message(), number)
    values = tracker.snapshot()
    assert values['messages'] == 200
    assert len(values['topics']) == 2
    assert len(values['topics']['/camera']['frames']) == 2
    assert values['topics']['/camera']['frames_not_retained'] == 98
    assert values['topic_limit_dropped_messages'] == 99


def test_snapshot_is_detached_and_invalid_receipt_cannot_produce_delta():
    tracker = AuditTracker()
    tracker.observe('/camera', message(), -1)
    values = topic(tracker)
    assert values['invalid_receipt_stamps'] == 1
    assert values['difference_samples'] == 0
    values['frames']['camera'] = 99
    assert topic(tracker)['frames']['camera'] == 1


@pytest.mark.parametrize('storage_id', ['mcap', 'sqlite3'])
def test_real_bag_readonly_audit_and_streamed_index(tmp_path, storage_id):
    rosbag2_py = pytest.importorskip('rosbag2_py')
    from rclpy.serialization import serialize_message
    from std_msgs.msg import Header, String

    bag = tmp_path / 'bag'
    writer = rosbag2_py.SequentialWriter()
    writer.open(rosbag2_py.StorageOptions(uri=str(bag), storage_id=storage_id),
                rosbag2_py.ConverterOptions('', ''))
    writer.create_topic(rosbag2_py.TopicMetadata(id=0, name='/header', type='std_msgs/msg/Header', serialization_format='cdr'))
    writer.create_topic(rosbag2_py.TopicMetadata(id=1, name='/plain', type='std_msgs/msg/String', serialization_format='cdr'))
    header = Header()
    header.stamp.sec = 3
    header.stamp.nanosec = 4
    header.frame_id = 'base'
    writer.write('/header', serialize_message(header), 3_000_000_010)
    writer.write('/plain', serialize_message(String(data='raw original')), 3_000_000_020)
    writer.close()

    def hashes():
        return {str(path.relative_to(bag)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in bag.rglob('*') if path.is_file()}

    before = hashes()
    index = tmp_path / 'index.jsonl'
    report = audit_bag(bag, index=index)
    assert report['complete'] is True
    assert report['messages'] == 2 and report['modified_messages'] == 0
    # Header is itself a top-level stamp; frame_id is metadata too.
    assert report['topics']['/header']['valid_source_stamps'] == 1
    assert report['topics']['/plain']['missing_source_stamps'] == 1
    records = [json.loads(line) for line in index.read_text().splitlines()]
    assert records[0]['source_ns'] == 3_000_000_004
    assert records[0]['receive_ns'] == 3_000_000_010
    assert records[0]['frame'] == 'base'
    assert records[1]['source_ns'] is None
    assert records[1]['sequence'] == 2
    assert before == hashes()
    with pytest.raises(FileExistsError):
        audit_bag(bag, index=index)
    with pytest.raises(ValueError, match='outside'):
        audit_bag(bag, index=bag / 'unsafe.jsonl')
    partial = audit_bag(bag, max_messages=1)
    assert partial['messages'] == 1 and not partial['complete']
