#!/usr/bin/env python3
"""Local mTLS MQTT -> Rust bridge -> gateway -> mock runtime integration.

Requires existing mosquitto clients/server, openssl and a built Rust adapter.
Uses ephemeral certificates in /tmp, a random loopback port, domain 189, and
only mock assets. Never edits the existing broker configuration/credentials.
"""

import importlib.util
import json
import os
from pathlib import Path
import queue
import shutil
import signal
import socket
import subprocess
import tempfile
import threading
import time
import uuid

from ament_index_python.packages import get_package_share_directory
import yaml

from chaos_assets.inventory import load_inventory
from chaos_mqtt.config import build_adapter_config


def run():
    for name in ('mosquitto', 'mosquitto_pub', 'mosquitto_sub', 'openssl'):
        if shutil.which(name) is None:
            raise RuntimeError(f'{name} is required; this script does not install dependencies')
    repo = Path(__file__).resolve().parents[2]
    binary = repo / 'mqtt_ros2_adapter/target/debug/mqtt_ros2_adapter'
    if not binary.is_file():
        raise RuntimeError('Build mqtt_ros2_adapter/target/debug/mqtt_ros2_adapter first')
    os.environ['ROS_DOMAIN_ID'] = '189'
    os.environ['ROS_AUTOMATIC_DISCOVERY_RANGE'] = 'LOCALHOST'
    os.environ.pop('ROS_LOCALHOST_ONLY', None)
    processes = []
    with tempfile.TemporaryDirectory(prefix='chaos-transport-smoke-', dir='/tmp') as folder:
        root = Path(folder)
        os.environ['ROS_LOG_DIR'] = str(root / 'ros-log')
        spec = importlib.util.spec_from_file_location('chaos_local_mqtt_certificates',
                                                      repo / 'mqtt_ros2_adapter/scripts/local_mqtt.py')
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        helper.STATE, helper.CERTS = root, root / 'certs'
        helper.prepare_certificates()
        certs = helper.CERTS
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as temporary_socket:
            temporary_socket.bind(('127.0.0.1', 0))
            port = temporary_socket.getsockname()[1]
        broker_config = root / 'mosquitto.conf'
        broker_config.write_text(
            f'listener {port} 127.0.0.1\ncafile {certs}/ca.crt\n'
            f'certfile {certs}/server.crt\nkeyfile {certs}/server.key\n'
            'require_certificate true\nuse_identity_as_username true\n'
            'allow_anonymous false\nset_tcp_nodelay true\npersistence false\nlog_dest stdout\n')
        share = Path(get_package_share_directory('chaos_assets'))
        inventory_data = {
            'schema_version': 1, 'hosts': {'local': {'ros_domain_id': 189}},
            'transports': {'test': {'executable': str(binary), 'config': {'mqtt': {
                'host': '127.0.0.1', 'port': port, 'client_id': 'chaos-{asset_id}',
                'ca_file': str(certs / 'ca.crt'),
                'client_certificate_file': str(certs / 'client.crt'),
                'client_key_file': str(certs / 'client.key'),
            }}}},
            'assets': [{'id': 'drone_001', 'host': 'local', 'mode': 'mock', 'transport': 'test',
                        'model_file': str(share / 'models/dji_m3td/v1/model.yaml')}],
        }
        inventory_path = root / 'inventory.yaml'
        inventory_path.write_text(yaml.safe_dump(inventory_data))
        inventory = load_inventory(inventory_path)
        asset = inventory.select('local')[0]
        assert asset['driver']['plugin'] == 'mock'
        config = build_adapter_config(asset, inventory.transports['test'])
        generated_path = root / 'generated-adapter.json'
        generated_path.write_text(json.dumps(config))
        subprocess.run([str(binary), '--check-config', str(generated_path)], check=True,
                       capture_output=True, text=True, timeout=10)
        print('PASS: native Rust preflight accepts actual asset-generated configuration')
        mqtt_args = ['-h', '127.0.0.1', '-p', str(port), '--cafile', str(certs / 'ca.crt'),
                     '--cert', str(certs / 'client.crt'), '--key', str(certs / 'client.key'), '-q', '1']
        incoming = queue.Queue()
        messages = []

        def wait_broker():
            deadline = time.monotonic() + 10
            while True:
                probe = subprocess.run(['mosquitto_pub', *mqtt_args, '-t', 'probe', '-m', 'ready'],
                                       capture_output=True, timeout=2)
                if probe.returncode == 0:
                    return
                if time.monotonic() >= deadline:
                    raise AssertionError('local TLS broker did not become ready')
                time.sleep(0.05)

        with (root / 'processes.log').open('w+') as logfile:
            try:
                processes.append(subprocess.Popen(['mosquitto', '-c', str(broker_config)],
                    stdout=logfile, stderr=subprocess.STDOUT, start_new_session=True))
                wait_broker()
                subscriber = subprocess.Popen(['mosquitto_sub', *mqtt_args, '-t', 'robots/drone_001/#', '-v'],
                    stdout=subprocess.PIPE, stderr=logfile, text=True, start_new_session=True)
                processes.append(subscriber)

                def receive(stream):
                    for line in stream:
                        topic, _, payload = line.strip().partition(' ')
                        try:
                            incoming.put((topic, json.loads(payload)))
                        except ValueError:
                            incoming.put((topic, payload))

                reader = threading.Thread(target=receive, args=(subscriber.stdout,), daemon=True)
                reader.start()
                processes.append(subprocess.Popen(
                    ['ros2', 'launch', 'chaos_bringup', 'system.launch.py',
                     f'inventory:={inventory_path}', 'host_id:=local', 'mode:=mock', 'enable_mqtt:=true'],
                    stdout=logfile, stderr=subprocess.STDOUT, start_new_session=True))

                def wait(predicate, timeout=15, after=0):
                    deadline = time.monotonic() + timeout
                    while True:
                        for topic, value in messages[after:]:
                            if predicate(topic, value):
                                return value
                        for process in processes:
                            if process.poll() is not None:
                                raise AssertionError(f'child exited: {process.returncode}')
                        if time.monotonic() >= deadline:
                            raise AssertionError('timed out waiting for local MQTT message')
                        try:
                            messages.append(incoming.get(timeout=0.1))
                        except queue.Empty:
                            pass

                envelopes = {}

                def publish_envelope(envelope):
                    subprocess.run(['mosquitto_pub', *mqtt_args, '-t', config['mqtt']['command_topic'],
                                    '-m', json.dumps(envelope)], check=True, timeout=5,
                                   capture_output=True)

                def request(command, **fields):
                    request_id = uuid.uuid4().hex
                    envelope = {'version': 1, 'message_id': request_id,
                                'timestamp_ms': time.time_ns() // 1_000_000,
                                'command': {'command': command, 'ttl_ms': 2000, **fields}}
                    envelopes[request_id] = envelope
                    publish_envelope(envelope)
                    reply = wait(lambda topic, value: topic == config['mqtt']['response_topic']
                                 and value.get('message_id') == request_id)
                    return request_id, reply['response']

                telemetry = wait(lambda topic, value: topic == 'robots/drone_001/state')
                assert json.loads(telemetry['data'])['asset_id'] == 'drone_001'
                _, described = request('system.describe')
                assert described['ok'] and described['data']['asset_id'] == 'drone_001', described
                _, acquired = request('control.acquire', owner='tls_smoke', duration_s=15.0)
                assert acquired['ok'], acquired
                token = acquired['data']['token']
                print('PASS: mTLS broker + unified enable_mqtt launch + state/description/control round trip')
                for name, altitude in (('flight.takeoff', 0.2), ('flight.land', 0.0)):
                    request_id, accepted = request(name, lease_token=token, altitude_m=altitude)
                    assert accepted['ok'] and accepted['phase'] == 'accepted', accepted

                    def is_result(topic, value):
                        if topic != 'robots/drone_001/task_events':
                            return False
                        event = json.loads(value['data'])
                        return event['request_id'] == request_id and event['terminal']

                    terminal = json.loads(wait(is_result)['data'])
                    assert terminal['ok'] and terminal['goal_id'] == accepted['goal_id'], terminal
                    final_reply = wait(lambda topic, value: topic == config['mqtt']['response_topic']
                                       and value.get('message_id') == request_id
                                       and value.get('response', {}).get('phase') == 'result')['response']
                    assert final_reply['ok'] and final_reply['goal_id'] == accepted['goal_id'], final_reply
                    # Retry the original wire envelope, without refreshing its timestamp or ID.
                    # The Rust replay layer must recover the terminal result without rerunning it.
                    previous = len(messages)
                    publish_envelope(envelopes[request_id])
                    repeated = wait(lambda topic, value: topic == config['mqtt']['response_topic']
                                    and value.get('message_id') == request_id,
                                    after=previous)['response']
                    assert repeated == final_reply, (repeated, final_reply)

                _, renewed = request('control.renew', owner='tls_smoke', token=token, duration_s=30.0)
                assert renewed['ok'], renewed
                offline_id, accepted = request('navigation.to_pose', lease_token=token,
                    target={'frame_id': asset['frames']['map'], 'position': {'x': 2.0}})
                assert accepted['ok'] and accepted['phase'] == 'accepted', accepted
                # Interrupt only this script's temporary broker. ROS execution continues
                # and finishes while the network transport reconnects in the background.
                # Disconnect the observer too: result recovery must work even when
                # the remote subscriber missed the initial terminal publication.
                os.killpg(subscriber.pid, signal.SIGTERM)
                subscriber.wait(timeout=5)
                reader.join(timeout=2)
                broker = processes[0]
                os.killpg(broker.pid, signal.SIGTERM)
                broker.wait(timeout=5)
                time.sleep(3.5)
                processes[0] = subprocess.Popen(['mosquitto', '-c', str(broker_config)],
                    stdout=logfile, stderr=subprocess.STDOUT, start_new_session=True)
                wait_broker()
                while not incoming.empty():
                    incoming.get_nowait()
                previous = len(messages)
                subscriber = subprocess.Popen(['mosquitto_sub', *mqtt_args, '-t', 'robots/drone_001/#', '-v'],
                    stdout=subprocess.PIPE, stderr=logfile, text=True, start_new_session=True)
                processes[1] = subscriber
                reader = threading.Thread(target=receive, args=(subscriber.stdout,), daemon=True)
                reader.start()
                wait(lambda topic, value: topic == 'robots/drone_001/state', timeout=30, after=previous)
                publish_envelope(envelopes[offline_id])
                recovered = wait(lambda topic, value: topic == config['mqtt']['response_topic']
                                 and value.get('message_id') == offline_id
                                 and value.get('response', {}).get('phase') == 'result',
                                 timeout=30, after=previous)['response']
                assert recovered['ok'] and recovered['goal_id'] == accepted['goal_id'], recovered
                previous = len(messages)
                publish_envelope(envelopes[offline_id])
                replayed = wait(lambda topic, value: topic == config['mqtt']['response_topic']
                                and value.get('message_id') == offline_id,
                                after=previous)['response']
                assert replayed == recovered, (replayed, recovered)
                print('PASS: temporary broker outage preserves terminal response and expired-envelope retry recovery')
                _, stopped = request('motion.stop', lease_token=token)
                assert stopped['ok'], stopped
                _, released = request('control.release', owner='tls_smoke', token=token)
                assert released['ok'], released
                print('PASS: actual MQTT action acceptance/terminal events, terminal retry recovery, mock takeoff/landing, stop/release')
            except Exception:
                logfile.flush()
                logfile.seek(0)
                print(logfile.read())
                raise
            finally:
                for process in reversed(processes):
                    if process.poll() is None:
                        os.killpg(process.pid, signal.SIGINT)
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait(timeout=2)


if __name__ == '__main__':
    run()
