import sys
import time

import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import Image

rclpy.init()
node = Node("rate_probe")
stamps = []


def cb(m):
    stamps.append(m.header.stamp.sec + m.header.stamp.nanosec * 1e-9)


node.create_subscription(Image, sys.argv[1] if len(sys.argv) > 1 else "/driver/vis/image_raw", cb, qos_profile_sensor_data)

end = time.time() + 16.0
while time.time() < end:
    rclpy.spin_once(node, timeout_sec=0.05)

if len(stamps) < 3:
    print("NO FRAMES")
else:
    cutoff = stamps[0] + 3.0
    s = [x for x in stamps if x >= cutoff]
    print("total %d, after-startup %d in %.0fs" % (len(stamps), len(s), 13.0))
    if len(s) > 2:
        d = sorted(b - a for a, b in zip(s, s[1:]))
        med = d[len(d) // 2]
        print("median stamp delta %.1f ms -> %.2f Hz; arrival %.2f Hz" % (
            med * 1000, 1.0 / med, (len(s) - 1) / 13.0))

node.destroy_node()
rclpy.shutdown()
