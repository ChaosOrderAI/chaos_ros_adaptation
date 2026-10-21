// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from chaos_interfaces:msg/CommandResult.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/msg/command_result.hpp"


#ifndef CHAOS_INTERFACES__MSG__DETAIL__COMMAND_RESULT__TRAITS_HPP_
#define CHAOS_INTERFACES__MSG__DETAIL__COMMAND_RESULT__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "chaos_interfaces/msg/detail/command_result__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

// Include directives for member types
// Member 'stamp'
#include "builtin_interfaces/msg/detail/time__traits.hpp"

namespace chaos_interfaces
{

namespace msg
{

inline void to_flow_style_yaml(
  const CommandResult & msg,
  std::ostream & out)
{
  out << "{";
  // member: stamp
  {
    out << "stamp: ";
    to_flow_style_yaml(msg.stamp, out);
    out << ", ";
  }

  // member: request_id
  {
    out << "request_id: ";
    rosidl_generator_traits::value_to_yaml(msg.request_id, out);
    out << ", ";
  }

  // member: success
  {
    out << "success: ";
    rosidl_generator_traits::value_to_yaml(msg.success, out);
    out << ", ";
  }

  // member: code
  {
    out << "code: ";
    rosidl_generator_traits::value_to_yaml(msg.code, out);
    out << ", ";
  }

  // member: message
  {
    out << "message: ";
    rosidl_generator_traits::value_to_yaml(msg.message, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const CommandResult & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: stamp
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "stamp:\n";
    to_block_style_yaml(msg.stamp, out, indentation + 2);
  }

  // member: request_id
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "request_id: ";
    rosidl_generator_traits::value_to_yaml(msg.request_id, out);
    out << "\n";
  }

  // member: success
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "success: ";
    rosidl_generator_traits::value_to_yaml(msg.success, out);
    out << "\n";
  }

  // member: code
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "code: ";
    rosidl_generator_traits::value_to_yaml(msg.code, out);
    out << "\n";
  }

  // member: message
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "message: ";
    rosidl_generator_traits::value_to_yaml(msg.message, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const CommandResult & msg, bool use_flow_style = false)
{
  std::ostringstream out;
  if (use_flow_style) {
    to_flow_style_yaml(msg, out);
  } else {
    to_block_style_yaml(msg, out);
  }
  return out.str();
}

}  // namespace msg

}  // namespace chaos_interfaces

namespace rosidl_generator_traits
{

[[deprecated("use chaos_interfaces::msg::to_block_style_yaml() instead")]]
inline void to_yaml(
  const chaos_interfaces::msg::CommandResult & msg,
  std::ostream & out, size_t indentation = 0)
{
  chaos_interfaces::msg::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use chaos_interfaces::msg::to_yaml() instead")]]
inline std::string to_yaml(const chaos_interfaces::msg::CommandResult & msg)
{
  return chaos_interfaces::msg::to_yaml(msg);
}

template<>
inline const char * data_type<chaos_interfaces::msg::CommandResult>()
{
  return "chaos_interfaces::msg::CommandResult";
}

template<>
inline const char * name<chaos_interfaces::msg::CommandResult>()
{
  return "chaos_interfaces/msg/CommandResult";
}

template<>
struct has_fixed_size<chaos_interfaces::msg::CommandResult>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<chaos_interfaces::msg::CommandResult>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<chaos_interfaces::msg::CommandResult>
  : std::true_type {};

}  // namespace rosidl_generator_traits

#endif  // CHAOS_INTERFACES__MSG__DETAIL__COMMAND_RESULT__TRAITS_HPP_
