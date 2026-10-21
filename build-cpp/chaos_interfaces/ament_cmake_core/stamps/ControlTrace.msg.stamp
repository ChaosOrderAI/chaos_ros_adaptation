# Runtime evidence for offline training; never a hardware execution confirmation.
# Event publication time and the configured robot base frame.
std_msgs/Header header
string asset_id
# Strictly increasing within one runtime process, including failed publications.
# A discontinuity or reset invalidates any previously held training action.
uint64 sequence
uint64 monotonic_ns
string request_id
# motion.velocity, motion.stop, navigation.pose, flight.takeoff, or flight.land.
string command
# issued, rejected, or invalidated.
string stage
# Only issued motion.velocity events may carry a usable velocity label.
bool velocity_valid
geometry_msgs/Twist velocity
# Remaining seconds from header.stamp, bounded by command and lease expiration.
# Zero supplies no valid action interval. Invalidation always ends the interval.
float64 valid_for_s
# Machine-readable code only. No lease token, owner, or exception message.
string code
