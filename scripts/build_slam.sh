#!/usr/bin/env bash
# Build the merged chaos_ros_adaptation + Go2 SLAM navigation workspace.
# Does not wipe build/ or install/. Use after cloning or when package set changes.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${root}"

if [[ ! -f /opt/ros/jazzy/setup.bash ]]; then
  echo "需要 ROS 2 Jazzy（/opt/ros/jazzy）。" >&2
  exit 1
fi
set +u
# shellcheck disable=SC1091
source /opt/ros/jazzy/setup.bash
set -u

livox="src/chaos_sensors/vendor/livox_ros_driver2"
if [[ -d "${livox}" ]]; then
  # livox_ros_driver2 ships package_ROS1.xml / package_ROS2.xml side by side;
  # colcon needs a plain package.xml, so materialize the ROS 2 one.
  cp -f "${livox}/package_ROS2.xml" "${livox}/package.xml"
  rm -rf "${livox}/launch"
  cp -a "${livox}/launch_ROS2" "${livox}/launch"
fi

sdk_prefix=""
if [[ -f /usr/local/lib/liblivox_lidar_sdk_shared.so ]] || ldconfig -p 2>/dev/null | grep -q 'liblivox_lidar_sdk_shared\.so'; then
  sdk_prefix="/usr/local"
elif [[ -f "${HOME}/.local/lib/liblivox_lidar_sdk_shared.so" ]]; then
  sdk_prefix="${HOME}/.local"
fi

# Drop stale per-package CMake build dirs when a package moved or was merged
# (e.g. src/livox_ros_driver2 -> src/chaos_sensors/vendor/livox_ros_driver2).
shopt -s nullglob
for cache in build/*/CMakeCache.txt; do
  src_dir="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${cache}")"
  if [[ -n "${src_dir}" && ! -d "${src_dir}" ]]; then
    rm -rf "$(dirname "${cache}")"
  fi
done
shopt -u nullglob

# The in-tree Livox vendor lives inside the chaos_sensors package tree;
# colcon does not descend into a package directory, so its own directory is an
# explicit second base path. Other vendor trees (Orbbec) are separate
# workspaces and must not leak into the main build.
build_args=(--base-paths src src/chaos_sensors/vendor/livox_ros_driver2 --build-base build --install-base install)
cmake_args=(-DCMAKE_BUILD_TYPE=Release)

if [[ -n "${sdk_prefix}" ]]; then
  # Livox SDK found: build the Mid-360 driver first with its own prefix,
  # then everything else.
  colcon build --symlink-install --packages-select livox_ros_driver2 \
    "${build_args[@]}" \
    --cmake-args -DROS_EDITION=ROS2 -DDISTRO_ROS=jazzy \
    -DCMAKE_PREFIX_PATH="${sdk_prefix}" \
    -DCMAKE_INSTALL_RPATH="${sdk_prefix}/lib" \
    -DCMAKE_BUILD_RPATH="${sdk_prefix}/lib" "${cmake_args[@]}"
  # "$@" goes before --cmake-args so user flags reach colcon, not cmake.
  colcon build --symlink-install --packages-skip livox_ros_driver2 \
    "${build_args[@]}" "$@" --cmake-args "${cmake_args[@]}"
else
  echo "未找到 Livox-SDK2，跳过 livox_ros_driver2。其余包照常编译。"
  echo "安装 SDK 后重新运行本脚本即可编进 Mid-360 驱动。"
  colcon build --symlink-install --packages-skip livox_ros_driver2 \
    "${build_args[@]}" "$@" --cmake-args "${cmake_args[@]}"
fi

echo "完成。接着: source ${root}/install/local_setup.bash"
