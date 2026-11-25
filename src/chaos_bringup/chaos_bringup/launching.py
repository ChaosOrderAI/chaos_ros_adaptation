"""Assemble the ROS launch actions from one already validated plan.

Every startup decision is made by the native planner: the selection of the
assets, the collision rules, the MQTT identity, the component derivation and
the preflight checks. This module only turns a native process specification
into a ROS action, writes the private launch artifacts and applies the
shared exit policy.
"""

import json
from pathlib import Path
import subprocess
import tempfile

from launch.actions import ExecuteProcess, LogInfo, OpaqueFunction, RegisterEventHandler, EmitEvent, UnsetEnvironmentVariable
from launch.event_handlers import OnProcessExit, OnShutdown
from launch.events import Shutdown
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue

from .collection import collection_processes, on_collection_exit
from .description import description_processes
from .drivers import driver_processes
from .planning import component_asset, plan_summary, preflight, snapshot_parameter_files
from .sensors import sensor_processes
from .skills import default_vocabulary_file, skill_processes
from .time_sync import time_sync_processes


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


def _runtime_node(asset, environment):
    """The runtime node of one asset."""
    node_asset = component_asset(asset)
    return Node(package='chaos_runtime', executable='robot_runtime',
                name='robot_runtime', namespace=asset['namespace'], output='screen',
                parameters=[{'asset_json': ParameterValue(json.dumps(node_asset), value_type=str)}],
                additional_env=environment)


def build_launch_actions(plan, *, dry_run=False, discovery='LOCALHOST', capabilities=None):
    """Preflight all selected components before returning any process action.

    This accepts only the resolved plan; no inventory/model file is read here.
    The capabilities of the build are injected, so the planner never probes a
    vendor SDK itself: ``registered_writers`` lists the available storage
    plugins, ``backend_available`` reports the native driver backends and
    ``executable_available`` reports the ROS executables of this deployment.
    """
    discovery = discovery_range(discovery)
    summary = plan_summary(plan)
    if dry_run:
        return [LogInfo(msg=json.dumps(summary, ensure_ascii=False, indent=2))]
    # The preflight resolves every dependency of the selection before a single
    # process action exists. The capability of the build is injected, so the
    # planner never probes a vendor SDK itself.
    from .collection import registered_writers
    from .drivers import _backend_available
    from .runtime import validate_asset
    options = dict(capabilities or {})
    preflight(plan,
              registered_writers=options.get('registered_writers', sorted(registered_writers())),
              backend_available=options.get('backend_available', _backend_available),
              executable_available=options.get('executable_available'),
              validate_asset=options.get('validate_asset', validate_asset),
              check_files=True)
    generated = tempfile.TemporaryDirectory(prefix='chaos-launch-')
    root = Path(generated.name)
    try:
        sensor_parameter_files = snapshot_parameter_files(plan, str(root))
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
        environment = {'ROS_DOMAIN_ID': str(asset['ros_domain_id']),
                       'ROS_AUTOMATIC_DISCOVERY_RANGE': discovery}
        # Only the publisher of the description receives the expanded XML.
        # Every other node gets the metadata of the asset, so the document
        # never reaches a process that does not publish it.
        node_asset = component_asset(asset)
        runtime = _runtime_node(asset, environment)
        # Fatal child exits stop this host launch; no automatic replay or control
        # reacquisition. An external service manager may restart the whole host.
        actions.append(RegisterEventHandler(OnProcessExit(target_action=runtime,
            on_exit=_on_exit(f'Runtime {asset["id"]}'))))
        actions.append(runtime)
        for label, child in driver_processes(node_asset, environment):
            actions.append(RegisterEventHandler(OnProcessExit(target_action=child, on_exit=_on_exit(label))))
            actions.append(child)
        for label, child in description_processes(asset, environment):
            actions.append(RegisterEventHandler(OnProcessExit(target_action=child, on_exit=_on_exit(label))))
            actions.append(child)
        for label, child in sensor_processes(node_asset, environment, sensor_parameter_files):
            actions.append(RegisterEventHandler(OnProcessExit(target_action=child, on_exit=_on_exit(label))))
            actions.append(child)
        # The sync board client (one per host) and the per-camera trigger
        # pairing run next to the drivers they stamp. Both are derived from the
        # plan item, so a mock plan or a host without a board starts neither.
        for label, child in time_sync_processes(item, environment):
            actions.append(RegisterEventHandler(OnProcessExit(target_action=child, on_exit=_on_exit(label))))
            actions.append(child)
        for label, child in collection_processes(node_asset, environment):
            actions.append(RegisterEventHandler(OnProcessExit(
                target_action=child, on_exit=on_collection_exit(label))))
            actions.append(child)
        # The skill/agent layer is opt-in per launch: a vocabulary_file selects
        # it for every chosen asset. It is derived in Python because it is a
        # launch-time selection, not an asset contract.
        skill_opts = options.get('skills') or {}
        vocabulary_file = skill_opts.get('vocabulary_file')
        if not vocabulary_file and item['mqtt']:
            # The MQTT capability commands reach the platform through the
            # skill layer, so the layer has to run when MQTT is enabled. The
            # shipped vocabulary is used when the operator named none.
            vocabulary_file = default_vocabulary_file()
            actions.append(LogInfo(msg=(
                f'MQTT is enabled for {asset["id"]}: the skill layer runs with '
                f'the shipped vocabulary {vocabulary_file}')))
        for label, child in skill_processes(
                asset, environment,
                enable_agent=skill_opts.get('enable_agent', False),
                vocabulary_file=vocabulary_file,
                initial_hub=skill_opts.get('initial_hub'),
                llama_endpoint=skill_opts.get('llama_endpoint'),
                task=skill_opts.get('task'),
                iteration_hz=skill_opts.get('iteration_hz'),
                intent_topic=skill_opts.get('intent_topic') or 'skill_intent',
                event_topic=skill_opts.get('event_topic') or 'skill_event',
                announcement_topic=skill_opts.get('announcement_topic') or
                'communication/announcement',
                sensor_cloud_topic=skill_opts.get('sensor_cloud_topic') or '',
                sensor_imu_topic=skill_opts.get('sensor_imu_topic') or ''):
            actions.append(RegisterEventHandler(OnProcessExit(target_action=child,
                on_exit=_on_exit(label))))
            actions.append(child)
        if item['mqtt']:
            transport = item['mqtt']
            config_file = root / f'{asset["id"]}-mqtt.json'
            gateway = Node(package='chaos_mqtt', executable='gateway', name='mqtt_api',
                           namespace=asset['namespace'], output='screen',
                           parameters=[{'asset_json': ParameterValue(json.dumps(component_asset(asset)), value_type=str),
                                        'maximum_clock_skew_ms': int(transport['config']['mqtt'].get('maximum_clock_skew_ms', 5000))}],
                           additional_env=environment)
            process = ExecuteProcess(cmd=[transport['executable'], '--config', str(config_file)],
                                     output='screen', additional_env=environment)
            for child in (gateway, process):
                actions.append(RegisterEventHandler(OnProcessExit(target_action=child,
                    on_exit=_on_exit(f'MQTT component {asset["id"]}'))))
                actions.append(child)
    return actions
