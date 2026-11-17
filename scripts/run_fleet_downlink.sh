#!/usr/bin/env bash
# Run the platform-side downlink: MQTT telemetry -> local ROS topics.
# Mirrors run_fleet_uplink.sh: robots report telemetry/<name>/{pose,joints,task_events}
# and the platform republishes them under /fleet/<name>/{odom,joint_states,task_events}.
# Also forwards robots/<name>/command to /fleet/<name>/command so an operator can
# send commands through the same broker to each robot's gateway.
# TLS is required; pass --ca/--cert/--key for the platform client.
set -eo pipefail
if [[ -z "${AMENT_PREFIX_PATH:-}" && -r /opt/ros/jazzy/setup.bash ]]; then
    source /opt/ros/jazzy/setup.bash
fi
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
share="$(source /opt/ros/jazzy/setup.bash >/dev/null 2>&1 && python3 -c 'from ament_index_python.packages import get_package_share_directory; print(get_package_share_directory("chaos_mqtt"))' 2>/dev/null || true)"
if [[ -z "$share" && -d "$task_root/install/chaos_mqtt/share/chaos_mqtt" ]]; then
    share="$task_root/install/chaos_mqtt/share/chaos_mqtt"
fi
transport="${share:+$share/transport}"
example="${ADAPTER_EXAMPLE:-${transport:+$transport/config.fleet.platform.example.json}}"
binary="${ADAPTER_BINARY:-$task_root/install/chaos_mqtt/lib/chaos_mqtt/mqtt_transport}"

broker=127.0.0.1
port=8883
output=/tmp/fleet_downlink.json
ca=/etc/mqtt_ros2_adapter/certs/ca.crt
cert=/etc/mqtt_ros2_adapter/certs/platform.crt
key=/etc/mqtt_ros2_adapter/certs/platform.key
robots_csv=
no_auth=false
check_only=false

usage() {
  echo "Usage: $0 --robots NAME[,NAME...] [--broker HOST] [--port N] [--output PATH] [--ca CRT] [--cert CRT] [--key KEY] [--no-auth] [--check]" >&2
}
while (( $# )); do
  case "$1" in
    --robots) shift; robots_csv="${1:?}" ;;
    --broker) shift; broker="${1:?}" ;;
    --port) shift; port="${1:?}" ;;
    --output) shift; output="${1:?}" ;;
    --ca) shift; ca="${1:?}" ;;
    --cert) shift; cert="${1:?}" ;;
    --key) shift; key="${1:?}" ;;
    --no-auth) no_auth=true ;;
    --check) check_only=true ;;
    *) usage; exit 2 ;;
  esac
  shift
done
[[ -n "$robots_csv" ]] || { usage; exit 2; }
[[ -n "$example" && -f "$example" ]] || { echo "fleet.platform example not found (build/install chaos_mqtt first)" >&2; exit 1; }
[[ -x "$binary" ]] || { echo "mqtt_transport binary not found (build/install chaos_mqtt first)" >&2; exit 1; }

# Validate every robot name up front: each lands in MQTT topics, and its
# '-'-sanitized form must not collide with another entry or violate ROS
# topic naming rules.
python3 - "$robots_csv" <<'PY'
import sys
robots = sys.argv[1].split(',')
ros_names = set()
for r in robots:
    if not r or not all(c.isalnum() or c in '-_' for c in r):
        sys.exit(f"invalid robot name {r!r} (letters, digits, '-' and '_' only)")
    ros = r.replace('-', '_')
    if ros in ros_names:
        sys.exit(f"robot name {r!r} collides with another after '-'->'_' sanitization")
    ros_names.add(ros)
PY

python3 - "$example" "$output" \
  "$broker" "$port" "$ca" "$cert" "$key" "$robots_csv" "$no_auth" <<'PY'
import json, sys
(src, dst,
 broker, port, ca, cert, key,
 robots_csv, no_auth) = sys.argv[1:]
c = json.load(open(src))
c['mqtt']['host'] = broker
c['mqtt']['port'] = int(port)
c['mqtt']['client_id'] = 'fleet-platform'
c['mqtt']['ca_file'] = ca
c['mqtt']['client_certificate_file'] = cert
c['mqtt']['client_key_file'] = key
if no_auth == 'true':
    c['mqtt']['username'] = ''
robots = robots_csv.split(',')
mappings = []
for robot in robots:
    ros_name = robot.replace('-', '_')
    mappings.extend([
        {
            'name': f'{robot}_pose_downlink',
            'direction': 'mqtt_to_ros2',
            'mqtt_topic': f'telemetry/{robot}/pose',
            'ros_topic': f'/fleet/{ros_name}/odom',
            'msg_type': 'nav_msgs/msg/Odometry',
            'mqtt': {'qos': 0, 'retain': False},
            'ros': {'depth': 4, 'reliability': 'reliable', 'durability': 'volatile'},
            'max_payload_bytes': 8192,
        },
        {
            'name': f'{robot}_joints_downlink',
            'direction': 'mqtt_to_ros2',
            'mqtt_topic': f'telemetry/{robot}/joints',
            'ros_topic': f'/fleet/{ros_name}/joint_states',
            'msg_type': 'sensor_msgs/msg/JointState',
            'mqtt': {'qos': 0, 'retain': False},
            'ros': {'depth': 4, 'reliability': 'reliable', 'durability': 'volatile'},
            'max_payload_bytes': 8192,
        },
        {
            'name': f'{robot}_events_downlink',
            'direction': 'mqtt_to_ros2',
            'mqtt_topic': f'telemetry/{robot}/task_events',
            'ros_topic': f'/fleet/{ros_name}/task_events',
            'msg_type': 'std_msgs/msg/String',
            'mqtt': {'qos': 1, 'retain': False},
            'ros': {'depth': 32, 'reliability': 'reliable', 'durability': 'volatile'},
            'max_payload_bytes': 8192,
        },
        {
            'name': f'{robot}_command_downlink',
            'direction': 'mqtt_to_ros2',
            'mqtt_topic': f'robots/{robot}/command',
            'ros_topic': f'/fleet/{ros_name}/command',
            'msg_type': 'std_msgs/msg/String',
            'mqtt': {'qos': 1, 'retain': False},
            'ros': {'depth': 8, 'reliability': 'reliable', 'durability': 'volatile'},
            'max_payload_bytes': 8192,
        },
    ])
c['topic_mappings'] = mappings
json.dump(c, open(dst, 'w'), indent=2)
PY

if $check_only; then
    exec "$binary" --check-config "$output"
fi
exec "$binary" --config "$output"
