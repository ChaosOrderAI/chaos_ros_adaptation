#!/usr/bin/env bash
# Full-resolution Shaoxing point cloud, Gaussian surfaces and Go2 locomotion.
set -eo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export PCD_GO2_SCENE_DIR="${PCD_GO2_SCENE_DIR:-$task_root/generated/shaoxing_go2}"
# Isolate this scene from the older courtyard's default ROS domain 198.
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-199}"
exec bash "$task_root/scripts/run_pcd_go2.sh" --gaussian "$@"
