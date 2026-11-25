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

## Not covered

- No trigger-to-frame pairing: that belongs to the camera driver. A triggered
  Hikvision node should cache `BoardTrigger` for its CAM channel and stamp each
  frame with the most recent trigger before arrival, with a staleness bound.
- No GPS decoding yet (GNGGA/GNRMC are counted, not published).
