// generated from rosidl_typesupport_fastrtps_c/resource/idl__rosidl_typesupport_fastrtps_c.h.em
// with input from chaos_interfaces:msg/ControlTrace.idl
// generated code does not contain a copyright notice
#ifndef CHAOS_INTERFACES__MSG__DETAIL__CONTROL_TRACE__ROSIDL_TYPESUPPORT_FASTRTPS_C_H_
#define CHAOS_INTERFACES__MSG__DETAIL__CONTROL_TRACE__ROSIDL_TYPESUPPORT_FASTRTPS_C_H_


#include <stddef.h>
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_typesupport_interface/macros.h"
#include "chaos_interfaces/msg/rosidl_typesupport_fastrtps_c__visibility_control.h"
#include "chaos_interfaces/msg/detail/control_trace__struct.h"
#include "fastcdr/Cdr.h"

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_chaos_interfaces
bool cdr_serialize_chaos_interfaces__msg__ControlTrace(
  const chaos_interfaces__msg__ControlTrace * ros_message,
  eprosima::fastcdr::Cdr & cdr);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_chaos_interfaces
bool cdr_deserialize_chaos_interfaces__msg__ControlTrace(
  eprosima::fastcdr::Cdr &,
  chaos_interfaces__msg__ControlTrace * ros_message);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_chaos_interfaces
size_t get_serialized_size_chaos_interfaces__msg__ControlTrace(
  const void * untyped_ros_message,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_chaos_interfaces
size_t max_serialized_size_chaos_interfaces__msg__ControlTrace(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_chaos_interfaces
bool cdr_serialize_key_chaos_interfaces__msg__ControlTrace(
  const chaos_interfaces__msg__ControlTrace * ros_message,
  eprosima::fastcdr::Cdr & cdr);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_chaos_interfaces
size_t get_serialized_size_key_chaos_interfaces__msg__ControlTrace(
  const void * untyped_ros_message,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_chaos_interfaces
size_t max_serialized_size_key_chaos_interfaces__msg__ControlTrace(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_chaos_interfaces
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_c, chaos_interfaces, msg, ControlTrace)();

#ifdef __cplusplus
}
#endif

#endif  // CHAOS_INTERFACES__MSG__DETAIL__CONTROL_TRACE__ROSIDL_TYPESUPPORT_FASTRTPS_C_H_
