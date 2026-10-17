#!/usr/bin/env bash
# systemd supplies adapter.env; this script also supports explicit environment
# variables for foreground diagnostics. Do not enable nounset before sourcing ROS.
set -eo pipefail

adapter_ros_setup="${ROS_SETUP:-/opt/ros/jazzy/setup.bash}"
if [[ ! -r "$adapter_ros_setup" ]]; then
    printf 'ROS setup file is not readable: %s\n' "$adapter_ros_setup" >&2
    exit 1
fi
# shellcheck disable=SC1090
source "$adapter_ros_setup"

if [[ -n "${ROS_OVERLAY_SETUP:-}" ]]; then
    if [[ ! -r "$ROS_OVERLAY_SETUP" ]]; then
        printf 'ROS overlay setup file is not readable: %s\n' "$ROS_OVERLAY_SETUP" >&2
        exit 1
    fi
    # shellcheck disable=SC1090
    source "$ROS_OVERLAY_SETUP"
fi

adapter_binary="${ADAPTER_BINARY:-/opt/mqtt_ros2_adapter/current/mqtt_ros2_adapter}"
adapter_config="${ADAPTER_CONFIG:-/etc/mqtt_ros2_adapter/config.json}"
if [[ ! -x "$adapter_binary" ]]; then
    printf 'Adapter binary is not executable: %s\n' "$adapter_binary" >&2
    exit 1
fi
if [[ ! -r "$adapter_config" ]]; then
    printf 'Adapter configuration is not readable: %s\n' "$adapter_config" >&2
    exit 1
fi

# Replace the shell so SIGTERM reaches the adapter's graceful shutdown handler.
exec "$adapter_binary" --config "$adapter_config"
