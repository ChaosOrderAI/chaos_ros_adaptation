// generated from rosidl_typesupport_introspection_c/resource/idl__type_support.c.em
// with input from chaos_interfaces:srv/Stop.idl
// generated code does not contain a copyright notice

#include <stddef.h>
#include "chaos_interfaces/srv/detail/stop__rosidl_typesupport_introspection_c.h"
#include "chaos_interfaces/msg/rosidl_typesupport_introspection_c__visibility_control.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "chaos_interfaces/srv/detail/stop__functions.h"
#include "chaos_interfaces/srv/detail/stop__struct.h"


// Include directives for member types
// Member `request_id`
// Member `lease_token`
#include "rosidl_runtime_c/string_functions.h"
// Member `expires_at`
#include "builtin_interfaces/msg/time.h"
// Member `expires_at`
#include "builtin_interfaces/msg/detail/time__rosidl_typesupport_introspection_c.h"

#ifdef __cplusplus
extern "C"
{
#endif

void chaos_interfaces__srv__Stop_Request__rosidl_typesupport_introspection_c__Stop_Request_init_function(
  void * message_memory, enum rosidl_runtime_c__message_initialization _init)
{
  // TODO(karsten1987): initializers are not yet implemented for typesupport c
  // see https://github.com/ros2/ros2/issues/397
  (void) _init;
  chaos_interfaces__srv__Stop_Request__init(message_memory);
}

void chaos_interfaces__srv__Stop_Request__rosidl_typesupport_introspection_c__Stop_Request_fini_function(void * message_memory)
{
  chaos_interfaces__srv__Stop_Request__fini(message_memory);
}

static rosidl_typesupport_introspection_c__MessageMember chaos_interfaces__srv__Stop_Request__rosidl_typesupport_introspection_c__Stop_Request_message_member_array[3] = {
  {
    "request_id",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces__srv__Stop_Request, request_id),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "lease_token",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces__srv__Stop_Request, lease_token),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "expires_at",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message (initialized later)
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces__srv__Stop_Request, expires_at),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  }
};

static const rosidl_typesupport_introspection_c__MessageMembers chaos_interfaces__srv__Stop_Request__rosidl_typesupport_introspection_c__Stop_Request_message_members = {
  "chaos_interfaces__srv",  // message namespace
  "Stop_Request",  // message name
  3,  // number of fields
  sizeof(chaos_interfaces__srv__Stop_Request),
  false,  // has_any_key_member_
  chaos_interfaces__srv__Stop_Request__rosidl_typesupport_introspection_c__Stop_Request_message_member_array,  // message members
  chaos_interfaces__srv__Stop_Request__rosidl_typesupport_introspection_c__Stop_Request_init_function,  // function to initialize message memory (memory has to be allocated)
  chaos_interfaces__srv__Stop_Request__rosidl_typesupport_introspection_c__Stop_Request_fini_function  // function to terminate message instance (will not free memory)
};

// this is not const since it must be initialized on first access
// since C does not allow non-integral compile-time constants
static rosidl_message_type_support_t chaos_interfaces__srv__Stop_Request__rosidl_typesupport_introspection_c__Stop_Request_message_type_support_handle = {
  0,
  &chaos_interfaces__srv__Stop_Request__rosidl_typesupport_introspection_c__Stop_Request_message_members,
  get_message_typesupport_handle_function,
  &chaos_interfaces__srv__Stop_Request__get_type_hash,
  &chaos_interfaces__srv__Stop_Request__get_type_description,
  &chaos_interfaces__srv__Stop_Request__get_type_description_sources,
};

ROSIDL_TYPESUPPORT_INTROSPECTION_C_EXPORT_chaos_interfaces
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, chaos_interfaces, srv, Stop_Request)() {
  chaos_interfaces__srv__Stop_Request__rosidl_typesupport_introspection_c__Stop_Request_message_member_array[2].members_ =
    ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, builtin_interfaces, msg, Time)();
  if (!chaos_interfaces__srv__Stop_Request__rosidl_typesupport_introspection_c__Stop_Request_message_type_support_handle.typesupport_identifier) {
    chaos_interfaces__srv__Stop_Request__rosidl_typesupport_introspection_c__Stop_Request_message_type_support_handle.typesupport_identifier =
      rosidl_typesupport_introspection_c__identifier;
  }
  return &chaos_interfaces__srv__Stop_Request__rosidl_typesupport_introspection_c__Stop_Request_message_type_support_handle;
}
#ifdef __cplusplus
}
#endif

