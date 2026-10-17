"""Resolve local recording policy without starting clocks, ROS or file writers."""

from copy import deepcopy
import math
from pathlib import Path
import re

from .inventory import ConfigurationError, _merge, _record


_TOPIC = re.compile(r'^(/[A-Za-z_][A-Za-z0-9_]*)+$')
_DEFAULTS = {
    'enabled': False, 'output_dir': '/var/lib/chaos/recordings',
    'storage_id': 'mcap', 'include_state': True, 'include_sensors': True,
    'include_tf': True, 'max_bagfile_duration_s': 300,
    'max_bagfile_size_mb': 1024, 'max_cache_size_mb': 64,
    'min_free_space_mb': 1024,
    'time_sync': {
        'require_sync': True, 'max_uncertainty_ms': 10.0,
        'max_reference_age_s': 120.0, 'poll_interval_s': 1.0,
        'probe_timeout_s': 0.5, 'stable_samples': 3, 'jump_threshold_ms': 100.0,
    },
}


def resolve_collection(model, instance, model_dir, instance_dir, asset):
    if model is None and instance is None:
        return None
    where = f'{asset["id"]}.data_collection'
    allowed = set(_DEFAULTS) | {'topics'}
    for value in (model, instance):
        if value is not None:
            _record(value, where, allowed)
            if 'time_sync' in value:
                _record(value['time_sync'], f'{where}.time_sync', _DEFAULTS['time_sync'])
    result = _merge(_merge(deepcopy(_DEFAULTS), model or {}), instance or {})
    for name in ('enabled', 'include_state', 'include_sensors', 'include_tf'):
        if type(result[name]) is not bool:
            raise ConfigurationError(f'{where}.{name}: expected boolean')
    if result['storage_id'] not in ('mcap', 'sqlite3'):
        raise ConfigurationError(f'{where}.storage_id: expected mcap or sqlite3')
    output = result['output_dir']
    if not isinstance(output, str) or not output.strip() or '\0' in output or '://' in output:
        raise ConfigurationError(f'{where}.output_dir: expected a local directory path')
    base = instance_dir if instance and 'output_dir' in instance else model_dir
    output = Path(output).expanduser()
    result['output_dir'] = str((base / output if not output.is_absolute() else output).resolve())
    for name, low, high in (
        ('max_bagfile_duration_s', 0, 86400), ('max_bagfile_size_mb', 0, 1048576),
        ('max_cache_size_mb', 1, 2048), ('min_free_space_mb', 1, 1048576),
    ):
        value = result[name]
        if type(value) is not int or not low <= value <= high:
            raise ConfigurationError(f'{where}.{name}: expected integer {low}..{high}')
    if not result['max_bagfile_duration_s'] and not result['max_bagfile_size_mb']:
        raise ConfigurationError(f'{where}: at least one bag splitting limit must be positive')
    if result['min_free_space_mb'] < result['max_cache_size_mb']:
        raise ConfigurationError(f'{where}: free-space reserve must cover the recorder cache')
    timing = result['time_sync']
    if type(timing['require_sync']) is not bool:
        raise ConfigurationError(f'{where}.time_sync.require_sync: expected boolean')
    for name, low, high in (
        ('max_uncertainty_ms', 0.001, 1000.0), ('max_reference_age_s', 1.0, 3600.0),
        ('poll_interval_s', 0.1, 10.0), ('probe_timeout_s', 0.01, 5.0),
        ('jump_threshold_ms', 1.0, 10000.0),
    ):
        value = timing[name]
        if (isinstance(value, bool) or not isinstance(value, (int, float))
                or not low <= value <= high or not math.isfinite(value)):
            raise ConfigurationError(f'{where}.time_sync.{name}: expected finite number {low}..{high}')
        timing[name] = float(value)
    if timing['probe_timeout_s'] > timing['poll_interval_s']:
        raise ConfigurationError(f'{where}: clock probe timeout must not exceed polling interval')
    if type(timing['stable_samples']) is not int or not 1 <= timing['stable_samples'] <= 100:
        raise ConfigurationError(f'{where}.time_sync.stable_samples: expected integer 1..100')
    description = asset.get('description')
    if result['enabled'] and description and description.get('enabled') and description.get('use_sim_time'):
        raise ConfigurationError(f'{where}: collection currently requires system time, not use_sim_time')
    if result['enabled']:
        for sensor in asset.get('sensors', {}).values():
            driver = sensor.get('driver', {})
            if (sensor['enabled'] and driver.get('mode') == 'node'
                    and driver.get('parameters', {}).get('use_sim_time') is True):
                raise ConfigurationError(f'{where}: managed sensor use_sim_time must be false during collection')
    namespace = asset['namespace']
    if 'topics' in result:
        topics = result['topics']
        if not isinstance(topics, list) or not topics:
            raise ConfigurationError(f'{where}.topics: expected a nonempty list')
        resolved = []
        for topic in topics:
            if not isinstance(topic, str):
                raise ConfigurationError(f'{where}.topics: expected topic strings')
            topic = topic if topic.startswith('/') else f'{namespace}/{topic}'
            if not _TOPIC.fullmatch(topic):
                raise ConfigurationError(f'{where}: invalid exact ROS topic {topic!r}')
            if topic in resolved:
                raise ConfigurationError(f'{where}: duplicate collection topic {topic}')
            resolved.append(topic)
    else:
        resolved = []
        if result['include_state']:
            resolved.extend(f'{namespace}/{name}' for name in ('state', 'command_results', 'control_trace'))
        if result['include_sensors']:
            for sensor in asset.get('sensors', {}).values():
                if sensor['enabled']:
                    resolved.extend(stream['topic'] for stream in sensor['streams'].values())
            if any(sensor['enabled'] for sensor in asset.get('sensors', {}).values()):
                resolved.extend(f'{namespace}/sensors/{name}' for name in ('description', 'diagnostics'))
        if result['include_tf']:
            resolved.extend(('/tf', '/tf_static'))
        if description and description.get('enabled'):
            resolved.extend((f'{namespace}/robot_description', description['joint_states_topic']))
    resolved.append(f'{namespace}/data_collection/status')
    result['topics'] = list(dict.fromkeys(resolved))
    if len(result['topics']) > 256:
        raise ConfigurationError(f'{where}: at most 256 topics can be recorded')
    return result
