// generated from rosidl_typesupport_introspection_c/resource/idl__type_support.c.em
// with input from chaos_interfaces:msg/CommandResult.idl
// generated code does not contain a copyright notice

#include <stddef.h>
#include "chaos_interfaces/msg/detail/command_result__rosidl_typesupport_introspection_c.h"
#include "chaos_interfaces/msg/rosidl_typesupport_introspection_c__visibility_control.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "chaos_interfaces/msg/detail/command_result__functions.h"
#include "chaos_interfaces/msg/detail/command_result__struct.h"


// Include directives for member types
// Member `stamp`
#include "builtin_interfaces/msg/time.h"
// Member `stamp`
#include "builtin_interfaces/msg/detail/time__rosidl_typesupport_introspection_c.h"
// Member `request_id`
// Member `code`
// Member `message`
#include "rosidl_runtime_c/string_functions.h"

#ifdef __cplusplus
extern "C"
{
#endif

void chaos_interfaces__msg__CommandResult__rosidl_typesupport_introspection_c__CommandResult_init_function(
  void * message_memory, enum rosidl_runtime_c__message_initialization _init)
{
  // TODO(karsten1987): initializers are not yet implemented for typesupport c
  // see https://github.com/ros2/ros2/issues/397
  (void) _init;
  chaos_interfaces__msg__CommandResult__init(message_memory);
}

void chaos_interfaces__msg__CommandResult__rosidl_typesupport_introspection_c__CommandResult_fini_function(void * message_memory)
{
  chaos_interfaces__msg__CommandResult__fini(message_memory);
}

static rosidl_typesupport_introspection_c__MessageMember chaos_interfaces__msg__CommandResult__rosidl_typesupport_introspection_c__CommandResult_message_member_array[5] = {
  {
    "stamp",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message (initialized later)
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces__msg__CommandResult, stamp),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "request_id",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces__msg__CommandResult, request_id),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "success",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_BOOLEAN,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces__msg__CommandResult, success),  // bytes offset in struct
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
    offsetof(chaos_interfaces__msg__CommandResult, code),  // bytes offset in struct
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
    offsetof(chaos_interfaces__msg__CommandResult, message),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  }
};

static const rosidl_typesupport_introspection_c__MessageMembers chaos_interfaces__msg__CommandResult__rosidl_typesupport_introspection_c__CommandResult_message_members = {
  "chaos_interfaces__msg",  // message namespace
  "CommandResult",  // message name
  5,  // number of fields
  sizeof(chaos_interfaces__msg__CommandResult),
  false,  // has_any_key_member_
  chaos_interfaces__msg__CommandResult__rosidl_typesupport_introspection_c__CommandResult_message_member_array,  // message members
  chaos_interfaces__msg__CommandResult__rosidl_typesupport_introspection_c__CommandResult_init_function,  // function to initialize message memory (memory has to be allocated)
  chaos_interfaces__msg__CommandResult__rosidl_typesupport_introspection_c__CommandResult_fini_function  // function to terminate message instance (will not free memory)
};

// this is not const since it must be initialized on first access
// since C does not allow non-integral compile-time constants
static rosidl_message_type_support_t chaos_interfaces__msg__CommandResult__rosidl_typesupport_introspection_c__CommandResult_message_type_support_handle = {
  0,
  &chaos_interfaces__msg__CommandResult__rosidl_typesupport_introspection_c__CommandResult_message_members,
  get_message_typesupport_handle_function,
  &chaos_interfaces__msg__CommandResult__get_type_hash,
  &chaos_interfaces__msg__CommandResult__get_type_description,
  &chaos_interfaces__msg__CommandResult__get_type_description_sources,
};

ROSIDL_TYPESUPPORT_INTROSPECTION_C_EXPORT_chaos_interfaces
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, chaos_interfaces, msg, CommandResult)() {
  chaos_interfaces__msg__CommandResult__rosidl_typesupport_introspection_c__CommandResult_message_member_array[0].members_ =
    ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, builtin_interfaces, msg, Time)();
  if (!chaos_interfaces__msg__CommandResult__rosidl_typesupport_introspection_c__CommandResult_message_type_support_handle.typesupport_identifier) {
    chaos_interfaces__msg__CommandResult__rosidl_typesupport_introspection_c__CommandResult_message_type_support_handle.typesupport_identifier =
      rosidl_typesupport_introspection_c__identifier;
  }
  return &chaos_interfaces__msg__CommandResult__rosidl_typesupport_introspection_c__CommandResult_message_type_support_handle;
}
#ifdef __cplusplus
}
#endif
