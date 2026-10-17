"""Construct launch actions from one already validated configuration snapshot."""

import json
from pathlib import Path
import subprocess
import tempfile

from launch.actions import ExecuteProcess, LogInfo, OpaqueFunction, RegisterEventHandler, EmitEvent, UnsetEnvironmentVariable
from launch.event_handlers import OnProcessExit, OnShutdown
from launch.events import Shutdown
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue

from .collection import (
    collection_processes, collection_summary, on_collection_exit, preflight_collection,
)
from .description import (
    component_asset, description_processes, description_summary, preflight_descriptions,
)
from .sensors import preflight_sensors, sensor_processes, sensor_summary, snapshot_parameter_files


def parse_bool(value, name):
    value = value.lower()
    if value not in ('true', 'false'):
        raise ValueError(f'{name} must be true or false')
    return value == 'true'


def discovery_range(value):
    value = value.upper()
    if value not in ('LOCALHOST', 'SUBNET'):
        raise ValueError('discovery_range must be LOCALHOST or SUBNET')
    return value


def _on_exit(label):
    def callback(event, context):
        if context.is_shutdown:
            return []
        if event.returncode != 0:
            # Raising through launch makes failure visible to service managers,
            # rather than reporting a clean exit after a child crashes.
            raise RuntimeError(f'{label} exited with code {event.returncode}')
        return [EmitEvent(event=Shutdown(reason=f'{label} exited'))]
    return callback


def build_launch_actions(plan, *, dry_run=False, discovery='LOCALHOST'):
    """Preflight all selected components before returning any process action.

    This accepts only the resolved plan; no inventory/model file is read here.
    """
    discovery = discovery_range(discovery)
    # Loading a plugin class validates its contract but never constructs hardware.
    from chaos_runtime.adapters.base import validate_asset
    for item in plan:
        validate_asset(item['asset'])
    summary = [{'id': p['asset']['id'], 'model': p['asset']['model'],
                'version': p['asset']['model_version'], 'mode': p['asset']['mode'],
                'plugin': p['asset']['driver']['plugin'], 'namespace': p['asset']['namespace'],
                'ros_domain_id': p['asset']['ros_domain_id'],
                'capabilities': p['asset']['capabilities'], 'mqtt': p['mqtt'] is not None,
                'sensors': sensor_summary(p['asset']),
                'description': description_summary(p['asset']),
                'data_collection': collection_summary(p['asset'])}
               for p in plan]
    if dry_run:
        return [LogInfo(msg=json.dumps(summary, ensure_ascii=False, indent=2))]
    preflight_descriptions(plan)
    preflight_sensors(plan)
    preflight_collection(plan)
    generated = tempfile.TemporaryDirectory(prefix='chaos-launch-')
    root = Path(generated.name)
    try:
        sensor_parameter_files = snapshot_parameter_files(plan, root)
        # Use the actual transport parser for authoritative validation, before
        # launch receives any process-start action. --check-config does no I/O
        # to ROS, network or hardware.
        for item in plan:
            if item['mqtt']:
                config_file = root / f'{item["asset"]["id"]}-mqtt.json'
                config_file.write_text(json.dumps(item['mqtt']['config'], indent=2), encoding='utf-8')
                config_file.chmod(0o600)
                check = subprocess.run([item['mqtt']['executable'], '--check-config', str(config_file)],
                                       capture_output=True, text=True, timeout=15)
                if check.returncode:
                    detail = check.stderr.strip() or check.stdout.strip()
                    raise RuntimeError(f'MQTT configuration rejected for {item["asset"]["id"]}: {detail}')
    except Exception:
        generated.cleanup()
        raise
    snapshot = root / 'resolved-assets.json'
    snapshot.write_text(json.dumps([p['asset'] for p in plan], indent=2), encoding='utf-8')
    snapshot.chmod(0o600)
    actions = [
        # This deprecated variable overrides ROS_AUTOMATIC_DISCOVERY_RANGE.
        # Clear it for every startup path, including direct ros2 launch use.
        UnsetEnvironmentVariable('ROS_LOCALHOST_ONLY'),
        LogInfo(msg=f'Resolved asset snapshot: {root / "resolved-assets.json"}'),
        RegisterEventHandler(OnShutdown(on_shutdown=[
            OpaqueFunction(function=lambda context: generated.cleanup())])),
    ]
    for item in plan:
        asset = item['asset']
        node_asset = component_asset(asset)
        environment = {'ROS_DOMAIN_ID': str(asset['ros_domain_id']),
                       'ROS_AUTOMATIC_DISCOVERY_RANGE': discovery}
        runtime = Node(package='chaos_runtime', executable='robot_runtime',
                       name='robot_runtime', namespace=asset['namespace'], output='screen',
                       parameters=[{'asset_json': ParameterValue(json.dumps(node_asset), value_type=str)}], additional_env=environment)
        # Fatal child exits stop this host launch; no automatic replay or control
        # reacquisition. An external service manager may restart the whole host.
        actions.append(RegisterEventHandler(OnProcessExit(target_action=runtime,
            on_exit=_on_exit(f'Runtime {asset["id"]}'))))
        actions.append(runtime)
        for label, child in description_processes(asset, environment):
            actions.append(RegisterEventHandler(OnProcessExit(target_action=child, on_exit=_on_exit(label))))
            actions.append(child)
        for label, child in sensor_processes(node_asset, environment, sensor_parameter_files):
            actions.append(RegisterEventHandler(OnProcessExit(target_action=child, on_exit=_on_exit(label))))
            actions.append(child)
        for label, child in collection_processes(node_asset, environment):
            actions.append(RegisterEventHandler(OnProcessExit(
                target_action=child, on_exit=on_collection_exit(label))))
            actions.append(child)
        if item['mqtt']:
            transport = item['mqtt']
            config_file = root / f'{asset["id"]}-mqtt.json'
            gateway = Node(package='chaos_mqtt', executable='gateway', name='mqtt_api',
                           namespace=asset['namespace'], output='screen',
                           parameters=[{'asset_json': ParameterValue(json.dumps(node_asset), value_type=str),
                                        'maximum_clock_skew_ms': int(transport['config']['mqtt'].get('maximum_clock_skew_ms', 5000))}],
                           additional_env=environment)
            process = ExecuteProcess(cmd=[transport['executable'], '--config', str(config_file)],
                                     output='screen', additional_env=environment)
            for child in (gateway, process):
                actions.append(RegisterEventHandler(OnProcessExit(target_action=child,
                    on_exit=_on_exit(f'MQTT component {asset["id"]}'))))
                actions.append(child)
    return actions
