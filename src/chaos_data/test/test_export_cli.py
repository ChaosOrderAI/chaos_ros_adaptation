#!/usr/bin/env python3
"""The native data_export as a black box, driven on real bags.

The test writes a real rosbag (SQLite and MCAP) with real ROS messages, a
real session directory and a real export profile, and then runs the native
executable and checks its exit code and the files it published.

The fixture contract is the one of the frozen Python baseline, because that
is the contract the native exporter has to keep:

* a session directory holds the manifest, the clock journal and one bag
  segment, and the manifest names the segment that the episode uses;
* a profile names the features, the action labels and the episode interval,
  and the session path of an episode is absolute;
* a record list is a list of (topic, message, receipt time) triples ordered
  by the receipt time, because the exporter reads the bag in the receive
  time order.

Only the builders of that contract are shared. The messages are real ROS
messages, the bag is written by rosbag2, and the expectations are the
documented behaviour of the native exporter. No Python implementation of the
export is part of this package and none is imported here.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess

import numpy as np
import pytest
import rosbag2_py
import yaml
from geometry_msgs.msg import PoseStamped
from rclpy.serialization import serialize_message
from sensor_msgs.msg import Image

from chaos_interfaces.msg import ControlTrace


# The executable under test. The build registers the path of the native
# exporter in the environment, so a missing path is a broken test setup and
# not a reason to skip the behaviour.
EXECUTABLE = os.environ.get('DATA_EXPORT_EXECUTABLE')
if not EXECUTABLE:
    raise RuntimeError(
        'DATA_EXPORT_EXECUTABLE is not set: the test needs the native exporter')

# The interval of the episode: one second, which is ten steps at ten frames
# per second.
START = 10_000_000_000
END = START + 1_000_000_000

# The topics of the bag and the type of every one of them. The profile names
# the same three topics, so the bag and the profile have to agree.
TOPICS = (
    ('/pose', 'geometry_msgs/msg/PoseStamped'),
    ('/image', 'sensor_msgs/msg/Image'),
    ('/control_trace', 'chaos_interfaces/msg/ControlTrace'),
)

# The identity of the recording. The manifest, the clock journal and every
# control trace have to name the same asset.
ASSET_ID = 'vehicle_001'
SESSION_ID = 'session'


def stamp(value_ns):
    """A header stamp of a nanosecond time."""
    return value_ns // 1_000_000_000, value_ns % 1_000_000_000


def pose_message(value_ns, x=1.0, frame='odom'):
    """A pose observation of one nanosecond time."""
    message = PoseStamped()
    message.header.stamp.sec, message.header.stamp.nanosec = stamp(value_ns)
    message.header.frame_id = frame
    message.pose.position.x = float(x)
    return message


def image_message(value_ns, *, encoding='rgb8', endian=0, height=1, width=2,
                  step=8, data=bytes([1, 2, 3, 4, 5, 6, 99, 98])):
    """An image observation.

    The payload is eight bytes for two pixels of three channels, so the two
    trailing bytes are the padding of the row and not pixels.
    """
    message = Image()
    message.header.stamp.sec, message.header.stamp.nanosec = stamp(value_ns)
    message.header.frame_id = 'camera'
    message.encoding = encoding
    message.is_bigendian = endian
    message.height = height
    message.width = width
    message.step = step
    message.data = data
    return message


def trace_message(offset_ns, sequence, *, stage='issued',
                  command='motion.velocity', ttl=1.0, x=1.0, frame='base_link',
                  asset_id=ASSET_ID):
    """A control trace of one sequence number.

    Only an issued velocity command carries a velocity and a time to live: a
    rejected command and an invalidated command are audit events, and the
    exporter refuses a trace that claims otherwise.
    """
    value = START + offset_ns
    message = ControlTrace()
    message.header.stamp.sec, message.header.stamp.nanosec = stamp(value)
    message.header.frame_id = frame
    message.asset_id = asset_id
    message.sequence = sequence
    message.monotonic_ns = value
    message.request_id = f'command_{sequence}'
    message.stage = stage
    message.command = command
    message.code = 'ok'
    active = stage == 'issued' and command == 'motion.velocity'
    message.velocity_valid = active
    message.valid_for_s = ttl if active else 0.0
    message.velocity.linear.x = x
    return message


def records(*, images=True, terminal_invalidation=True):
    """The records of one episode, ordered by the receipt time.

    The control traces carry the three cases of the label semantics: an
    issued command, a rejected command that must never become a label, and
    an invalidation that ends the action at its own source time. The stop of
    the episode is received late, so a label may arrive after the step it
    belongs to.
    """
    result = [('/pose', pose_message(START + index * 100_000_000, float(index)),
               START + index * 100_000_000) for index in range(10)]
    if images:
        result += [('/image', image_message(START + index * 100_000_000),
                    START + index * 100_000_000) for index in range(10)]
    result += [('/control_trace', trace_message(0, 1), START),
               ('/control_trace',
                trace_message(200_000_000, 2, stage='rejected'),
                START + 200_000_000),
               # A stop is effective before its reception: treating the
               # reception as the time of the action would be wrong.
               ('/control_trace',
                trace_message(400_000_000, 3, stage='invalidated'),
                START + 650_000_000),
               ('/control_trace',
                trace_message(600_000_000, 4, x=2.0, ttl=0.4),
                START + 650_000_000)]
    if terminal_invalidation:
        result += [('/control_trace',
                    trace_message(1_000_000_000, 5, stage='invalidated'),
                    END + 100_000_000)]
    result.sort(key=lambda item: item[2])
    return result


def profile(root, *, images=True):
    """The export profile of the episode."""
    features = {'observation.state': {
        'kind': 'vector', 'topic': '/pose',
        'type': 'geometry_msgs/msg/PoseStamped',
        'fields': ['pose.position.x'], 'units': ['m'],
        'stamp_field': 'header.stamp', 'frame_field': 'header.frame_id',
        'frame': 'odom', 'clock_domain': 'system_utc', 'max_age_ms': 500,
    }}
    if images:
        features['observation.images.front'] = {
            'kind': 'image', 'topic': '/image',
            'type': 'sensor_msgs/msg/Image', 'units': 'intensity',
            'stamp_field': 'header.stamp', 'frame_field': 'header.frame_id',
            'frame': 'camera', 'clock_domain': 'system_utc',
            'max_age_ms': 500,
        }
    return {
        'schema_version': 1, 'fps': 10, 'allow_unverified_time': False,
        'features': features,
        'action': {
            'topic': '/control_trace',
            'fields': ['velocity.linear.x', 'velocity.angular.z'],
            'units': ['m/s', 'rad/s'], 'frame': 'base_link',
            'space': 'base_velocity_2d', 'clock_domain': 'system_utc',
            'max_age_ms': 1000},
        'episodes': [{
            'id': 'pick_001', 'session': str(root), 'segment': 1,
            'start_utc_ns': START, 'end_utc_ns': END,
            'task': 'drive to marker', 'outcome': 'success'}]}


class Session:
    """The session directory of one episode.

    The manifest and the clock journal are written when the session is
    created, and a test may change them before the export, so the exporter
    has to refuse the changed session and not publish anything.
    """

    def __init__(self, root):
        self.root = root
        self.manifest_path = root / 'manifest.json'
        self.journal_path = root / 'time_sync.jsonl'
        self.bag = root / 'segments' / 'bag_0001'
        clock = {'epoch': 0, 'allowed': True,
                 'time_sample': {'status': 'synced', 'uncertainty_s': 0.001}}
        self.manifest = {
            'schema_version': 1, 'session_id': SESSION_ID, 'status': 'completed',
            'asset': {'id': ASSET_ID, 'mode': 'real', 'model': 'vehicle'},
            'recording': {'time_sync': {'require_sync': True}},
            'segments': [{
                'index': 1, 'relative_path': 'segments/bag_0001',
                'status': 'closed',
                'started_utc_ns': START - 1_000_000_000,
                'finished_utc_ns': END + 1_000_000_000,
                'suspect_interval': None, 'clock_at_start': clock,
                'startup_clock_check': {'clock_after_start': clock}}]}
        self.journal = [
            {'event': 'clock_sample', 'session_id': SESSION_ID,
             'asset_id': ASSET_ID, 'segment_count': 1, 'state': 'recording',
             'clock': clock},
            {'event': 'session_end', 'session_id': SESSION_ID,
             'asset_id': ASSET_ID}]

    def write(self):
        """Writes the manifest and the clock journal of the session."""
        self.root.mkdir(parents=True)
        (self.root / 'segments').mkdir()
        self.manifest_path.write_text(json.dumps(self.manifest))
        self.journal_path.write_text(
            ''.join(json.dumps(row) + '\n' for row in self.journal))

    def save_manifest(self):
        """Writes the manifest again, after a test has changed it."""
        self.manifest_path.write_text(json.dumps(self.manifest))


def write_bag(bag, storage, values):
    """Writes the records into a real bag of the given storage."""
    writer = rosbag2_py.SequentialWriter()
    writer.open(rosbag2_py.StorageOptions(uri=str(bag), storage_id=storage),
                rosbag2_py.ConverterOptions('', ''))
    for index, (name, kind) in enumerate(TOPICS):
        writer.create_topic(rosbag2_py.TopicMetadata(
            id=index, name=name, type=kind, serialization_format='cdr'))
    for topic, message, receipt in values:
        writer.write(topic, serialize_message(message), receipt)
    writer.close()


def save_profile(tmp_path, value):
    """Writes the export profile and returns its path."""
    path = tmp_path / 'export.yaml'
    path.write_text(yaml.safe_dump(value))
    return path


def source_hashes(root):
    """The digest of every file of the source, for the read only proof."""
    return {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in root.rglob('*') if path.is_file()}


def run_export(profile_path, output):
    """Runs the native exporter and returns the finished process."""
    return subprocess.run(
        [EXECUTABLE, str(profile_path), '--output', str(output)],
        text=True, capture_output=True, timeout=120)


def rows(output):
    """The steps of the exported episode."""
    path = output / 'episodes/pick_001/steps.jsonl'
    return [json.loads(line) for line in path.read_text().splitlines()]


def prepare(tmp_path, storage, values):
    """Builds the session, the bag and the profile of one case.

    Returns the session and the profile path. The digests of the source are
    taken by the test itself, after it has changed the session, so a change
    of the recording by the export is what the digests prove.
    """
    session = Session(tmp_path / 'session')
    session.write()
    write_bag(session.bag, storage, values)
    profile_path = save_profile(tmp_path, profile(session.root))
    return session, profile_path


@pytest.mark.parametrize('storage', ['sqlite3', 'mcap'])
def test_a_valid_episode_publishes_the_vectors_and_the_images(tmp_path, storage):
    """The happy path has to succeed, or the rejections prove nothing."""
    session, profile_path = prepare(tmp_path, storage, records())
    before = source_hashes(session.root)
    output = tmp_path / 'export'
    done = run_export(profile_path, output)
    assert done.returncode == 0, done.stdout + done.stderr
    assert source_hashes(session.root) == before, 'the source recording changed'

    document = json.loads((output / 'dataset.json').read_text())
    assert document['status'] == 'completed'
    assert document['time_verified'] and not document['synthetic']
    assert document['episodes'][0]['rows'] == 10

    steps = rows(output)
    assert len(steps) == 10
    assert [row['grid_index'] for row in steps] == list(range(10))
    # The rejected command is an audit event and never becomes a label.
    assert steps[2]['action']['values'] == [1.0, 0.0]
    assert steps[2]['action']['valid']
    # The invalidation ends the action at its own source time, so the two
    # steps after it have no action.
    assert not steps[4]['action']['valid'] and not steps[5]['action']['valid']
    # The next issued command becomes the label again.
    assert steps[6]['action']['values'] == [2.0, 0.0]
    assert steps[6]['action']['receive_ns'] > steps[6]['timestamp_ns']

    saved = steps[0]['features']['observation.images.front']
    assert saved['encoding'] == 'rgb8' and saved['original_step'] == 8
    pixels = np.load(output / 'episodes/pick_001' / saved['path'], allow_pickle=False)
    # The padding of the row is not a pixel.
    assert pixels.tolist() == [[[1, 2, 3], [4, 5, 6]]]
    assert steps[0]['features']['observation.state']['values'] == [0.0]

    events = (output / 'episodes/pick_001/control_events.jsonl').read_text()
    assert len(events.splitlines()) == 5
    episode = json.loads((output / 'episodes/pick_001/episode.json').read_text())
    assert episode['valid_rows'] == 8 and episode['invalid_rows'] == 2
    assert all(len(item['sha256']) == 64 for item in episode['source_files'])


@pytest.mark.parametrize('storage', ['sqlite3', 'mcap'])
def test_a_control_sequence_gap_is_refused(tmp_path, storage):
    """A gap in the sequence of the trace hides a command, so it is fatal."""
    values = records()
    traces = [message for topic, message, _ in values
              if topic == '/control_trace']
    traces[1].sequence = 4
    session, profile_path = prepare(tmp_path, storage, values)
    before = source_hashes(session.root)
    output = tmp_path / 'export'
    done = run_export(profile_path, output)
    assert done.returncode != 0
    assert 'sequence has a gap' in done.stderr, done.stderr
    assert not output.exists(), 'a refused export must not publish anything'
    assert source_hashes(session.root) == before


@pytest.mark.parametrize('storage', ['sqlite3', 'mcap'])
def test_a_trace_of_another_asset_is_refused(tmp_path, storage):
    """A trace of another robot does not label this episode."""
    values = records()
    traces = [message for topic, message, _ in values
              if topic == '/control_trace']
    traces[0].asset_id = 'other_robot'
    session, profile_path = prepare(tmp_path, storage, values)
    before = source_hashes(session.root)
    output = tmp_path / 'export'
    done = run_export(profile_path, output)
    assert done.returncode != 0
    assert 'asset_id does not match' in done.stderr, done.stderr
    assert not output.exists(), 'a refused export must not publish anything'
    assert source_hashes(session.root) == before


@pytest.mark.parametrize('storage', ['sqlite3', 'mcap'])
def test_an_episode_without_a_terminal_invalidation_is_refused(tmp_path, storage):
    """An action that is never closed is not a confirmed training label.

    The single issued command is still open at the end of the episode, so
    every step of the episode would carry an unconfirmed action and the
    episode has no valid row at all.
    """
    values = records(terminal_invalidation=False)
    values = [row for row in values if row[0] != '/control_trace']
    values.append(('/control_trace', trace_message(0, 1), START))
    values.sort(key=lambda item: item[2])
    session, profile_path = prepare(tmp_path, storage, values)
    before = source_hashes(session.root)
    output = tmp_path / 'export'
    done = run_export(profile_path, output)
    assert done.returncode != 0
    assert 'no fully valid training rows' in done.stderr, done.stderr
    assert not output.exists(), 'a refused export must not publish anything'
    assert source_hashes(session.root) == before


@pytest.mark.parametrize('storage', ['sqlite3', 'mcap'])
def test_an_image_with_a_malformed_length_is_refused(tmp_path, storage):
    """A payload that is shorter than the declared image is not pixels."""
    values = records()
    for topic, message, _ in values:
        if topic == '/image':
            message.data = b'123'
    session, profile_path = prepare(tmp_path, storage, values)
    before = source_hashes(session.root)
    output = tmp_path / 'export'
    done = run_export(profile_path, output)
    assert done.returncode != 0
    assert 'image payload size does not match' in done.stderr, done.stderr
    assert not output.exists(), 'a refused export must not publish anything'
    assert source_hashes(session.root) == before


@pytest.mark.parametrize('storage', ['sqlite3', 'mcap'])
def test_an_unsynchronized_manifest_is_refused(tmp_path, storage):
    """A recording without clock evidence may not be exported."""
    session, profile_path = prepare(tmp_path, storage, records())
    session.manifest['recording']['time_sync']['require_sync'] = False
    session.save_manifest()
    before = source_hashes(session.root)
    output = tmp_path / 'export'
    done = run_export(profile_path, output)
    assert done.returncode != 0
    assert 'require_sync' in done.stderr, done.stderr
    assert not output.exists(), 'a refused export must not publish anything'
    assert source_hashes(session.root) == before


@pytest.mark.parametrize('storage', ['sqlite3', 'mcap'])
def test_a_metadata_path_that_escapes_the_session_is_refused(tmp_path, storage):
    """The bag metadata may not name a file outside of the session."""
    session, profile_path = prepare(tmp_path, storage, records())
    outside = session.bag.parent / 'outside.mcap'
    outside.write_bytes(b'untouched')
    metadata_path = session.bag / 'metadata.yaml'
    metadata = yaml.safe_load(metadata_path.read_text())
    metadata['rosbag2_bagfile_information']['relative_file_paths'] = [
        '../outside.mcap']
    metadata_path.write_text(yaml.safe_dump(metadata))
    before = source_hashes(session.root)
    output = tmp_path / 'export'
    done = run_export(profile_path, output)
    assert done.returncode != 0
    assert 'must be inside' in done.stderr, done.stderr
    assert not output.exists(), 'a refused export must not publish anything'
    assert outside.read_bytes() == b'untouched'


@pytest.mark.parametrize('storage', ['sqlite3', 'mcap'])
def test_an_existing_destination_is_never_replaced(tmp_path, storage):
    """A destination that exists is the work of somebody else."""
    session, profile_path = prepare(tmp_path, storage, records())
    before = source_hashes(session.root)
    output = tmp_path / 'export'
    output.mkdir()
    marker = output / 'keep'
    marker.write_text('unchanged')
    done = run_export(profile_path, output)
    assert done.returncode != 0
    assert 'already exists' in done.stderr, done.stderr
    assert marker.read_text() == 'unchanged', 'the destination was modified'
    assert list(output.iterdir()) == [marker], 'the destination was replaced'
    assert source_hashes(session.root) == before