// already included above
// #include <stddef.h>
// already included above
// #include "chaos_interfaces/srv/detail/stop__rosidl_typesupport_introspection_c.h"
// already included above
// #include "chaos_interfaces/msg/rosidl_typesupport_introspection_c__visibility_control.h"
// already included above
// #include "rosidl_typesupport_introspection_c/field_types.h"
// already included above
// #include "rosidl_typesupport_introspection_c/identifier.h"
// already included above
// #include "rosidl_typesupport_introspection_c/message_introspection.h"
// already included above
// #include "chaos_interfaces/srv/detail/stop__functions.h"
// already included above
// #include "chaos_interfaces/srv/detail/stop__struct.h"


// Include directives for member types
// Member `code`
// Member `message`
// already included above
// #include "rosidl_runtime_c/string_functions.h"

#ifdef __cplusplus
extern "C"
{
#endif

void chaos_interfaces__srv__Stop_Response__rosidl_typesupport_introspection_c__Stop_Response_init_function(
  void * message_memory, enum rosidl_runtime_c__message_initialization _init)
{
  // TODO(karsten1987): initializers are not yet implemented for typesupport c
  // see https://github.com/ros2/ros2/issues/397
  (void) _init;
  chaos_interfaces__srv__Stop_Response__init(message_memory);
}

void chaos_interfaces__srv__Stop_Response__rosidl_typesupport_introspection_c__Stop_Response_fini_function(void * message_memory)
{
  chaos_interfaces__srv__Stop_Response__fini(message_memory);
}

static rosidl_typesupport_introspection_c__MessageMember chaos_interfaces__srv__Stop_Response__rosidl_typesupport_introspection_c__Stop_Response_message_member_array[3] = {
  {
    "success",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_BOOLEAN,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces__srv__Stop_Response, success),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "code",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces__srv__Stop_Response, code),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "message",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces__srv__Stop_Response, message),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  }
};

static const rosidl_typesupport_introspection_c__MessageMembers chaos_interfaces__srv__Stop_Response__rosidl_typesupport_introspection_c__Stop_Response_message_members = {
  "chaos_interfaces__srv",  // message namespace
  "Stop_Response",  // message name
  3,  // number of fields
  sizeof(chaos_interfaces__srv__Stop_Response),
  false,  // has_any_key_member_
  chaos_interfaces__srv__Stop_Response__rosidl_typesupport_introspection_c__Stop_Response_message_member_array,  // message members
  chaos_interfaces__srv__Stop_Response__rosidl_typesupport_introspection_c__Stop_Response_init_function,  // function to initialize message memory (memory has to be allocated)
  chaos_interfaces__srv__Stop_Response__rosidl_typesupport_introspection_c__Stop_Response_fini_function  // function to terminate message instance (will not free memory)
};

// this is not const since it must be initialized on first access
// since C does not allow non-integral compile-time constants
static rosidl_message_type_support_t chaos_interfaces__srv__Stop_Response__rosidl_typesupport_introspection_c__Stop_Response_message_type_support_handle = {
  0,
  &chaos_interfaces__srv__Stop_Response__rosidl_typesupport_introspection_c__Stop_Response_message_members,
  get_message_typesupport_handle_function,
  &chaos_interfaces__srv__Stop_Response__get_type_hash,
  &chaos_interfaces__srv__Stop_Response__get_type_description,
  &chaos_interfaces__srv__Stop_Response__get_type_description_sources,
};

ROSIDL_TYPESUPPORT_INTROSPECTION_C_EXPORT_chaos_interfaces
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, chaos_interfaces, srv, Stop_Response)() {
  if (!chaos_interfaces__srv__Stop_Response__rosidl_typesupport_introspection_c__Stop_Response_message_type_support_handle.typesupport_identifier) {
    chaos_interfaces__srv__Stop_Response__rosidl_typesupport_introspection_c__Stop_Response_message_type_support_handle.typesupport_identifier =
      rosidl_typesupport_introspection_c__identifier;
  }
  return &chaos_interfaces__srv__Stop_Response__rosidl_typesupport_introspection_c__Stop_Response_message_type_support_handle;
}
#ifdef __cplusplus
}
#endif

// already included above
// #include <stddef.h>
// already included above
// #include "chaos_interfaces/srv/detail/stop__rosidl_typesupport_introspection_c.h"
// already included above
// #include "chaos_interfaces/msg/rosidl_typesupport_introspection_c__visibility_control.h"
// already included above
// #include "rosidl_typesupport_introspection_c/field_types.h"
// already included above
// #include "rosidl_typesupport_introspection_c/identifier.h"
// already included above
// #include "rosidl_typesupport_introspection_c/message_introspection.h"
// already included above
// #include "chaos_interfaces/srv/detail/stop__functions.h"
// already included above
// #include "chaos_interfaces/srv/detail/stop__struct.h"


