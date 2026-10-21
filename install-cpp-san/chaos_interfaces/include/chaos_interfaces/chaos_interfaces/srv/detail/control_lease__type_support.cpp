// generated from rosidl_typesupport_introspection_cpp/resource/idl__type_support.cpp.em
// with input from chaos_interfaces:srv/ControlLease.idl
// generated code does not contain a copyright notice

#include "array"
#include "cstddef"
#include "string"
#include "vector"
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "rosidl_typesupport_interface/macros.h"
#include "chaos_interfaces/srv/detail/control_lease__functions.h"
#include "chaos_interfaces/srv/detail/control_lease__struct.hpp"
#include "rosidl_typesupport_introspection_cpp/field_types.hpp"
#include "rosidl_typesupport_introspection_cpp/identifier.hpp"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"
#include "rosidl_typesupport_introspection_cpp/message_type_support_decl.hpp"
#include "rosidl_typesupport_introspection_cpp/visibility_control.h"

namespace chaos_interfaces
{

namespace srv
{

namespace rosidl_typesupport_introspection_cpp
{

void ControlLease_Request_init_function(
  void * message_memory, rosidl_runtime_cpp::MessageInitialization _init)
{
  new (message_memory) chaos_interfaces::srv::ControlLease_Request(_init);
}

void ControlLease_Request_fini_function(void * message_memory)
{
  auto typed_message = static_cast<chaos_interfaces::srv::ControlLease_Request *>(message_memory);
  typed_message->~ControlLease_Request();
}

static const ::rosidl_typesupport_introspection_cpp::MessageMember ControlLease_Request_message_member_array[5] = {
  {
    "operation",  // name
    ::rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    nullptr,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces::srv::ControlLease_Request, operation),  // bytes offset in struct
    nullptr,  // default value
    nullptr,  // size() function pointer
    nullptr,  // get_const(index) function pointer
    nullptr,  // get(index) function pointer
    nullptr,  // fetch(index, &value) function pointer
    nullptr,  // assign(index, value) function pointer
    nullptr  // resize(index) function pointer
  },
  {
    "owner",  // name
    ::rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    nullptr,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces::srv::ControlLease_Request, owner),  // bytes offset in struct
    nullptr,  // default value
    nullptr,  // size() function pointer
    nullptr,  // get_const(index) function pointer
    nullptr,  // get(index) function pointer
    nullptr,  // fetch(index, &value) function pointer
    nullptr,  // assign(index, value) function pointer
    nullptr  // resize(index) function pointer
  },
  {
    "token",  // name
    ::rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    nullptr,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces::srv::ControlLease_Request, token),  // bytes offset in struct
    nullptr,  // default value
    nullptr,  // size() function pointer
    nullptr,  // get_const(index) function pointer
    nullptr,  // get(index) function pointer
    nullptr,  // fetch(index, &value) function pointer
    nullptr,  // assign(index, value) function pointer
    nullptr  // resize(index) function pointer
  },
  {
    "duration_s",  // name
    ::rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE,  // type
    0,  // upper bound of string
    nullptr,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces::srv::ControlLease_Request, duration_s),  // bytes offset in struct
    nullptr,  // default value
    nullptr,  // size() function pointer
    nullptr,  // get_const(index) function pointer
    nullptr,  // get(index) function pointer
    nullptr,  // fetch(index, &value) function pointer
    nullptr,  // assign(index, value) function pointer
    nullptr  // resize(index) function pointer
  },
  {
    "expires_at",  // name
    ::rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE,  // type
    0,  // upper bound of string
    ::rosidl_typesupport_introspection_cpp::get_message_type_support_handle<builtin_interfaces::msg::Time>(),  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces::srv::ControlLease_Request, expires_at),  // bytes offset in struct
    nullptr,  // default value
    nullptr,  // size() function pointer
    nullptr,  // get_const(index) function pointer
    nullptr,  // get(index) function pointer
    nullptr,  // fetch(index, &value) function pointer
    nullptr,  // assign(index, value) function pointer
    nullptr  // resize(index) function pointer
  }
};

