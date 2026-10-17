"""Read-only, bounded-memory export of explicitly labelled robot episodes.

This is the Chaos portable intermediate format, not a native LeRobot dataset.
Observation inputs are causal in both source and receive time. ControlTrace
actions are offline labels: late delivery does not postpone an actual stop.
"""

import argparse
from collections import Counter
import ctypes
from dataclasses import dataclass
from fractions import Fraction
import hashlib
import heapq
import json
import math
import numbers
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile

import yaml

from .audit import _stamp_ns


_NS = 1_000_000_000
_TRACE_TYPE = 'chaos_interfaces/msg/ControlTrace'
_NAME = re.compile(r'^[A-Za-z][A-Za-z0-9_.-]{0,127}$')
_FIELD = re.compile(r'^[A-Za-z][A-Za-z0-9_]*(\.(?:[A-Za-z][A-Za-z0-9_]*|[0-9]+))*$')
_TOPIC = re.compile(r'^(/[A-Za-z_][A-Za-z0-9_]*)+$')
_MAX_PENDING = 128
_MAX_RETAINED_IMAGE_BYTES = 128 * 1024 * 1024
_MAX_MESSAGE_BYTES = 64 * 1024 * 1024
_MAX_STEPS = 1_000_000


class _UniqueLoader(yaml.SafeLoader):
    pass


def _unique_mapping(loader, node, deep=False):
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if not isinstance(key, str) or key in result:
            raise ValueError('profile mapping keys must be unique strings')
        result[key] = loader.construct_object(value_node, deep=deep)
    return result


_UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _unique_mapping)


def _json(path, value):
    with path.open('x', encoding='utf-8') as output:
        json.dump(value, output, ensure_ascii=False, allow_nan=False, indent=2)
        output.write('\n')


def _line(output, value):
    output.write(json.dumps(value, ensure_ascii=False, allow_nan=False) + '\n')


def _keys(value, allowed, required, label):
    if not isinstance(value, dict) or set(value) - set(allowed):
        raise ValueError(f'{label}: expected a mapping with known fields {sorted(allowed)}')
    if set(required) - set(value):
        raise ValueError(f'{label}: missing {sorted(set(required) - set(value))}')


def _text(value, label):
    if not isinstance(value, str) or not value.strip() or len(value) > 4096 or '\x00' in value:
        raise ValueError(f'{label} must be nonempty text')
    return value


def _integer(value, label, minimum=1, maximum=2**63 - 1):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f'{label} must be an integer in [{minimum}, {maximum}]')
    return value


def _positive(value, label, maximum=60_000):
    if (isinstance(value, bool) or not isinstance(value, (int, float))
            or not 0 < value <= maximum or not math.isfinite(value)):
        raise ValueError(f'{label} must be finite and in (0, {maximum}]')
    return value


def _field_name(value):
    if not isinstance(value, str) or not _FIELD.fullmatch(value) or '__' in value:
        raise ValueError(f'invalid ROS field path: {value!r}')
    return value


def field_value(message, path):
    """Explicit ROS field access, including numeric indexes; never eval()."""
    _field_name(path)
    value = message
    for part in path.split('.'):
        if part.isdecimal():
            value = value[int(part)]
        elif isinstance(value, dict):
            value = value[part]
        else:
            value = getattr(value, part)
    return value


def _numeric_fields(message, paths):
    result = []
    for path in paths:
        value = field_value(message, path)
        if isinstance(value, bool) or not isinstance(value, numbers.Real) or not math.isfinite(value):
            raise ValueError(f'{path} is not a finite numeric scalar')
        result.append(float(value))
    return result


