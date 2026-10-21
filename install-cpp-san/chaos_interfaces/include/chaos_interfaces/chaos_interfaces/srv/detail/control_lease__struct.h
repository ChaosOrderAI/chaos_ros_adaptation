// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from chaos_interfaces:srv/ControlLease.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/srv/control_lease.h"


#ifndef CHAOS_INTERFACES__SRV__DETAIL__CONTROL_LEASE__STRUCT_H_
#define CHAOS_INTERFACES__SRV__DETAIL__CONTROL_LEASE__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Constants defined in the message

// Include directives for member types
// Member 'operation'
// Member 'owner'
// Member 'token'
#include "rosidl_runtime_c/string.h"
// Member 'expires_at'
#include "builtin_interfaces/msg/detail/time__struct.h"

/// Struct defined in srv/ControlLease in the package chaos_interfaces.
typedef struct chaos_interfaces__srv__ControlLease_Request
{
  rosidl_runtime_c__String operation;
  rosidl_runtime_c__String owner;
  rosidl_runtime_c__String token;
  double duration_s;
  /// Unix UTC deadline for accepting this request; zero means no transport deadline.
  builtin_interfaces__msg__Time expires_at;
} chaos_interfaces__srv__ControlLease_Request;

// Struct for a sequence of chaos_interfaces__srv__ControlLease_Request.
typedef struct chaos_interfaces__srv__ControlLease_Request__Sequence
{
  chaos_interfaces__srv__ControlLease_Request * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__srv__ControlLease_Request__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'code'
// Member 'message'
// Member 'token'
// already included above
// #include "rosidl_runtime_c/string.h"

/// Struct defined in srv/ControlLease in the package chaos_interfaces.
typedef struct chaos_interfaces__srv__ControlLease_Response
{
  bool success;
  rosidl_runtime_c__String code;
  rosidl_runtime_c__String message;
  rosidl_runtime_c__String token;
  double remaining_s;
} chaos_interfaces__srv__ControlLease_Response;

// Struct for a sequence of chaos_interfaces__srv__ControlLease_Response.
typedef struct chaos_interfaces__srv__ControlLease_Response__Sequence
{
  chaos_interfaces__srv__ControlLease_Response * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__srv__ControlLease_Response__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'info'
#include "service_msgs/msg/detail/service_event_info__struct.h"

// constants for array fields with an upper bound
// request
enum
{
  chaos_interfaces__srv__ControlLease_Event__request__MAX_SIZE = 1
};
// response
enum
{
  chaos_interfaces__srv__ControlLease_Event__response__MAX_SIZE = 1
};

/// Struct defined in srv/ControlLease in the package chaos_interfaces.
typedef struct chaos_interfaces__srv__ControlLease_Event
{
  service_msgs__msg__ServiceEventInfo info;
  chaos_interfaces__srv__ControlLease_Request__Sequence request;
  chaos_interfaces__srv__ControlLease_Response__Sequence response;
} chaos_interfaces__srv__ControlLease_Event;

// Struct for a sequence of chaos_interfaces__srv__ControlLease_Event.
typedef struct chaos_interfaces__srv__ControlLease_Event__Sequence
{
  chaos_interfaces__srv__ControlLease_Event * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__srv__ControlLease_Event__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // CHAOS_INTERFACES__SRV__DETAIL__CONTROL_LEASE__STRUCT_H_
