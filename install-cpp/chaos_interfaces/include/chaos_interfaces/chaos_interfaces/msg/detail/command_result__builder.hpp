// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from chaos_interfaces:msg/CommandResult.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/msg/command_result.hpp"


#ifndef CHAOS_INTERFACES__MSG__DETAIL__COMMAND_RESULT__BUILDER_HPP_
#define CHAOS_INTERFACES__MSG__DETAIL__COMMAND_RESULT__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "chaos_interfaces/msg/detail/command_result__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace chaos_interfaces
{

namespace msg
{

namespace builder
{

class Init_CommandResult_message
{
public:
  explicit Init_CommandResult_message(::chaos_interfaces::msg::CommandResult & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::msg::CommandResult message(::chaos_interfaces::msg::CommandResult::_message_type arg)
  {
    msg_.message = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::msg::CommandResult msg_;
};

class Init_CommandResult_code
{
public:
  explicit Init_CommandResult_code(::chaos_interfaces::msg::CommandResult & msg)
  : msg_(msg)
  {}
  Init_CommandResult_message code(::chaos_interfaces::msg::CommandResult::_code_type arg)
  {
    msg_.code = std::move(arg);
    return Init_CommandResult_message(msg_);
  }

private:
  ::chaos_interfaces::msg::CommandResult msg_;
};

class Init_CommandResult_success
{
public:
  explicit Init_CommandResult_success(::chaos_interfaces::msg::CommandResult & msg)
  : msg_(msg)
  {}
  Init_CommandResult_code success(::chaos_interfaces::msg::CommandResult::_success_type arg)
  {
    msg_.success = std::move(arg);
    return Init_CommandResult_code(msg_);
  }

private:
  ::chaos_interfaces::msg::CommandResult msg_;
};

class Init_CommandResult_request_id
{
public:
  explicit Init_CommandResult_request_id(::chaos_interfaces::msg::CommandResult & msg)
  : msg_(msg)
  {}
  Init_CommandResult_success request_id(::chaos_interfaces::msg::CommandResult::_request_id_type arg)
  {
    msg_.request_id = std::move(arg);
    return Init_CommandResult_success(msg_);
  }

private:
  ::chaos_interfaces::msg::CommandResult msg_;
};

class Init_CommandResult_stamp
{
public:
  Init_CommandResult_stamp()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_CommandResult_request_id stamp(::chaos_interfaces::msg::CommandResult::_stamp_type arg)
  {
    msg_.stamp = std::move(arg);
    return Init_CommandResult_request_id(msg_);
  }

private:
  ::chaos_interfaces::msg::CommandResult msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::msg::CommandResult>()
{
  return chaos_interfaces::msg::builder::Init_CommandResult_stamp();
}

}  // namespace chaos_interfaces

#endif  // CHAOS_INTERFACES__MSG__DETAIL__COMMAND_RESULT__BUILDER_HPP_