static const ::rosidl_typesupport_introspection_cpp::MessageMembers ControlLease_Request_message_members = {
  "chaos_interfaces::srv",  // message namespace
  "ControlLease_Request",  // message name
  5,  // number of fields
  sizeof(chaos_interfaces::srv::ControlLease_Request),
  false,  // has_any_key_member_
  ControlLease_Request_message_member_array,  // message members
  ControlLease_Request_init_function,  // function to initialize message memory (memory has to be allocated)
  ControlLease_Request_fini_function  // function to terminate message instance (will not free memory)
};

static const rosidl_message_type_support_t ControlLease_Request_message_type_support_handle = {
  ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  &ControlLease_Request_message_members,
  get_message_typesupport_handle_function,
  &chaos_interfaces__srv__ControlLease_Request__get_type_hash,
  &chaos_interfaces__srv__ControlLease_Request__get_type_description,
  &chaos_interfaces__srv__ControlLease_Request__get_type_description_sources,
};

}  // namespace rosidl_typesupport_introspection_cpp

}  // namespace srv

}  // namespace chaos_interfaces


namespace rosidl_typesupport_introspection_cpp
{

template<>
ROSIDL_TYPESUPPORT_INTROSPECTION_CPP_PUBLIC
const rosidl_message_type_support_t *
get_message_type_support_handle<chaos_interfaces::srv::ControlLease_Request>()
{
  return &::chaos_interfaces::srv::rosidl_typesupport_introspection_cpp::ControlLease_Request_message_type_support_handle;
}

}  // namespace rosidl_typesupport_introspection_cpp

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_INTROSPECTION_CPP_PUBLIC
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, chaos_interfaces, srv, ControlLease_Request)() {
  return &::chaos_interfaces::srv::rosidl_typesupport_introspection_cpp::ControlLease_Request_message_type_support_handle;
}

#ifdef __cplusplus
}
#endif

// already included above
// #include "array"
// already included above
// #include "cstddef"
// already included above
// #include "string"
// already included above
// #include "vector"
// already included above
// #include "rosidl_runtime_c/message_type_support_struct.h"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support.hpp"
// already included above
// #include "rosidl_typesupport_interface/macros.h"
// already included above
// #include "chaos_interfaces/srv/detail/control_lease__functions.h"
// already included above
// #include "chaos_interfaces/srv/detail/control_lease__struct.hpp"
// already included above
// #include "rosidl_typesupport_introspection_cpp/field_types.hpp"
// already included above
// #include "rosidl_typesupport_introspection_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"
// already included above
// #include "rosidl_typesupport_introspection_cpp/message_type_support_decl.hpp"
// already included above
// #include "rosidl_typesupport_introspection_cpp/visibility_control.h"

namespace chaos_interfaces
{

namespace srv
{

namespace rosidl_typesupport_introspection_cpp
{

void ControlLease_Response_init_function(
  void * message_memory, rosidl_runtime_cpp::MessageInitialization _init)
{
  new (message_memory) chaos_interfaces::srv::ControlLease_Response(_init);
}

void ControlLease_Response_fini_function(void * message_memory)
{
  auto typed_message = static_cast<chaos_interfaces::srv::ControlLease_Response *>(message_memory);
  typed_message->~ControlLease_Response();
}

static const ::rosidl_typesupport_introspection_cpp::MessageMember ControlLease_Response_message_member_array[5] = {
  {
    "success",  // name
    ::rosidl_typesupport_introspection_cpp::ROS_TYPE_BOOLEAN,  // type
    0,  // upper bound of string
    nullptr,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces::srv::ControlLease_Response, success),  // bytes offset in struct
    nullptr,  // default value
    nullptr,  // size() function pointer
    nullptr,  // get_const(index) function pointer
    nullptr,  // get(index) function pointer
    nullptr,  // fetch(index, &value) function pointer
    nullptr,  // assign(index, value) function pointer
    nullptr  // resize(index) function pointer
  },
  {
    "code",  // name
    ::rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    nullptr,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces::srv::ControlLease_Response, code),  // bytes offset in struct
    nullptr,  // default value
    nullptr,  // size() function pointer
    nullptr,  // get_const(index) function pointer
    nullptr,  // get(index) function pointer
    nullptr,  // fetch(index, &value) function pointer
    nullptr,  // assign(index, value) function pointer
    nullptr  // resize(index) function pointer
  },
  {
    "message",  // name
    ::rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    nullptr,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces::srv::ControlLease_Response, message),  // bytes offset in struct
    nullptr,  // default value
    nullptr,  // size() function pointer
    nullptr,  // get_const(index) function pointer
    nullptr,  // get(index) function pointer
    nullptr,  // fetch(index, &value) function pointer
    nullptr,  // assign(index, value) function pointer
    nullptr  // resize(index) function pointer
  },
  {
    "token",  // name
    ::rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    nullptr,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces::srv::ControlLease_Response, token),  // bytes offset in struct
    nullptr,  // default value
    nullptr,  // size() function pointer
    nullptr,  // get_const(index) function pointer
    nullptr,  // get(index) function pointer
    nullptr,  // fetch(index, &value) function pointer
    nullptr,  // assign(index, value) function pointer
    nullptr  // resize(index) function pointer
  },
  {
    "remaining_s",  // name
    ::rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE,  // type
    0,  // upper bound of string
    nullptr,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces::srv::ControlLease_Response, remaining_s),  // bytes offset in struct
    nullptr,  // default value
    nullptr,  // size() function pointer
    nullptr,  // get_const(index) function pointer
    nullptr,  // get(index) function pointer
    nullptr,  // fetch(index, &value) function pointer
    nullptr,  // assign(index, value) function pointer
    nullptr  // resize(index) function pointer
  }
};

