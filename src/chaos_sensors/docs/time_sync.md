# chaos_time_sync

Host-side client for the InfiniteSense **SimpleSensorSync V4** board, speaking
the board's own JSON protocol over UDP. It deliberately does **not** vendor the
vendor SDK: that SDK is GPL-3.0 (its ROS packages claim BSD), its CMake fails to
configure on the current `main`, and its `Messenger` passes raw pointers to
callbacks. Only the wire protocol is reimplemented here.

## What it does

- **Clock exchange** (NTP-style, 4 timestamps) every `ptp_period_ms`:
  ```
  host  -> board  {"f":"a","a":<host_epoch_us>}
  board -> host   {"f":"a","a":<t1>,"b":<t2>}
  board -> host   {"f":"b","a":<t3>}
  host  -> board  {"f":"b","a":<delay>,"b":<offset>}
  ```
  The board has no RTC; its UTC *comes from this exchange*. Keep the host on
  chrony/NTP.
- **Trigger reports** `{"f":"t","t":<us>,"s":<mask>}` → `chaos_interfaces/BoardTrigger`
  on `board/trigger`. Mask bits: 0 onboard IMU, 1 ext IMU, 2..5 CAM1..CAM4,
  6 laser, 7 GPS.
- **Onboard IMU** `{"f":"imu","t":<us>,"d":[ax,ay,az,gx,gy,gz,temp],"q":[w,x,y,z]}`
  → `sensor_msgs/Imu` on `board/imu`.
- **Diagnostics** once per second on `board/diagnostics`: link state, PTP
  offset/delay, trigger count/age/mask, clock skew, parse errors.

`header.stamp` of every message is the **board time**, not the host arrival
time. That is the whole point: a triggered camera frame is aligned to the most
recent `CAM_n` trigger, not to when its buffer happened to arrive.

## Run

```bash
# board on a dedicated subnet, e.g. host 192.168.10.10, board 192.168.123.188
ros2 launch chaos_time_sync time_sync.launch.py board_ip:=192.168.123.188
```

Ethernet caveats from the vendor docs:

- The board's RJ45 does not provide power — feed it 12 V or Type-C.
- Once an Ethernet cable is attached, the board's serial port is disabled
  until the next power cycle. Configure the board over serial **before**
  plugging the network in.
- Configuration (`{"f":"cfg",...}`) is written over serial/web tool, not by
  this node.

## Check

```bash
ros2 topic hz board/trigger          # matches hz_cam_N when a CAM channel is wired
ros2 topic echo board/diagnostics    # link up, ptp_offset_us small, clock_skew_us small
```

If `clock_skew_us` stays above `clock_warn_us`, the board has not converged —
check that `ptp_exchanges` is increasing and the host clock is sane.

## Trigger-to-frame pairing (`trigger_pair_node`)

The board client is only half of the chain. The `trigger_pair_node` executable
in this package sits between the camera driver and the consumers and replaces
each frame's host-arrival stamp with the board's own trigger edge. It is driver
agnostic on purpose: it consumes any `sensor_msgs/Image` topic, which keeps the
Hikvision driver choice open — `camera_ros` (Aravis, no MVS SDK) or the vendor
MVS node both work, as long as the camera is configured for external
triggering on Line0 and the board's `CAM_n` channel is wired to it.

```
board CAM1  --GND---- camera Line0 GND
board CAM1  --TRIG--- camera Line0 (rising edge)
camera      --12V---- board 12V out (or its own supply)
```

MVS: `TriggerMode=On`, `TriggerSource=Line0`, `TriggerActivation=RisingEdge`,
`ExposureTime` shorter than the trigger period, save a UserSet. Verify the
frame rate in MVS equals `hz_cam_1`.

Board config: `hz_cam_1: 10`, `cam1_ms: 10` (period 100 ms, pulse 10 ms).

```bash
ros2 run chaos_time_sync trigger_pair_node --ros-args \
  -p image_topic:=/robots/vehicle_001/sensors/front_camera/image_raw \
  -p image_output_topic:=.../image_synced \
  -p board_channel:=CAM_1 \
  -p max_age_ms:=80.0 \
  -p expected_period_ms:=100.0
```

Key parameters (full list in `config/trigger_pair.yaml`):

| Parameter | Default | Meaning |
|---|---|---|
| `board_channel` | `CAM_1` | Which trigger bit pairs with this camera (`IMU_2` also accepted) |
| `max_age_ms` | `100` | A frame pairs only if it arrived within this long after the trigger; keep it below the trigger period |
| `exposure_offset_us` | `0` | Added to the trigger stamp (e.g. half the exposure) |
| `missing_trigger` | `drop` | `drop` discards unpaired frames, `arrival` stamps with host time, `last` reuses a stale trigger (debug only) |
| `expected_period_ms` | `0` | When > 0, trigger intervals above 1.5× this count as gaps |
| `frame_id` | empty | Override the driver's frame_id |
| `publish_camera_info` | `true` | Set false when the driver publishes no CameraInfo |

Diagnostics on `diagnostics` (once per second): `paired_frames`,
`unpaired_frames`, `dropped_frames`, `stale_camera_info`, `triggers`,
`trigger_gaps`, `clock_outliers`, `last_age_ms`, `max_age_ms`,
`last_trigger_age_ms`. Level becomes WARN when any unpaired/dropped/gap/outlier
counter is non-zero, and ERROR when no trigger has been seen on the channel at
all.

## Not covered

- No GPS decoding yet (GNGGA/GNRMC are counted, not published).
- The pairing logic is unit-tested against a synthetic trigger source but has
  **not** been run against a real MV-CS050-10GC yet: the camera is not attached
  and `ros-jazzy-camera-ros` is not installed on the Jetson.
