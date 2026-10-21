// generated from rosidl_typesupport_fastrtps_c/resource/idl__type_support_c.cpp.em
// with input from chaos_interfaces:msg/VelocityCommand.idl
// generated code does not contain a copyright notice
#include "chaos_interfaces/msg/detail/velocity_command__rosidl_typesupport_fastrtps_c.h"


#include <cassert>
#include <cstddef>
#include <limits>
#include <string>
#include "rosidl_typesupport_fastrtps_c/identifier.h"
#include "rosidl_typesupport_fastrtps_c/serialization_helpers.hpp"
#include "rosidl_typesupport_fastrtps_c/wstring_conversion.hpp"
#include "rosidl_typesupport_fastrtps_cpp/message_type_support.h"
#include "chaos_interfaces/msg/rosidl_typesupport_fastrtps_c__visibility_control.h"
#include "chaos_interfaces/msg/detail/velocity_command__struct.h"
#include "chaos_interfaces/msg/detail/velocity_command__functions.h"
#include "fastcdr/Cdr.h"

#ifndef _WIN32
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wunused-parameter"
# ifdef __clang__
#  pragma clang diagnostic ignored "-Wdeprecated-register"
#  pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
# endif
#endif
#ifndef _WIN32
# pragma GCC diagnostic pop
#endif

// includes and forward declarations of message dependencies and their conversion functions

