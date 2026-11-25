#!/usr/bin/env bash
# Build the pinned upstream driver in its own workspace; never install system rules.
set -eo pipefail

driver_workspace="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ ! -r /opt/ros/jazzy/setup.bash ]]; then
  echo "ROS 2 Jazzy is required: /opt/ros/jazzy/setup.bash was not found." >&2
  exit 1
fi
if [[ $# -ne 0 ]]; then
  echo "Usage: $0 (no arguments; see ../docs/orbbec-driver.md for custom install paths)" >&2
  exit 2
fi

source /opt/ros/jazzy/setup.bash
set -u
# Upstream uses the presence of this variable to install files under /etc.
unset BUILDING_PACKAGE
cd -- "$driver_workspace"
exec colcon --log-base log build \
  --base-paths src \
  --build-base build \
  --install-base install \
  --packages-up-to orbbec_camera \
  --executor sequential \
  --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DINSTALL_UDEV_RULES=OFF
