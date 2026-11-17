#!/usr/bin/env bash
# Run one robot's telemetry uplink over 4G/LoRa via the MQTT transport.
# TLS is required by the transport: pass --ca/--cert/--key (the example paths
# under /etc/mqtt_ros2_adapter/certs are used by default). For a local broker
# generate a test CA first, e.g. scripts/fleet_test_certs.sh.
set -eo pipefail
# The adapter needs a sourced ROS environment for its message libraries.
if [[ -z "${AMENT_PREFIX_PATH:-}" && -r /opt/ros/jazzy/setup.bash ]]; then
    source /opt/ros/jazzy/setup.bash
fi
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
share="$(source /opt/ros/jazzy/setup.bash >/dev/null 2>&1 && python3 -c 'from ament_index_python.packages import get_package_share_directory; print(get_package_share_directory("chaos_mqtt"))' 2>/dev/null || true)"
if [[ -z "$share" && -d "$task_root/install/chaos_mqtt/share/chaos_mqtt" ]]; then
    share="$task_root/install/chaos_mqtt/share/chaos_mqtt"
fi
transport="${share:+$share/transport}"
example="${ADAPTER_EXAMPLE:-${transport:+$transport/config.fleet.robot.example.json}}"
binary="${ADAPTER_BINARY:-$task_root/install/chaos_mqtt/lib/chaos_mqtt/mqtt_transport}"

robot=go2-01
broker=127.0.0.1
port=8883
output="/tmp/fleet_uplink.json"
ca=/etc/mqtt_ros2_adapter/certs/ca.crt
cert=
key=
odom_topic=
joints_topic=
events_topic=
pose_rate=5
joint_rate=10
bind_iface=true
no_auth=false
check_only=false

usage() {
  echo "Usage: $0 --robot NAME [--broker HOST] [--port N] [--output PATH] [--ca CRT] [--cert CRT] [--key KEY] [--pose-rate HZ] [--joint-rate HZ] [--ros-odom TOPIC] [--ros-joints TOPIC] [--ros-events TOPIC] [--no-bind] [--no-auth] [--check]" >&2
}
while (( $# )); do
  case "$1" in
    --robot) shift; robot="${1:?}" ;;
    --broker) shift; broker="${1:?}" ;;
    --port) shift; port="${1:?}" ;;
    --output) shift; output="${1:?}" ;;
    --ca) shift; ca="${1:?}" ;;
    --cert) shift; cert="${1:?}" ;;
    --key) shift; key="${1:?}" ;;
    --pose-rate) shift; pose_rate="${1:?}" ;;
    --joint-rate) shift; joint_rate="${1:?}" ;;
    --ros-odom) shift; odom_topic="${1:?}" ;;
    --ros-joints) shift; joints_topic="${1:?}" ;;
    --ros-events) shift; events_topic="${1:?}" ;;
    --no-bind) bind_iface=false ;;
    --no-auth) no_auth=true ;;
    --check) check_only=true ;;
    *) usage; exit 2 ;;
  esac
  shift
done
[[ -n "$example" && -f "$example" ]] || { echo "fleet.robot example not found (build/install chaos_mqtt first)" >&2; exit 1; }
[[ -x "$binary" ]] || { echo "mqtt_transport binary not found (build/install chaos_mqtt first)" >&2; exit 1; }

# Robot names land in MQTT topics and client IDs; keep to the MQTT-safe set
# so the generated config passes the transport's own validation.
if ! [[ "$robot" =~ ^[A-Za-z0-9_-]+$ ]]; then
  echo "invalid --robot name '$robot' (letters, digits, '-' and '_' only)" >&2
  exit 2
fi
cert="${cert:-/etc/mqtt_ros2_adapter/certs/${robot}.crt}"
key="${key:-/etc/mqtt_ros2_adapter/certs/${robot}.key}"
odom_topic="${odom_topic:-/robot/odom}"
joints_topic="${joints_topic:-/robot/joint_states}"
events_topic="${events_topic:-/robot/mqtt/task_events}"

# All values are passed to Python via argv (never interpolated into source)
# so quoting/injection issues cannot corrupt the generated config.
python3 - "$example" "$output" \
  "$broker" "$port" "$robot" "$ca" "$cert" "$key" \
  "$odom_topic" "$joints_topic" "$events_topic" \
  "$pose_rate" "$joint_rate" "$bind_iface" "$no_auth" <<'PY'
import json, sys
(src, dst,
 broker, port, robot, ca, cert, key,
 odom_topic, joints_topic, events_topic,
 pose_rate, joint_rate, bind_iface, no_auth) = sys.argv[1:]
c = json.load(open(src))
c['mqtt']['host'] = broker
c['mqtt']['port'] = int(port)
c['mqtt']['client_id'] = robot
c['mqtt']['command_topic'] = f'robots/{robot}/command'
c['mqtt']['response_topic'] = f'robots/{robot}/command/result'
c['mqtt']['ca_file'] = ca
c['mqtt']['client_certificate_file'] = cert
c['mqtt']['client_key_file'] = key
if bind_iface != 'true':
    c['network'] = {'access': 'system'}
if no_auth == 'true':
    c['mqtt']['username'] = ''
for m in c['topic_mappings']:
    if m['name'] == 'pose_uplink':
        m['mqtt_topic'] = f'telemetry/{robot}/pose'
        m['ros_topic'] = odom_topic
        m['max_rate_hz'] = float(pose_rate)
    elif m['name'] == 'joints_uplink':
        m['mqtt_topic'] = f'telemetry/{robot}/joints'
        m['ros_topic'] = joints_topic
        m['max_rate_hz'] = float(joint_rate)
    elif m['name'] == 'task_events_uplink':
        m['mqtt_topic'] = f'telemetry/{robot}/task_events'
        m['ros_topic'] = events_topic
json.dump(c, open(dst, 'w'), indent=2)
PY

if $check_only; then
    exec "$binary" --check-config "$output"
fi
exec "$binary" --config "$output"
