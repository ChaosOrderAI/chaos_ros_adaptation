// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from chaos_interfaces:srv/GetDescription.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/srv/get_description.hpp"


#ifndef CHAOS_INTERFACES__SRV__DETAIL__GET_DESCRIPTION__BUILDER_HPP_
#define CHAOS_INTERFACES__SRV__DETAIL__GET_DESCRIPTION__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "chaos_interfaces/srv/detail/get_description__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace chaos_interfaces
{

namespace srv
{


}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::srv::GetDescription_Request>()
{
  return ::chaos_interfaces::srv::GetDescription_Request(rosidl_runtime_cpp::MessageInitialization::ZERO);
}

}  // namespace chaos_interfaces


namespace chaos_interfaces
{

namespace srv
{

namespace builder
{

class Init_GetDescription_Response_lease_max_s
{
public:
  explicit Init_GetDescription_Response_lease_max_s(::chaos_interfaces::srv::GetDescription_Response & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::srv::GetDescription_Response lease_max_s(::chaos_interfaces::srv::GetDescription_Response::_lease_max_s_type arg)
  {
    msg_.lease_max_s = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::srv::GetDescription_Response msg_;
};

class Init_GetDescription_Response_command_timeout_s
{
public:
  explicit Init_GetDescription_Response_command_timeout_s(::chaos_interfaces::srv::GetDescription_Response & msg)
  : msg_(msg)
  {}
  Init_GetDescription_Response_lease_max_s command_timeout_s(::chaos_interfaces::srv::GetDescription_Response::_command_timeout_s_type arg)
  {
    msg_.command_timeout_s = std::move(arg);
    return Init_GetDescription_Response_lease_max_s(msg_);
  }

private:
  ::chaos_interfaces::srv::GetDescription_Response msg_;
};

class Init_GetDescription_Response_angular_limit_rps
{
public:
  explicit Init_GetDescription_Response_angular_limit_rps(::chaos_interfaces::srv::GetDescription_Response & msg)
  : msg_(msg)
  {}
  Init_GetDescription_Response_command_timeout_s angular_limit_rps(::chaos_interfaces::srv::GetDescription_Response::_angular_limit_rps_type arg)
  {
    msg_.angular_limit_rps = std::move(arg);
    return Init_GetDescription_Response_command_timeout_s(msg_);
  }

private:
  ::chaos_interfaces::srv::GetDescription_Response msg_;
};

class Init_GetDescription_Response_linear_limit_mps
{
public:
  explicit Init_GetDescription_Response_linear_limit_mps(::chaos_interfaces::srv::GetDescription_Response & msg)
  : msg_(msg)
  {}
  Init_GetDescription_Response_angular_limit_rps linear_limit_mps(::chaos_interfaces::srv::GetDescription_Response::_linear_limit_mps_type arg)
  {
    msg_.linear_limit_mps = std::move(arg);
    return Init_GetDescription_Response_angular_limit_rps(msg_);
  }

private:
  ::chaos_interfaces::srv::GetDescription_Response msg_;
};

class Init_GetDescription_Response_map_frame
{
public:
  explicit Init_GetDescription_Response_map_frame(::chaos_interfaces::srv::GetDescription_Response & msg)
  : msg_(msg)
  {}
  Init_GetDescription_Response_linear_limit_mps map_frame(::chaos_interfaces::srv::GetDescription_Response::_map_frame_type arg)
  {
    msg_.map_frame = std::move(arg);
    return Init_GetDescription_Response_linear_limit_mps(msg_);
  }

private:
  ::chaos_interfaces::srv::GetDescription_Response msg_;
};

class Init_GetDescription_Response_odom_frame
{
public:
  explicit Init_GetDescription_Response_odom_frame(::chaos_interfaces::srv::GetDescription_Response & msg)
  : msg_(msg)
  {}
  Init_GetDescription_Response_map_frame odom_frame(::chaos_interfaces::srv::GetDescription_Response::_odom_frame_type arg)
  {
    msg_.odom_frame = std::move(arg);
    return Init_GetDescription_Response_map_frame(msg_);
  }

private:
  ::chaos_interfaces::srv::GetDescription_Response msg_;
};

class Init_GetDescription_Response_base_frame
{
public:
  explicit Init_GetDescription_Response_base_frame(::chaos_interfaces::srv::GetDescription_Response & msg)
  : msg_(msg)
  {}
  Init_GetDescription_Response_odom_frame base_frame(::chaos_interfaces::srv::GetDescription_Response::_base_frame_type arg)
  {
    msg_.base_frame = std::move(arg);
    return Init_GetDescription_Response_odom_frame(msg_);
  }

private:
  ::chaos_interfaces::srv::GetDescription_Response msg_;
};

class Init_GetDescription_Response_capabilities
{
public:
  explicit Init_GetDescription_Response_capabilities(::chaos_interfaces::srv::GetDescription_Response & msg)
  : msg_(msg)
  {}
  Init_GetDescription_Response_base_frame capabilities(::chaos_interfaces::srv::GetDescription_Response::_capabilities_type arg)
  {
    msg_.capabilities = std::move(arg);
    return Init_GetDescription_Response_base_frame(msg_);
  }

private:
  ::chaos_interfaces::srv::GetDescription_Response msg_;
};

class Init_GetDescription_Response_model_version
{
public:
  explicit Init_GetDescription_Response_model_version(::chaos_interfaces::srv::GetDescription_Response & msg)
  : msg_(msg)
  {}
  Init_GetDescription_Response_capabilities model_version(::chaos_interfaces::srv::GetDescription_Response::_model_version_type arg)
  {
    msg_.model_version = std::move(arg);
    return Init_GetDescription_Response_capabilities(msg_);
  }

private:
  ::chaos_interfaces::srv::GetDescription_Response msg_;
};

class Init_GetDescription_Response_model
{
public:
  explicit Init_GetDescription_Response_model(::chaos_interfaces::srv::GetDescription_Response & msg)
  : msg_(msg)
  {}
  Init_GetDescription_Response_model_version model(::chaos_interfaces::srv::GetDescription_Response::_model_type arg)
  {
    msg_.model = std::move(arg);
    return Init_GetDescription_Response_model_version(msg_);
  }

private:
  ::chaos_interfaces::srv::GetDescription_Response msg_;
};

class Init_GetDescription_Response_asset_id
{
public:
  Init_GetDescription_Response_asset_id()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_GetDescription_Response_model asset_id(::chaos_interfaces::srv::GetDescription_Response::_asset_id_type arg)
  {
    msg_.asset_id = std::move(arg);
    return Init_GetDescription_Response_model(msg_);
  }

private:
  ::chaos_interfaces::srv::GetDescription_Response msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::srv::GetDescription_Response>()
{
  return chaos_interfaces::srv::builder::Init_GetDescription_Response_asset_id();
}

}  // namespace chaos_interfaces


namespace chaos_interfaces
{

namespace srv
{

namespace builder
{

class Init_GetDescription_Event_response
{
public:
  explicit Init_GetDescription_Event_response(::chaos_interfaces::srv::GetDescription_Event & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::srv::GetDescription_Event response(::chaos_interfaces::srv::GetDescription_Event::_response_type arg)
  {
    msg_.response = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::srv::GetDescription_Event msg_;
};

class Init_GetDescription_Event_request
{
public:
  explicit Init_GetDescription_Event_request(::chaos_interfaces::srv::GetDescription_Event & msg)
  : msg_(msg)
  {}
  Init_GetDescription_Event_response request(::chaos_interfaces::srv::GetDescription_Event::_request_type arg)
  {
    msg_.request = std::move(arg);
    return Init_GetDescription_Event_response(msg_);
  }

private:
  ::chaos_interfaces::srv::GetDescription_Event msg_;
};

class Init_GetDescription_Event_info
{
public:
  Init_GetDescription_Event_info()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_GetDescription_Event_request info(::chaos_interfaces::srv::GetDescription_Event::_info_type arg)
  {
    msg_.info = std::move(arg);
    return Init_GetDescription_Event_request(msg_);
  }

private:
  ::chaos_interfaces::srv::GetDescription_Event msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::srv::GetDescription_Event>()
{
  return chaos_interfaces::srv::builder::Init_GetDescription_Event_info();
}

}  // namespace chaos_interfaces

#endif  // CHAOS_INTERFACES__SRV__DETAIL__GET_DESCRIPTION__BUILDER_HPP_
