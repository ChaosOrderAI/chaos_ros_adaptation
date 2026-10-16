"""Generate the existing Rust adapter configuration from resolved asset data."""

from copy import deepcopy
import re


COMMAND_CAPABILITIES = {
    'system.describe': 'state',
    'control.acquire': None,
    'control.renew': None,
    'control.release': None,
    'motion.stop': 'motion.stop',
    'motion.velocity': 'motion.velocity',
    'navigation.to_pose': 'navigation.pose',
    'flight.takeoff': 'flight.takeoff',
    'flight.land': 'flight.land',
}


def build_adapter_config(asset, transport_profile):
    """Return an independent JSON-compatible Rust config, without I/O.

    ``transport_profile['config']`` contains Rust network/mqtt settings. MQTT
    strings can use ``{asset_id}``. Explicit client IDs and command topics are
    preserved; inventory validation must reject collisions between assets.
    Credentials are referenced by paths/environment names, never loaded here.
    """
    asset_id = asset.get('id', asset.get('asset_id'))
    namespace = asset.get('namespace', '')
    if not isinstance(asset_id, str) or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', asset_id):
        raise ValueError('asset id must be a ROS name segment')
    if (not isinstance(namespace, str) or not namespace.startswith('/')
            or not all(re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', part)
                       for part in namespace[1:].split('/'))):
        raise ValueError('asset namespace must be an absolute ROS namespace')
    config = deepcopy(transport_profile.get('config', {}))
    if not isinstance(config, dict):
        raise ValueError('transport config must be a mapping')
    mqtt = config.setdefault('mqtt', {})
    if not isinstance(mqtt, dict):
        raise ValueError('transport config.mqtt must be a mapping')
    for key, value in list(mqtt.items()):
        if isinstance(value, str):
            mqtt[key] = value.replace('{asset_id}', asset_id)
    mqtt.setdefault('client_id', f'chaos_{asset_id}')
    mqtt.setdefault('command_topic', f'robots/{asset_id}/commands')
    mqtt.setdefault('response_topic', f'robots/{asset_id}/responses')
    # The Rust envelope event path is reserved. Task events use topic_mappings.
    mqtt.setdefault('event_topic', '')
    config['route_profile'] = 'custom'
    config['ros2'] = {
        'node_name': f'{asset_id}_mqtt_bridge',
        'topic_prefix': f'{namespace.lstrip("/")}/mqtt',
        'qos_depth': 32,
    }
    config.setdefault('response_timeout_ms', 30000)
    # Must exceed the gateway's action observation timeout (300 seconds by
    # default); it bounds orphan ACCEPTED records after a gateway failure.
    config.setdefault('action_timeout_ms', 360000)
    timeout = config['action_timeout_ms']
    if isinstance(timeout, bool) or not isinstance(timeout, int) or not 360000 <= timeout <= 86400000:
        raise ValueError('action_timeout_ms must be in 360000..86400000 for the unified gateway')
    capabilities = asset.get('capabilities', [])
    config['routes'] = [
        {'command': command, 'topic_suffix': 'command', 'msg_type': 'std_msgs/String'}
        for command, capability in COMMAND_CAPABILITIES.items()
        if capability is None or capability in capabilities
    ]
    # Controlled outward-only mappings. Incoming control always uses the facade.
    config['topic_mappings'] = [
        {
            'name': f'chaos_{name}', 'direction': 'ros2_to_mqtt',
            'mqtt_topic': f'robots/{asset_id}/{name}',
            'ros_topic': f'{namespace}/mqtt/{name}',
            'msg_type': 'std_msgs/msg/String',
            'delivery': 'latest_nonterminal' if name == 'task_events' else 'latest',
            'mqtt': {'qos': 1 if name == 'task_events' else 0, 'retain': False},
            'ros': {'depth': 32, 'reliability': 'reliable', 'durability': 'volatile'},
            'max_payload_bytes': 16384,
        }
        for name in ('state', 'task_events')
    ]
    return config
