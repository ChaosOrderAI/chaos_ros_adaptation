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
while (( $# )); do
  case "$1" in
    --headless) headless=true ;;
    --mesh) mesh_viewer=true ;;
    --no-locomotion) locomotion=false ;;
    --gaussian) render_mode=gaussian ;;
    --robots)
      shift
      robots="${1:-}"
      if ! [[ "$robots" =~ ^[1-8]$ ]]; then
        echo "Usage: $0 [--headless | --mesh] [--no-locomotion] [--gaussian] [--robots N]" >&2
        exit 2
      fi
      ;;
    *) echo "Usage: $0 [--headless | --mesh] [--no-locomotion] [--gaussian] [--robots N]" >&2; exit 2 ;;
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
if (( robots > 1 )); then
  python3 "$task_root/scripts/compose_multi_go2.py" --scene-dir "$scene_dir" --count "$robots"
fi
# Point-cloud visuals are supplied by the GPU. New scenes provide a light robot
# model so the viewer need not duplicate the simulator's invisible collision map.
gpu_viewer_model="$scene_dir/go2_viewer.xml"
if (( robots > 1 )) && [[ -f "$scene_dir/go2_viewer_multi.xml" ]]; then
  gpu_viewer_model="$scene_dir/go2_viewer_multi.xml"
elif [[ ! -f "$gpu_viewer_model" ]]; then
  gpu_viewer_model="$scene_dir/go2_physics.xml"
  if (( robots > 1 )); then gpu_viewer_model="$scene_dir/go2_physics_multi.xml"; fi
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
  python3 - "$scene_dir" "$robots" "$locomotion" <<'PY'
import json, sys
from pathlib import Path
scene = Path(sys.argv[1])
robots = int(sys.argv[2])
locomotion = sys.argv[3] == 'true'
config = json.loads((scene / 'simulator.yaml').read_text())
params = config['chaos_simulator']['ros__parameters']
worlds = json.loads(params['worlds_json'])
for world in worlds:
    if world['name'] == 'go2':
        if robots > 1:
            world['model_path'] = str(scene / 'go2_physics_multi.xml')
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
  robot_names=(go2)
  if (( robots > 1 )); then
    mapfile -t robot_names < <(python3 -c 'import json,sys; print("\n".join(r["name"] for r in json.load(open(sys.argv[1]))["robots"]))' "$scene_dir/robots.json")
  fi
  for name in "${robot_names[@]}"; do
    log="$scene_dir/locomotion.log"
    if (( robots > 1 )); then log="$scene_dir/locomotion_${name}.log"; fi
    "$viewer_python" -u "$task_root/scripts/go2_locomotion.py" \
      --name "$name" --namespace "$(python3 -c 'import json,sys
data = json.load(open(sys.argv[1]))
name = sys.argv[2]
print(next(r["namespace"] for r in data["robots"] if r["name"] == name))' "$scene_dir/robots.json" "$name")" \
      >"$log" 2>&1 9>&- &
    loco_pids+=("$!")
    echo "Go2 locomotion $name PID=${loco_pids[-1]}; log=$log"
  done
  children+=("${loco_pids[@]}")
  if (( robots > 1 )); then
    echo "Keys 1-$robots select one robot (1=go2, default), 0 selects all; W/S A/D Q/E move the selection. Space stops, R resets it."
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
      --render-mode "$render_mode" --state-timeout 240 \
      --camera-distance 5 --camera-azimuth -110 --camera-elevation -25 \
      >"$scene_dir/viewer.log" 2>&1 9>&- &
  fi
  viewer_pid=$!
  echo "Go2 viewer PID=$viewer_pid; log=$scene_dir/viewer.log; close the viewer or press Ctrl-C to stop."
  children+=("$viewer_pid")
  wait -n "${children[@]}"
fi
