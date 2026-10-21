// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from chaos_interfaces:msg/RobotState.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/msg/robot_state.h"


#ifndef CHAOS_INTERFACES__MSG__DETAIL__ROBOT_STATE__STRUCT_H_
#define CHAOS_INTERFACES__MSG__DETAIL__ROBOT_STATE__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Constants defined in the message

// Include directives for member types
// Member 'stamp'
#include "builtin_interfaces/msg/detail/time__struct.h"
// Member 'asset_id'
// Member 'model'
// Member 'operational_state'
// Member 'detail'
#include "rosidl_runtime_c/string.h"
// Member 'pose'
#include "geometry_msgs/msg/detail/pose_stamped__struct.h"
// Member 'twist'
#include "geometry_msgs/msg/detail/twist_stamped__struct.h"

/// Struct defined in msg/RobotState in the package chaos_interfaces.
typedef struct chaos_interfaces__msg__RobotState
{
  builtin_interfaces__msg__Time stamp;
  rosidl_runtime_c__String asset_id;
  rosidl_runtime_c__String model;
  rosidl_runtime_c__String operational_state;
  /// Connected means recent adapter telemetry, not merely a live ROS node.
  bool connected;
  bool ready;
  bool pose_valid;
  geometry_msgs__msg__PoseStamped pose;
  geometry_msgs__msg__TwistStamped twist;
  /// [0,1], NaN when unknown. Never substitute missing data with zero.
  float battery_percentage;
  rosidl_runtime_c__String detail;
} chaos_interfaces__msg__RobotState;

// Struct for a sequence of chaos_interfaces__msg__RobotState.
typedef struct chaos_interfaces__msg__RobotState__Sequence
{
  chaos_interfaces__msg__RobotState * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__msg__RobotState__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // CHAOS_INTERFACES__MSG__DETAIL__ROBOT_STATE__STRUCT_H_
