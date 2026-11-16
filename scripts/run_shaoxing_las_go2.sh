#!/usr/bin/env bash
# Shaoxing scene imported from the full LAS survey, Gaussian surfaces and Go2 locomotion.
set -eo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export PCD_GO2_SCENE_DIR="${PCD_GO2_SCENE_DIR:-$task_root/generated/shaoxing_las_go2}"
# Isolate this scene from the courtyard (198) and the 24M-point Shaoxing scene (199).
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-200}"
exec bash "$task_root/scripts/run_pcd_go2.sh" --gaussian "$@"