// Include directives for member types
// Member `info`
#include "service_msgs/msg/service_event_info.h"
// Member `info`
#include "service_msgs/msg/detail/service_event_info__rosidl_typesupport_introspection_c.h"
// Member `request`
// Member `response`
#include "chaos_interfaces/srv/stop.h"
// Member `request`
// Member `response`
// already included above
// #include "chaos_interfaces/srv/detail/stop__rosidl_typesupport_introspection_c.h"

#ifdef __cplusplus
extern "C"
{
#endif

void chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__Stop_Event_init_function(
  void * message_memory, enum rosidl_runtime_c__message_initialization _init)
{
  // TODO(karsten1987): initializers are not yet implemented for typesupport c
  // see https://github.com/ros2/ros2/issues/397
  (void) _init;
  chaos_interfaces__srv__Stop_Event__init(message_memory);
}

void chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__Stop_Event_fini_function(void * message_memory)
{
  chaos_interfaces__srv__Stop_Event__fini(message_memory);
}

size_t chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__size_function__Stop_Event__request(
  const void * untyped_member)
{
  const chaos_interfaces__srv__Stop_Request__Sequence * member =
    (const chaos_interfaces__srv__Stop_Request__Sequence *)(untyped_member);
  return member->size;
}

const void * chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__get_const_function__Stop_Event__request(
  const void * untyped_member, size_t index)
{
  const chaos_interfaces__srv__Stop_Request__Sequence * member =
    (const chaos_interfaces__srv__Stop_Request__Sequence *)(untyped_member);
  return &member->data[index];
}

void * chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__get_function__Stop_Event__request(
  void * untyped_member, size_t index)
{
  chaos_interfaces__srv__Stop_Request__Sequence * member =
    (chaos_interfaces__srv__Stop_Request__Sequence *)(untyped_member);
  return &member->data[index];
}

void chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__fetch_function__Stop_Event__request(
  const void * untyped_member, size_t index, void * untyped_value)
{
  const chaos_interfaces__srv__Stop_Request * item =
    ((const chaos_interfaces__srv__Stop_Request *)
    chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__get_const_function__Stop_Event__request(untyped_member, index));
  chaos_interfaces__srv__Stop_Request * value =
    (chaos_interfaces__srv__Stop_Request *)(untyped_value);
  *value = *item;
}

void chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__assign_function__Stop_Event__request(
  void * untyped_member, size_t index, const void * untyped_value)
{
  chaos_interfaces__srv__Stop_Request * item =
    ((chaos_interfaces__srv__Stop_Request *)
    chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__get_function__Stop_Event__request(untyped_member, index));
  const chaos_interfaces__srv__Stop_Request * value =
    (const chaos_interfaces__srv__Stop_Request *)(untyped_value);
  *item = *value;
}

bool chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__resize_function__Stop_Event__request(
  void * untyped_member, size_t size)
{
  chaos_interfaces__srv__Stop_Request__Sequence * member =
    (chaos_interfaces__srv__Stop_Request__Sequence *)(untyped_member);
  chaos_interfaces__srv__Stop_Request__Sequence__fini(member);
  return chaos_interfaces__srv__Stop_Request__Sequence__init(member, size);
}

size_t chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__size_function__Stop_Event__response(
  const void * untyped_member)
{
  const chaos_interfaces__srv__Stop_Response__Sequence * member =
    (const chaos_interfaces__srv__Stop_Response__Sequence *)(untyped_member);
  return member->size;
}

const void * chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__get_const_function__Stop_Event__response(
  const void * untyped_member, size_t index)
{
  const chaos_interfaces__srv__Stop_Response__Sequence * member =
    (const chaos_interfaces__srv__Stop_Response__Sequence *)(untyped_member);
  return &member->data[index];
}

void * chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__get_function__Stop_Event__response(
  void * untyped_member, size_t index)
{
  chaos_interfaces__srv__Stop_Response__Sequence * member =
    (chaos_interfaces__srv__Stop_Response__Sequence *)(untyped_member);
  return &member->data[index];
}

void chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__fetch_function__Stop_Event__response(
  const void * untyped_member, size_t index, void * untyped_value)
{
  const chaos_interfaces__srv__Stop_Response * item =
    ((const chaos_interfaces__srv__Stop_Response *)
    chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__get_const_function__Stop_Event__response(untyped_member, index));
  chaos_interfaces__srv__Stop_Response * value =
    (chaos_interfaces__srv__Stop_Response *)(untyped_value);
  *value = *item;
}

void chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__assign_function__Stop_Event__response(
  void * untyped_member, size_t index, const void * untyped_value)
{
  chaos_interfaces__srv__Stop_Response * item =
    ((chaos_interfaces__srv__Stop_Response *)
    chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__get_function__Stop_Event__response(untyped_member, index));
  const chaos_interfaces__srv__Stop_Response * value =
    (const chaos_interfaces__srv__Stop_Response *)(untyped_value);
  *item = *value;
}

bool chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__resize_function__Stop_Event__response(
  void * untyped_member, size_t size)
{
  chaos_interfaces__srv__Stop_Response__Sequence * member =
    (chaos_interfaces__srv__Stop_Response__Sequence *)(untyped_member);
  chaos_interfaces__srv__Stop_Response__Sequence__fini(member);
  return chaos_interfaces__srv__Stop_Response__Sequence__init(member, size);
}

static rosidl_typesupport_introspection_c__MessageMember chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__Stop_Event_message_member_array[3] = {
  {
    "info",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message (initialized later)
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces__srv__Stop_Event, info),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "request",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message (initialized later)
    false,  // is key
    true,  // is array
    1,  // array size
    true,  // is upper bound
    offsetof(chaos_interfaces__srv__Stop_Event, request),  // bytes offset in struct
    NULL,  // default value
    chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__size_function__Stop_Event__request,  // size() function pointer
    chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__get_const_function__Stop_Event__request,  // get_const(index) function pointer
    chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__get_function__Stop_Event__request,  // get(index) function pointer
    chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__fetch_function__Stop_Event__request,  // fetch(index, &value) function pointer
    chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__assign_function__Stop_Event__request,  // assign(index, value) function pointer
    chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__resize_function__Stop_Event__request  // resize(index) function pointer
  },
  {
    "response",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message (initialized later)
    false,  // is key
    true,  // is array
    1,  // array size
    true,  // is upper bound
    offsetof(chaos_interfaces__srv__Stop_Event, response),  // bytes offset in struct
    NULL,  // default value
    chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__size_function__Stop_Event__response,  // size() function pointer
    chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__get_const_function__Stop_Event__response,  // get_const(index) function pointer
    chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__get_function__Stop_Event__response,  // get(index) function pointer
    chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__fetch_function__Stop_Event__response,  // fetch(index, &value) function pointer
    chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__assign_function__Stop_Event__response,  // assign(index, value) function pointer
    chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__resize_function__Stop_Event__response  // resize(index) function pointer
  }
};