static const ::rosidl_typesupport_introspection_cpp::MessageMembers ControlLease_Response_message_members = {
  "chaos_interfaces::srv",  // message namespace
  "ControlLease_Response",  // message name
  5,  // number of fields
  sizeof(chaos_interfaces::srv::ControlLease_Response),
  false,  // has_any_key_member_
  ControlLease_Response_message_member_array,  // message members
  ControlLease_Response_init_function,  // function to initialize message memory (memory has to be allocated)
  ControlLease_Response_fini_function  // function to terminate message instance (will not free memory)
};

static const rosidl_message_type_support_t ControlLease_Response_message_type_support_handle = {
  ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  &ControlLease_Response_message_members,
  get_message_typesupport_handle_function,
  &chaos_interfaces__srv__ControlLease_Response__get_type_hash,
  &chaos_interfaces__srv__ControlLease_Response__get_type_description,
  &chaos_interfaces__srv__ControlLease_Response__get_type_description_sources,
};

}  // namespace rosidl_typesupport_introspection_cpp

}  // namespace srv

}  // namespace chaos_interfaces


namespace rosidl_typesupport_introspection_cpp
{

template<>
ROSIDL_TYPESUPPORT_INTROSPECTION_CPP_PUBLIC
const rosidl_message_type_support_t *
get_message_type_support_handle<chaos_interfaces::srv::ControlLease_Response>()
{
  return &::chaos_interfaces::srv::rosidl_typesupport_introspection_cpp::ControlLease_Response_message_type_support_handle;
}

}  // namespace rosidl_typesupport_introspection_cpp

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_INTROSPECTION_CPP_PUBLIC
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, chaos_interfaces, srv, ControlLease_Response)() {
  return &::chaos_interfaces::srv::rosidl_typesupport_introspection_cpp::ControlLease_Response_message_type_support_handle;
}

#ifdef __cplusplus
}
#endif

// already included above
// #include "array"
// already included above
// #include "cstddef"
// already included above
// #include "string"
// already included above
// #include "vector"
// already included above
// #include "rosidl_runtime_c/message_type_support_struct.h"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support.hpp"
// already included above
// #include "rosidl_typesupport_interface/macros.h"
// already included above
// #include "chaos_interfaces/srv/detail/control_lease__functions.h"
// already included above
// #include "chaos_interfaces/srv/detail/control_lease__struct.hpp"
// already included above
// #include "rosidl_typesupport_introspection_cpp/field_types.hpp"
// already included above
// #include "rosidl_typesupport_introspection_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"
// already included above
// #include "rosidl_typesupport_introspection_cpp/message_type_support_decl.hpp"
// already included above
// #include "rosidl_typesupport_introspection_cpp/visibility_control.h"

