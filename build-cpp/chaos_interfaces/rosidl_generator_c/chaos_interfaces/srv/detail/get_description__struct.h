// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from chaos_interfaces:srv/GetDescription.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/srv/get_description.h"


#ifndef CHAOS_INTERFACES__SRV__DETAIL__GET_DESCRIPTION__STRUCT_H_
#define CHAOS_INTERFACES__SRV__DETAIL__GET_DESCRIPTION__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Constants defined in the message

/// Struct defined in srv/GetDescription in the package chaos_interfaces.
typedef struct chaos_interfaces__srv__GetDescription_Request
{
  uint8_t structure_needs_at_least_one_member;
} chaos_interfaces__srv__GetDescription_Request;

// Struct for a sequence of chaos_interfaces__srv__GetDescription_Request.
typedef struct chaos_interfaces__srv__GetDescription_Request__Sequence
{
  chaos_interfaces__srv__GetDescription_Request * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__srv__GetDescription_Request__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'asset_id'
// Member 'model'
// Member 'model_version'
// Member 'capabilities'
// Member 'base_frame'
// Member 'odom_frame'
// Member 'map_frame'
#include "rosidl_runtime_c/string.h"

/// Struct defined in srv/GetDescription in the package chaos_interfaces.
typedef struct chaos_interfaces__srv__GetDescription_Response
{
  rosidl_runtime_c__String asset_id;
  rosidl_runtime_c__String model;
  rosidl_runtime_c__String model_version;
  rosidl_runtime_c__String__Sequence capabilities;
  rosidl_runtime_c__String base_frame;
  rosidl_runtime_c__String odom_frame;
  rosidl_runtime_c__String map_frame;
  double linear_limit_mps;
  double angular_limit_rps;
  double command_timeout_s;
  double lease_max_s;
} chaos_interfaces__srv__GetDescription_Response;

// Struct for a sequence of chaos_interfaces__srv__GetDescription_Response.
typedef struct chaos_interfaces__srv__GetDescription_Response__Sequence
{
  chaos_interfaces__srv__GetDescription_Response * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__srv__GetDescription_Response__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'info'
#include "service_msgs/msg/detail/service_event_info__struct.h"

// constants for array fields with an upper bound
// request
enum
{
  chaos_interfaces__srv__GetDescription_Event__request__MAX_SIZE = 1
};
// response
enum
{
  chaos_interfaces__srv__GetDescription_Event__response__MAX_SIZE = 1
};

/// Struct defined in srv/GetDescription in the package chaos_interfaces.
typedef struct chaos_interfaces__srv__GetDescription_Event
{
  service_msgs__msg__ServiceEventInfo info;
  chaos_interfaces__srv__GetDescription_Request__Sequence request;
  chaos_interfaces__srv__GetDescription_Response__Sequence response;
} chaos_interfaces__srv__GetDescription_Event;

// Struct for a sequence of chaos_interfaces__srv__GetDescription_Event.
typedef struct chaos_interfaces__srv__GetDescription_Event__Sequence
{
  chaos_interfaces__srv__GetDescription_Event * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__srv__GetDescription_Event__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // CHAOS_INTERFACES__SRV__DETAIL__GET_DESCRIPTION__STRUCT_H_
