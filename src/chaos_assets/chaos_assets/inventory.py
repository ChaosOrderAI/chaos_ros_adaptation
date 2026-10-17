"""Resolve model resources and instance overrides into deterministic runtime assets."""

from copy import deepcopy
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import re
from urllib.parse import urlsplit

import yaml


class ConfigurationError(ValueError):
    pass


class _UniqueLoader(yaml.SafeLoader):
    pass


def _mapping(loader, node, deep=False):
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if not isinstance(key, str):
            raise ConfigurationError('YAML mapping keys must be strings')
        if key in result:
            raise ConfigurationError(f'duplicate YAML key: {key}')
        result[key] = loader.construct_object(value_node, deep=deep)
    return result


_UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _mapping)
_IDENTIFIER = re.compile(r'^[A-Za-z][A-Za-z0-9_]*$')
_TOKEN = re.compile(r'^[A-Za-z0-9][A-Za-z0-9_.-]*$')
_NAMESPACE = re.compile(r'^(/[A-Za-z][A-Za-z0-9_]*)+$')
_DEFAULT_LIMITS = {
    'linear_mps': 1.0, 'angular_rps': 1.0, 'command_timeout_s': 0.5,
    'state_timeout_s': 2.0, 'lease_max_s': 30.0, 'action_timeout_s': 120.0,
    'max_altitude_m': 20.0,
}


def _record(value, where, allowed=None):
    if not isinstance(value, dict):
        raise ConfigurationError(f'{where}: expected a mapping')
    if allowed is not None and set(value) - set(allowed):
        raise ConfigurationError(f'{where}: unknown fields {sorted(set(value) - set(allowed))}')
    return value


def _text(value, where, pattern=None):
    if not isinstance(value, str) or not value or (pattern and not pattern.fullmatch(value)):
        raise ConfigurationError(f'{where}: invalid nonempty string {value!r}')
    return value


def _schema(data, where):
    if type(data.get('schema_version')) is not int or data['schema_version'] != 1:
        raise ConfigurationError(f'{where}: schema_version must be 1')


def _load(path):
    try:
        if path.stat().st_size > 1024 * 1024:
            raise ConfigurationError(f'{path}: configuration exceeds 1 MiB')
        data = yaml.load(path.read_text(encoding='utf-8'), Loader=_UniqueLoader)
    except (OSError, yaml.YAMLError, RecursionError) as error:
        raise ConfigurationError(f'{path}: {error}') from error
    return _record(data, str(path))


def _capabilities(value, where):
    if not isinstance(value, list) or not value:
        raise ConfigurationError(f'{where}: expected a nonempty capability list')
    for capability in value:
        _text(capability, where, _TOKEN)
    if len(set(value)) != len(value):
        raise ConfigurationError(f'{where}: duplicate capabilities')
    if 'state' not in value:
        raise ConfigurationError(f'{where}: state capability is required')
    return list(value)


def _merge(base, override):
    result = deepcopy(base)
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(result.get(key), dict):
            result[key] = _merge(result[key], value)
        else:
            result[key] = deepcopy(value)
    return result


def _limits(base, overrides, where):
    result = dict(_DEFAULT_LIMITS)
    for values in (base, overrides):
        _record(values, where, _DEFAULT_LIMITS)
        for name, value in values.items():
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise ConfigurationError(f'{where}.{name}: expected a positive finite number')
            if not math.isfinite(value) or value <= 0:
                raise ConfigurationError(f'{where}.{name}: expected a positive finite number')
            result[name] = float(value)
    return result


def _resolve_path(value, base, where):
    value = _text(value, where)
    if value.startswith('package://'):
        try:
            uri = urlsplit(value)
        except ValueError as error:
            raise ConfigurationError(f'{where}: invalid package resource URI {value!r}') from error
        relative = Path(uri.path.lstrip('/'))
        if (not re.fullmatch(r'[a-z][a-z0-9_]*', uri.netloc) or not uri.path.strip('/')
                or '..' in relative.parts or uri.query or uri.fragment):
            raise ConfigurationError(f'{where}: invalid package resource URI {value!r}')
        try:
            from ament_index_python.packages import get_package_share_directory
            share = Path(get_package_share_directory(uri.netloc))
        except (ImportError, LookupError) as error:
            raise ConfigurationError(f'{where}: package {uri.netloc!r} is unavailable; source the installed ROS workspace') from error
        return (share / relative).resolve()
    path = Path(value).expanduser()
    return (base / path).resolve() if not path.is_absolute() else path.resolve()