namespace chaos_interfaces
{

namespace srv
{

namespace rosidl_typesupport_introspection_cpp
{

void ControlLease_Event_init_function(
  void * message_memory, rosidl_runtime_cpp::MessageInitialization _init)
{
  new (message_memory) chaos_interfaces::srv::ControlLease_Event(_init);
}

void ControlLease_Event_fini_function(void * message_memory)
{
  auto typed_message = static_cast<chaos_interfaces::srv::ControlLease_Event *>(message_memory);
  typed_message->~ControlLease_Event();
}

size_t size_function__ControlLease_Event__request(const void * untyped_member)
{
  const auto * member = reinterpret_cast<const std::vector<chaos_interfaces::srv::ControlLease_Request> *>(untyped_member);
  return member->size();
}

const void * get_const_function__ControlLease_Event__request(const void * untyped_member, size_t index)
{
  const auto & member =
    *reinterpret_cast<const std::vector<chaos_interfaces::srv::ControlLease_Request> *>(untyped_member);
  return &member[index];
}

void * get_function__ControlLease_Event__request(void * untyped_member, size_t index)
{
  auto & member =
    *reinterpret_cast<std::vector<chaos_interfaces::srv::ControlLease_Request> *>(untyped_member);
  return &member[index];
}

void fetch_function__ControlLease_Event__request(
  const void * untyped_member, size_t index, void * untyped_value)
{
  const auto & item = *reinterpret_cast<const chaos_interfaces::srv::ControlLease_Request *>(
    get_const_function__ControlLease_Event__request(untyped_member, index));
  auto & value = *reinterpret_cast<chaos_interfaces::srv::ControlLease_Request *>(untyped_value);
  value = item;
}

void assign_function__ControlLease_Event__request(
  void * untyped_member, size_t index, const void * untyped_value)
{
  auto & item = *reinterpret_cast<chaos_interfaces::srv::ControlLease_Request *>(
    get_function__ControlLease_Event__request(untyped_member, index));
  const auto & value = *reinterpret_cast<const chaos_interfaces::srv::ControlLease_Request *>(untyped_value);
  item = value;
}

void resize_function__ControlLease_Event__request(void * untyped_member, size_t size)
{
  auto * member =
    reinterpret_cast<std::vector<chaos_interfaces::srv::ControlLease_Request> *>(untyped_member);
  member->resize(size);
}

size_t size_function__ControlLease_Event__response(const void * untyped_member)
{
  const auto * member = reinterpret_cast<const std::vector<chaos_interfaces::srv::ControlLease_Response> *>(untyped_member);
  return member->size();
}

const void * get_const_function__ControlLease_Event__response(const void * untyped_member, size_t index)
{
  const auto & member =
    *reinterpret_cast<const std::vector<chaos_interfaces::srv::ControlLease_Response> *>(untyped_member);
  return &member[index];
}

void * get_function__ControlLease_Event__response(void * untyped_member, size_t index)
{
  auto & member =
    *reinterpret_cast<std::vector<chaos_interfaces::srv::ControlLease_Response> *>(untyped_member);
  return &member[index];
}

void fetch_function__ControlLease_Event__response(
  const void * untyped_member, size_t index, void * untyped_value)
{
  const auto & item = *reinterpret_cast<const chaos_interfaces::srv::ControlLease_Response *>(
    get_const_function__ControlLease_Event__response(untyped_member, index));
  auto & value = *reinterpret_cast<chaos_interfaces::srv::ControlLease_Response *>(untyped_value);
  value = item;
}

void assign_function__ControlLease_Event__response(
  void * untyped_member, size_t index, const void * untyped_value)
{
  auto & item = *reinterpret_cast<chaos_interfaces::srv::ControlLease_Response *>(
    get_function__ControlLease_Event__response(untyped_member, index));
  const auto & value = *reinterpret_cast<const chaos_interfaces::srv::ControlLease_Response *>(untyped_value);
  item = value;
}

void resize_function__ControlLease_Event__response(void * untyped_member, size_t size)
{
  auto * member =
    reinterpret_cast<std::vector<chaos_interfaces::srv::ControlLease_Response> *>(untyped_member);
  member->resize(size);
}

static const ::rosidl_typesupport_introspection_cpp::MessageMember ControlLease_Event_message_member_array[3] = {
  {
    "info",  // name
    ::rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE,  // type
    0,  // upper bound of string
    ::rosidl_typesupport_introspection_cpp::get_message_type_support_handle<service_msgs::msg::ServiceEventInfo>(),  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(chaos_interfaces::srv::ControlLease_Event, info),  // bytes offset in struct
    nullptr,  // default value
    nullptr,  // size() function pointer
    nullptr,  // get_const(index) function pointer
    nullptr,  // get(index) function pointer
    nullptr,  // fetch(index, &value) function pointer
    nullptr,  // assign(index, value) function pointer
    nullptr  // resize(index) function pointer
  },
  {
    "request",  // name
    ::rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE,  // type
    0,  // upper bound of string
    ::rosidl_typesupport_introspection_cpp::get_message_type_support_handle<chaos_interfaces::srv::ControlLease_Request>(),  // members of sub message
    false,  // is key
    true,  // is array
    1,  // array size
    true,  // is upper bound
    offsetof(chaos_interfaces::srv::ControlLease_Event, request),  // bytes offset in struct
    nullptr,  // default value
    size_function__ControlLease_Event__request,  // size() function pointer
    get_const_function__ControlLease_Event__request,  // get_const(index) function pointer
    get_function__ControlLease_Event__request,  // get(index) function pointer
    fetch_function__ControlLease_Event__request,  // fetch(index, &value) function pointer
    assign_function__ControlLease_Event__request,  // assign(index, value) function pointer
    resize_function__ControlLease_Event__request  // resize(index) function pointer
  },
  {
    "response",  // name
    ::rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE,  // type
    0,  // upper bound of string
    ::rosidl_typesupport_introspection_cpp::get_message_type_support_handle<chaos_interfaces::srv::ControlLease_Response>(),  // members of sub message
    false,  // is key
    true,  // is array
    1,  // array size
    true,  // is upper bound
    offsetof(chaos_interfaces::srv::ControlLease_Event, response),  // bytes offset in struct
    nullptr,  // default value
    size_function__ControlLease_Event__response,  // size() function pointer
    get_const_function__ControlLease_Event__response,  // get_const(index) function pointer
    get_function__ControlLease_Event__response,  // get(index) function pointer
    fetch_function__ControlLease_Event__response,  // fetch(index, &value) function pointer
    assign_function__ControlLease_Event__response,  // assign(index, value) function pointer
    resize_function__ControlLease_Event__response  // resize(index) function pointer
  }
};

static const ::rosidl_typesupport_introspection_cpp::MessageMembers ControlLease_Event_message_members = {
  "chaos_interfaces::srv",  // message namespace
  "ControlLease_Event",  // message name
  3,  // number of fields
  sizeof(chaos_interfaces::srv::ControlLease_Event),
  false,  // has_any_key_member_
  ControlLease_Event_message_member_array,  // message members
  ControlLease_Event_init_function,  // function to initialize message memory (memory has to be allocated)
  ControlLease_Event_fini_function  // function to terminate message instance (will not free memory)
};

static const rosidl_message_type_support_t ControlLease_Event_message_type_support_handle = {
  ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  &ControlLease_Event_message_members,
  get_message_typesupport_handle_function,
  &chaos_interfaces__srv__ControlLease_Event__get_type_hash,
  &chaos_interfaces__srv__ControlLease_Event__get_type_description,
  &chaos_interfaces__srv__ControlLease_Event__get_type_description_sources,
};

}  // namespace rosidl_typesupport_introspection_cpp

}  // namespace srv

}  // namespace chaos_interfaces


