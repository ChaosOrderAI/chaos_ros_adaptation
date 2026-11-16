#!/usr/bin/env bash
# Run the prepared 0.pcd courtyard with the native ROS simulator and Go2 viewer.
set -eo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
scene_dir="${PCD_GO2_SCENE_DIR:-$task_root/generated/pcd0_go2}"
viewer_python="${VIEWER_PYTHON:-/home/yunze/mujoco_playground/.venv/bin/python}"
headless=false
mesh_viewer=false
locomotion=true
render_mode=surfel
robots=1
spacing=1.0
layout=line
base_name=go2
ns_style=nested
declare -A policy_for=()
usage() {
  echo "Usage: $0 [--headless | --mesh] [--no-locomotion] [--gaussian] [--robots N] [--spacing M] [--layout line|ring|grid] [--base-name NAME] [--namespaces flat|nested] [--policy PATH | --policy NAME=PATH]..." >&2
}
while (( $# )); do
  case "$1" in
    --headless) headless=true ;;
    --mesh) mesh_viewer=true ;;
    --no-locomotion) locomotion=false ;;
    --gaussian) render_mode=gaussian ;;
    --robots)
      shift
      robots="${1:-}"
      if ! [[ "$robots" =~ ^[1-8]$ ]]; then usage; exit 2; fi
      ;;
    --spacing)
      shift
      spacing="${1:-}"
      python3 -c 'import sys; v=float(sys.argv[1]); sys.exit(0 if 0.5<=v<=5 else 1)' "$spacing" || { usage; exit 2; }
      ;;
    --layout)
      shift
      layout="${1:-}"
      [[ "$layout" =~ ^(line|ring|grid)$ ]] || { usage; exit 2; }
      ;;
    --base-name)
      shift
      base_name="${1:-}"
      [[ "$base_name" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || { usage; exit 2; }
      ;;
    --namespaces)
      shift
      ns_style="${1:-}"
      [[ "$ns_style" =~ ^(flat|nested)$ ]] || { usage; exit 2; }
      ;;
    --policy)
      shift
      policy_arg="${1:-}"
      [[ -n "$policy_arg" ]] || { usage; exit 2; }
      if [[ "$policy_arg" == *=* ]]; then
        robot_name="${policy_arg%%=*}"
        policy_path="${policy_arg#*=}"
        [[ "$robot_name" =~ ^[A-Za-z_][A-Za-z0-9_]*$ && -f "$policy_path" ]] || { usage; exit 2; }
        policy_for["$robot_name"]="$policy_path"
      else
        [[ -f "$policy_arg" ]] || { usage; exit 2; }
        policy_for['*']="$policy_arg"
      fi
      ;;
    *) usage; exit 2 ;;
  esac
  shift
done
if $mesh_viewer && [[ "$render_mode" == gaussian ]]; then
  echo "--gaussian uses the GPU viewer and cannot be combined with --mesh." >&2
  exit 2
fi
if $mesh_viewer && (( robots > 1 )); then
  echo "--robots uses the GPU viewer and cannot be combined with --mesh." >&2
  exit 2
fi
if [[ ! -f "$scene_dir/simulator.yaml" || ! -f "$scene_dir/go2_scene.xml" ]]; then
  echo "Prepare the scene with scripts/prepare_pcd_go2.py first; see docs/pcd-go2-scene.md." >&2
  exit 1
fi
exec 9>"$scene_dir/run.lock"
if ! flock -n 9; then
  echo "This prepared scene is already running; close its viewer before restarting." >&2
  exit 1
fi
if ! $headless && ! $mesh_viewer; then
  python3 "$task_root/scripts/prepare_pcd_gpu.py" --scene-dir "$scene_dir" --if-needed
  python3 "$task_root/scripts/prepare_pcd_normals.py" --scene-dir "$scene_dir" --if-needed
  python3 "$task_root/scripts/prepare_3d_gaussians.py" --scene-dir "$scene_dir" --if-needed
fi
manifest="$scene_dir/robots.json"
if (( robots > 1 )); then
  [[ "$base_name" != go2 ]] && manifest="$scene_dir/robots_${base_name}.json"
  python3 "$task_root/scripts/compose_multi_go2.py" --scene-dir "$scene_dir" \
    --count "$robots" --base-name "$base_name" --layout "$layout" \
    --spacing "$spacing" --namespace-style "$ns_style" \
    --manifest "$manifest" --if-needed
fi
# Point-cloud visuals are supplied by the GPU. New scenes provide a light robot
# model so the viewer need not duplicate the simulator's invisible collision map.
gpu_viewer_model="$scene_dir/go2_viewer.xml"
if (( robots > 1 )); then
  [[ -f "$scene_dir/go2_viewer_multi.xml" ]] && gpu_viewer_model="$scene_dir/go2_viewer_multi.xml"
  [[ ! -f "$gpu_viewer_model" ]] && gpu_viewer_model="$scene_dir/go2_physics_multi.xml"
elif [[ ! -f "$gpu_viewer_model" ]]; then
  gpu_viewer_model="$scene_dir/go2_physics.xml"
fi
source /opt/ros/jazzy/setup.bash
source "$task_root/install-sim/local_setup.bash"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-198}"
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
unset ROS_LOCALHOST_ONLY
export ROS_LOG_DIR="$scene_dir/ros-log"
export PYTHONPATH="$scene_dir/python:/usr/lib/python3/dist-packages:${PYTHONPATH:-}"
sim_config="$scene_dir/simulator.yaml"
if $locomotion || (( robots > 1 )); then
  if $locomotion && ! "$viewer_python" -c 'import onnxruntime' >/dev/null 2>&1; then
    echo "Install the locomotion runtime: python3 -m pip install --target '$scene_dir/python' --no-deps onnxruntime==1.23.2" >&2
    exit 1
  fi
  # Keep the original standing configuration for --no-locomotion. No changes
  # to point-cloud data or the contact geometry are needed. Several robots share
  # one compiled collision scene instead of loading a copy per robot.
  python3 - "$scene_dir" "$robots" "$locomotion" "$manifest" <<'PY'