def load_profile(path, *, include_digest=False):
    """Load a versioned, explicit mapping; task boundaries are never guessed."""
    path = Path(path).expanduser().resolve(strict=True)
    if path.stat().st_size > 1_048_576:
        raise ValueError('export profile must not exceed 1 MiB')
    raw = path.read_bytes()
    profile = yaml.load(raw, Loader=_UniqueLoader)
    _keys(profile, {'schema_version', 'fps', 'allow_unverified_time', 'features', 'action', 'episodes'},
          {'schema_version', 'fps', 'features', 'action', 'episodes'}, 'profile')
    if type(profile['schema_version']) is not int or profile['schema_version'] != 1:
        raise ValueError('schema_version must be 1')
    _positive(profile['fps'], 'fps', 240)
    if type(profile.get('allow_unverified_time', False)) is not bool:
        raise ValueError('allow_unverified_time must be boolean')
    profile.setdefault('allow_unverified_time', False)
    features = profile['features']
    if not isinstance(features, dict) or not 1 <= len(features) <= 32:
        raise ValueError('features must contain between 1 and 32 explicit mappings')
    common = {'kind', 'topic', 'type', 'stamp_field', 'frame_field', 'frame', 'clock_domain',
              'validity_fields', 'max_age_ms', 'units'}
    for name, feature in features.items():
        if not isinstance(name, str) or not _NAME.fullmatch(name):
            raise ValueError(f'invalid feature name: {name!r}')
        _keys(feature, common | {'fields'}, common - {'validity_fields'}, f'feature {name}')
        if feature['kind'] not in ('vector', 'image'):
            raise ValueError(f'{name}: kind must be vector or image')
        if not _TOPIC.fullmatch(_text(feature['topic'], 'topic')):
            raise ValueError(f'{name}: topic must be an absolute ROS topic')
        _text(feature['type'], 'type')
        _text(feature['frame'], 'frame')
        _field_name(feature['stamp_field'])
        _field_name(feature['frame_field'])
        if feature['clock_domain'] != 'system_utc':
            raise ValueError(f'{name}: only explicitly established system_utc clocks are supported')
        _positive(feature['max_age_ms'], 'max_age_ms')
        valid = feature.get('validity_fields', [])
        if not isinstance(valid, list) or len(valid) > 32:
            raise ValueError('validity_fields must be a list of at most 32 boolean ROS fields')
        for item in valid:
            _field_name(item)
        if feature['kind'] == 'vector':
            _check_vector(feature)
        else:
            if feature['type'] != 'sensor_msgs/msg/Image' or 'fields' in feature:
                raise ValueError('image features require sensor_msgs/msg/Image without fields')
            _text(feature['units'], 'image units (for example intensity or depth_mm)')
        if feature['type'] == 'chaos_interfaces/msg/RobotState':
            if feature['stamp_field'] != 'pose.header.stamp' or feature['frame_field'] != 'pose.header.frame_id':
                raise ValueError('RobotState observations require pose.header source time and frame')
            if not {'pose_valid', 'connected'} <= set(valid):
                raise ValueError('RobotState observations must require pose_valid and connected')
            if any(not path.startswith('pose.pose.') for path in feature.get('fields', [])):
                raise ValueError('RobotState observations currently support only pose.pose fields with their source stamp')
    action = profile['action']
    _keys(action, {'topic', 'fields', 'units', 'frame', 'space', 'clock_domain', 'max_age_ms'},
          {'topic', 'fields', 'units', 'frame', 'space', 'clock_domain', 'max_age_ms'}, 'action')
    if not _TOPIC.fullmatch(_text(action['topic'], 'action.topic')):
        raise ValueError('action.topic must be an absolute ROS topic')
    if action['clock_domain'] != 'system_utc':
        raise ValueError('action.clock_domain must be system_utc')
    _text(action['space'], 'action.space')
    _text(action['frame'], 'action.frame')
    _positive(action['max_age_ms'], 'action.max_age_ms')
    _check_vector(action)
    supported = {'velocity.linear.x', 'velocity.linear.y', 'velocity.linear.z',
                 'velocity.angular.x', 'velocity.angular.y', 'velocity.angular.z'}
    if not set(action['fields']) <= supported:
        raise ValueError('action fields must select ControlTrace velocity components')
    topics = {}
    for feature in features.values():
        previous = topics.setdefault(feature['topic'], feature['type'])
        if previous != feature['type']:
            raise ValueError('one topic cannot have conflicting feature types')
    if action['topic'] in topics:
        raise ValueError('control trace must not also be an observation input')
    episodes = profile['episodes']
    if not isinstance(episodes, list) or not 1 <= len(episodes) <= 1024:
        raise ValueError('episodes must contain between 1 and 1024 task intervals')
    names = set()
    for episode in episodes:
        _keys(episode, {'id', 'session', 'segment', 'start_utc_ns', 'end_utc_ns', 'task', 'outcome'},
              {'id', 'session', 'segment', 'start_utc_ns', 'end_utc_ns', 'task', 'outcome'}, 'episode')
        if (not isinstance(episode['id'], str) or not _NAME.fullmatch(episode['id'])
                or episode['id'] in names):
            raise ValueError('episode ids must be safe and unique')
        names.add(episode['id'])
        _integer(episode['segment'], 'segment')
        start = _integer(episode['start_utc_ns'], 'start_utc_ns')
        end = _integer(episode['end_utc_ns'], 'end_utc_ns')
        if not start < end or end - start > 86_400 * _NS:
            raise ValueError('episode interval must be positive and at most 24 hours')
        if Fraction(end - start, _NS) * Fraction(str(profile['fps'])) > _MAX_STEPS:
            raise ValueError(f'episode exceeds the {_MAX_STEPS} grid step limit')
        _text(episode['task'], 'task')
        if episode['outcome'] not in ('success', 'failure', 'aborted', 'unknown'):
            raise ValueError('outcome must be success, failure, aborted, or unknown')
        source = Path(_text(episode['session'], 'session')).expanduser()
        episode['session'] = str((path.parent / source).resolve(strict=True))
    return (profile, path, hashlib.sha256(raw).hexdigest()) if include_digest else (profile, path)