namespace rosidl_typesupport_introspection_cpp
{

template<>
ROSIDL_TYPESUPPORT_INTROSPECTION_CPP_PUBLIC
const rosidl_message_type_support_t *
get_message_type_support_handle<chaos_interfaces::srv::ControlLease_Event>()
{
  return &::chaos_interfaces::srv::rosidl_typesupport_introspection_cpp::ControlLease_Event_message_type_support_handle;
}

}  // namespace rosidl_typesupport_introspection_cpp

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_INTROSPECTION_CPP_PUBLIC
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, chaos_interfaces, srv, ControlLease_Event)() {
  return &::chaos_interfaces::srv::rosidl_typesupport_introspection_cpp::ControlLease_Event_message_type_support_handle;
}

#ifdef __cplusplus
}
#endif

// already included above
// #include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "rosidl_typesupport_cpp/service_type_support.hpp"
// already included above
// #include "rosidl_typesupport_interface/macros.h"
// already included above
// #include "rosidl_typesupport_introspection_cpp/visibility_control.h"
// already included above
// #include "chaos_interfaces/srv/detail/control_lease__functions.h"
// already included above
// #include "chaos_interfaces/srv/detail/control_lease__struct.hpp"
// already included above
// #include "rosidl_typesupport_introspection_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_introspection_cpp/message_type_support_decl.hpp"
#include "rosidl_typesupport_introspection_cpp/service_introspection.hpp"
#include "rosidl_typesupport_introspection_cpp/service_type_support_decl.hpp"

