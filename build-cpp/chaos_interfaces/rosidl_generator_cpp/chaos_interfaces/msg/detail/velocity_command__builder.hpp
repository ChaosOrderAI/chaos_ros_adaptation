// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from chaos_interfaces:msg/VelocityCommand.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/msg/velocity_command.hpp"


#ifndef CHAOS_INTERFACES__MSG__DETAIL__VELOCITY_COMMAND__BUILDER_HPP_
#define CHAOS_INTERFACES__MSG__DETAIL__VELOCITY_COMMAND__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "chaos_interfaces/msg/detail/velocity_command__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace chaos_interfaces
{

namespace msg
{

namespace builder
{

class Init_VelocityCommand_velocity
{
public:
  explicit Init_VelocityCommand_velocity(::chaos_interfaces::msg::VelocityCommand & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::msg::VelocityCommand velocity(::chaos_interfaces::msg::VelocityCommand::_velocity_type arg)
  {
    msg_.velocity = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::msg::VelocityCommand msg_;
};

class Init_VelocityCommand_valid_for_s
{
public:
  explicit Init_VelocityCommand_valid_for_s(::chaos_interfaces::msg::VelocityCommand & msg)
  : msg_(msg)
  {}
  Init_VelocityCommand_velocity valid_for_s(::chaos_interfaces::msg::VelocityCommand::_valid_for_s_type arg)
  {
    msg_.valid_for_s = std::move(arg);
    return Init_VelocityCommand_velocity(msg_);
  }

private:
  ::chaos_interfaces::msg::VelocityCommand msg_;
};

class Init_VelocityCommand_lease_token
{
public:
  explicit Init_VelocityCommand_lease_token(::chaos_interfaces::msg::VelocityCommand & msg)
  : msg_(msg)
  {}
  Init_VelocityCommand_valid_for_s lease_token(::chaos_interfaces::msg::VelocityCommand::_lease_token_type arg)
  {
    msg_.lease_token = std::move(arg);
    return Init_VelocityCommand_valid_for_s(msg_);
  }

private:
  ::chaos_interfaces::msg::VelocityCommand msg_;
};

class Init_VelocityCommand_request_id
{
public:
  explicit Init_VelocityCommand_request_id(::chaos_interfaces::msg::VelocityCommand & msg)
  : msg_(msg)
  {}
  Init_VelocityCommand_lease_token request_id(::chaos_interfaces::msg::VelocityCommand::_request_id_type arg)
  {
    msg_.request_id = std::move(arg);
    return Init_VelocityCommand_lease_token(msg_);
  }

private:
  ::chaos_interfaces::msg::VelocityCommand msg_;
};

class Init_VelocityCommand_header
{
public:
  Init_VelocityCommand_header()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_VelocityCommand_request_id header(::chaos_interfaces::msg::VelocityCommand::_header_type arg)
  {
    msg_.header = std::move(arg);
    return Init_VelocityCommand_request_id(msg_);
  }

private:
  ::chaos_interfaces::msg::VelocityCommand msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::msg::VelocityCommand>()
{
  return chaos_interfaces::msg::builder::Init_VelocityCommand_header();
}

}  // namespace chaos_interfaces

#endif  // CHAOS_INTERFACES__MSG__DETAIL__VELOCITY_COMMAND__BUILDER_HPP_