def _check_vector(value):
    fields, units = value.get('fields'), value['units']
    if not isinstance(fields, list) or not 1 <= len(fields) <= 256:
        raise ValueError('fields must contain 1 to 256 unique field paths')
    if not isinstance(units, list) or len(fields) != len(units):
        raise ValueError('vector units must match the field count')
    for path, unit in zip(fields, units):
        _field_name(path)
        _text(unit, 'unit')
    if len(set(fields)) != len(fields):
        raise ValueError('fields must be unique')


def _digest(path):
    hasher = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            hasher.update(chunk)
    return hasher.hexdigest()


def _contained_file(path, root):
    real = path.resolve(strict=True)
    if not real.is_relative_to(root) or not real.is_file() or path.is_symlink():
        raise ValueError(f'source file must be a regular file inside its session: {path}')
    return real


def _source_episode(episode, profile):
    root = Path(episode['session'])
    manifest_path = _contained_file(root / 'manifest.json', root)
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if manifest.get('schema_version') != 1 or manifest.get('status') != 'completed':
        raise ValueError('source session must be schema 1 and completed')
    asset = manifest.get('asset', {})
    synthetic = asset.get('mode') == 'mock'
    bypass = profile['allow_unverified_time'] and synthetic
    if profile['allow_unverified_time'] and not synthetic:
        raise ValueError('allow_unverified_time is restricted to mock assets')
    matches = [row for row in manifest.get('segments', []) if row.get('index') == episode['segment']]
    if len(matches) != 1 or matches[0].get('status') != 'closed':
        raise ValueError('episode requires exactly one closed recording segment')
    segment = matches[0]
    if segment.get('suspect_interval') is not None:
        raise ValueError('segments with a suspect_interval cannot be training episodes')
    start, end = episode['start_utc_ns'], episode['end_utc_ns']
    if not (segment.get('started_utc_ns', end) <= start < end <= segment.get('finished_utc_ns', start)):
        raise ValueError('episode interval must be contained in one clock segment')
    clock = segment.get('clock_at_start', {})
    epoch = clock.get('epoch')
    _integer(epoch, 'clock epoch', minimum=0)
    if not bypass:
        if manifest.get('recording', {}).get('time_sync', {}).get('require_sync') is not True:
            raise ValueError('strict export requires recording.time_sync.require_sync=true')
        _verified_clock(clock, epoch)
        startup = segment.get('startup_clock_check', {}).get('clock_after_start')
        if startup is None:
            raise ValueError('strict export requires startup clock evidence')
        _verified_clock(startup, epoch)
    relative = segment.get('relative_path')
    if not isinstance(relative, str) or Path(relative).is_absolute():
        raise ValueError('segment must use a relative bag path')
    bag = (root / relative).resolve(strict=True)
    if not bag.is_relative_to(root) or not bag.is_dir():
        raise ValueError('bag must be inside its source session')
    metadata = _contained_file(bag / 'metadata.yaml', root)
    bag_info = yaml.safe_load(metadata.read_text(encoding='utf-8'))['rosbag2_bagfile_information']
    if bag_info['storage_identifier'] not in ('mcap', 'sqlite3'):
        raise ValueError('only MCAP and SQLite rosbag storage are supported')
    files = [manifest_path, _contained_file(root / 'time_sync.jsonl', root), metadata]
    for name in bag_info['relative_file_paths']:
        if not isinstance(name, str) or Path(name).is_absolute():
            raise ValueError('bag metadata contains an invalid relative file path')
        # A session may contain several clock segments. Storage metadata must
        # not pull files from another segment through a relative parent path.
        files.append(_contained_file(bag / name, bag))
    evidence = 0
    finalized = False
    with (root / 'time_sync.jsonl').open(encoding='utf-8') as stream:
        for line in stream:
            event = json.loads(line)
            if event.get('event') in ('clock_sample', 'session_end'):
                if event.get('session_id') != manifest.get('session_id') or event.get('asset_id') != asset.get('id'):
                    raise ValueError('clock journal belongs to a different session or asset')
                if event['event'] == 'session_end':
                    finalized = True
                if (event['event'] == 'clock_sample' and event.get('segment_count') == episode['segment']
                        and event.get('state') == 'recording'):
                    if not bypass:
                        _verified_clock(event.get('clock', {}), epoch)
                    evidence += 1
    if not finalized:
        raise ValueError('clock journal is incomplete: missing session_end')
    if not bypass and not evidence:
        raise ValueError('strict export requires recorded clock samples for the selected segment')
    snapshots = []
    for source in dict.fromkeys(files):
        stat = source.stat()
        snapshots.append({'path': str(source.relative_to(root)), 'size_bytes': stat.st_size,
                          'sha256': _digest(source), '_mtime_ns': stat.st_mtime_ns})
    return {'episode': episode, 'manifest': manifest, 'segment': segment, 'bag': bag,
            'storage_id': bag_info['storage_identifier'], 'epoch': epoch,
            'synthetic': synthetic, 'time_verified': not bypass, 'sources': snapshots}


