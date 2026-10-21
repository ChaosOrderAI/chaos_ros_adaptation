"""Durable metadata without copying driver credentials into data archives."""

from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
import time
import uuid


def public_asset(asset):
    """A deliberately small allowlist, never the full launch/driver configuration."""
    result = {key: asset[key] for key in (
        'id', 'namespace', 'kind', 'model', 'model_version', 'mode', 'config_digest', 'frames')
        if key in asset}
    result['sensors'] = [
        {'id': sensor_id, 'kind': sensor.get('kind'),
         'streams': {key: {field: stream[field] for field in ('topic', 'type', 'frame_id')
                           if field in stream}
                     for key, stream in sensor.get('streams', {}).items()}}
        for sensor_id, sensor in asset.get('sensors', {}).items()]
    description = asset.get('description', {})
    if description:
        result['description'] = {key: description[key] for key in (
            'enabled', 'format', 'frame_prefix', 'root', 'sha256', 'source_sha256')
            if key in description}
    return result


def _atomic_json(path, data):
    descriptor, temporary = tempfile.mkstemp(prefix='.manifest-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8') as stream:
            json.dump(data, stream, ensure_ascii=False, allow_nan=False, indent=2)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


class Session:
    """One non-overwriting run containing independently readable clock segments.

    A manifest left ``running`` indicates interruption without clean finalization.
    A ``completed`` manifest means shutdown flushed rosbag metadata; it is not a
    claim that clock quality or every selected topic was continuously available.
    """

    def __init__(self, asset, config, *, wall_ns=None, monotonic_ns=None):
        asset_id = asset['id']
        if not isinstance(asset_id, str) or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', asset_id):
            raise ValueError('session asset id must be ROS-safe')
        utc_ns = time.time_ns() if wall_ns is None else wall_ns
        self._start_steady = time.monotonic_ns() if monotonic_ns is None else monotonic_ns
        stamp = datetime.fromtimestamp(utc_ns / 1e9, timezone.utc).strftime('%Y%m%dT%H%M%S')
        self.path = Path(config['output_dir']) / asset_id / f'{stamp}_{uuid.uuid4().hex}'
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.mkdir(mode=0o700, exist_ok=False)
        (self.path / 'segments').mkdir()
        self._manifest_path = self.path / 'manifest.json'
        self._finalized = False
        self._journal = None
        self.manifest = {
            'schema_version': 1, 'session_id': self.path.name,
            'status': 'running', 'started_utc_ns': utc_ns,
            'started_monotonic_ns': self._start_steady,
            'asset': public_asset(asset),
            'recording': {key: config[key] for key in (
                'storage_id', 'topics', 'max_bagfile_size_mb', 'max_bagfile_duration_s',
                'max_cache_size_mb', 'min_free_space_mb', 'time_sync') if key in config},
            'timestamps': {
                'bag_receive_time': 'native rosbag RMW received timestamp (system clock)',
                'bag_publish_time': 'MCAP stores RMW source timestamp; may be zero or unavailable',
                'message_header': 'original publisher stamp, never rewritten',
                'publisher_header_clock_domain': 'unverified; depends on the sensor driver',
                'collector_clock_domain': 'system',
                'monitor_intervals': 'monotonic clock',
                'clock_evidence': 'time_sync.jsonl',
            },
            'segments': [],
        }
        try:
            _atomic_json(self._manifest_path, self.manifest)
            descriptor = os.open(self.path / 'time_sync.jsonl',
                                 os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            self._journal = os.fdopen(descriptor, 'w', encoding='utf-8')
        except BaseException:
            if self._journal is not None:
                self._journal.close()
            raise

    def free_bytes(self):
        return shutil.disk_usage(self.path).free

    def append(self, event):
        if self._finalized:
            raise RuntimeError('session already finalized')
        self._journal.write(json.dumps(event, ensure_ascii=False, allow_nan=False) + '\n')
        self._journal.flush()
        os.fsync(self._journal.fileno())

    def begin_segment(self, *, clock, wall_ns=None, monotonic_ns=None):
        if self._finalized:
            raise RuntimeError('session already finalized')
        if any(row['status'] == 'recording' for row in self.manifest['segments']):
            raise RuntimeError('previous segment is still recording')
        index = len(self.manifest['segments']) + 1
        relative = f'segments/bag_{index:04d}'
        row = {
            'index': index, 'relative_path': relative, 'status': 'recording',
            'started_utc_ns': time.time_ns() if wall_ns is None else wall_ns,
            'started_monotonic_ns': time.monotonic_ns() if monotonic_ns is None else monotonic_ns,
            'clock_at_start': clock,
        }
        self.manifest['segments'].append(row)
        _atomic_json(self._manifest_path, self.manifest)
        return self.path / relative

    def end_segment(self, reason, *, failed=False, suspect_interval=None,
                    wall_ns=None, monotonic_ns=None):
        if not self.manifest['segments']:
            raise RuntimeError('there is no active segment')
        row = self.manifest['segments'][-1]
        if row['status'] != 'recording':
            raise RuntimeError('there is no active segment')
        if not (self.path / row['relative_path'] / 'metadata.yaml').is_file():
            failed = True
            reason += '; bag metadata missing after recorder shutdown'
        row.update({
            'status': 'failed' if failed else 'closed', 'reason': reason,
            'finished_utc_ns': time.time_ns() if wall_ns is None else wall_ns,
            'finished_monotonic_ns': time.monotonic_ns() if monotonic_ns is None else monotonic_ns,
            'suspect_interval': suspect_interval,
        })
        _atomic_json(self._manifest_path, self.manifest)
        return not failed

    def record_startup_check(self, index, evidence):
        self.manifest['segments'][index - 1]['startup_clock_check'] = evidence
        _atomic_json(self._manifest_path, self.manifest)

    def finish(self, status, reason, *, summary=None, wall_ns=None, monotonic_ns=None):
        if self._finalized:
            return
        if status not in ('completed', 'failed'):
            raise ValueError('final status must be completed or failed')
        if status == 'completed' and any(
                row['status'] != 'closed' for row in self.manifest['segments']):
            status, reason = 'failed', 'one or more recording segments did not close cleanly'
        now = time.monotonic_ns() if monotonic_ns is None else monotonic_ns
        self.manifest.update({
            'status': status, 'reason': reason,
            'finished_utc_ns': time.time_ns() if wall_ns is None else wall_ns,
            'duration_monotonic_s': max(0, now - self._start_steady) / 1e9,
            'summary': summary or {},
        })
        try:
            self._journal.flush()
            os.fsync(self._journal.fileno())
            _atomic_json(self._manifest_path, self.manifest)
        finally:
            self._journal.close()
            self._finalized = True
