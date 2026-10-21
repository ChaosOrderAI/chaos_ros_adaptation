builtin_interfaces/Time stamp
string asset_id
string model
string operational_state
# Connected means recent adapter telemetry, not merely a live ROS node.
bool connected
bool ready
bool pose_valid
geometry_msgs/PoseStamped pose
geometry_msgs/TwistStamped twist
# [0,1], NaN when unknown. Never substitute missing data with zero.
float32 battery_percentage
string detail
