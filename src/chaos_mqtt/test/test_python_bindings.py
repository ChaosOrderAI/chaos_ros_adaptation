"""Thin-binding contract: the Python facade delegates to native C++ code."""
import pytest

from chaos_mqtt.config import build_adapter_config


def test_build_adapter_config_is_native_and_validates():
    profile = {'config': {'mqtt': {'host': 'example.invalid'}}}
    asset = {'id': 'vehicle_001', 'namespace': '/robots/vehicle_001',
             'capabilities': ['state', 'motion.velocity']}
    config = build_adapter_config(asset, profile)
    assert config['route_profile'] == 'custom'
    assert config['mqtt']['client_id'] == 'chaos_vehicle_001'
    assert config['ros2']['topic_prefix'] == 'robots/vehicle_001/mqtt'
    commands = [route['command'] for route in config['routes']]
    assert commands == ['system.describe', 'control.acquire', 'control.renew',
                        'control.release', 'motion.velocity']
    with pytest.raises(ValueError):
        build_adapter_config({'id': 'bad/id', 'namespace': '/robots/unit'}, profile)
    with pytest.raises(ValueError):
        build_adapter_config(asset, {'config': {'action_timeout_ms': 1}})


def test_native_extension_is_compiled_cpp():
    from chaos_mqtt import _chaos_mqtt_core
    assert _chaos_mqtt_core.__file__.endswith('.so')
    assert 'build_adapter_config' in dir(_chaos_mqtt_core)
