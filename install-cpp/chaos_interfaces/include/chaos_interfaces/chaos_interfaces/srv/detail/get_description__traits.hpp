// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from chaos_interfaces:srv/GetDescription.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/srv/get_description.hpp"


#ifndef CHAOS_INTERFACES__SRV__DETAIL__GET_DESCRIPTION__TRAITS_HPP_
#define CHAOS_INTERFACES__SRV__DETAIL__GET_DESCRIPTION__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "chaos_interfaces/srv/detail/get_description__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

namespace chaos_interfaces
{

namespace srv
{

inline void to_flow_style_yaml(
  const GetDescription_Request & msg,
  std::ostream & out)
{
  (void)msg;
  out << "null";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const GetDescription_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  (void)msg;
  (void)indentation;
  out << "null\n";
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const GetDescription_Request & msg, bool use_flow_style = false)
{
  std::ostringstream out;
  if (use_flow_style) {
    to_flow_style_yaml(msg, out);
  } else {
    to_block_style_yaml(msg, out);
  }
  return out.str();
}

}  // namespace srv

}  // namespace chaos_interfaces

namespace rosidl_generator_traits
{

[[deprecated("use chaos_interfaces::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const chaos_interfaces::srv::GetDescription_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  chaos_interfaces::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use chaos_interfaces::srv::to_yaml() instead")]]
inline std::string to_yaml(const chaos_interfaces::srv::GetDescription_Request & msg)
{
  return chaos_interfaces::srv::to_yaml(msg);
}

template<>
inline const char * data_type<chaos_interfaces::srv::GetDescription_Request>()
{
  return "chaos_interfaces::srv::GetDescription_Request";
}

template<>
inline const char * name<chaos_interfaces::srv::GetDescription_Request>()
{
  return "chaos_interfaces/srv/GetDescription_Request";
}

template<>
struct has_fixed_size<chaos_interfaces::srv::GetDescription_Request>
  : std::integral_constant<bool, true> {};

template<>
struct has_bounded_size<chaos_interfaces::srv::GetDescription_Request>
  : std::integral_constant<bool, true> {};

template<>
struct is_message<chaos_interfaces::srv::GetDescription_Request>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace chaos_interfaces
{

namespace srv
{

inline void to_flow_style_yaml(
  const GetDescription_Response & msg,
  std::ostream & out)
{
  out << "{";
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

  // member: model_version
  {
    out << "model_version: ";
    rosidl_generator_traits::value_to_yaml(msg.model_version, out);
    out << ", ";
  }

  // member: capabilities
  {
    if (msg.capabilities.size() == 0) {
      out << "capabilities: []";
    } else {
      out << "capabilities: [";
      size_t pending_items = msg.capabilities.size();
      for (auto item : msg.capabilities) {
        rosidl_generator_traits::value_to_yaml(item, out);
        if (--pending_items > 0) {
          out << ", ";
        }
      }
      out << "]";
    }
    out << ", ";
  }

  // member: base_frame
  {
    out << "base_frame: ";
    rosidl_generator_traits::value_to_yaml(msg.base_frame, out);
    out << ", ";
  }

  // member: odom_frame
  {
    out << "odom_frame: ";
    rosidl_generator_traits::value_to_yaml(msg.odom_frame, out);
    out << ", ";
  }

  // member: map_frame
  {
    out << "map_frame: ";
    rosidl_generator_traits::value_to_yaml(msg.map_frame, out);
    out << ", ";
  }

  // member: linear_limit_mps
  {
    out << "linear_limit_mps: ";
    rosidl_generator_traits::value_to_yaml(msg.linear_limit_mps, out);
    out << ", ";
  }

  // member: angular_limit_rps
  {
    out << "angular_limit_rps: ";
    rosidl_generator_traits::value_to_yaml(msg.angular_limit_rps, out);
    out << ", ";
  }

  // member: command_timeout_s
  {
    out << "command_timeout_s: ";
    rosidl_generator_traits::value_to_yaml(msg.command_timeout_s, out);
    out << ", ";
  }

  // member: lease_max_s
  {
    out << "lease_max_s: ";
    rosidl_generator_traits::value_to_yaml(msg.lease_max_s, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const GetDescription_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
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

  // member: model_version
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "model_version: ";
    rosidl_generator_traits::value_to_yaml(msg.model_version, out);
    out << "\n";
  }

  // member: capabilities
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    if (msg.capabilities.size() == 0) {
      out << "capabilities: []\n";
    } else {
      out << "capabilities:\n";
      for (auto item : msg.capabilities) {
        if (indentation > 0) {
          out << std::string(indentation, ' ');
        }
        out << "- ";
        rosidl_generator_traits::value_to_yaml(item, out);
        out << "\n";
      }
    }
  }

  // member: base_frame
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "base_frame: ";
    rosidl_generator_traits::value_to_yaml(msg.base_frame, out);
    out << "\n";
  }

