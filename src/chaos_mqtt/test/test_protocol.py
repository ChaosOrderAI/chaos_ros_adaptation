import json

import pytest

from chaos_mqtt.config import build_adapter_config
from chaos_mqtt.protocol import (Command, CommandError, Correlations, parse_command,
                                 recover_identity, response)
from chaos_mqtt.protocol import wire_value


def envelope(command='motion.velocity', timestamp_ms=10000, **fields):
    payload = {'command': command, **fields}
    return json.dumps({'bridge': {'source': 'mqtt', 'request_id': 'request_0001',
                                  'timestamp_ms': timestamp_ms}, 'payload': payload})


def velocity(**fields):
    values = {'lease_token': 'lease_secret', 'velocity': {'linear': {'x': 0.1}},
              'valid_for_s': 0.5, **fields}
    return envelope(**values)


def test_remote_velocity_keeps_original_deadline_after_transport_delay():
    command = parse_command(velocity(), ['motion.velocity'], 10400)
    assert command.deadline_ms == 10500
    assert command.arguments['velocity']['angular']['z'] == 0
    with pytest.raises(CommandError, match='validity elapsed'):
        parse_command(velocity(), ['motion.velocity'], 10501)


@pytest.mark.parametrize('data,now', [
    (velocity(ttl_ms=100), 10100), (velocity(timestamp_ms=10200), 10000),
    (velocity(timestamp_ms=1000, ttl_ms=30000, valid_for_s=30), 10000),
])
def test_stale_future_and_clock_skew_commands_are_rejected(data, now):
    with pytest.raises(CommandError) as error:
        parse_command(data, ['motion.velocity'], now)
    assert error.value.code == 'EXPIRED'


@pytest.mark.parametrize('data', [
    velocity(velocity={'linear': {'x': float('nan')}}),
    velocity(valid_for_s=True), velocity(velocity={'topic': '/hardware/cmd_vel'}),
    velocity(topic='/hardware/cmd_vel'), velocity(lease_token=''),
    envelope(command='navigation.to_pose', lease_token='secret', target={
        'frame_id': 'map', 'position': {}, 'orientation': {'w': 0}}),
    envelope(command='navigation.to_pose', lease_token='secret', target={
        'frame_id': 'map', 'position': {}, 'orientation': {'w': 1e308}}),
])
def test_malformed_control_payloads_are_rejected(data):
    with pytest.raises(CommandError) as error:
        parse_command(data, ['motion.velocity', 'navigation.pose'], 10000)
    assert error.value.code == 'INVALID_ARGUMENT'


def test_capability_filter_and_unknown_commands():
    for data in (envelope('flight.takeoff', lease_token='secret', altitude_m=1),
                 envelope('vendor.shell', command_line='anything')):
        with pytest.raises(CommandError) as error:
            parse_command(data, ['motion.stop'], 10000)
        assert error.value.code == 'UNSUPPORTED'


def test_only_valid_identity_can_receive_a_correlated_error():
    assert recover_identity(velocity(velocity='invalid')) == ('request_0001', 'motion.velocity')
    assert recover_identity('not JSON') is None
    assert recover_identity('{"bridge": []}') is None
    assert recover_identity(velocity().replace('"mqtt"', '"ros"')) is None


def test_pending_and_completed_correlations_are_bounded_and_timeout_is_monotonic():
    records = Correlations(max_pending=1, max_completed=2)
    command = Command('request_0001', 'flight.takeoff', 11000, {})
    records.start(command, 100, 5)
    with pytest.raises(CommandError) as error:
        records.start(Command('request_0002', 'flight.land', 11000, {}), 100, 5)
    assert error.value.code == 'BUSY'
    assert records.expired(104.9) == []
    assert records.expired(105) == ['request_0001']
    reply = response(command.request_id, command.name, success=True, code='ACCEPTED',
                     message='', phase='accepted')
    assert not reply['response']['terminal']
    records.finish(command.request_id, reply, {'phase': 'result'})
    assert not records.pending
    with pytest.raises(CommandError) as error:
        records.start(command, 110, 5)
    assert error.value.code == 'DUPLICATE'
    records.finish('request_0002', {})
    records.finish('request_0003', {})
    assert list(records.completed) == ['request_0002', 'request_0003']


