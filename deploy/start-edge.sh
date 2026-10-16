#!/usr/bin/env bash
# systemd supplies edge.env. No .bashrc dependency and no shell eval.
# ROS setup scripts may reference unset variables, so source before nounset.
set -eo pipefail

case "${1:-}" in
    '') chaos_dry_run=false ;;
    --check) chaos_dry_run=true ;;
    *) printf 'Usage: %s [--check]\n' "$0" >&2; exit 2 ;;
esac
if [[ $# -gt 1 ]]; then
    printf 'Unexpected arguments\n' >&2
    exit 2
fi

chaos_ros_setup="${CHAOS_ROS_SETUP:-/opt/ros/jazzy/setup.bash}"
chaos_overlay_setup="${CHAOS_OVERLAY_SETUP:-/opt/chaos/ros/local_setup.bash}"
chaos_setup_files=("$chaos_ros_setup")
if [[ -n "${CHAOS_DRIVER_SETUP:-}" ]]; then
    chaos_setup_files+=("$CHAOS_DRIVER_SETUP")
fi
chaos_setup_files+=("$chaos_overlay_setup")
for chaos_setup in "${chaos_setup_files[@]}"; do
    if [[ ! -r "$chaos_setup" ]]; then
        printf 'Setup file is not readable: %s\n' "$chaos_setup" >&2
        exit 1
    fi
    # shellcheck disable=SC1090
    source "$chaos_setup"
done
set -u

: "${CHAOS_INVENTORY:?Set CHAOS_INVENTORY to this device asset.yaml}"
: "${CHAOS_ASSET_ID:?Set CHAOS_ASSET_ID to this robot identity}"
if [[ "$CHAOS_INVENTORY" != /* || ! -r "$CHAOS_INVENTORY" ]]; then
    printf 'CHAOS_INVENTORY must be an absolute readable file: %s\n' "$CHAOS_INVENTORY" >&2
    exit 1
fi
chaos_mqtt="${CHAOS_ENABLE_MQTT:-true}"
if [[ "$chaos_mqtt" != true && "$chaos_mqtt" != false ]]; then
    printf 'CHAOS_ENABLE_MQTT must be true or false\n' >&2
    exit 2
fi
export ROS_AUTOMATIC_DISCOVERY_RANGE="${CHAOS_DISCOVERY_RANGE:-LOCALHOST}"
if [[ "$ROS_AUTOMATIC_DISCOVERY_RANGE" != LOCALHOST && "$ROS_AUTOMATIC_DISCOVERY_RANGE" != SUBNET ]]; then
    printf 'CHAOS_DISCOVERY_RANGE must be LOCALHOST or SUBNET\n' >&2
    exit 2
fi
# The legacy variable would override the declared Jazzy discovery policy.
unset ROS_LOCALHOST_ONLY

exec ros2 launch chaos_bringup edge.launch.py \
    "inventory:=$CHAOS_INVENTORY" "asset_id:=$CHAOS_ASSET_ID" \
    "enable_mqtt:=$chaos_mqtt" "dry_run:=$chaos_dry_run" \
    "discovery_range:=$ROS_AUTOMATIC_DISCOVERY_RANGE"
