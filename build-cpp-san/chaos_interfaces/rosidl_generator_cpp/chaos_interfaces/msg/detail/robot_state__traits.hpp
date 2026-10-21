// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from chaos_interfaces:msg/RobotState.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/msg/robot_state.hpp"


#ifndef CHAOS_INTERFACES__MSG__DETAIL__ROBOT_STATE__TRAITS_HPP_
#define CHAOS_INTERFACES__MSG__DETAIL__ROBOT_STATE__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "chaos_interfaces/msg/detail/robot_state__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

// Include directives for member types
// Member 'stamp'
#include "builtin_interfaces/msg/detail/time__traits.hpp"
// Member 'pose'
#include "geometry_msgs/msg/detail/pose_stamped__traits.hpp"
// Member 'twist'
#include "geometry_msgs/msg/detail/twist_stamped__traits.hpp"

namespace chaos_interfaces
{

namespace msg
{

inline void to_flow_style_yaml(
  const RobotState & msg,
  std::ostream & out)
{
  out << "{";
  // member: stamp
  {
    out << "stamp: ";
    to_flow_style_yaml(msg.stamp, out);
    out << ", ";
  }

  // member: asset_id
  {
    out << "asset_id: ";
    rosidl_generator_traits::value_to_yaml(msg.asset_id, out);
    out << ", ";
  }

  // member: model
  {
    out << "model: ";
    rosidl_generator_traits::value_to_yaml(msg.model, out);
    out << ", ";
  }

  // member: operational_state
  {
    out << "operational_state: ";
    rosidl_generator_traits::value_to_yaml(msg.operational_state, out);
    out << ", ";
  }

  // member: connected
  {
    out << "connected: ";
    rosidl_generator_traits::value_to_yaml(msg.connected, out);
    out << ", ";
  }

  // member: ready
  {
    out << "ready: ";
    rosidl_generator_traits::value_to_yaml(msg.ready, out);
    out << ", ";
  }

  // member: pose_valid
  {
    out << "pose_valid: ";
    rosidl_generator_traits::value_to_yaml(msg.pose_valid, out);
    out << ", ";
  }

  // member: pose
  {
    out << "pose: ";
    to_flow_style_yaml(msg.pose, out);
    out << ", ";
  }

  // member: twist
  {
    out << "twist: ";
    to_flow_style_yaml(msg.twist, out);
    out << ", ";
  }

  // member: battery_percentage
  {
    out << "battery_percentage: ";
    rosidl_generator_traits::value_to_yaml(msg.battery_percentage, out);
    out << ", ";
  }

  // member: detail
  {
    out << "detail: ";
    rosidl_generator_traits::value_to_yaml(msg.detail, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const RobotState & msg,
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

  // member: asset_id
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "asset_id: ";
    rosidl_generator_traits::value_to_yaml(msg.asset_id, out);
    out << "\n";
  }

  // member: model
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "model: ";
    rosidl_generator_traits::value_to_yaml(msg.model, out);
    out << "\n";
  }

  // member: operational_state
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "operational_state: ";
    rosidl_generator_traits::value_to_yaml(msg.operational_state, out);
    out << "\n";
  }

  // member: connected
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "connected: ";
    rosidl_generator_traits::value_to_yaml(msg.connected, out);
    out << "\n";
  }

  // member: ready
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "ready: ";
    rosidl_generator_traits::value_to_yaml(msg.ready, out);
    out << "\n";
  }

  // member: pose_valid
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "pose_valid: ";
    rosidl_generator_traits::value_to_yaml(msg.pose_valid, out);
    out << "\n";
  }

  // member: pose
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "pose:\n";
    to_block_style_yaml(msg.pose, out, indentation + 2);
  }

  // member: twist
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "twist:\n";
    to_block_style_yaml(msg.twist, out, indentation + 2);
  }

  // member: battery_percentage
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "battery_percentage: ";
    rosidl_generator_traits::value_to_yaml(msg.battery_percentage, out);
    out << "\n";
  }

  // member: detail
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "detail: ";
    rosidl_generator_traits::value_to_yaml(msg.detail, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const RobotState & msg, bool use_flow_style = false)
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
  const chaos_interfaces::msg::RobotState & msg,
  std::ostream & out, size_t indentation = 0)
{
  chaos_interfaces::msg::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use chaos_interfaces::msg::to_yaml() instead")]]
inline std::string to_yaml(const chaos_interfaces::msg::RobotState & msg)
{
  return chaos_interfaces::msg::to_yaml(msg);
}

template<>
inline const char * data_type<chaos_interfaces::msg::RobotState>()
{
  return "chaos_interfaces::msg::RobotState";
}

template<>
inline const char * name<chaos_interfaces::msg::RobotState>()
{
  return "chaos_interfaces/msg/RobotState";
}

template<>
struct has_fixed_size<chaos_interfaces::msg::RobotState>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<chaos_interfaces::msg::RobotState>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<chaos_interfaces::msg::RobotState>
  : std::true_type {};

}  // namespace rosidl_generator_traits

#endif  // CHAOS_INTERFACES__MSG__DETAIL__ROBOT_STATE__TRAITS_HPP_