def _verified_clock(clock, epoch):
    sample = clock.get('time_sample', {})
    if clock.get('epoch') != epoch or clock.get('allowed') is not True or sample.get('status') != 'synced':
        raise ValueError('clock evidence must be synchronized in one epoch')
    uncertainty = sample.get('uncertainty_s')
    if (isinstance(uncertainty, bool) or not isinstance(uncertainty, (int, float))
            or not math.isfinite(uncertainty) or uncertainty < 0):
        raise ValueError('clock evidence requires finite uncertainty')


def _records(source, expected):
    """Yield decoded messages in verified receive order; no ROS node or network."""
    import rosbag2_py
    from rclpy.serialization import deserialize_message
    from rosidl_runtime_py.utilities import get_message

    reader = rosbag2_py.SequentialReader()
    reader.open(rosbag2_py.StorageOptions(uri=str(source['bag']), storage_id=source['storage_id']),
                rosbag2_py.ConverterOptions('', ''))
    try:
        if not reader.set_read_order(rosbag2_py.ReadOrder(rosbag2_py.ReadOrderSortBy.ReceivedTimestamp, False)):
            raise ValueError('bag storage must support explicit receive timestamp ordering')
        found = {}
        for metadata in reader.get_all_topics_and_types():
            if metadata.name in expected:
                if metadata.name in found or metadata.type != expected[metadata.name] or metadata.serialization_format != 'cdr':
                    raise ValueError(f'topic type/serialization mismatch: {metadata.name}')
                found[metadata.name] = get_message(metadata.type)
        if set(found) != set(expected):
            raise ValueError(f'bag is missing required topics: {sorted(set(expected) - set(found))}')
        reader.set_filter(rosbag2_py.StorageFilter(topics=list(expected)))
        previous = None
        sequence = 0
        while reader.has_next():
            topic, payload, receive_ns = reader.read_next()
            if len(payload) > _MAX_MESSAGE_BYTES:
                raise ValueError('selected ROS message exceeds the 64 MiB export limit')
            _integer(receive_ns, 'bag receive time')
            if previous is not None and receive_ns < previous:
                raise ValueError('bag receive timestamps reversed despite explicit read order')
            previous = receive_ns
            sequence += 1
            yield topic, deserialize_message(payload, found[topic]), receive_ns, sequence
    finally:
        reader.close()


