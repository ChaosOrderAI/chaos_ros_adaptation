// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from chaos_interfaces:action/Flight.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/action/flight.h"


#ifndef CHAOS_INTERFACES__ACTION__DETAIL__FLIGHT__STRUCT_H_
#define CHAOS_INTERFACES__ACTION__DETAIL__FLIGHT__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Constants defined in the message

// Include directives for member types
// Member 'lease_token'
#include "rosidl_runtime_c/string.h"
// Member 'expires_at'
#include "builtin_interfaces/msg/detail/time__struct.h"

/// Struct defined in action/Flight in the package chaos_interfaces.
typedef struct chaos_interfaces__action__Flight_Goal
{
  rosidl_runtime_c__String lease_token;
  /// Local altitude above the mock ground; used only for takeoff. Metres.
  double altitude_m;
  /// Unix UTC deadline for starting this goal, not for completing it; zero disables it.
  builtin_interfaces__msg__Time expires_at;
} chaos_interfaces__action__Flight_Goal;

// Struct for a sequence of chaos_interfaces__action__Flight_Goal.
typedef struct chaos_interfaces__action__Flight_Goal__Sequence
{
  chaos_interfaces__action__Flight_Goal * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__action__Flight_Goal__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'code'
// Member 'message'
// already included above
// #include "rosidl_runtime_c/string.h"

/// Struct defined in action/Flight in the package chaos_interfaces.
typedef struct chaos_interfaces__action__Flight_Result
{
  bool success;
  rosidl_runtime_c__String code;
  rosidl_runtime_c__String message;
} chaos_interfaces__action__Flight_Result;

// Struct for a sequence of chaos_interfaces__action__Flight_Result.
typedef struct chaos_interfaces__action__Flight_Result__Sequence
{
  chaos_interfaces__action__Flight_Result * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__action__Flight_Result__Sequence;

// Constants defined in the message

/// Struct defined in action/Flight in the package chaos_interfaces.
typedef struct chaos_interfaces__action__Flight_Feedback
{
  double altitude_m;
  float progress;
} chaos_interfaces__action__Flight_Feedback;

// Struct for a sequence of chaos_interfaces__action__Flight_Feedback.
typedef struct chaos_interfaces__action__Flight_Feedback__Sequence
{
  chaos_interfaces__action__Flight_Feedback * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__action__Flight_Feedback__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'goal_id'
#include "unique_identifier_msgs/msg/detail/uuid__struct.h"
// Member 'goal'
#include "chaos_interfaces/action/detail/flight__struct.h"

/// Struct defined in action/Flight in the package chaos_interfaces.
typedef struct chaos_interfaces__action__Flight_SendGoal_Request
{
  unique_identifier_msgs__msg__UUID goal_id;
  chaos_interfaces__action__Flight_Goal goal;
} chaos_interfaces__action__Flight_SendGoal_Request;

// Struct for a sequence of chaos_interfaces__action__Flight_SendGoal_Request.
typedef struct chaos_interfaces__action__Flight_SendGoal_Request__Sequence
{
  chaos_interfaces__action__Flight_SendGoal_Request * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__action__Flight_SendGoal_Request__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'stamp'
// already included above
// #include "builtin_interfaces/msg/detail/time__struct.h"

/// Struct defined in action/Flight in the package chaos_interfaces.
typedef struct chaos_interfaces__action__Flight_SendGoal_Response
{
  bool accepted;
  builtin_interfaces__msg__Time stamp;
} chaos_interfaces__action__Flight_SendGoal_Response;

// Struct for a sequence of chaos_interfaces__action__Flight_SendGoal_Response.
typedef struct chaos_interfaces__action__Flight_SendGoal_Response__Sequence
{
  chaos_interfaces__action__Flight_SendGoal_Response * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__action__Flight_SendGoal_Response__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'info'
#include "service_msgs/msg/detail/service_event_info__struct.h"

// constants for array fields with an upper bound
// request
enum
{
  chaos_interfaces__action__Flight_SendGoal_Event__request__MAX_SIZE = 1
};
// response
enum
{
  chaos_interfaces__action__Flight_SendGoal_Event__response__MAX_SIZE = 1
};

/// Struct defined in action/Flight in the package chaos_interfaces.
typedef struct chaos_interfaces__action__Flight_SendGoal_Event
{
  service_msgs__msg__ServiceEventInfo info;
  chaos_interfaces__action__Flight_SendGoal_Request__Sequence request;
  chaos_interfaces__action__Flight_SendGoal_Response__Sequence response;
} chaos_interfaces__action__Flight_SendGoal_Event;

// Struct for a sequence of chaos_interfaces__action__Flight_SendGoal_Event.
typedef struct chaos_interfaces__action__Flight_SendGoal_Event__Sequence
{
  chaos_interfaces__action__Flight_SendGoal_Event * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__action__Flight_SendGoal_Event__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'goal_id'
// already included above
// #include "unique_identifier_msgs/msg/detail/uuid__struct.h"

/// Struct defined in action/Flight in the package chaos_interfaces.
typedef struct chaos_interfaces__action__Flight_GetResult_Request
{
  unique_identifier_msgs__msg__UUID goal_id;
} chaos_interfaces__action__Flight_GetResult_Request;

// Struct for a sequence of chaos_interfaces__action__Flight_GetResult_Request.
typedef struct chaos_interfaces__action__Flight_GetResult_Request__Sequence
{
  chaos_interfaces__action__Flight_GetResult_Request * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__action__Flight_GetResult_Request__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'result'
// already included above
// #include "chaos_interfaces/action/detail/flight__struct.h"

/// Struct defined in action/Flight in the package chaos_interfaces.
typedef struct chaos_interfaces__action__Flight_GetResult_Response
{
  int8_t status;
  chaos_interfaces__action__Flight_Result result;
} chaos_interfaces__action__Flight_GetResult_Response;

// Struct for a sequence of chaos_interfaces__action__Flight_GetResult_Response.
typedef struct chaos_interfaces__action__Flight_GetResult_Response__Sequence
{
  chaos_interfaces__action__Flight_GetResult_Response * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__action__Flight_GetResult_Response__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'info'
// already included above
// #include "service_msgs/msg/detail/service_event_info__struct.h"

// constants for array fields with an upper bound
// request
enum
{
  chaos_interfaces__action__Flight_GetResult_Event__request__MAX_SIZE = 1
};
// response
enum
{
  chaos_interfaces__action__Flight_GetResult_Event__response__MAX_SIZE = 1
};

/// Struct defined in action/Flight in the package chaos_interfaces.
typedef struct chaos_interfaces__action__Flight_GetResult_Event
{
  service_msgs__msg__ServiceEventInfo info;
  chaos_interfaces__action__Flight_GetResult_Request__Sequence request;
  chaos_interfaces__action__Flight_GetResult_Response__Sequence response;
} chaos_interfaces__action__Flight_GetResult_Event;

// Struct for a sequence of chaos_interfaces__action__Flight_GetResult_Event.
typedef struct chaos_interfaces__action__Flight_GetResult_Event__Sequence
{
  chaos_interfaces__action__Flight_GetResult_Event * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__action__Flight_GetResult_Event__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'goal_id'
// already included above
// #include "unique_identifier_msgs/msg/detail/uuid__struct.h"
// Member 'feedback'
// already included above
// #include "chaos_interfaces/action/detail/flight__struct.h"

/// Struct defined in action/Flight in the package chaos_interfaces.
typedef struct chaos_interfaces__action__Flight_FeedbackMessage
{
  unique_identifier_msgs__msg__UUID goal_id;
  chaos_interfaces__action__Flight_Feedback feedback;
} chaos_interfaces__action__Flight_FeedbackMessage;

// Struct for a sequence of chaos_interfaces__action__Flight_FeedbackMessage.
typedef struct chaos_interfaces__action__Flight_FeedbackMessage__Sequence
{
  chaos_interfaces__action__Flight_FeedbackMessage * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} chaos_interfaces__action__Flight_FeedbackMessage__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // CHAOS_INTERFACES__ACTION__DETAIL__FLIGHT__STRUCT_H_
