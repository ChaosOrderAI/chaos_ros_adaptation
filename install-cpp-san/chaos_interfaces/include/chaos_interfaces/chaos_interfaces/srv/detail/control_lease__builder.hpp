// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from chaos_interfaces:srv/ControlLease.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/srv/control_lease.hpp"


#ifndef CHAOS_INTERFACES__SRV__DETAIL__CONTROL_LEASE__BUILDER_HPP_
#define CHAOS_INTERFACES__SRV__DETAIL__CONTROL_LEASE__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "chaos_interfaces/srv/detail/control_lease__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace chaos_interfaces
{

namespace srv
{

namespace builder
{

class Init_ControlLease_Request_expires_at
{
public:
  explicit Init_ControlLease_Request_expires_at(::chaos_interfaces::srv::ControlLease_Request & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::srv::ControlLease_Request expires_at(::chaos_interfaces::srv::ControlLease_Request::_expires_at_type arg)
  {
    msg_.expires_at = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::srv::ControlLease_Request msg_;
};

class Init_ControlLease_Request_duration_s
{
public:
  explicit Init_ControlLease_Request_duration_s(::chaos_interfaces::srv::ControlLease_Request & msg)
  : msg_(msg)
  {}
  Init_ControlLease_Request_expires_at duration_s(::chaos_interfaces::srv::ControlLease_Request::_duration_s_type arg)
  {
    msg_.duration_s = std::move(arg);
    return Init_ControlLease_Request_expires_at(msg_);
  }

private:
  ::chaos_interfaces::srv::ControlLease_Request msg_;
};

class Init_ControlLease_Request_token
{
public:
  explicit Init_ControlLease_Request_token(::chaos_interfaces::srv::ControlLease_Request & msg)
  : msg_(msg)
  {}
  Init_ControlLease_Request_duration_s token(::chaos_interfaces::srv::ControlLease_Request::_token_type arg)
  {
    msg_.token = std::move(arg);
    return Init_ControlLease_Request_duration_s(msg_);
  }

private:
  ::chaos_interfaces::srv::ControlLease_Request msg_;
};

class Init_ControlLease_Request_owner
{
public:
  explicit Init_ControlLease_Request_owner(::chaos_interfaces::srv::ControlLease_Request & msg)
  : msg_(msg)
  {}
  Init_ControlLease_Request_token owner(::chaos_interfaces::srv::ControlLease_Request::_owner_type arg)
  {
    msg_.owner = std::move(arg);
    return Init_ControlLease_Request_token(msg_);
  }

private:
  ::chaos_interfaces::srv::ControlLease_Request msg_;
};

class Init_ControlLease_Request_operation
{
public:
  Init_ControlLease_Request_operation()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_ControlLease_Request_owner operation(::chaos_interfaces::srv::ControlLease_Request::_operation_type arg)
  {
    msg_.operation = std::move(arg);
    return Init_ControlLease_Request_owner(msg_);
  }

private:
  ::chaos_interfaces::srv::ControlLease_Request msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::srv::ControlLease_Request>()
{
  return chaos_interfaces::srv::builder::Init_ControlLease_Request_operation();
}

}  // namespace chaos_interfaces


namespace chaos_interfaces
{

namespace srv
{

namespace builder
{

class Init_ControlLease_Response_remaining_s
{
public:
  explicit Init_ControlLease_Response_remaining_s(::chaos_interfaces::srv::ControlLease_Response & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::srv::ControlLease_Response remaining_s(::chaos_interfaces::srv::ControlLease_Response::_remaining_s_type arg)
  {
    msg_.remaining_s = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::srv::ControlLease_Response msg_;
};

class Init_ControlLease_Response_token
{
public:
  explicit Init_ControlLease_Response_token(::chaos_interfaces::srv::ControlLease_Response & msg)
  : msg_(msg)
  {}
  Init_ControlLease_Response_remaining_s token(::chaos_interfaces::srv::ControlLease_Response::_token_type arg)
  {
    msg_.token = std::move(arg);
    return Init_ControlLease_Response_remaining_s(msg_);
  }

private:
  ::chaos_interfaces::srv::ControlLease_Response msg_;
};

class Init_ControlLease_Response_message
{
public:
  explicit Init_ControlLease_Response_message(::chaos_interfaces::srv::ControlLease_Response & msg)
  : msg_(msg)
  {}
  Init_ControlLease_Response_token message(::chaos_interfaces::srv::ControlLease_Response::_message_type arg)
  {
    msg_.message = std::move(arg);
    return Init_ControlLease_Response_token(msg_);
  }

private:
  ::chaos_interfaces::srv::ControlLease_Response msg_;
};

class Init_ControlLease_Response_code
{
public:
  explicit Init_ControlLease_Response_code(::chaos_interfaces::srv::ControlLease_Response & msg)
  : msg_(msg)
  {}
  Init_ControlLease_Response_message code(::chaos_interfaces::srv::ControlLease_Response::_code_type arg)
  {
    msg_.code = std::move(arg);
    return Init_ControlLease_Response_message(msg_);
  }

private:
  ::chaos_interfaces::srv::ControlLease_Response msg_;
};

class Init_ControlLease_Response_success
{
public:
  Init_ControlLease_Response_success()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_ControlLease_Response_code success(::chaos_interfaces::srv::ControlLease_Response::_success_type arg)
  {
    msg_.success = std::move(arg);
    return Init_ControlLease_Response_code(msg_);
  }

private:
  ::chaos_interfaces::srv::ControlLease_Response msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::srv::ControlLease_Response>()
{
  return chaos_interfaces::srv::builder::Init_ControlLease_Response_success();
}

}  // namespace chaos_interfaces


namespace chaos_interfaces
{

namespace srv
{

namespace builder
{

class Init_ControlLease_Event_response
{
public:
  explicit Init_ControlLease_Event_response(::chaos_interfaces::srv::ControlLease_Event & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::srv::ControlLease_Event response(::chaos_interfaces::srv::ControlLease_Event::_response_type arg)
  {
    msg_.response = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::srv::ControlLease_Event msg_;
};

class Init_ControlLease_Event_request
{
public:
  explicit Init_ControlLease_Event_request(::chaos_interfaces::srv::ControlLease_Event & msg)
  : msg_(msg)
  {}
  Init_ControlLease_Event_response request(::chaos_interfaces::srv::ControlLease_Event::_request_type arg)
  {
    msg_.request = std::move(arg);
    return Init_ControlLease_Event_response(msg_);
  }

private:
  ::chaos_interfaces::srv::ControlLease_Event msg_;
};

class Init_ControlLease_Event_info
{
public:
  Init_ControlLease_Event_info()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_ControlLease_Event_request info(::chaos_interfaces::srv::ControlLease_Event::_info_type arg)
  {
    msg_.info = std::move(arg);
    return Init_ControlLease_Event_request(msg_);
  }

private:
  ::chaos_interfaces::srv::ControlLease_Event msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::srv::ControlLease_Event>()
{
  return chaos_interfaces::srv::builder::Init_ControlLease_Event_info();
}

}  // namespace chaos_interfaces

#endif  // CHAOS_INTERFACES__SRV__DETAIL__CONTROL_LEASE__BUILDER_HPP_
