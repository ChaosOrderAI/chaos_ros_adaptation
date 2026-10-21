// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from chaos_interfaces:msg/ControlTrace.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/msg/control_trace.hpp"


#ifndef CHAOS_INTERFACES__MSG__DETAIL__CONTROL_TRACE__BUILDER_HPP_
#define CHAOS_INTERFACES__MSG__DETAIL__CONTROL_TRACE__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "chaos_interfaces/msg/detail/control_trace__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace chaos_interfaces
{

namespace msg
{

namespace builder
{

class Init_ControlTrace_code
{
public:
  explicit Init_ControlTrace_code(::chaos_interfaces::msg::ControlTrace & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::msg::ControlTrace code(::chaos_interfaces::msg::ControlTrace::_code_type arg)
  {
    msg_.code = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::msg::ControlTrace msg_;
};

class Init_ControlTrace_valid_for_s
{
public:
  explicit Init_ControlTrace_valid_for_s(::chaos_interfaces::msg::ControlTrace & msg)
  : msg_(msg)
  {}
  Init_ControlTrace_code valid_for_s(::chaos_interfaces::msg::ControlTrace::_valid_for_s_type arg)
  {
    msg_.valid_for_s = std::move(arg);
    return Init_ControlTrace_code(msg_);
  }

private:
  ::chaos_interfaces::msg::ControlTrace msg_;
};

class Init_ControlTrace_velocity
{
public:
  explicit Init_ControlTrace_velocity(::chaos_interfaces::msg::ControlTrace & msg)
  : msg_(msg)
  {}
  Init_ControlTrace_valid_for_s velocity(::chaos_interfaces::msg::ControlTrace::_velocity_type arg)
  {
    msg_.velocity = std::move(arg);
    return Init_ControlTrace_valid_for_s(msg_);
  }

private:
  ::chaos_interfaces::msg::ControlTrace msg_;
};

class Init_ControlTrace_velocity_valid
{
public:
  explicit Init_ControlTrace_velocity_valid(::chaos_interfaces::msg::ControlTrace & msg)
  : msg_(msg)
  {}
  Init_ControlTrace_velocity velocity_valid(::chaos_interfaces::msg::ControlTrace::_velocity_valid_type arg)
  {
    msg_.velocity_valid = std::move(arg);
    return Init_ControlTrace_velocity(msg_);
  }

private:
  ::chaos_interfaces::msg::ControlTrace msg_;
};

class Init_ControlTrace_stage
{
public:
  explicit Init_ControlTrace_stage(::chaos_interfaces::msg::ControlTrace & msg)
  : msg_(msg)
  {}
  Init_ControlTrace_velocity_valid stage(::chaos_interfaces::msg::ControlTrace::_stage_type arg)
  {
    msg_.stage = std::move(arg);
    return Init_ControlTrace_velocity_valid(msg_);
  }

private:
  ::chaos_interfaces::msg::ControlTrace msg_;
};

class Init_ControlTrace_command
{
public:
  explicit Init_ControlTrace_command(::chaos_interfaces::msg::ControlTrace & msg)
  : msg_(msg)
  {}
  Init_ControlTrace_stage command(::chaos_interfaces::msg::ControlTrace::_command_type arg)
  {
    msg_.command = std::move(arg);
    return Init_ControlTrace_stage(msg_);
  }

private:
  ::chaos_interfaces::msg::ControlTrace msg_;
};

class Init_ControlTrace_request_id
{
public:
  explicit Init_ControlTrace_request_id(::chaos_interfaces::msg::ControlTrace & msg)
  : msg_(msg)
  {}
  Init_ControlTrace_command request_id(::chaos_interfaces::msg::ControlTrace::_request_id_type arg)
  {
    msg_.request_id = std::move(arg);
    return Init_ControlTrace_command(msg_);
  }

private:
  ::chaos_interfaces::msg::ControlTrace msg_;
};

class Init_ControlTrace_monotonic_ns
{
public:
  explicit Init_ControlTrace_monotonic_ns(::chaos_interfaces::msg::ControlTrace & msg)
  : msg_(msg)
  {}
  Init_ControlTrace_request_id monotonic_ns(::chaos_interfaces::msg::ControlTrace::_monotonic_ns_type arg)
  {
    msg_.monotonic_ns = std::move(arg);
    return Init_ControlTrace_request_id(msg_);
  }

private:
  ::chaos_interfaces::msg::ControlTrace msg_;
};

class Init_ControlTrace_sequence
{
public:
  explicit Init_ControlTrace_sequence(::chaos_interfaces::msg::ControlTrace & msg)
  : msg_(msg)
  {}
  Init_ControlTrace_monotonic_ns sequence(::chaos_interfaces::msg::ControlTrace::_sequence_type arg)
  {
    msg_.sequence = std::move(arg);
    return Init_ControlTrace_monotonic_ns(msg_);
  }

private:
  ::chaos_interfaces::msg::ControlTrace msg_;
};

class Init_ControlTrace_asset_id
{
public:
  explicit Init_ControlTrace_asset_id(::chaos_interfaces::msg::ControlTrace & msg)
  : msg_(msg)
  {}
  Init_ControlTrace_sequence asset_id(::chaos_interfaces::msg::ControlTrace::_asset_id_type arg)
  {
    msg_.asset_id = std::move(arg);
    return Init_ControlTrace_sequence(msg_);
  }

private:
  ::chaos_interfaces::msg::ControlTrace msg_;
};

class Init_ControlTrace_header
{
public:
  Init_ControlTrace_header()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_ControlTrace_asset_id header(::chaos_interfaces::msg::ControlTrace::_header_type arg)
  {
    msg_.header = std::move(arg);
    return Init_ControlTrace_asset_id(msg_);
  }

private:
  ::chaos_interfaces::msg::ControlTrace msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::msg::ControlTrace>()
{
  return chaos_interfaces::msg::builder::Init_ControlTrace_header();
}

}  // namespace chaos_interfaces

#endif  // CHAOS_INTERFACES__MSG__DETAIL__CONTROL_TRACE__BUILDER_HPP_