  // member: odom_frame
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "odom_frame: ";
    rosidl_generator_traits::value_to_yaml(msg.odom_frame, out);
    out << "\n";
  }

  // member: map_frame
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "map_frame: ";
    rosidl_generator_traits::value_to_yaml(msg.map_frame, out);
    out << "\n";
  }

  // member: linear_limit_mps
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "linear_limit_mps: ";
    rosidl_generator_traits::value_to_yaml(msg.linear_limit_mps, out);
    out << "\n";
  }

  // member: angular_limit_rps
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "angular_limit_rps: ";
    rosidl_generator_traits::value_to_yaml(msg.angular_limit_rps, out);
    out << "\n";
  }

  // member: command_timeout_s
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "command_timeout_s: ";
    rosidl_generator_traits::value_to_yaml(msg.command_timeout_s, out);
    out << "\n";
  }

  // member: lease_max_s
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "lease_max_s: ";
    rosidl_generator_traits::value_to_yaml(msg.lease_max_s, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const GetDescription_Response & msg, bool use_flow_style = false)
{
  std::ostringstream out;
  if (use_flow_style) {
    to_flow_style_yaml(msg, out);
  } else {
    to_block_style_yaml(msg, out);
  }
  return out.str();
}

}  // namespace srv

}  // namespace chaos_interfaces

namespace rosidl_generator_traits
{

[[deprecated("use chaos_interfaces::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const chaos_interfaces::srv::GetDescription_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  chaos_interfaces::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use chaos_interfaces::srv::to_yaml() instead")]]
inline std::string to_yaml(const chaos_interfaces::srv::GetDescription_Response & msg)
{
  return chaos_interfaces::srv::to_yaml(msg);
}

template<>
inline const char * data_type<chaos_interfaces::srv::GetDescription_Response>()
{
  return "chaos_interfaces::srv::GetDescription_Response";
}

template<>
inline const char * name<chaos_interfaces::srv::GetDescription_Response>()
{
  return "chaos_interfaces/srv/GetDescription_Response";
}

template<>
struct has_fixed_size<chaos_interfaces::srv::GetDescription_Response>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<chaos_interfaces::srv::GetDescription_Response>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<chaos_interfaces::srv::GetDescription_Response>
  : std::true_type {};

}  // namespace rosidl_generator_traits

// Include directives for member types
// Member 'info'
#include "service_msgs/msg/detail/service_event_info__traits.hpp"

namespace chaos_interfaces
{

namespace srv
{

inline void to_flow_style_yaml(
  const GetDescription_Event & msg,
  std::ostream & out)
{
  out << "{";
  // member: info
  {
    out << "info: ";
    to_flow_style_yaml(msg.info, out);
    out << ", ";
  }

  // member: request
  {
    if (msg.request.size() == 0) {
      out << "request: []";
    } else {
      out << "request: [";
      size_t pending_items = msg.request.size();
      for (auto item : msg.request) {
        to_flow_style_yaml(item, out);
        if (--pending_items > 0) {
          out << ", ";
        }
      }
      out << "]";
    }
    out << ", ";
  }

  // member: response
  {
    if (msg.response.size() == 0) {
      out << "response: []";
    } else {
      out << "response: [";
      size_t pending_items = msg.response.size();
      for (auto item : msg.response) {
        to_flow_style_yaml(item, out);
        if (--pending_items > 0) {
          out << ", ";
        }
      }
      out << "]";
    }
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const GetDescription_Event & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: info
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "info:\n";
    to_block_style_yaml(msg.info, out, indentation + 2);
  }

  // member: request
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    if (msg.request.size() == 0) {
      out << "request: []\n";
    } else {
      out << "request:\n";
      for (auto item : msg.request) {
        if (indentation > 0) {
          out << std::string(indentation, ' ');
        }
        out << "-\n";
        to_block_style_yaml(item, out, indentation + 2);
      }
    }
  }