def image_array(message):
    """Decode pixels losslessly, retaining declared endian order and data type."""
    import numpy as np

    layouts = {'rgb8': ('u1', 3), 'bgr8': ('u1', 3), 'mono8': ('u1', 1),
               '16UC1': ('u2', 1), '32FC1': ('f4', 1)}
    if message.encoding not in layouts:
        raise ValueError(f'unsupported image encoding: {message.encoding}')
    height = _integer(message.height, 'image height', maximum=32768)
    width = _integer(message.width, 'image width', maximum=32768)
    code, channels = layouts[message.encoding]
    if message.is_bigendian not in (0, 1):
        raise ValueError('invalid image endian flag')
    dtype = np.dtype(('>' if message.is_bigendian else '<') + code)
    row_bytes = width * channels * dtype.itemsize
    if type(message.step) is not int or message.step < row_bytes:
        raise ValueError('image row step is shorter than its pixels')
    size = height * message.step
    if size > _MAX_MESSAGE_BYTES or len(message.data) != size:
        raise ValueError('image payload size does not match height*step or exceeds 64 MiB')
    shape = (height, width, channels) if channels > 1 else (height, width)
    strides = (message.step, channels * dtype.itemsize, dtype.itemsize) if channels > 1 else (message.step, dtype.itemsize)
    return np.array(np.ndarray(shape, dtype=dtype, buffer=bytes(message.data), strides=strides),
                    copy=True, order='C')


def _control_events(source, action, output):
    episode = source['episode']
    previous_sequence = previous_source = previous_monotonic = None
    count = 0
    last_stamp = None
    with output.open('x', encoding='utf-8') as journal:
        for _, message, receipt, _ in _records(source, {action['topic']: _TRACE_TYPE}):
            stamp, category = _stamp_ns(message.header.stamp)
            if category != 'valid':
                raise ValueError('ControlTrace requires a valid nonzero header timestamp')
            if previous_source is not None and stamp < previous_source:
                raise ValueError('ControlTrace source timestamps reversed; cannot reorder labels')
            previous_source = stamp
            if not episode['start_utc_ns'] <= stamp <= episode['end_utc_ns']:
                continue
            sequence = _integer(message.sequence, 'ControlTrace sequence', maximum=2**64 - 1)
            monotonic = _integer(message.monotonic_ns, 'ControlTrace monotonic_ns', maximum=2**64 - 1)
            if previous_sequence is not None and sequence != previous_sequence + 1:
                raise ValueError('ControlTrace sequence gap, duplicate, or reversal in episode')
            if previous_monotonic is not None and monotonic < previous_monotonic:
                raise ValueError('ControlTrace monotonic time reversed in episode')
            previous_sequence, previous_monotonic = sequence, monotonic
            if message.asset_id != source['manifest']['asset'].get('id'):
                raise ValueError('ControlTrace asset_id does not match the recording asset')
            if message.header.frame_id != action['frame']:
                raise ValueError('ControlTrace frame does not match action.frame')
            if message.stage not in ('issued', 'rejected', 'invalidated'):
                raise ValueError(f'unknown ControlTrace stage: {message.stage}')
            if message.command not in ('motion.velocity', 'motion.stop', 'navigation.pose',
                                       'flight.takeoff', 'flight.land'):
                raise ValueError(f'unknown ControlTrace command: {message.command}')
            ttl = message.valid_for_s
            if (isinstance(ttl, bool) or not isinstance(ttl, numbers.Real) or not 0 <= ttl <= 86_400
                    or not math.isfinite(ttl)):
                raise ValueError('ControlTrace valid_for_s must be finite and within [0, 86400]')
            active = message.stage == 'issued' and message.command == 'motion.velocity' and message.velocity_valid
            if not active and (message.velocity_valid or ttl != 0):
                raise ValueError('ControlTrace non-velocity events must not carry a valid velocity or TTL')
            values = _numeric_fields(message, action['fields']) if active else None
            count += 1
            last_stamp = stamp
            _line(journal, {'event_index': count - 1, 'sequence': sequence, 'monotonic_ns': monotonic,
                            'source_ns': stamp, 'receive_ns': receipt,
                            'receive_minus_source_ns': receipt - stamp,
                            'stage': message.stage, 'command': message.command, 'request_id': message.request_id,
                            'code': message.code, 'velocity_valid': bool(message.velocity_valid),
                            'valid_until_ns': stamp + math.floor(ttl * _NS), 'values': values})
    if not count:
        raise ValueError('episode contains no ControlTrace events; request/result logs are not action labels')
    return count, last_stamp


