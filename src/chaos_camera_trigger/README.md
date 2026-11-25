# chaos_camera_trigger

Stamp externally triggered camera frames with the sync board's trigger time.

A triggered camera tells the driver *that* it exposed, but most ROS drivers
stamp frames with the host arrival time (or the last trigger the driver
happened to see). This node sits between the driver and the consumers and
replaces the stamp with the board's own trigger edge, which is the time base
the rest of the robot uses.

It is driver agnostic on purpose: it consumes any `sensor_msgs/Image` topic.
That keeps the Hikvision driver choice open — `camera_ros` (Aravis, no MVS
SDK) or the vendor MVS node both work, as long as the camera is configured for
external triggering on Line0 and the board's CAM_n channel is wired to it.

## Wiring it up (Hikvision MV-CS050-10GC)

```
board CAM1  --GND---- camera Line0 GND
board CAM1  --TRIG--- camera Line0 (rising edge)
camera      --12V---- board 12V out (or its own supply)
```

MVS: `TriggerMode=On`, `TriggerSource=Line0`, `TriggerActivation=RisingEdge`,
`ExposureTime` shorter than the trigger period, save a UserSet. Verify the
frame rate in MVS equals `hz_cam_1`.

Board config: `hz_cam_1: 10`, `cam1_ms: 10` (period 100 ms, pulse 10 ms).

## Run

```bash
ros2 run chaos_camera_trigger trigger_pair_node --ros-args \
  -p image_topic:=/robots/vehicle_001/sensors/front_camera/image_raw \
  -p image_output_topic:=.../image_synced \
  -p board_channel:=CAM_1 \
  -p max_age_ms:=80.0 \
  -p expected_period_ms:=100.0
```

Then point consumers at `image_synced` and `camera_info_synced`.

## Parameters

| Parameter | Default | Meaning |
|---|---|---|
| `board_channel` | `CAM_1` | Which trigger bit pairs with this camera (`IMU_2` also accepted) |
| `max_age_ms` | `100` | A frame pairs only if it arrived within this long after the trigger; keep it below the trigger period |
| `exposure_offset_us` | `0` | Added to the trigger stamp (e.g. half the exposure) |
| `missing_trigger` | `drop` | `drop` discards unpaired frames (a frame without a trigger has no trustworthy time), `arrival` stamps with host time, `last` reuses a stale trigger (debug only) |
| `expected_period_ms` | `0` | When > 0, trigger intervals above 1.5× this count as gaps |
| `frame_id` | empty | Override the driver's frame_id |
| `publish_camera_info` | `true` | Set false when the driver publishes no CameraInfo |

## Diagnostics

One `DiagnosticArray` per second on `diagnostics`:

- `paired_frames`, `unpaired_frames`, `dropped_frames`, `stale_camera_info`
- `triggers`, `trigger_gaps` (missed edges), `clock_outliers` (stamp more than
  1 s away from arrival — board clock drift)
- `last_age_ms`, `max_age_ms`, `last_trigger_age_ms`

Level becomes WARN when any unpaired/dropped/gap/outlier counter is non-zero,
and ERROR when no trigger has been seen on the channel at all.

## Status

The pairing logic is implemented and unit-tested against a synthetic trigger
source. It has **not** been run against a real MV-CS050-10GC yet: the camera is
not attached and `ros-jazzy-camera-ros` is not installed on the Jetson.
