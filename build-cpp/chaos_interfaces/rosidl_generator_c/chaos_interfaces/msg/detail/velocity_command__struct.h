// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from chaos_interfaces:msg/VelocityCommand.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/msg/velocity_command.h"


#ifndef CHAOS_INTERFACES__MSG__DETAIL__VELOCITY_COMMAND__STRUCT_H_
#define CHAOS_INTERFACES__MSG__DETAIL__VELOCITY_COMMAND__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Constants defined in the message

// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__struct.h"
// Member 'request_id'
// Member 'lease_token'
#include "rosidl_runtime_c/string.h"
// Member 'velocity'
#include "geometry_msgs/msg/detail/twist__struct.h"

/// Struct defined in msg/VelocityCommand in the package chaos_interfaces.
/**
  * ROS time of generation; frame_id must equal the asset's configured base frame.
 */
typedef struct chaos_interfaces__msg__VelocityCommand
{
  std_msgs__msg__Header header;
  rosidl_runtime_c__String request_id;
  rosidl_runtime_c__String lease_token;
  /// Validity from header.stamp, bounded by asset command_timeout_s.
  double valid_for_s;
  geometry_msgs__msg__Twist velocity;
} chaos_interfaces__msg__VelocityCommand;

// Struct for a sequence of chaos_interfaces__msg__VelocityCommand.
typedef struct chaos_interfaces__msg__VelocityCommand__Sequence
{
  chaos_interfaces__msg__VelocityCommand * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__msg__VelocityCommand__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // CHAOS_INTERFACES__MSG__DETAIL__VELOCITY_COMMAND__STRUCT_H_
