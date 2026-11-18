#!/usr/bin/env python3
"""Synthetic Livox Mid-360 feed for local fast_livo2 bring-up.

Publishes /livox/lidar (PointCloud2, XYZIRT Livox layout) and /livox/imu so the
LIO can initialize and produce /aft_mapped_to_init without real hardware.
Simulates a robot slowly rotating in a square room so the scan has parallax
and the IMU shows gravity + a small yaw rate.
"""
import math
import struct
import time

import numpy as np
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Imu, PointCloud2, PointField
from std_msgs.msg import Header

# Livox XYZIRT point layout (xfer_format 0 / pointclou2). fast_livo2's
# livox_handler reads x,y,z,intensity,tag,line(,timestamp) per point.
FIELDS = [
    PointField(name="x", offset=0, datatype=PointField.FLOAT32, count=1),
    PointField(name="y", offset=4, datatype=PointField.FLOAT32, count=1),
    PointField(name="z", offset=8, datatype=PointField.FLOAT32, count=1),
    PointField(name="intensity", offset=12, datatype=PointField.FLOAT32, count=1),
    PointField(name="tag", offset=16, datatype=PointField.UINT8, count=1),
    PointField(name="line", offset=17, datatype=PointField.UINT8, count=1),
    PointField(name="timestamp", offset=18, datatype=PointField.FLOAT64, count=1),
]
POINT_STEP = 26
# Square room: walls at +/-8 m, floor z=0, ceiling z=4. Robot at centre.
ROOM = 8.0
CEIL = 4.0
LINES = 4          # Mid-360 scan_line
N_PER_LINE = 400   # azimuth samples per line


SENSOR_Z = 0.4


def room_scan(t: float, yaw_rate: float) -> np.ndarray:
    """Return Nx7 float array [x,y,z,intensity,tag,line,timestamp] for a
    slowly-rotating sensor in the square room, Livox non-repetitive pattern."""
    yaw = yaw_rate * t
    pts = []
    now = time.time()
    for line in range(LINES):
        # Elevation: lines fan slightly downward like Mid-360's non-repetitive
        # pattern (approx -7..-24 deg across 4 lines) plus a forward-ish sweep.
        elev = math.radians(-7.0 - 5.0 * line)
        for i in range(N_PER_LINE):
            az = (2 * math.pi * i / N_PER_LINE) + yaw + line * 0.13
            dx, dy, dz = math.cos(elev) * math.cos(az), math.cos(elev) * math.sin(az), math.sin(elev)
            # Ray origin is the sensor at z=SENSOR_Z; intersect with walls,
            # floor (z=0) and ceiling (z=CEIL).
            t_hit = 1e9
            for ax, bound in ((0, ROOM), (0, -ROOM), (1, ROOM), (1, -ROOM)):
                d = (dx, dy, dz)[ax]
                if abs(d) < 1e-6:
                    continue
                t_wall = bound / d
                if t_wall > 0:
                    t_hit = min(t_hit, t_wall)
            if dz < -1e-6:                       # floor: SENSOR_Z + dz*t = 0
                t_hit = min(t_hit, -SENSOR_Z / dz)
            if dz > 1e-6:                        # ceiling
                t_hit = min(t_hit, (CEIL - SENSOR_Z) / dz)
            if not math.isfinite(t_hit) or t_hit <= 0 or t_hit > 30:
                continue
            x, y, z = dx * t_hit, dy * t_hit, SENSOR_Z + dz * t_hit
            pts.append((x, y, z, 120.0, 0, line, now))
    arr = np.zeros(len(pts), dtype=[
        ("x", "<f4"), ("y", "<f4"), ("z", "<f4"), ("intensity", "<f4"),
        ("tag", "u1"), ("line", "u1"), ("ts_pad", "<f8"),
    ])
    for k, p in enumerate(pts):
        arr[k] = (p[0], p[1], p[2], p[3], p[4], p[5], p[6])
    return arr


class SynthLivox(Node):
    def __init__(self):
        super().__init__("synth_livox")
        self.lidar = self.create_publisher(PointCloud2, "/livox/lidar", 10)
        self.imu = self.create_publisher(Imu, "/livox/imu", 50)
        self.t0 = time.time()
        self.create_timer(0.1, self.pub_lidar)   # 10 Hz scans
        self.create_timer(0.005, self.pub_imu)   # 200 Hz imu

    def pub_lidar(self):
        t = time.time() - self.t0
        arr = room_scan(t, yaw_rate=0.15)
        data = bytearray()
        for row in arr:
            data += struct.pack("<fff f B B d", row["x"], row["y"], row["z"],
                                row["intensity"], row["tag"], row["line"], row["ts_pad"])
        msg = PointCloud2()
        msg.header = Header()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = "livox_frame"
        msg.height, msg.width = 1, len(arr)
        msg.fields = FIELDS
        msg.is_bigendian = False
        msg.point_step = POINT_STEP
        msg.row_step = POINT_STEP * len(arr)
        msg.is_dense = True
        msg.data = bytes(data)
        self.lidar.publish(msg)

    def pub_imu(self):
        t = time.time() - self.t0
        msg = Imu()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = "livox_frame"
        # gravity on -z plus the tiny yaw rate of the rotating sensor
        msg.linear_acceleration.x = 0.0
        msg.linear_acceleration.y = 0.0
        msg.linear_acceleration.z = 9.81
        msg.angular_velocity.x = 0.0
        msg.angular_velocity.y = 0.0
        msg.angular_velocity.z = 0.15
        msg.orientation_covariance[0] = -1.0
        self.imu.publish(msg)


def main():
    rclpy.init()
    node = SynthLivox()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    node.destroy_node()
    rclpy.shutdown()


if __name__ == "__main__":
    main()
