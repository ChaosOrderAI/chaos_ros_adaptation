"""Exercise generated ROS messages without creating a node or using DDS."""

from types import SimpleNamespace

import pytest

pytest.importorskip('rclpy')
from builtin_interfaces.msg import Time
from chaos_mqtt.gateway import Gateway
from chaos_mqtt.protocol import Command, CommandError, Correlations


@pytest.mark.parametrize('deadline_ms', [0, (2 ** 31) * 1000])
def test_deadline_must_fit_generated_ros_time(deadline_ms):
    with pytest.raises(CommandError, match='outside ROS Time range'):
        Gateway._deadline(Command('request_0001', 'motion.stop', deadline_ms, {}))


@pytest.mark.parametrize('name,args', [
    ('control.acquire', {'owner': 'operator', 'token': '', 'duration_s': 3.0}),
    ('control.renew', {'owner': 'operator', 'token': 'token', 'duration_s': 3.0}),
    ('control.release', {'owner': 'operator', 'token': 'token', 'duration_s': 3.0}),
    ('motion.stop', {'lease_token': 'token'}),
    ('flight.takeoff', {'lease_token': 'token', 'altitude_m': 1.0}),
    ('flight.land', {'lease_token': 'token', 'altitude_m': 0.0}),
    ('navigation.to_pose', {'lease_token': 'token', 'target': {
        'frame_id': 'map', 'position': {'x': 1.0, 'y': 0.0, 'z': 0.0},
        'orientation': {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0}}}),
])
def test_services_and_actions_preserve_sender_deadline_after_dispatch_delay(monkeypatch, name, args):
    timestamp_ms = 1700000000000
    monkeypatch.setattr('chaos_mqtt.gateway.time.time_ns', lambda: (timestamp_ms + 400) * 1000000)
    command = Command('request_0001', name, timestamp_ms + 500, args)
    sent = []
    client = SimpleNamespace(server_is_ready=lambda: True,
                             send_goal_async=lambda request: sent.append(request) or
                             SimpleNamespace(add_done_callback=lambda callback: None))
    node = SimpleNamespace(
        lease_client=object(), stop_client=object(), records=Correlations(),
        _deadline=Gateway._deadline, action_clients={name: client},
        get_clock=lambda: SimpleNamespace(now=lambda: SimpleNamespace(to_msg=lambda: Time(sec=42))),
        _service=lambda command, client, request: sent.append(request),
    )
    node._action = lambda request: Gateway._action(node, request)
    node.records.start(command, 100, 5)
    Gateway._dispatch(node, command)
    assert len(sent) == 1
    assert sent[0].expires_at == Time(sec=1700000000, nanosec=500000000)


def test_action_completion_replaces_accepted_reply_and_preserves_event_replay():
    command = Command('request_0001', 'flight.takeoff', 1700000000500, {})
    records = Correlations()
    records.start(command, 100, 5)
    pending = records.pending[command.request_id]
    pending.reply = {'response': {'code': 'ACCEPTED', 'terminal': False}}
    published = []
    node = SimpleNamespace(records=records, result_pub=object(),
                           _publish=lambda publisher, value: published.append(value))
    event = {'ok': True, 'code': 'OK', 'message': 'done', 'goal_id': 'goal-1',
             'execution_state': 'known', 'ros_goal_status': 4, 'terminal': True}
    Gateway._complete_action(node, pending, event)
    reply = published[0]
    assert reply['response']['phase'] == 'result'
    assert reply['response']['terminal'] is True
    assert reply['response']['goal_id'] == 'goal-1'
    assert records.completed[command.request_id] == (reply, event)
