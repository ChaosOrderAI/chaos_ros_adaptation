# Internal driver transport: one motion command admitted by the runtime with
# its immutable local deadline. This is not part of the public API; the
# runtime and the supervised Go2 driver must be rebuilt and deployed as one
# unit because this replaces the previous internal TwistStamped transport.
#
# The issued/expires fields use Linux CLOCK_MONOTONIC nanoseconds; the header
# retains ROS source time. The runtime and the driver must run on the
# same boot and in the same Linux time namespace, otherwise the values are
# not comparable and motion must be refused.

# Original source stamp of the admitted command. The frame_id must equal the
# configured base frame. The stamp is provenance only: it never extends the
# deadline below.
std_msgs/Header header

# Identity of the boot instance and the Linux time namespace that the
# deadline belongs to. This is a clock-compatibility check, not
# authentication, and it does not protect against an arbitrary publisher.
string clock_domain_id

# Actual local CLOCK_MONOTONIC sample taken when the runtime issued the
# command. It is never fabricated forward to defeat an equality check.
int64 issued_monotonic_ns

# Immutable absolute CLOCK_MONOTONIC deadline of the motion. The driver
# stores it unchanged: transit, queueing, runtime pauses, retransmission and
# a later lease renewal consume this interval and never extend it. The value
# is ignored when stop is true.
int64 expires_monotonic_ns

# Explicit stop barrier. All velocity components are zero and the motion
# deadline is ignored. The barrier never moves backwards.
bool stop

# Commanded velocity. It is zero for a stop barrier.
geometry_msgs/Twist velocity