@dataclass
class _Sample:
    source_ns: int
    receive_ns: int
    message_index: int
    message: object
    reason: str | None
    values: list | None = None
    image_path: str | None = None


def _check_buffers(latest, pending):
    retained_bytes = sum(len(sample.message.data) for sample in latest.values()
                         if sample is not None and sample.message is not None)
    retained_bytes += sum(len(item[2].message.data) for queue in pending.values() for item in queue
                          if item[2].message is not None)
    if retained_bytes > _MAX_RETAINED_IMAGE_BYTES:
        raise ValueError('retained image buffers exceed the 128 MiB export limit')


def _sample(message, receipt, index, feature):
    stamp, category = _stamp_ns(field_value(message, feature['stamp_field']))
    if category != 'valid':
        return None
    reason = None
    if field_value(message, feature['frame_field']) != feature['frame']:
        reason = 'frame_mismatch'
    for path in feature.get('validity_fields', []):
        if field_value(message, path) is not True:
            reason = 'invalid_source_flag'
    values = None
    if feature['kind'] == 'vector':
        try:
            values = _numeric_fields(message, feature['fields'])
        except ValueError:
            reason = 'nonfinite_vector'
    return _Sample(stamp, receipt, index, message if feature['kind'] == 'image' else None, reason, values)


def _feature_row(sample, grid, feature, name, episode_dir):
    if sample is None:
        return {'valid': False, 'reason': 'no_causal_sample'}
    result = {'source_ns': sample.source_ns, 'receive_ns': sample.receive_ns,
              'receive_minus_source_ns': sample.receive_ns - sample.source_ns,
              'age_ns': grid - sample.source_ns, 'message_index': sample.message_index,
              'frame': feature['frame'], 'clock_domain': feature['clock_domain']}
    reason = sample.reason
    if grid - sample.source_ns > round(feature['max_age_ms'] * 1_000_000):
        reason = 'sample_too_old'
    result['valid'] = reason is None
    if reason:
        result['reason'] = reason
    elif feature['kind'] == 'vector':
        result['values'] = sample.values
    else:
        import numpy as np

        if sample.image_path is None:
            relative = Path('images') / name / f'{sample.message_index:012d}.npy'
            target = episode_dir / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            pixels = image_array(sample.message)
            with target.open('xb') as output:
                np.save(output, pixels, allow_pickle=False)
            sample.image_path = str(relative)
        result.update({'path': sample.image_path, 'encoding': sample.message.encoding,
                       'height': sample.message.height, 'width': sample.message.width,
                       'original_step': sample.message.step, 'is_bigendian': sample.message.is_bigendian,
                       'units': feature['units']})
    return result


def _action_row(active, grid, action, last_event_ns):
    if active is None:
        return {'valid': False, 'reason': 'no_active_issued_velocity'}
    result = {key: active[key] for key in ('event_index', 'sequence', 'monotonic_ns', 'source_ns',
              'receive_ns', 'receive_minus_source_ns', 'valid_until_ns', 'request_id')}
    reason = None
    if grid >= last_event_ns:
        reason = 'action_unclosed'
    elif grid >= active['valid_until_ns']:
        reason = 'velocity_expired'
    elif grid - active['source_ns'] > round(action['max_age_ms'] * 1_000_000):
        reason = 'action_too_old'
    result.update({'valid': reason is None, 'frame': action['frame'], 'space': action['space'],
                   'clock_domain': action['clock_domain'], 'semantics': 'runtime_issued'})
    if reason:
        result['reason'] = reason
    else:
        result['values'] = active['values']
    return result