import json, sys
from pathlib import Path
scene = Path(sys.argv[1])
robots = int(sys.argv[2])
locomotion = sys.argv[3] == 'true'
manifest_path = Path(sys.argv[4])
config = json.loads((scene / 'simulator.yaml').read_text())
params = config['chaos_simulator']['ros__parameters']
worlds = json.loads(params['worlds_json'])
base = 'go2'
if robots > 1:
    manifest = json.loads(manifest_path.read_text())
    base = manifest['robots'][0]['name']
for world in worlds:
    if world['name'] == 'go2':
        if robots > 1:
            world['model_path'] = str(scene / 'go2_physics_multi.xml')
            world['robot_namespaces'] = manifest['robot_namespaces']
            world['namespace'] = manifest['robots'][0]['namespace']
        if locomotion:
            world.update(kp=50., kd=.5)
if locomotion:
    params['publish_rate_hz'] = 100.
params['worlds_json'] = json.dumps(worlds)
(scene / 'simulator_locomotion.yaml').write_text(json.dumps(config, indent=2) + '\n')
PY
  sim_config="$scene_dir/simulator_locomotion.yaml"
fi
mkdir -p "$ROS_LOG_DIR"
sim_pid=""
viewer_pid=""
loco_pids=()
cleanup() {
  trap - EXIT INT TERM
  local pid
  for pid in "$viewer_pid" "${loco_pids[@]}" "$sim_pid"; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then kill -INT "$pid"; fi
  done
  wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM
"$task_root/install-sim/chaos_sim/lib/chaos_sim/simulator" \
  --ros-args --params-file "$sim_config" >"$scene_dir/simulator.log" 2>&1 9>&- &
sim_pid=$!
echo "Go2 simulator PID=$sim_pid; ROS domain=$ROS_DOMAIN_ID; log=$scene_dir/simulator.log"
children=("$sim_pid")
if $locomotion; then
  robot_names=("$base_name")
  if (( robots > 1 )); then
    if [[ ! -f "$manifest" ]]; then
      echo "Missing robot manifest $manifest; run with --robots N to compose it." >&2
      exit 1
    fi
    mapfile -t robot_names < <(python3 -c 'import json,sys; print("\n".join(r["name"] for r in json.load(open(sys.argv[1]))["robots"]))' "$manifest")
  fi
  if (( robots > 1 )); then
    mapfile -t robot_ns < <(python3 - "$manifest" <<'PY'
import json, sys
data = json.load(open(sys.argv[1]))
print('\n'.join(r['namespace'] for r in data['robots']))
PY
)
  fi
  for i in "${!robot_names[@]}"; do
    name="${robot_names[i]}"
    ns="/sim/$name"
    if (( robots > 1 )); then ns="${robot_ns[i]}"; fi
    log="$scene_dir/locomotion.log"
    if (( robots > 1 )); then log="$scene_dir/locomotion_${name}.log"; fi
    policy_args=()
    if [[ -n "${policy_for[$name]:-}" ]]; then
      policy_args=(--policy "${policy_for[$name]}")
    elif [[ -n "${policy_for['*']:-}" ]]; then
      policy_args=(--policy "${policy_for['*']}")
    fi
    "$viewer_python" -u "$task_root/scripts/go2_locomotion.py" \
      --name "$name" --namespace "$ns" "${policy_args[@]}" \
      >"$log" 2>&1 9>&- &
    loco_pids+=("$!")
    echo "Go2 locomotion $name PID=${loco_pids[-1]} namespace=$ns log=$log"
  done
  children+=("${loco_pids[@]}")
  if (( robots > 1 )); then
    echo "Keys 1-$robots select one robot (1=$base_name, default), 0 selects all; W/S A/D Q/E move the selection. Space stops, R resets it."
  else
    echo "W/S forward/back, A/D turn, Q/E sideways, Space stop, R reset."
  fi
fi
if $headless; then
  wait -n "${children[@]}"
else
  if $mesh_viewer; then
    "$viewer_python" -u "$task_root/src/chaos_sim/tools/viewer.py" --robot go2 \
      --model-path "$scene_dir/go2_scene.xml" \
      --camera-distance 5 --camera-azimuth -110 --camera-elevation -25 \
      >"$scene_dir/viewer.log" 2>&1 9>&- &
  else
    # Select the discrete GPU on PRIME laptops; the viewer reports the actual
    # OpenGL renderer and rejects software rendering unless explicitly selected.
    __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia \
      "$viewer_python" -u "$task_root/scripts/pcd_gpu_viewer.py" \
      --cloud "$scene_dir/cloud_gpu.npy" --model-path "$gpu_viewer_model" \
      --namespaces "$manifest" \
      --render-mode "$render_mode" --state-timeout 240 \
      --camera-distance 5 --camera-azimuth -110 --camera-elevation -25 \
      >"$scene_dir/viewer.log" 2>&1 9>&- &
  fi
  viewer_pid=$!
  echo "Go2 viewer PID=$viewer_pid; log=$scene_dir/viewer.log; close the viewer or press Ctrl-C to stop."
  children+=("$viewer_pid")
  wait -n "${children[@]}"
fi
