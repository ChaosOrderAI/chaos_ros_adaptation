// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from chaos_interfaces:srv/Stop.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/srv/stop.hpp"


#ifndef CHAOS_INTERFACES__SRV__DETAIL__STOP__BUILDER_HPP_
#define CHAOS_INTERFACES__SRV__DETAIL__STOP__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "chaos_interfaces/srv/detail/stop__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace chaos_interfaces
{

namespace srv
{

namespace builder
{

class Init_Stop_Request_expires_at
{
public:
  explicit Init_Stop_Request_expires_at(::chaos_interfaces::srv::Stop_Request & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::srv::Stop_Request expires_at(::chaos_interfaces::srv::Stop_Request::_expires_at_type arg)
  {
    msg_.expires_at = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::srv::Stop_Request msg_;
};

class Init_Stop_Request_lease_token
{
public:
  explicit Init_Stop_Request_lease_token(::chaos_interfaces::srv::Stop_Request & msg)
  : msg_(msg)
  {}
  Init_Stop_Request_expires_at lease_token(::chaos_interfaces::srv::Stop_Request::_lease_token_type arg)
  {
    msg_.lease_token = std::move(arg);
    return Init_Stop_Request_expires_at(msg_);
  }

private:
  ::chaos_interfaces::srv::Stop_Request msg_;
};

class Init_Stop_Request_request_id
{
public:
  Init_Stop_Request_request_id()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_Stop_Request_lease_token request_id(::chaos_interfaces::srv::Stop_Request::_request_id_type arg)
  {
    msg_.request_id = std::move(arg);
    return Init_Stop_Request_lease_token(msg_);
  }

private:
  ::chaos_interfaces::srv::Stop_Request msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::srv::Stop_Request>()
{
  return chaos_interfaces::srv::builder::Init_Stop_Request_request_id();
}

}  // namespace chaos_interfaces


namespace chaos_interfaces
{

namespace srv
{

namespace builder
{

class Init_Stop_Response_message
{
public:
  explicit Init_Stop_Response_message(::chaos_interfaces::srv::Stop_Response & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::srv::Stop_Response message(::chaos_interfaces::srv::Stop_Response::_message_type arg)
  {
    msg_.message = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::srv::Stop_Response msg_;
};

class Init_Stop_Response_code
{
public:
  explicit Init_Stop_Response_code(::chaos_interfaces::srv::Stop_Response & msg)
  : msg_(msg)
  {}
  Init_Stop_Response_message code(::chaos_interfaces::srv::Stop_Response::_code_type arg)
  {
    msg_.code = std::move(arg);
    return Init_Stop_Response_message(msg_);
  }

private:
  ::chaos_interfaces::srv::Stop_Response msg_;
};

class Init_Stop_Response_success
{
public:
  Init_Stop_Response_success()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_Stop_Response_code success(::chaos_interfaces::srv::Stop_Response::_success_type arg)
  {
    msg_.success = std::move(arg);
    return Init_Stop_Response_code(msg_);
  }

private:
  ::chaos_interfaces::srv::Stop_Response msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::srv::Stop_Response>()
{
  return chaos_interfaces::srv::builder::Init_Stop_Response_success();
}

}  // namespace chaos_interfaces


namespace chaos_interfaces
{

namespace srv
{

namespace builder
{

class Init_Stop_Event_response
{
public:
  explicit Init_Stop_Event_response(::chaos_interfaces::srv::Stop_Event & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::srv::Stop_Event response(::chaos_interfaces::srv::Stop_Event::_response_type arg)
  {
    msg_.response = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::srv::Stop_Event msg_;
};

class Init_Stop_Event_request
{
public:
  explicit Init_Stop_Event_request(::chaos_interfaces::srv::Stop_Event & msg)
  : msg_(msg)
  {}
  Init_Stop_Event_response request(::chaos_interfaces::srv::Stop_Event::_request_type arg)
  {
    msg_.request = std::move(arg);
    return Init_Stop_Event_response(msg_);
  }

private:
  ::chaos_interfaces::srv::Stop_Event msg_;
};

class Init_Stop_Event_info
{
public:
  Init_Stop_Event_info()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_Stop_Event_request info(::chaos_interfaces::srv::Stop_Event::_info_type arg)
  {
    msg_.info = std::move(arg);
    return Init_Stop_Event_request(msg_);
  }

private:
  ::chaos_interfaces::srv::Stop_Event msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::srv::Stop_Event>()
{
  return chaos_interfaces::srv::builder::Init_Stop_Event_info();
}

}  // namespace chaos_interfaces

#endif  // CHAOS_INTERFACES__SRV__DETAIL__STOP__BUILDER_HPP_