namespace chaos_interfaces
{

namespace srv
{

namespace rosidl_typesupport_introspection_cpp
{

// this is intentionally not const to allow initialization later to prevent an initialization race
static ::rosidl_typesupport_introspection_cpp::ServiceMembers ControlLease_service_members = {
  "chaos_interfaces::srv",  // service namespace
  "ControlLease",  // service name
  // the following fields are initialized below on first access
  // see get_service_type_support_handle<chaos_interfaces::srv::ControlLease>()
  nullptr,  // request message
  nullptr,  // response message
  nullptr,  // event message
};

static const rosidl_service_type_support_t ControlLease_service_type_support_handle = {
  ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  &ControlLease_service_members,
  get_service_typesupport_handle_function,
  ::rosidl_typesupport_introspection_cpp::get_message_type_support_handle<chaos_interfaces::srv::ControlLease_Request>(),
  ::rosidl_typesupport_introspection_cpp::get_message_type_support_handle<chaos_interfaces::srv::ControlLease_Response>(),
  ::rosidl_typesupport_introspection_cpp::get_message_type_support_handle<chaos_interfaces::srv::ControlLease_Event>(),
  &::rosidl_typesupport_cpp::service_create_event_message<chaos_interfaces::srv::ControlLease>,
  &::rosidl_typesupport_cpp::service_destroy_event_message<chaos_interfaces::srv::ControlLease>,
  &chaos_interfaces__srv__ControlLease__get_type_hash,
  &chaos_interfaces__srv__ControlLease__get_type_description,
  &chaos_interfaces__srv__ControlLease__get_type_description_sources,
};

}  // namespace rosidl_typesupport_introspection_cpp

}  // namespace srv

}  // namespace chaos_interfaces


namespace rosidl_typesupport_introspection_cpp
{

template<>
ROSIDL_TYPESUPPORT_INTROSPECTION_CPP_PUBLIC
const rosidl_service_type_support_t *
get_service_type_support_handle<chaos_interfaces::srv::ControlLease>()
{
  // get a handle to the value to be returned
  auto service_type_support =
    &::chaos_interfaces::srv::rosidl_typesupport_introspection_cpp::ControlLease_service_type_support_handle;
  // get a non-const and properly typed version of the data void *
  auto service_members = const_cast<::rosidl_typesupport_introspection_cpp::ServiceMembers *>(
    static_cast<const ::rosidl_typesupport_introspection_cpp::ServiceMembers *>(
      service_type_support->data));
  // make sure all of the service_members are initialized
  // if they are not, initialize them
  if (
    service_members->request_members_ == nullptr ||
    service_members->response_members_ == nullptr ||
    service_members->event_members_ == nullptr)
  {
    // initialize the request_members_ with the static function from the external library
    service_members->request_members_ = static_cast<
      const ::rosidl_typesupport_introspection_cpp::MessageMembers *
      >(
      ::rosidl_typesupport_introspection_cpp::get_message_type_support_handle<
        ::chaos_interfaces::srv::ControlLease_Request
      >()->data
      );
    // initialize the response_members_ with the static function from the external library
    service_members->response_members_ = static_cast<
      const ::rosidl_typesupport_introspection_cpp::MessageMembers *
      >(
      ::rosidl_typesupport_introspection_cpp::get_message_type_support_handle<
        ::chaos_interfaces::srv::ControlLease_Response
      >()->data
      );
    // initialize the event_members_ with the static function from the external library
    service_members->event_members_ = static_cast<
      const ::rosidl_typesupport_introspection_cpp::MessageMembers *
      >(
      ::rosidl_typesupport_introspection_cpp::get_message_type_support_handle<
        ::chaos_interfaces::srv::ControlLease_Event
      >()->data
      );
  }
  // finally return the properly initialized service_type_support handle
  return service_type_support;
}

}  // namespace rosidl_typesupport_introspection_cpp

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_INTROSPECTION_CPP_PUBLIC
const rosidl_service_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, chaos_interfaces, srv, ControlLease)() {
  return ::rosidl_typesupport_introspection_cpp::get_service_type_support_handle<chaos_interfaces::srv::ControlLease>();
}

#ifdef __cplusplus
}
#endif
