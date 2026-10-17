"""Prepare all selected assets before any subprocess can start."""

from collections.abc import Sequence
from dataclasses import dataclass, field
import json
import os
from pathlib import Path
import shutil

from ament_index_python.packages import get_package_prefix, PackageNotFoundError

from chaos_assets import ConfigurationError, load_inventory
from chaos_mqtt.config import build_adapter_config


def resolve_mqtt_executable(executable='mqtt_transport'):
    """Resolve chaos_mqtt's transport through the sourced ROS installation.

    Explicit paths and other executable names remain valid overrides. The
    default never falls back to a potentially unrelated binary on PATH.
    """
    # Keep the former default name as an alias, without relying on the removed
    # ROS package or an old binary that happens to remain on PATH.
    if executable in ('mqtt_transport', 'mqtt_ros2_adapter'):
        try:
            prefix = get_package_prefix('chaos_mqtt')
        except PackageNotFoundError as exc:
            raise ConfigurationError(
                'Unified chaos_mqtt package not found; build the workspace '
                'with colcon and source its install/local_setup.bash') from exc
        path = Path(prefix) / 'lib' / 'chaos_mqtt' / 'mqtt_transport'
        if not path.is_file() or not os.access(path, os.X_OK):
            raise ConfigurationError(f'Installed MQTT executable not found or not executable: {path}')
        return str(path.absolute())
    path = shutil.which(executable)
    if path is None:
        raise ConfigurationError(f'MQTT executable not found or not executable: {executable}')
    return str(Path(path).absolute())


@dataclass(frozen=True)
class LaunchPlan(Sequence):
    """An immutable startup snapshot; each access returns detached JSON data.

    Keeping the encoded configuration prevents a caller from modifying the
    checked plan through a nested dict. Launch never reloads the input files.
    """
    _items: tuple[str, ...] = field(repr=False)

    @classmethod
    def from_items(cls, items):
        return cls(tuple(json.dumps(item, allow_nan=False) for item in items))

    def __len__(self):
        return len(self._items)

    def __getitem__(self, index):
        if isinstance(index, slice):
            return tuple(json.loads(item) for item in self._items[index])
        return json.loads(self._items[index])


def prepare_edge_plan(inventory_path, asset_id, enable_mqtt=True, check_files=True):
    """Onboard deployment is explicitly bound to one real asset on one host.

    The developer-facing system launch remains available for multi-asset/mock
    configurations. Edge startup never silently promotes a mock asset to real.
    """
    if not Path(inventory_path).is_absolute():
        raise ConfigurationError('edge inventory must be an absolute path')
    inventory = load_inventory(inventory_path)
    enabled = [asset for asset in inventory.assets if asset['enabled']]
    if len(inventory.hosts) != 1 or len(enabled) != 1:
        raise ConfigurationError('edge deployment requires one host and exactly one enabled asset')
    asset = enabled[0]
    if not asset_id or asset['id'] != asset_id:
        raise ConfigurationError(f'edge asset_id must match the configured asset: {asset["id"]}')
    if asset['mode'] != 'real':
        raise ConfigurationError('edge deployment requires an explicitly configured real backend; use system.launch.py for mock')
    if enable_mqtt and not asset['transport']:
        raise ConfigurationError('edge MQTT is enabled but this asset has no transport profile; configure transport or explicitly disable MQTT')
    return _plan_inventory(inventory, asset['host'], asset_ids=[asset_id],
                           enable_mqtt=enable_mqtt, check_files=check_files)


def prepare_plan(inventory_path, host_id, mode=None, asset_ids=None, enable_mqtt=False,
                 check_files=True):
    inventory = load_inventory(inventory_path, mode=mode)
    return _plan_inventory(inventory, host_id, asset_ids, enable_mqtt, check_files)


def _plan_inventory(inventory, host_id, asset_ids=None, enable_mqtt=False,
                    check_files=True):
    """Validate and freeze the exact inventory already selected by the caller."""
    selected_ids = {a['id'] for a in inventory.select(host_id, asset_ids)}
    client_ids, topics, outputs = set(), set(), set()
    plan = []
    for asset in inventory.assets:
        if not asset['enabled']:
            continue
        options = asset['driver']['options']
        if asset['mode'] == 'real' and 'cmd_vel_topic' in options:
            output = (asset['ros_domain_id'], options['cmd_vel_topic'])
            if output in outputs:
                raise ConfigurationError(f'multiple assets own the same velocity output: {output}')
            outputs.add(output)
        item = {'asset': asset, 'mqtt': None}
        if enable_mqtt and asset['transport']:
            profile = inventory.transports[asset['transport']]
            config = build_adapter_config(asset, profile)
            mqtt = config['mqtt']
            # Conservative global uniqueness, even if two profiles use separate
            # brokers: moving profiles must not silently introduce collisions.
            if mqtt['client_id'] in client_ids:
                raise ConfigurationError(f'duplicate MQTT client_id: {mqtt["client_id"]}')
            client_ids.add(mqtt['client_id'])
            used_topics = [mqtt['command_topic'], mqtt['response_topic']]
            if mqtt.get('event_topic'):
                used_topics.append(mqtt['event_topic'])
            used_topics += [mapping['mqtt_topic'] for mapping in config['topic_mappings']]
            for topic in used_topics:
                if not isinstance(topic, str) or not topic or any(c in topic for c in ('\0', '+', '#')):
                    raise ConfigurationError('MQTT requires nonempty exact topic names')
                if topic in topics:
                    raise ConfigurationError(f'duplicate MQTT topic: {topic}')
                topics.add(topic)
            executable = profile['executable']
            if check_files and asset['id'] in selected_ids:
                executable = resolve_mqtt_executable(executable)
                if not mqtt.get('host'):
                    raise ConfigurationError('MQTT broker host is required')
                for key in ('ca_file', 'client_certificate_file', 'client_key_file'):
                    if not mqtt.get(key) or not Path(mqtt[key]).is_file():
                        raise ConfigurationError(f'MQTT {key} file does not exist')
            item['mqtt'] = {'executable': executable, 'config': config}
        if asset['id'] in selected_ids:
            plan.append(item)
    return LaunchPlan.from_items(plan)