def _export_episode(source, profile, destination):
    destination.mkdir()
    episode = source['episode']
    controls = destination / 'control_events.jsonl'
    control_count, last_event_ns = _control_events(source, profile['action'], controls)
    expected = {feature['topic']: feature['type'] for feature in profile['features'].values()}
    stream = _records(source, expected)
    latest = {name: None for name in profile['features']}
    pending = {name: [] for name in profile['features']}
    invalid_stamps = {name: 0 for name in profile['features']}
    source_reversals = {name: 0 for name in profile['features']}
    previous_stamp = {}
    step = Fraction(_NS, 1) / Fraction(str(profile['fps']))
    row_count = valid_count = 0
    invalid_reasons = Counter()
    next_record = next(stream, None)
    active = None
    try:
        with controls.open(encoding='utf-8') as control_stream, (destination / 'steps.jsonl').open('x', encoding='utf-8') as output:
            next_control = next(control_stream, None)
            next_control = json.loads(next_control) if next_control else None
            while True:
                grid = episode['start_utc_ns'] + (row_count * step.numerator) // step.denominator
                if grid >= episode['end_utc_ns']:
                    break
                while next_control is not None and next_control['source_ns'] <= grid:
                    if next_control['stage'] == 'invalidated':
                        active = None
                    elif next_control['stage'] == 'issued':
                        active = next_control if next_control['values'] is not None else None
                    # Rejection is an audit event, never a new command or stop.
                    encoded = next(control_stream, None)
                    next_control = json.loads(encoded) if encoded else None
                while next_record is not None and next_record[2] <= grid:
                    topic, message, receipt, index = next_record
                    for name, feature in profile['features'].items():
                        if feature['topic'] != topic:
                            continue
                        sample = _sample(message, receipt, index, feature)
                        if sample is None:
                            invalid_stamps[name] += 1
                            # An un-timestamped state cannot safely refresh a prior observation.
                            latest[name] = None
                            pending[name].clear()
                            continue
                        if name in previous_stamp and sample.source_ns < previous_stamp[name]:
                            raise ValueError(f'observation source timestamps reversed: {name}')
                        previous_stamp[name] = sample.source_ns
                        if sample.source_ns < episode['start_utc_ns']:
                            continue
                        if sample.source_ns <= grid:
                            old = latest[name]
                            if old is None or (sample.source_ns, receipt, index) >= (old.source_ns, old.receive_ns, old.message_index):
                                latest[name] = sample
                        else:
                            heapq.heappush(pending[name], (sample.source_ns, index, sample))
                            if len(pending[name]) > _MAX_PENDING:
                                raise ValueError('too many future-stamped observations; clock domain may be wrong')
                        _check_buffers(latest, pending)
                    next_record = next(stream, None)
                for name in latest:
                    while pending[name] and pending[name][0][0] <= grid:
                        sample = heapq.heappop(pending[name])[2]
                        old = latest[name]
                        if old is None or (sample.source_ns, sample.receive_ns, sample.message_index) >= (old.source_ns, old.receive_ns, old.message_index):
                            latest[name] = sample
                _check_buffers(latest, pending)
                features = {name: _feature_row(latest[name], grid, feature, name, destination)
                            for name, feature in profile['features'].items()}
                action = _action_row(active, grid, profile['action'], last_event_ns)
                valid = action['valid'] and all(item['valid'] for item in features.values())
                if not action['valid']:
                    invalid_reasons['action:' + action['reason']] += 1
                for name, feature in features.items():
                    if not feature['valid']:
                        invalid_reasons[f'feature:{name}:{feature["reason"]}'] += 1
                _line(output, {'grid_index': row_count, 'timestamp_ns': grid,
                               'elapsed_ns': grid - episode['start_utc_ns'], 'clock_epoch': source['epoch'],
                               'valid': valid, 'features': features, 'action': action})
                row_count += 1
                valid_count += valid
    finally:
        stream.close()
    if not valid_count:
        raise ValueError(f'episode {episode["id"]} has no fully valid training rows; '
                         f'invalid reasons: {dict(invalid_reasons)}')
    report = {**episode, 'clock_epoch': source['epoch'], 'synthetic': source['synthetic'],
              'time_verified': source['time_verified'], 'rows': row_count, 'valid_rows': valid_count,
              'invalid_rows': row_count - valid_count, 'control_events': control_count,
              'invalid_reasons': dict(invalid_reasons),
              'invalid_source_stamps': invalid_stamps, 'observation_source_reversals': source_reversals,
              'asset': source['manifest']['asset'],
              'source_files': [{key: value for key, value in item.items() if not key.startswith('_')}
                               for item in source['sources']]}
    _json(destination / 'episode.json', report)
    return report