static const rosidl_typesupport_introspection_c__MessageMembers chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__Stop_Event_message_members = {
  "chaos_interfaces__srv",  // message namespace
  "Stop_Event",  // message name
  3,  // number of fields
  sizeof(chaos_interfaces__srv__Stop_Event),
  false,  // has_any_key_member_
  chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__Stop_Event_message_member_array,  // message members
  chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__Stop_Event_init_function,  // function to initialize message memory (memory has to be allocated)
  chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__Stop_Event_fini_function  // function to terminate message instance (will not free memory)
};

// this is not const since it must be initialized on first access
// since C does not allow non-integral compile-time constants
static rosidl_message_type_support_t chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__Stop_Event_message_type_support_handle = {
  0,
  &chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__Stop_Event_message_members,
  get_message_typesupport_handle_function,
  &chaos_interfaces__srv__Stop_Event__get_type_hash,
  &chaos_interfaces__srv__Stop_Event__get_type_description,
  &chaos_interfaces__srv__Stop_Event__get_type_description_sources,
};

ROSIDL_TYPESUPPORT_INTROSPECTION_C_EXPORT_chaos_interfaces
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, chaos_interfaces, srv, Stop_Event)() {
  chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__Stop_Event_message_member_array[0].members_ =
    ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, service_msgs, msg, ServiceEventInfo)();
  chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__Stop_Event_message_member_array[1].members_ =
    ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, chaos_interfaces, srv, Stop_Request)();
  chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__Stop_Event_message_member_array[2].members_ =
    ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, chaos_interfaces, srv, Stop_Response)();
  if (!chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__Stop_Event_message_type_support_handle.typesupport_identifier) {
    chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__Stop_Event_message_type_support_handle.typesupport_identifier =
      rosidl_typesupport_introspection_c__identifier;
  }
  return &chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__Stop_Event_message_type_support_handle;
}
#ifdef __cplusplus
}
#endif

