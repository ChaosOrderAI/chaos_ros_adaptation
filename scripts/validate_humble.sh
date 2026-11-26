#!/usr/bin/env bash
# Native Ubuntu 22.04 / ROS 2 Humble acceptance; never installs dependencies.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/validate_humble.sh [--smoke]

Run from a clean terminal on Ubuntu 22.04 with ROS 2 Humble installed.
The script sources /opt/ros/humble/setup.bash, builds all packages from src
(the chaos_sdk BT library is built with CHAOS_SDK_WITH_BT=OFF because Humble
does not ship BehaviorTree.CPP 4.x), runs the Python tests and chaos_mqtt
CTest tests, and preserves their output
in build-humble/, install-humble/, and log-humble/.

  --smoke  Also run all seven existing mock integration tests, including the
           local TLS/MQTT broker, model, sensor, recording, and export tests.
  --help   Show this help.

Dependencies must already be installed. The locked Cargo build may download
dependencies into the existing Cargo cache. No existing build is deleted.
EOF
}

fail() {
    printf 'Humble validation: %s\n' "$*" >&2
    exit 2
}

run_smoke=false
while (($#)); do
    case "$1" in
        --smoke) run_smoke=true ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; fail "unknown argument: $1" ;;
    esac
    shift
done

[[ -r /etc/os-release ]] || fail '/etc/os-release is unavailable.'
# shellcheck source=/dev/null
source /etc/os-release
[[ "${ID:-}" == ubuntu && "${VERSION_ID:-}" == 22.04 ]] ||
    fail "requires Ubuntu 22.04; detected ${PRETTY_NAME:-unknown OS}. No build was started."
[[ -z "${ROS_DISTRO:-}" || "$ROS_DISTRO" == humble ]] ||
    fail "ROS_DISTRO=$ROS_DISTRO is already sourced. Open a clean terminal without another ROS distribution."
[[ -z "${ROS_VERSION:-}" || "$ROS_VERSION" == 2 ]] ||
    fail 'a ROS 1 environment is already sourced; open a clean terminal.'

# Sourcing Humble does not remove paths added by an earlier overlay. Refuse
# these paths before sourcing, including a previous install of this project.
for variable in AMENT_PREFIX_PATH COLCON_PREFIX_PATH CMAKE_PREFIX_PATH PYTHONPATH LD_LIBRARY_PATH; do
    IFS=: read -r -a prefixes <<< "${!variable-}"
    for prefix in "${prefixes[@]}"; do
        case "$prefix" in
            ''|/opt/ros/humble|/opt/ros/humble/*) ;;
            *) fail "$variable contains $prefix. Open a clean terminal with no workspace or other distribution sourced." ;;
        esac
    done
done
IFS=: read -r -a path_entries <<< "$PATH"
for prefix in "${path_entries[@]}"; do
    case "$prefix" in
        /opt/ros/humble/*) ;;
        /opt/ros/*) fail "PATH contains another ROS distribution: $prefix. Open a clean terminal." ;;
    esac
done
[[ -z "${VIRTUAL_ENV:-}" && -z "${CONDA_PREFIX:-}" ]] ||
    fail 'deactivate the Python virtual/Conda environment before native Humble validation.'
[[ -r /opt/ros/humble/setup.bash ]] || fail 'install ROS 2 Humble before running this script.'

# ROS-generated setup scripts can expand unset variables.
set +u
# shellcheck source=/dev/null
source /opt/ros/humble/setup.bash
set -u
[[ "${ROS_DISTRO:-}" == humble && "${ROS_VERSION:-}" == 2 ]] ||
    fail '/opt/ros/humble/setup.bash did not select ROS 2 Humble.'

for executable in python3 colcon cargo rustc cmake pkg-config check_urdf; do
    command -v "$executable" >/dev/null || fail "required executable is missing: $executable (install project dependencies first)."
done
python3 - <<'PY' || fail 'the system Python 3.10 / Humble dependencies are unavailable; see the error above.'
from pathlib import Path
import sys

if sys.version_info[:2] != (3, 10):
    raise SystemExit(f'Expected Python 3.10, got {sys.version.split()[0]} at {sys.executable}')
import ament_index_python
import numpy
import pytest
import rclpy
import xacro
import yaml

if not Path(rclpy.__file__).resolve().is_relative_to('/opt/ros/humble'):
    raise SystemExit(f'Expected Humble rclpy, imported {rclpy.__file__}')
PY

if "$run_smoke"; then
    for executable in mosquitto mosquitto_pub mosquitto_sub openssl; do
        command -v "$executable" >/dev/null || fail "--smoke requires $executable."
    done
    python3 -c 'import rosbag2_py' || fail '--smoke requires the Humble rosbag2 Python/storage dependencies.'
fi

script_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
project_directory="$(cd -- "$script_directory/.." && pwd -P)"
cd -- "$project_directory"
[[ -d src/chaos_interfaces && -d src/chaos_mqtt ]] || fail 'cannot locate the project src directory.'

export ROS_LOG_DIR="$project_directory/log-humble/ros"
mkdir -p -- "$ROS_LOG_DIR"
[[ -w "$ROS_LOG_DIR" ]] || fail "ROS_LOG_DIR is not writable: $ROS_LOG_DIR"
export ROS_LOCALHOST_ONLY=1
export ROS_DOMAIN_ID=187
unset ROS_AUTOMATIC_DISCOVERY_RANGE ROS_STATIC_PEERS ROS_DISCOVERY_SERVER
# Ubuntu's ROS pytest entry-point plugins can turn optional-module skips into
# collection failures. These tests do not require automatic third-party hooks.
export PYTEST_DISABLE_PLUGIN_AUTOLOAD=1

printf 'Building all src packages for Ubuntu 22.04 / ROS 2 Humble.\n'
# The BehaviorTree.CPP node library needs the 4.x API. Humble ships
# behaviortree_cpp_v3 (3.8) instead, so this build turns the SDK gate off.
colcon --log-base log-humble build --base-paths src \
    --build-base build-humble --install-base install-humble --symlink-install \
    --cmake-args -DBUILD_TESTING=ON -DCHAOS_SDK_WITH_BT=OFF

set +u
# Source only the newly built packages; the underlay was explicitly selected.
# shellcheck source=/dev/null
source install-humble/local_setup.bash
set -u

test_status=0
python3 -m pytest -q src || test_status=1
colcon --log-base log-humble test --base-paths src \
    --build-base build-humble --install-base install-humble \
    --packages-select chaos_mqtt --return-code-on-test-failure \
    --event-handlers console_direct+ || test_status=1
colcon --log-base log-humble test-result --test-result-base build-humble --verbose || test_status=1
if ((test_status)); then
    printf 'Humble validation failed; inspect log-humble/ and build-humble/ test results.\n' >&2
    exit "$test_status"
fi

if "$run_smoke"; then
    for script in smoke_test.py mqtt_smoke_test.py transport_smoke_test.py \
                  sensor_smoke_test.py description_smoke_test.py \
                  data_smoke_test.py training_data_smoke_test.py; do
        printf 'Running scripts/%s\n' "$script"
        python3 "scripts/$script"
    done
fi

printf 'PASS: Humble build, Python tests, and chaos_mqtt CTest%s.\n' \
    "$(if "$run_smoke"; then printf ', plus seven mock integration tests'; fi)"
