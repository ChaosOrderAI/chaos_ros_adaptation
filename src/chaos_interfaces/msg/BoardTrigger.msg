# One trigger report from the InfiniteSense sync board (V3/V4/MINI).
#
# The board timestamps its own PWM rising edges in microseconds and sends one
# report per trigger event; `device_mask` says which channels fired. Paired
# with this stamp, a triggered camera frame is aligned by "the most recent
# CAM_n trigger before the frame arrived", not by the frame's arrival time.
std_msgs/Header header    # stamp: board UTC time of the trigger edge
uint32 device_mask        # bit0 onboard IMU, bit1 ext IMU, bit2..5 CAM1..4, bit6 laser, bit7 GPS
uint32 seq                # counter maintained by the receiving node
uint64 board_stamp_us     # raw board microseconds (epoch us once PTP converged)
uint64 host_stamp_us      # host receive time in epoch microseconds