@dataclass(frozen=True)
class Inventory:
    path: Path
    hosts: dict
    assets: tuple
    transports: dict

    def select(self, host_id, asset_ids=None):
        if host_id not in self.hosts:
            raise ConfigurationError(f'unknown host_id: {host_id}')
        requested = set(asset_ids or [])
        enabled = [a for a in self.assets if a['enabled'] and a['host'] == host_id]
        if requested - {a['id'] for a in enabled}:
            raise ConfigurationError(f'assets unavailable on {host_id}: {sorted(requested - {a["id"] for a in enabled})}')
        selected = [deepcopy(a) for a in enabled if not requested or a['id'] in requested]
        if not selected:
            raise ConfigurationError(f'no enabled assets selected on {host_id}')
        return selected

    def as_dict(self):
        return {'schema_version': 1, 'source': str(self.path), 'hosts': deepcopy(self.hosts),
                'assets': deepcopy(list(self.assets)), 'transports': deepcopy(self.transports)}


def load_inventory(path, mode=None):
    """Validate an inventory without starting ROS, importing plugins or opening devices.

    ``mode`` is an explicit backend override (mock/real); absent means each
    asset's configured backend. Paths are relative to their declaring file.
    """
    from .sensors import resolve_sensors, validate_sensor_graph
    from .description import resolve_description, validate_description_graph
    from .collection import resolve_collection

    if mode not in (None, '', 'mock', 'real'):
        raise ConfigurationError('mode override must be mock or real')
    path = Path(path).expanduser().resolve()
    data = _load(path)
    _record(data, 'inventory', {'schema_version', 'hosts', 'assets', 'transports'})
    _schema(data, 'inventory')
    hosts = _record(data.get('hosts'), 'hosts')
    if not hosts:
        raise ConfigurationError('hosts must not be empty')
    for host, config in hosts.items():
        _text(host, 'host ID', _IDENTIFIER)
        _record(config, f'hosts.{host}', {'ros_domain_id'})
        domain = config.get('ros_domain_id')
        if type(domain) is not int or not 0 <= domain <= 232:
            raise ConfigurationError(f'hosts.{host}.ros_domain_id: expected integer 0..232')
    transports = deepcopy(_record(data.get('transports', {}), 'transports'))
    for name, profile in transports.items():
        _text(name, 'transport profile name', _IDENTIFIER)
        _record(profile, f'transports.{name}', {'executable', 'config'})
        executable = _text(profile.get('executable', 'mqtt_transport'), f'transports.{name}.executable')
        profile['executable'] = executable
        if '/' in executable:
            profile['executable'] = str(_resolve_path(executable, path.parent, f'transports.{name}.executable'))
        _record(profile.get('config'), f'transports.{name}.config')
        mqtt = profile['config'].get('mqtt', {})
        _record(mqtt, f'transports.{name}.config.mqtt')
        if 'password' in mqtt:
            raise ConfigurationError('MQTT passwords must use password_environment, not inline password')
        for key in ('ca_file', 'client_certificate_file', 'client_key_file'):
            if key in mqtt:
                mqtt[key] = str(_resolve_path(mqtt[key], path.parent, f'mqtt.{key}'))
    entries = data.get('assets')
    if not isinstance(entries, list) or not entries:
        raise ConfigurationError('assets: expected a nonempty list')
    assets = []
    ids, namespaces, frames_seen = set(), set(), set()
    for entry in entries:
        _record(entry, 'asset', {'id', 'model_file', 'host', 'namespace', 'mode', 'enabled',
                               'driver', 'capabilities', 'frames', 'limits', 'transport', 'sensors', 'description', 'data_collection'})
        asset_id = _text(entry.get('id'), 'asset.id', _IDENTIFIER)
        if asset_id in ids:
            raise ConfigurationError(f'duplicate asset ID: {asset_id}')
        ids.add(asset_id)
        enabled = entry.get('enabled', True)
        if type(enabled) is not bool:
            raise ConfigurationError(f'{asset_id}.enabled: expected boolean')
        host = _text(entry.get('host'), f'{asset_id}.host')
        if host not in hosts:
            raise ConfigurationError(f'{asset_id}: unknown host {host}')
        namespace = _text(entry.get('namespace', f'/robots/{asset_id}'), f'{asset_id}.namespace', _NAMESPACE)
        if namespace in namespaces:
            raise ConfigurationError(f'duplicate namespace: {namespace}')
        namespaces.add(namespace)
        model_path = _resolve_path(entry.get('model_file'), path.parent, f'{asset_id}.model_file')
        model = _load(model_path)
        _record(model, str(model_path), {'schema_version', 'model', 'model_version', 'kind',
                                      'capabilities', 'backends', 'limits', 'resources', 'sensors', 'description', 'data_collection'})
        for declaration in (model, entry):
            if 'description' in declaration:
                _record(declaration['description'], f'{asset_id}.description')
            if 'data_collection' in declaration:
                _record(declaration['data_collection'], f'{asset_id}.data_collection')
        _schema(model, str(model_path))
        model_id = _text(model.get('model'), 'model.model', _TOKEN)
        version = _text(model.get('model_version'), 'model.model_version', _TOKEN)
        kind = _text(model.get('kind'), 'model.kind', _TOKEN)
        supported = _capabilities(model.get('capabilities'), f'{model_id}.capabilities')
        selected_mode = mode or entry.get('mode', 'mock')
        if selected_mode not in ('mock', 'real'):
            raise ConfigurationError(f'{asset_id}.mode: expected mock or real')
        backends = _record(model.get('backends'), f'{model_id}.backends', {'mock', 'real'})
        if selected_mode not in backends:
            raise ConfigurationError(f'{model_id}: no {selected_mode} backend')
        backend = _record(backends[selected_mode], f'{model_id}.{selected_mode}', {'plugin', 'options', 'capabilities'})
        plugin = _text(backend.get('plugin'), 'driver.plugin', _TOKEN)
        backend_caps = _capabilities(backend.get('capabilities', supported), 'backend.capabilities')
        if set(backend_caps) - set(supported):
            raise ConfigurationError(f'{model_id}: backend advertises undeclared capabilities')
        caps = _capabilities(entry.get('capabilities', backend_caps), f'{asset_id}.capabilities')
        if set(caps) - set(backend_caps):
            raise ConfigurationError(f'{asset_id}: capabilities unsupported by {selected_mode} backend: {sorted(set(caps) - set(backend_caps))}')
        override = _record(entry.get('driver', {}), f'{asset_id}.driver', {'options'})
        options = _merge(_record(backend.get('options', {}), 'backend.options'),
                         _record(override.get('options', {}), 'driver.options'))
        frames = {'base': f'{asset_id}/base_link', 'odom': f'{asset_id}/odom', 'map': f'{asset_id}/map'}
        frames.update(_record(entry.get('frames', {}), f'{asset_id}.frames', frames))
        for name, frame in frames.items():
            _text(frame, f'{asset_id}.frames.{name}', re.compile(r'^[A-Za-z][A-Za-z0-9_/]*$'))
            if '//' in frame or frame.endswith('/'):
                raise ConfigurationError(f'invalid frame ID: {frame}')
            # Shared map/odom frames may be intentionally supplied by a fleet
            # localization system; body frames must remain unique within a domain.
            key = (hosts[host]['ros_domain_id'], frame)
            if name == 'base':
                if key in frames_seen:
                    raise ConfigurationError(f'duplicate base frame in ROS domain: {frame}')
                frames_seen.add(key)
        resources = {}
        for name, value in _record(model.get('resources', {}), 'model.resources').items():
            resource = _resolve_path(value, model_path.parent, f'resources.{name}')
            if not resource.is_file():
                raise ConfigurationError(f'resource does not exist: {resource}')
            resources[name] = str(resource)
        transport = entry.get('transport')
        if transport is not None and (not isinstance(transport, str) or transport not in transports):
            raise ConfigurationError(f'{asset_id}: unknown transport profile {transport!r}')
        asset = {
            'id': asset_id, 'model': model_id, 'model_version': version, 'kind': kind,
            'host': host, 'ros_domain_id': hosts[host]['ros_domain_id'], 'namespace': namespace,
            'enabled': enabled, 'mode': selected_mode, 'driver': {'plugin': plugin, 'options': options},
            'capabilities': caps, 'frames': frames,
            'limits': _limits(model.get('limits', {}), entry.get('limits', {}), f'{asset_id}.limits'),
            'resources': resources, 'transport': transport, 'model_file': str(model_path),
        }
        try:
            asset['description'] = resolve_description(
                model.get('description'), entry.get('description'), model_path.parent, path.parent, asset)
            asset['sensors'] = resolve_sensors(
                model.get('sensors', {}), entry.get('sensors', {}), model_path.parent, path.parent, asset)
            asset['data_collection'] = resolve_collection(
                model.get('data_collection'), entry.get('data_collection'), model_path.parent, path.parent, asset)
        except RecursionError as error:
            raise ConfigurationError(f'{asset_id}: configuration nesting is too deep or recursive') from error
        try:
            encoded = json.dumps(asset, sort_keys=True, allow_nan=False)
        except (ValueError, TypeError, RecursionError) as error:
            raise ConfigurationError(f'{asset_id}: options must contain finite JSON-compatible values') from error
        asset['config_digest'] = hashlib.sha256(encoded.encode()).hexdigest()
        assets.append(asset)
    validate_sensor_graph(assets)
    validate_description_graph(assets)
    return Inventory(path, deepcopy(hosts), tuple(assets), transports)