#include "rosidl_runtime_c/service_type_support_struct.h"
// already included above
// #include "chaos_interfaces/msg/rosidl_typesupport_introspection_c__visibility_control.h"
// already included above
// #include "chaos_interfaces/srv/detail/stop__rosidl_typesupport_introspection_c.h"
// already included above
// #include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/service_introspection.h"

// this is intentionally not const to allow initialization later to prevent an initialization race
static rosidl_typesupport_introspection_c__ServiceMembers chaos_interfaces__srv__detail__stop__rosidl_typesupport_introspection_c__Stop_service_members = {
  "chaos_interfaces__srv",  // service namespace
  "Stop",  // service name
  // the following fields are initialized below on first access
  NULL,  // request message
  // chaos_interfaces__srv__detail__stop__rosidl_typesupport_introspection_c__Stop_Request_message_type_support_handle,
  NULL,  // response message
  // chaos_interfaces__srv__detail__stop__rosidl_typesupport_introspection_c__Stop_Response_message_type_support_handle
  NULL  // event_message
  // chaos_interfaces__srv__detail__stop__rosidl_typesupport_introspection_c__Stop_Response_message_type_support_handle
};


static rosidl_service_type_support_t chaos_interfaces__srv__detail__stop__rosidl_typesupport_introspection_c__Stop_service_type_support_handle = {
  0,
  &chaos_interfaces__srv__detail__stop__rosidl_typesupport_introspection_c__Stop_service_members,
  get_service_typesupport_handle_function,
  &chaos_interfaces__srv__Stop_Request__rosidl_typesupport_introspection_c__Stop_Request_message_type_support_handle,
  &chaos_interfaces__srv__Stop_Response__rosidl_typesupport_introspection_c__Stop_Response_message_type_support_handle,
  &chaos_interfaces__srv__Stop_Event__rosidl_typesupport_introspection_c__Stop_Event_message_type_support_handle,
  ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_CREATE_EVENT_MESSAGE_SYMBOL_NAME(
    rosidl_typesupport_c,
    chaos_interfaces,
    srv,
    Stop
  ),
  ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_DESTROY_EVENT_MESSAGE_SYMBOL_NAME(
    rosidl_typesupport_c,
    chaos_interfaces,
    srv,
    Stop
  ),
  &chaos_interfaces__srv__Stop__get_type_hash,
  &chaos_interfaces__srv__Stop__get_type_description,
  &chaos_interfaces__srv__Stop__get_type_description_sources,
};

// Forward declaration of message type support functions for service members
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, chaos_interfaces, srv, Stop_Request)(void);

const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, chaos_interfaces, srv, Stop_Response)(void);

const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, chaos_interfaces, srv, Stop_Event)(void);

ROSIDL_TYPESUPPORT_INTROSPECTION_C_EXPORT_chaos_interfaces
const rosidl_service_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(rosidl_typesupport_introspection_c, chaos_interfaces, srv, Stop)(void) {
  if (!chaos_interfaces__srv__detail__stop__rosidl_typesupport_introspection_c__Stop_service_type_support_handle.typesupport_identifier) {
    chaos_interfaces__srv__detail__stop__rosidl_typesupport_introspection_c__Stop_service_type_support_handle.typesupport_identifier =
      rosidl_typesupport_introspection_c__identifier;
  }
  rosidl_typesupport_introspection_c__ServiceMembers * service_members =
    (rosidl_typesupport_introspection_c__ServiceMembers *)chaos_interfaces__srv__detail__stop__rosidl_typesupport_introspection_c__Stop_service_type_support_handle.data;

  if (!service_members->request_members_) {
    service_members->request_members_ =
      (const rosidl_typesupport_introspection_c__MessageMembers *)
      ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, chaos_interfaces, srv, Stop_Request)()->data;
  }
  if (!service_members->response_members_) {
    service_members->response_members_ =
      (const rosidl_typesupport_introspection_c__MessageMembers *)
      ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, chaos_interfaces, srv, Stop_Response)()->data;
  }
  if (!service_members->event_members_) {
    service_members->event_members_ =
      (const rosidl_typesupport_introspection_c__MessageMembers *)
      ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, chaos_interfaces, srv, Stop_Event)()->data;
  }

  return &chaos_interfaces__srv__detail__stop__rosidl_typesupport_introspection_c__Stop_service_type_support_handle;
}