#if defined(__cplusplus)
extern "C"
{
#endif

#include "geometry_msgs/msg/detail/twist__functions.h"  // velocity
#include "rosidl_runtime_c/string.h"  // lease_token, request_id
#include "rosidl_runtime_c/string_functions.h"  // lease_token, request_id
#include "std_msgs/msg/detail/header__functions.h"  // header

// forward declare type support functions

ROSIDL_TYPESUPPORT_FASTRTPS_C_IMPORT_chaos_interfaces
bool cdr_serialize_geometry_msgs__msg__Twist(
  const geometry_msgs__msg__Twist * ros_message,
  eprosima::fastcdr::Cdr & cdr);

ROSIDL_TYPESUPPORT_FASTRTPS_C_IMPORT_chaos_interfaces
bool cdr_deserialize_geometry_msgs__msg__Twist(
  eprosima::fastcdr::Cdr & cdr,
  geometry_msgs__msg__Twist * ros_message);

ROSIDL_TYPESUPPORT_FASTRTPS_C_IMPORT_chaos_interfaces
size_t get_serialized_size_geometry_msgs__msg__Twist(
  const void * untyped_ros_message,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_IMPORT_chaos_interfaces
size_t max_serialized_size_geometry_msgs__msg__Twist(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_IMPORT_chaos_interfaces
bool cdr_serialize_key_geometry_msgs__msg__Twist(
  const geometry_msgs__msg__Twist * ros_message,
  eprosima::fastcdr::Cdr & cdr);

ROSIDL_TYPESUPPORT_FASTRTPS_C_IMPORT_chaos_interfaces
size_t get_serialized_size_key_geometry_msgs__msg__Twist(
  const void * untyped_ros_message,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_IMPORT_chaos_interfaces
size_t max_serialized_size_key_geometry_msgs__msg__Twist(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_IMPORT_chaos_interfaces
const rosidl_message_type_support_t *
  ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_c, geometry_msgs, msg, Twist)();

ROSIDL_TYPESUPPORT_FASTRTPS_C_IMPORT_chaos_interfaces
bool cdr_serialize_std_msgs__msg__Header(
  const std_msgs__msg__Header * ros_message,
  eprosima::fastcdr::Cdr & cdr);

ROSIDL_TYPESUPPORT_FASTRTPS_C_IMPORT_chaos_interfaces
bool cdr_deserialize_std_msgs__msg__Header(
  eprosima::fastcdr::Cdr & cdr,
  std_msgs__msg__Header * ros_message);

ROSIDL_TYPESUPPORT_FASTRTPS_C_IMPORT_chaos_interfaces
size_t get_serialized_size_std_msgs__msg__Header(
  const void * untyped_ros_message,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_IMPORT_chaos_interfaces
size_t max_serialized_size_std_msgs__msg__Header(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_IMPORT_chaos_interfaces
bool cdr_serialize_key_std_msgs__msg__Header(
  const std_msgs__msg__Header * ros_message,
  eprosima::fastcdr::Cdr & cdr);

ROSIDL_TYPESUPPORT_FASTRTPS_C_IMPORT_chaos_interfaces
size_t get_serialized_size_key_std_msgs__msg__Header(
  const void * untyped_ros_message,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_IMPORT_chaos_interfaces
size_t max_serialized_size_key_std_msgs__msg__Header(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_IMPORT_chaos_interfaces
const rosidl_message_type_support_t *
  ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_c, std_msgs, msg, Header)();


using _VelocityCommand__ros_msg_type = chaos_interfaces__msg__VelocityCommand;


ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_chaos_interfaces
bool cdr_serialize_chaos_interfaces__msg__VelocityCommand(
  const chaos_interfaces__msg__VelocityCommand * ros_message,
  eprosima::fastcdr::Cdr & cdr)
{
  // Field name: header
  {
    cdr_serialize_std_msgs__msg__Header(
      &ros_message->header, cdr);
  }

  // Field name: request_id
  {
    const rosidl_runtime_c__String * str = &ros_message->request_id;
    if (str->capacity == 0 || str->capacity <= str->size) {
      fprintf(stderr, "string capacity not greater than size\n");
      return false;
    }
    if (str->data[str->size] != '\0') {
      fprintf(stderr, "string not null-terminated\n");
      return false;
    }
    cdr << str->data;
  }

  // Field name: lease_token
  {
    const rosidl_runtime_c__String * str = &ros_message->lease_token;
    if (str->capacity == 0 || str->capacity <= str->size) {
      fprintf(stderr, "string capacity not greater than size\n");
      return false;
    }
    if (str->data[str->size] != '\0') {
      fprintf(stderr, "string not null-terminated\n");
      return false;
    }
    cdr << str->data;
  }

  // Field name: valid_for_s
  {
    cdr << ros_message->valid_for_s;
  }

  // Field name: velocity
  {
    cdr_serialize_geometry_msgs__msg__Twist(
      &ros_message->velocity, cdr);
  }

  return true;
}

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_chaos_interfaces
bool cdr_deserialize_chaos_interfaces__msg__VelocityCommand(
  eprosima::fastcdr::Cdr & cdr,
  chaos_interfaces__msg__VelocityCommand * ros_message)
{
  // Field name: header
  {
    cdr_deserialize_std_msgs__msg__Header(cdr, &ros_message->header);
  }

  // Field name: request_id
  {
    std::string tmp;
    cdr >> tmp;
    if (!ros_message->request_id.data) {
      rosidl_runtime_c__String__init(&ros_message->request_id);
    }
    bool succeeded = rosidl_runtime_c__String__assign(
      &ros_message->request_id,
      tmp.c_str());
    if (!succeeded) {
      fprintf(stderr, "failed to assign string into field 'request_id'\n");
      return false;
    }
  }

  // Field name: lease_token
  {
    std::string tmp;
    cdr >> tmp;
    if (!ros_message->lease_token.data) {
      rosidl_runtime_c__String__init(&ros_message->lease_token);
    }
    bool succeeded = rosidl_runtime_c__String__assign(
      &ros_message->lease_token,
      tmp.c_str());
    if (!succeeded) {
      fprintf(stderr, "failed to assign string into field 'lease_token'\n");
      return false;
    }
  }

  // Field name: valid_for_s
  {
    cdr >> ros_message->valid_for_s;
  }

  // Field name: velocity
  {
    cdr_deserialize_geometry_msgs__msg__Twist(cdr, &ros_message->velocity);
  }

  return true;
}  // NOLINT(readability/fn_size)


ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_chaos_interfaces
size_t get_serialized_size_chaos_interfaces__msg__VelocityCommand(
  const void * untyped_ros_message,
  size_t current_alignment)
{
  const _VelocityCommand__ros_msg_type * ros_message = static_cast<const _VelocityCommand__ros_msg_type *>(untyped_ros_message);
  (void)ros_message;
  size_t initial_alignment = current_alignment;

  const size_t padding = 4;
  const size_t wchar_size = 4;
  (void)padding;
  (void)wchar_size;

  // Field name: header
  current_alignment += get_serialized_size_std_msgs__msg__Header(
    &(ros_message->header), current_alignment);

  // Field name: request_id
  current_alignment += padding +
    eprosima::fastcdr::Cdr::alignment(current_alignment, padding) +
    (ros_message->request_id.size + 1);

  // Field name: lease_token
  current_alignment += padding +
    eprosima::fastcdr::Cdr::alignment(current_alignment, padding) +
    (ros_message->lease_token.size + 1);

  // Field name: valid_for_s
  {
    size_t item_size = sizeof(ros_message->valid_for_s);
    current_alignment += item_size +
      eprosima::fastcdr::Cdr::alignment(current_alignment, item_size);
  }

  // Field name: velocity
  current_alignment += get_serialized_size_geometry_msgs__msg__Twist(
    &(ros_message->velocity), current_alignment);

  return current_alignment - initial_alignment;
}


ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_chaos_interfaces
size_t max_serialized_size_chaos_interfaces__msg__VelocityCommand(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment)
{
  size_t initial_alignment = current_alignment;

  const size_t padding = 4;
  const size_t wchar_size = 4;
  size_t last_member_size = 0;
  (void)last_member_size;
  (void)padding;
  (void)wchar_size;

  full_bounded = true;
  is_plain = true;

  // Field name: header
  {
    size_t array_size = 1;
    last_member_size = 0;
    for (size_t index = 0; index < array_size; ++index) {
      bool inner_full_bounded;
      bool inner_is_plain;
      size_t inner_size;
      inner_size =
        max_serialized_size_std_msgs__msg__Header(
        inner_full_bounded, inner_is_plain, current_alignment);
      last_member_size += inner_size;
      current_alignment += inner_size;
      full_bounded &= inner_full_bounded;
      is_plain &= inner_is_plain;
    }
  }

  // Field name: request_id
  {
    size_t array_size = 1;
    full_bounded = false;
    is_plain = false;
    for (size_t index = 0; index < array_size; ++index) {
      current_alignment += padding +
        eprosima::fastcdr::Cdr::alignment(current_alignment, padding) +
        1;
    }
  }

  // Field name: lease_token
  {
    size_t array_size = 1;
    full_bounded = false;
    is_plain = false;
    for (size_t index = 0; index < array_size; ++index) {
      current_alignment += padding +
        eprosima::fastcdr::Cdr::alignment(current_alignment, padding) +
        1;
    }
  }

  // Field name: valid_for_s
  {
    size_t array_size = 1;
    last_member_size = array_size * sizeof(uint64_t);
    current_alignment += array_size * sizeof(uint64_t) +
      eprosima::fastcdr::Cdr::alignment(current_alignment, sizeof(uint64_t));
  }

  // Field name: velocity
  {
    size_t array_size = 1;
    last_member_size = 0;
    for (size_t index = 0; index < array_size; ++index) {
      bool inner_full_bounded;
      bool inner_is_plain;
      size_t inner_size;
      inner_size =
        max_serialized_size_geometry_msgs__msg__Twist(
        inner_full_bounded, inner_is_plain, current_alignment);
      last_member_size += inner_size;
      current_alignment += inner_size;
      full_bounded &= inner_full_bounded;
      is_plain &= inner_is_plain;
    }
  }


  size_t ret_val = current_alignment - initial_alignment;
  if (is_plain) {
    // All members are plain, and type is not empty.
    // We still need to check that the in-memory alignment
    // is the same as the CDR mandated alignment.
    using DataType = chaos_interfaces__msg__VelocityCommand;
    is_plain =
      (
      offsetof(DataType, velocity) +
      last_member_size
      ) == ret_val;
  }
  return ret_val;
}

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_chaos_interfaces
bool cdr_serialize_key_chaos_interfaces__msg__VelocityCommand(
  const chaos_interfaces__msg__VelocityCommand * ros_message,
  eprosima::fastcdr::Cdr & cdr)
{
  // Field name: header
  {
    cdr_serialize_key_std_msgs__msg__Header(
      &ros_message->header, cdr);
  }

  // Field name: request_id
  {
    const rosidl_runtime_c__String * str = &ros_message->request_id;
    if (str->capacity == 0 || str->capacity <= str->size) {
      fprintf(stderr, "string capacity not greater than size\n");
      return false;
    }
    if (str->data[str->size] != '\0') {
      fprintf(stderr, "string not null-terminated\n");
      return false;
    }
    cdr << str->data;
  }

  // Field name: lease_token
  {
    const rosidl_runtime_c__String * str = &ros_message->lease_token;
    if (str->capacity == 0 || str->capacity <= str->size) {
      fprintf(stderr, "string capacity not greater than size\n");
      return false;
    }
    if (str->data[str->size] != '\0') {
      fprintf(stderr, "string not null-terminated\n");
      return false;
    }
    cdr << str->data;
  }

  // Field name: valid_for_s
  {
    cdr << ros_message->valid_for_s;
  }

  // Field name: velocity
  {
    cdr_serialize_key_geometry_msgs__msg__Twist(
      &ros_message->velocity, cdr);
  }

  return true;
}

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_chaos_interfaces
size_t get_serialized_size_key_chaos_interfaces__msg__VelocityCommand(
  const void * untyped_ros_message,
  size_t current_alignment)
{
  const _VelocityCommand__ros_msg_type * ros_message = static_cast<const _VelocityCommand__ros_msg_type *>(untyped_ros_message);
  (void)ros_message;

  size_t initial_alignment = current_alignment;

  const size_t padding = 4;
  const size_t wchar_size = 4;
  (void)padding;
  (void)wchar_size;

  // Field name: header
  current_alignment += get_serialized_size_key_std_msgs__msg__Header(
    &(ros_message->header), current_alignment);

  // Field name: request_id
  current_alignment += padding +
    eprosima::fastcdr::Cdr::alignment(current_alignment, padding) +
    (ros_message->request_id.size + 1);

  // Field name: lease_token
  current_alignment += padding +
    eprosima::fastcdr::Cdr::alignment(current_alignment, padding) +
    (ros_message->lease_token.size + 1);

  // Field name: valid_for_s
  {
    size_t item_size = sizeof(ros_message->valid_for_s);
    current_alignment += item_size +
      eprosima::fastcdr::Cdr::alignment(current_alignment, item_size);
  }

  // Field name: velocity
  current_alignment += get_serialized_size_key_geometry_msgs__msg__Twist(
    &(ros_message->velocity), current_alignment);

  return current_alignment - initial_alignment;
}

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_chaos_interfaces
size_t max_serialized_size_key_chaos_interfaces__msg__VelocityCommand(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment)
{
  size_t initial_alignment = current_alignment;

  const size_t padding = 4;
  const size_t wchar_size = 4;
  size_t last_member_size = 0;
  (void)last_member_size;
  (void)padding;
  (void)wchar_size;

  full_bounded = true;
  is_plain = true;
  // Field name: header
  {
    size_t array_size = 1;
    last_member_size = 0;
    for (size_t index = 0; index < array_size; ++index) {
      bool inner_full_bounded;
      bool inner_is_plain;
      size_t inner_size;
      inner_size =
        max_serialized_size_key_std_msgs__msg__Header(
        inner_full_bounded, inner_is_plain, current_alignment);
      last_member_size += inner_size;
      current_alignment += inner_size;
      full_bounded &= inner_full_bounded;
      is_plain &= inner_is_plain;
    }
  }

  // Field name: request_id
  {
    size_t array_size = 1;
    full_bounded = false;
    is_plain = false;
    for (size_t index = 0; index < array_size; ++index) {
      current_alignment += padding +
        eprosima::fastcdr::Cdr::alignment(current_alignment, padding) +
        1;
    }
  }

  // Field name: lease_token
  {
    size_t array_size = 1;
    full_bounded = false;
    is_plain = false;
    for (size_t index = 0; index < array_size; ++index) {
      current_alignment += padding +
        eprosima::fastcdr::Cdr::alignment(current_alignment, padding) +
        1;
    }
  }

  // Field name: valid_for_s
  {
    size_t array_size = 1;
    last_member_size = array_size * sizeof(uint64_t);
    current_alignment += array_size * sizeof(uint64_t) +
      eprosima::fastcdr::Cdr::alignment(current_alignment, sizeof(uint64_t));
  }

  // Field name: velocity
  {
    size_t array_size = 1;
    last_member_size = 0;
    for (size_t index = 0; index < array_size; ++index) {
      bool inner_full_bounded;
      bool inner_is_plain;
      size_t inner_size;
      inner_size =
        max_serialized_size_key_geometry_msgs__msg__Twist(
        inner_full_bounded, inner_is_plain, current_alignment);
      last_member_size += inner_size;
      current_alignment += inner_size;
      full_bounded &= inner_full_bounded;
      is_plain &= inner_is_plain;
    }
  }

  size_t ret_val = current_alignment - initial_alignment;
  if (is_plain) {
    // All members are plain, and type is not empty.
    // We still need to check that the in-memory alignment
    // is the same as the CDR mandated alignment.
    using DataType = chaos_interfaces__msg__VelocityCommand;
    is_plain =
      (
      offsetof(DataType, velocity) +
      last_member_size
      ) == ret_val;
  }
  return ret_val;
}


static bool _VelocityCommand__cdr_serialize(
  const void * untyped_ros_message,
  eprosima::fastcdr::Cdr & cdr)
{
  if (!untyped_ros_message) {
    fprintf(stderr, "ros message handle is null\n");
    return false;
  }
  const chaos_interfaces__msg__VelocityCommand * ros_message = static_cast<const chaos_interfaces__msg__VelocityCommand *>(untyped_ros_message);
  (void)ros_message;
  return cdr_serialize_chaos_interfaces__msg__VelocityCommand(ros_message, cdr);
}

static bool _VelocityCommand__cdr_deserialize(
  eprosima::fastcdr::Cdr & cdr,
  void * untyped_ros_message)
{
  if (!untyped_ros_message) {
    fprintf(stderr, "ros message handle is null\n");
    return false;
  }
  chaos_interfaces__msg__VelocityCommand * ros_message = static_cast<chaos_interfaces__msg__VelocityCommand *>(untyped_ros_message);
  (void)ros_message;
  return cdr_deserialize_chaos_interfaces__msg__VelocityCommand(cdr, ros_message);
}

static uint32_t _VelocityCommand__get_serialized_size(const void * untyped_ros_message)
{
  return static_cast<uint32_t>(
    get_serialized_size_chaos_interfaces__msg__VelocityCommand(
      untyped_ros_message, 0));
}

static size_t _VelocityCommand__max_serialized_size(char & bounds_info)
{
  bool full_bounded;
  bool is_plain;
  size_t ret_val;

  ret_val = max_serialized_size_chaos_interfaces__msg__VelocityCommand(
    full_bounded, is_plain, 0);

  bounds_info =
    is_plain ? ROSIDL_TYPESUPPORT_FASTRTPS_PLAIN_TYPE :
    full_bounded ? ROSIDL_TYPESUPPORT_FASTRTPS_BOUNDED_TYPE : ROSIDL_TYPESUPPORT_FASTRTPS_UNBOUNDED_TYPE;
  return ret_val;
}


static message_type_support_callbacks_t __callbacks_VelocityCommand = {
  "chaos_interfaces::msg",
  "VelocityCommand",
  _VelocityCommand__cdr_serialize,
  _VelocityCommand__cdr_deserialize,
  _VelocityCommand__get_serialized_size,
  _VelocityCommand__max_serialized_size,
  nullptr
};

static rosidl_message_type_support_t _VelocityCommand__type_support = {
  rosidl_typesupport_fastrtps_c__identifier,
  &__callbacks_VelocityCommand,
  get_message_typesupport_handle_function,
  &chaos_interfaces__msg__VelocityCommand__get_type_hash,
  &chaos_interfaces__msg__VelocityCommand__get_type_description,
  &chaos_interfaces__msg__VelocityCommand__get_type_description_sources,
};

const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_c, chaos_interfaces, msg, VelocityCommand)() {
  return &_VelocityCommand__type_support;
}

#if defined(__cplusplus)
}
#endif
