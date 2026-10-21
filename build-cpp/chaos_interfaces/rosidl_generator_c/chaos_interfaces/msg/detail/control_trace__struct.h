// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from chaos_interfaces:msg/ControlTrace.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/msg/control_trace.h"


#ifndef CHAOS_INTERFACES__MSG__DETAIL__CONTROL_TRACE__STRUCT_H_
#define CHAOS_INTERFACES__MSG__DETAIL__CONTROL_TRACE__STRUCT_H_

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
// Member 'asset_id'
// Member 'request_id'
// Member 'command'
// Member 'stage'
// Member 'code'
#include "rosidl_runtime_c/string.h"
// Member 'velocity'
#include "geometry_msgs/msg/detail/twist__struct.h"

/// Struct defined in msg/ControlTrace in the package chaos_interfaces.
/**
  * Runtime evidence for offline training; never a hardware execution confirmation.
  * Event publication time and the configured robot base frame.
 */
typedef struct chaos_interfaces__msg__ControlTrace
{
  std_msgs__msg__Header header;
  rosidl_runtime_c__String asset_id;
  /// Strictly increasing within one runtime process, including failed publications.
  /// A discontinuity or reset invalidates any previously held training action.
  uint64_t sequence;
  uint64_t monotonic_ns;
  rosidl_runtime_c__String request_id;
  /// motion.velocity, motion.stop, navigation.pose, flight.takeoff, or flight.land.
  rosidl_runtime_c__String command;
  /// issued, rejected, or invalidated.
  rosidl_runtime_c__String stage;
  /// Only issued motion.velocity events may carry a usable velocity label.
  bool velocity_valid;
  geometry_msgs__msg__Twist velocity;
  /// Remaining seconds from header.stamp, bounded by command and lease expiration.
  /// Zero supplies no valid action interval. Invalidation always ends the interval.
  double valid_for_s;
  /// Machine-readable code only. No lease token, owner, or exception message.
  rosidl_runtime_c__String code;
} chaos_interfaces__msg__ControlTrace;

// Struct for a sequence of chaos_interfaces__msg__ControlTrace.
typedef struct chaos_interfaces__msg__ControlTrace__Sequence
{
  chaos_interfaces__msg__ControlTrace * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__msg__ControlTrace__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // CHAOS_INTERFACES__MSG__DETAIL__CONTROL_TRACE__STRUCT_H_
