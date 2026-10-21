// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from chaos_interfaces:msg/ControlTrace.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/msg/control_trace.hpp"


#ifndef CHAOS_INTERFACES__MSG__DETAIL__CONTROL_TRACE__TRAITS_HPP_
#define CHAOS_INTERFACES__MSG__DETAIL__CONTROL_TRACE__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "chaos_interfaces/msg/detail/control_trace__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__traits.hpp"
// Member 'velocity'
#include "geometry_msgs/msg/detail/twist__traits.hpp"

namespace chaos_interfaces
{

namespace msg
{

inline void to_flow_style_yaml(
  const ControlTrace & msg,
  std::ostream & out)
{
  out << "{";
  // member: header
  {
    out << "header: ";
    to_flow_style_yaml(msg.header, out);
    out << ", ";
  }

  // member: asset_id
  {
    out << "asset_id: ";
    rosidl_generator_traits::value_to_yaml(msg.asset_id, out);
    out << ", ";
  }

  // member: sequence
  {
    out << "sequence: ";
    rosidl_generator_traits::value_to_yaml(msg.sequence, out);
    out << ", ";
  }

  // member: monotonic_ns
  {
    out << "monotonic_ns: ";
    rosidl_generator_traits::value_to_yaml(msg.monotonic_ns, out);
    out << ", ";
  }

  // member: request_id
  {
    out << "request_id: ";
    rosidl_generator_traits::value_to_yaml(msg.request_id, out);
    out << ", ";
  }

  // member: command
  {
    out << "command: ";
    rosidl_generator_traits::value_to_yaml(msg.command, out);
    out << ", ";
  }

  // member: stage
  {
    out << "stage: ";
    rosidl_generator_traits::value_to_yaml(msg.stage, out);
    out << ", ";
  }

  // member: velocity_valid
  {
    out << "velocity_valid: ";
    rosidl_generator_traits::value_to_yaml(msg.velocity_valid, out);
    out << ", ";
  }

  // member: velocity
  {
    out << "velocity: ";
    to_flow_style_yaml(msg.velocity, out);
    out << ", ";
  }

  // member: valid_for_s
  {
    out << "valid_for_s: ";
    rosidl_generator_traits::value_to_yaml(msg.valid_for_s, out);
    out << ", ";
  }

  // member: code
  {
    out << "code: ";
    rosidl_generator_traits::value_to_yaml(msg.code, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const ControlTrace & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: header
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "header:\n";
    to_block_style_yaml(msg.header, out, indentation + 2);
  }

  // member: asset_id
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "asset_id: ";
    rosidl_generator_traits::value_to_yaml(msg.asset_id, out);
    out << "\n";
  }

  // member: sequence
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "sequence: ";
    rosidl_generator_traits::value_to_yaml(msg.sequence, out);
    out << "\n";
  }

  // member: monotonic_ns
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "monotonic_ns: ";
    rosidl_generator_traits::value_to_yaml(msg.monotonic_ns, out);
    out << "\n";
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

  // member: command
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "command: ";
    rosidl_generator_traits::value_to_yaml(msg.command, out);
    out << "\n";
  }

  // member: stage
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "stage: ";
    rosidl_generator_traits::value_to_yaml(msg.stage, out);
    out << "\n";
  }

  // member: velocity_valid
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "velocity_valid: ";
    rosidl_generator_traits::value_to_yaml(msg.velocity_valid, out);
    out << "\n";
  }

  // member: velocity
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "velocity:\n";
    to_block_style_yaml(msg.velocity, out, indentation + 2);
  }

  // member: valid_for_s
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "valid_for_s: ";
    rosidl_generator_traits::value_to_yaml(msg.valid_for_s, out);
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
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const ControlTrace & msg, bool use_flow_style = false)
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
  const chaos_interfaces::msg::ControlTrace & msg,
  std::ostream & out, size_t indentation = 0)
{
  chaos_interfaces::msg::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use chaos_interfaces::msg::to_yaml() instead")]]
inline std::string to_yaml(const chaos_interfaces::msg::ControlTrace & msg)
{
  return chaos_interfaces::msg::to_yaml(msg);
}

template<>
inline const char * data_type<chaos_interfaces::msg::ControlTrace>()
{
  return "chaos_interfaces::msg::ControlTrace";
}

template<>
inline const char * name<chaos_interfaces::msg::ControlTrace>()
{
  return "chaos_interfaces/msg/ControlTrace";
}

template<>
struct has_fixed_size<chaos_interfaces::msg::ControlTrace>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<chaos_interfaces::msg::ControlTrace>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<chaos_interfaces::msg::ControlTrace>
  : std::true_type {};

}  // namespace rosidl_generator_traits

#endif  // CHAOS_INTERFACES__MSG__DETAIL__CONTROL_TRACE__TRAITS_HPP_
