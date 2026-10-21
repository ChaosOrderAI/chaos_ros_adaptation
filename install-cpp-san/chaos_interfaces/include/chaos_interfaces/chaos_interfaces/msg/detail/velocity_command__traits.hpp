// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from chaos_interfaces:msg/VelocityCommand.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/msg/velocity_command.hpp"


#ifndef CHAOS_INTERFACES__MSG__DETAIL__VELOCITY_COMMAND__TRAITS_HPP_
#define CHAOS_INTERFACES__MSG__DETAIL__VELOCITY_COMMAND__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "chaos_interfaces/msg/detail/velocity_command__struct.hpp"
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
  const VelocityCommand & msg,
  std::ostream & out)
{
  out << "{";
  // member: header
  {
    out << "header: ";
    to_flow_style_yaml(msg.header, out);
    out << ", ";
  }

  // member: request_id
  {
    out << "request_id: ";
    rosidl_generator_traits::value_to_yaml(msg.request_id, out);
    out << ", ";
  }

  // member: lease_token
  {
    out << "lease_token: ";
    rosidl_generator_traits::value_to_yaml(msg.lease_token, out);
    out << ", ";
  }

  // member: valid_for_s
  {
    out << "valid_for_s: ";
    rosidl_generator_traits::value_to_yaml(msg.valid_for_s, out);
    out << ", ";
  }

  // member: velocity
  {
    out << "velocity: ";
    to_flow_style_yaml(msg.velocity, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const VelocityCommand & msg,
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

  // member: request_id
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "request_id: ";
    rosidl_generator_traits::value_to_yaml(msg.request_id, out);
    out << "\n";
  }

  // member: lease_token
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "lease_token: ";
    rosidl_generator_traits::value_to_yaml(msg.lease_token, out);
    out << "\n";
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

  // member: velocity
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "velocity:\n";
    to_block_style_yaml(msg.velocity, out, indentation + 2);
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const VelocityCommand & msg, bool use_flow_style = false)
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
  const chaos_interfaces::msg::VelocityCommand & msg,
  std::ostream & out, size_t indentation = 0)
{
  chaos_interfaces::msg::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use chaos_interfaces::msg::to_yaml() instead")]]
inline std::string to_yaml(const chaos_interfaces::msg::VelocityCommand & msg)
{
  return chaos_interfaces::msg::to_yaml(msg);
}

template<>
inline const char * data_type<chaos_interfaces::msg::VelocityCommand>()
{
  return "chaos_interfaces::msg::VelocityCommand";
}

template<>
inline const char * name<chaos_interfaces::msg::VelocityCommand>()
{
  return "chaos_interfaces/msg/VelocityCommand";
}

template<>
struct has_fixed_size<chaos_interfaces::msg::VelocityCommand>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<chaos_interfaces::msg::VelocityCommand>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<chaos_interfaces::msg::VelocityCommand>
  : std::true_type {};

}  // namespace rosidl_generator_traits

#endif  // CHAOS_INTERFACES__MSG__DETAIL__VELOCITY_COMMAND__TRAITS_HPP_
