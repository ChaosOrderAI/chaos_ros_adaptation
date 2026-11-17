#!/usr/bin/env bash
# Shaoxing scene imported from the reconstructed triangle mesh, Gaussian surfaces and Go2 locomotion.
set -eo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export PCD_GO2_SCENE_DIR="${PCD_GO2_SCENE_DIR:-$task_root/generated/merged_recon_go2}"
# Isolate this scene from the courtyard (198) and the other Shaoxing scenes (199, 200).
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-201}"
exec bash "$task_root/scripts/run_pcd_go2.sh" --gaussian "$@"