  // member: response
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    if (msg.response.size() == 0) {
      out << "response: []\n";
    } else {
      out << "response:\n";
      for (auto item : msg.response) {
        if (indentation > 0) {
          out << std::string(indentation, ' ');
        }
        out << "-\n";
        to_block_style_yaml(item, out, indentation + 2);
      }
    }
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const GetDescription_Event & msg, bool use_flow_style = false)
{
  std::ostringstream out;
  if (use_flow_style) {
    to_flow_style_yaml(msg, out);
  } else {
    to_block_style_yaml(msg, out);
  }
  return out.str();
}

}  // namespace srv

}  // namespace chaos_interfaces

namespace rosidl_generator_traits
{

[[deprecated("use chaos_interfaces::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const chaos_interfaces::srv::GetDescription_Event & msg,
  std::ostream & out, size_t indentation = 0)
{
  chaos_interfaces::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use chaos_interfaces::srv::to_yaml() instead")]]
inline std::string to_yaml(const chaos_interfaces::srv::GetDescription_Event & msg)
{
  return chaos_interfaces::srv::to_yaml(msg);
}

template<>
inline const char * data_type<chaos_interfaces::srv::GetDescription_Event>()
{
  return "chaos_interfaces::srv::GetDescription_Event";
}

template<>
inline const char * name<chaos_interfaces::srv::GetDescription_Event>()
{
  return "chaos_interfaces/srv/GetDescription_Event";
}

template<>
struct has_fixed_size<chaos_interfaces::srv::GetDescription_Event>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<chaos_interfaces::srv::GetDescription_Event>
  : std::integral_constant<bool, has_bounded_size<chaos_interfaces::srv::GetDescription_Request>::value && has_bounded_size<chaos_interfaces::srv::GetDescription_Response>::value && has_bounded_size<service_msgs::msg::ServiceEventInfo>::value> {};

template<>
struct is_message<chaos_interfaces::srv::GetDescription_Event>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace rosidl_generator_traits
{

template<>
inline const char * data_type<chaos_interfaces::srv::GetDescription>()
{
  return "chaos_interfaces::srv::GetDescription";
}

template<>
inline const char * name<chaos_interfaces::srv::GetDescription>()
{
  return "chaos_interfaces/srv/GetDescription";
}

template<>
struct has_fixed_size<chaos_interfaces::srv::GetDescription>
  : std::integral_constant<
    bool,
    has_fixed_size<chaos_interfaces::srv::GetDescription_Request>::value &&
    has_fixed_size<chaos_interfaces::srv::GetDescription_Response>::value
  >
{
};

template<>
struct has_bounded_size<chaos_interfaces::srv::GetDescription>
  : std::integral_constant<
    bool,
    has_bounded_size<chaos_interfaces::srv::GetDescription_Request>::value &&
    has_bounded_size<chaos_interfaces::srv::GetDescription_Response>::value
  >
{
};

template<>
struct is_service<chaos_interfaces::srv::GetDescription>
  : std::true_type
{
};

template<>
struct is_service_request<chaos_interfaces::srv::GetDescription_Request>
  : std::true_type
{
};

template<>
struct is_service_response<chaos_interfaces::srv::GetDescription_Response>
  : std::true_type
{
};

}  // namespace rosidl_generator_traits

#endif  // CHAOS_INTERFACES__SRV__DETAIL__GET_DESCRIPTION__TRAITS_HPP_