def _publish_directory(staging, output):
    """Atomic no-replace publication on the Ubuntu/Linux deployment platform."""
    library = ctypes.CDLL(None, use_errno=True)
    rename = library.renameat2
    rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    if rename(-100, os.fsencode(staging), -100, os.fsencode(output), 1):
        code = ctypes.get_errno()
        raise OSError(code, os.strerror(code), str(output))


def export_dataset(profile_path, output):
    """Export complete labelled episodes, publishing only after every check passes."""
    profile, profile_path, profile_digest = load_profile(profile_path, include_digest=True)
    output = Path(output).expanduser().resolve()
    if output.exists():
        raise FileExistsError(f'output already exists: {output}')
    for episode in profile['episodes']:
        root = Path(episode['session'])
        if output == root or output.is_relative_to(root):
            raise ValueError('dataset output must be outside every source session')
    if not output.parent.is_dir():
        raise ValueError('output parent directory must already exist')
    sources = [_source_episode(episode, profile) for episode in profile['episodes']]
    staging = Path(tempfile.mkdtemp(prefix=f'.{output.name}.partial-', dir=output.parent))
    try:
        (staging / 'episodes').mkdir()
        reports = [_export_episode(source, profile, staging / 'episodes' / source['episode']['id'])
                   for source in sources]
        # Detect source mutation during the two passes and hash collection.
        for source in sources:
            for item in source['sources']:
                path = Path(source['episode']['session']) / item['path']
                stat = path.stat()
                if stat.st_size != item['size_bytes'] or stat.st_mtime_ns != item['_mtime_ns'] or _digest(path) != item['sha256']:
                    raise ValueError('a source recording changed during export')
        if _digest(profile_path) != profile_digest:
            raise ValueError('export profile changed during export')
        report = {'schema_version': 1, 'format': 'chaos_portable_episode', 'status': 'completed',
                  'fps': profile['fps'], 'profile': profile,
                  'profile_sha256': profile_digest, 'synthetic': any(item['synthetic'] for item in sources),
                  'time_verified': all(item['time_verified'] for item in sources),
                  'action_semantics': 'runtime_issued; not hardware-confirmed execution',
                  'observation_alignment': 'latest source<=grid and receive<=grid within episode; no interpolation',
                  'action_alignment': 'offline labels effective at source header time, including late received stops',
                  'image_policy': 'lossless pixel arrays; original encoding/endian/units, no resize or depth scaling',
                  'clock_claim': 'host quality evidence only; sensor UTC clock domain is a profile assertion',
                  'episodes': [{'id': item['id'], 'rows': item['rows'], 'valid_rows': item['valid_rows'],
                                'path': f'episodes/{item["id"]}/episode.json'} for item in reports]}
        _json(staging / 'dataset.json', report)
        _publish_directory(staging, output)
        return report
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile', help='Explicit YAML feature mappings and labelled episode intervals')
    parser.add_argument('--output', required=True, help='New output directory outside every source session')
    args = parser.parse_args(argv)
    try:
        report = export_dataset(args.profile, args.output)
        print(json.dumps({'output': str(Path(args.output).resolve()), 'episodes': report['episodes'],
                          'synthetic': report['synthetic'], 'time_verified': report['time_verified']},
                         ensure_ascii=False, allow_nan=False))
    except Exception as error:
        print(f'Cannot export training dataset: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
