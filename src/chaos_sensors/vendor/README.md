# Vendored sensor drivers

Third-party drivers live here as **separate colcon packages**. They keep
their own `package.xml`, LICENSE and upstream history; the umbrella
`chaos_sensors` package pulls them in with `exec_depend`, so `ros2 pkg` and
the bringup see a single sensor entry point while provenance stays intact.

| driver | mode | where |
|---|---|---|
| `livox_ros_driver2` | `in-tree` | `vendor/livox_ros_driver2` (source committed) |
| `orbbec_camera` | `pinned-import` | `vendor/orbbec/src/OrbbecSDK_ROS2` (fetched, not committed) |
| `realsense2_camera` | `apt` | system package, version pinned in the manifest |
| `camera_aravis2` | `apt` | system package, version pinned in the manifest |

`manifest.yaml` is the single source of truth: upstream URL, pinned ref,
license, build entry point and local patches for every driver.

## Update procedure

```bash
# verify every driver matches the manifest (apt versions, pinned refs, diffs)
python3 src/chaos_sensors/scripts/check_vendor.py

# move one in-tree driver to a new upstream ref, re-apply local patches
python3 src/chaos_sensors/scripts/update_vendor.py livox_ros_driver2 --ref <commit>

# rebuild the affected package and run its tests
colcon build --symlink-install --packages-select <driver>   # the script prints the exact command
```

Rules:

- Never run a vendor's own `build.sh` inside this workspace; some of them
  delete the workspace `build/` and `install/` (Livox does).
- Local changes to a vendored tree must be recorded as patches in
  `vendor/patches/<driver>/` *and* listed in the manifest. The check script
  refuses a tree with unrecorded modifications.
- `orbbec_camera` is built in its own workspace (`vendor/orbbec/build-orbbec.sh`,
  `INSTALL_UDEV_RULES=OFF`); its SDK binaries are fetched for the pinned ref
  and are not committed.
- apt drivers are pinned by version; upgrading one means updating the manifest
  and re-running the hardware smoke tests.