def test_generator_is_pure_scoped_and_no_legacy_drone_or_direct_hardware_routes():
    profile = {'config': {'mqtt': {'host': 'example.invalid', 'client_id': 'edge_{asset_id}'}},
               'executable': '/path/to/mqtt_ros2_adapter'}
    asset = {'id': 'vehicle_001', 'namespace': '/robots/vehicle_001',
             'capabilities': ['state', 'motion.stop', 'motion.velocity']}
    config = build_adapter_config(asset, profile)
    assert config['route_profile'] == 'custom'
    assert config['ros2']['topic_prefix'] == 'robots/vehicle_001/mqtt'
    assert config['mqtt']['client_id'] == 'edge_vehicle_001'
    assert profile['config']['mqtt']['client_id'] == 'edge_{asset_id}'
    commands = {item['command'] for item in config['routes']}
    assert commands == {'system.describe', 'control.acquire', 'control.renew',
                        'control.release', 'motion.stop', 'motion.velocity'}
    assert {item['topic_suffix'] for item in config['routes']} == {'command'}
    assert all(item['direction'] == 'ros2_to_mqtt' for item in config['topic_mappings'])
    assert {item['ros_topic'] for item in config['topic_mappings']} == {
        '/robots/vehicle_001/mqtt/state', '/robots/vehicle_001/mqtt/task_events'}
    assert config['mqtt']['command_topic'] == 'robots/vehicle_001/commands'
    assert {item['name']: item['delivery'] for item in config['topic_mappings']} == {
        'chaos_state': 'latest', 'chaos_task_events': 'latest_nonterminal'}
    assert config['action_timeout_ms'] == 360000


def test_generator_cannot_expire_accepted_actions_before_gateway_timeout():
    asset = {'id': 'unit', 'namespace': '/robots/unit'}
    for timeout in (0, 300000, True, '360000', 86400001):
        with pytest.raises(ValueError, match='action_timeout_ms'):
            build_adapter_config(asset, {'config': {'action_timeout_ms': timeout}})


@pytest.mark.parametrize('asset', [
    {'id': 'bad/id', 'namespace': '/robots/unit'},
    {'id': 'unit', 'namespace': '/robots//unit'},
    {'id': 'unit', 'namespace': 'relative'},
])
def test_generator_rejects_invalid_names(asset):
    with pytest.raises(ValueError):
        build_adapter_config(asset, {'config': {}})


def test_unknown_telemetry_is_null_while_control_nan_remains_invalid():
    state = {'battery_percentage': float('nan'), 'pose': {'z': float('inf')},
             'values': [1.5, float('-inf')], 'pose_valid': False}
    encoded = json.dumps(wire_value(state), allow_nan=False)
    assert json.loads(encoded) == {'battery_percentage': None, 'pose': {'z': None},
                                  'values': [1.5, None], 'pose_valid': False}
    with pytest.raises(CommandError):
        parse_command(velocity(valid_for_s=float('nan')), ['motion.velocity'], 10000)


def test_extreme_json_cannot_escape_validation():
    deeply_nested = '[' * 2000 + '0' + ']' * 2000
    assert recover_identity(deeply_nested) is None
    with pytest.raises(CommandError):
        parse_command(deeply_nested, ['state'], 10000)
    with pytest.raises(CommandError):
        parse_command(velocity(ttl_ms=10 ** 500), ['motion.velocity'], 10000)
    with pytest.raises(CommandError):
        parse_command(velocity(timestamp_ms=10 ** 500), ['motion.velocity'], 10000)


def test_finishing_timed_out_request_cancels_only_local_observation():
    class Future:
        cancelled = False

        def done(self):
            return False

        def cancel(self):
            self.cancelled = True

    records = Correlations()
    command = Command('request_0001', 'flight.takeoff', 11000, {})
    records.start(command, 100, 5)
    future = Future()
    records.pending[command.request_id].future = future
    records.finish(command.request_id, {'execution_state': 'unknown'})
    assert future.cancelled
    assert not records.pending
