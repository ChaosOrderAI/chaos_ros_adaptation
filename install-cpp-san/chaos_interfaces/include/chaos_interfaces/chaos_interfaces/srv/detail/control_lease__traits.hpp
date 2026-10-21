// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from chaos_interfaces:srv/ControlLease.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/srv/control_lease.hpp"


#ifndef CHAOS_INTERFACES__SRV__DETAIL__CONTROL_LEASE__TRAITS_HPP_
#define CHAOS_INTERFACES__SRV__DETAIL__CONTROL_LEASE__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "chaos_interfaces/srv/detail/control_lease__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

// Include directives for member types
// Member 'expires_at'
#include "builtin_interfaces/msg/detail/time__traits.hpp"

namespace chaos_interfaces
{

namespace srv
{

inline void to_flow_style_yaml(
  const ControlLease_Request & msg,
  std::ostream & out)
{
  out << "{";
  // member: operation
  {
    out << "operation: ";
    rosidl_generator_traits::value_to_yaml(msg.operation, out);
    out << ", ";
  }

  // member: owner
  {
    out << "owner: ";
    rosidl_generator_traits::value_to_yaml(msg.owner, out);
    out << ", ";
  }

  // member: token
  {
    out << "token: ";
    rosidl_generator_traits::value_to_yaml(msg.token, out);
    out << ", ";
  }

  // member: duration_s
  {
    out << "duration_s: ";
    rosidl_generator_traits::value_to_yaml(msg.duration_s, out);
    out << ", ";
  }

  // member: expires_at
  {
    out << "expires_at: ";
    to_flow_style_yaml(msg.expires_at, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const ControlLease_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: operation
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "operation: ";
    rosidl_generator_traits::value_to_yaml(msg.operation, out);
    out << "\n";
  }

  // member: owner
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "owner: ";
    rosidl_generator_traits::value_to_yaml(msg.owner, out);
    out << "\n";
  }

  // member: token
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "token: ";
    rosidl_generator_traits::value_to_yaml(msg.token, out);
    out << "\n";
  }

  // member: duration_s
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "duration_s: ";
    rosidl_generator_traits::value_to_yaml(msg.duration_s, out);
    out << "\n";
  }

  // member: expires_at
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "expires_at:\n";
    to_block_style_yaml(msg.expires_at, out, indentation + 2);
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const ControlLease_Request & msg, bool use_flow_style = false)
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
  const chaos_interfaces::srv::ControlLease_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  chaos_interfaces::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use chaos_interfaces::srv::to_yaml() instead")]]
inline std::string to_yaml(const chaos_interfaces::srv::ControlLease_Request & msg)
{
  return chaos_interfaces::srv::to_yaml(msg);
}

template<>
inline const char * data_type<chaos_interfaces::srv::ControlLease_Request>()
{
  return "chaos_interfaces::srv::ControlLease_Request";
}

template<>
inline const char * name<chaos_interfaces::srv::ControlLease_Request>()
{
  return "chaos_interfaces/srv/ControlLease_Request";
}

template<>
struct has_fixed_size<chaos_interfaces::srv::ControlLease_Request>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<chaos_interfaces::srv::ControlLease_Request>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<chaos_interfaces::srv::ControlLease_Request>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace chaos_interfaces
{

namespace srv
{

inline void to_flow_style_yaml(
  const ControlLease_Response & msg,
  std::ostream & out)
{
  out << "{";
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
    out << ", ";
  }

  // member: token
  {
    out << "token: ";
    rosidl_generator_traits::value_to_yaml(msg.token, out);
    out << ", ";
  }

  // member: remaining_s
  {
    out << "remaining_s: ";
    rosidl_generator_traits::value_to_yaml(msg.remaining_s, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const ControlLease_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
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

  // member: token
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "token: ";
    rosidl_generator_traits::value_to_yaml(msg.token, out);
    out << "\n";
  }

  // member: remaining_s
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "remaining_s: ";
    rosidl_generator_traits::value_to_yaml(msg.remaining_s, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const ControlLease_Response & msg, bool use_flow_style = false)
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
  const chaos_interfaces::srv::ControlLease_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  chaos_interfaces::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use chaos_interfaces::srv::to_yaml() instead")]]
inline std::string to_yaml(const chaos_interfaces::srv::ControlLease_Response & msg)
{
  return chaos_interfaces::srv::to_yaml(msg);
}

template<>
inline const char * data_type<chaos_interfaces::srv::ControlLease_Response>()
{
  return "chaos_interfaces::srv::ControlLease_Response";
}

template<>
inline const char * name<chaos_interfaces::srv::ControlLease_Response>()
{
  return "chaos_interfaces/srv/ControlLease_Response";
}

template<>
struct has_fixed_size<chaos_interfaces::srv::ControlLease_Response>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<chaos_interfaces::srv::ControlLease_Response>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<chaos_interfaces::srv::ControlLease_Response>
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
  const ControlLease_Event & msg,
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
  const ControlLease_Event & msg,
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

inline std::string to_yaml(const ControlLease_Event & msg, bool use_flow_style = false)
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
  const chaos_interfaces::srv::ControlLease_Event & msg,
  std::ostream & out, size_t indentation = 0)
{
  chaos_interfaces::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use chaos_interfaces::srv::to_yaml() instead")]]
inline std::string to_yaml(const chaos_interfaces::srv::ControlLease_Event & msg)
{
  return chaos_interfaces::srv::to_yaml(msg);
}

template<>
inline const char * data_type<chaos_interfaces::srv::ControlLease_Event>()
{
  return "chaos_interfaces::srv::ControlLease_Event";
}

template<>
inline const char * name<chaos_interfaces::srv::ControlLease_Event>()
{
  return "chaos_interfaces/srv/ControlLease_Event";
}

template<>
struct has_fixed_size<chaos_interfaces::srv::ControlLease_Event>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<chaos_interfaces::srv::ControlLease_Event>
  : std::integral_constant<bool, has_bounded_size<chaos_interfaces::srv::ControlLease_Request>::value && has_bounded_size<chaos_interfaces::srv::ControlLease_Response>::value && has_bounded_size<service_msgs::msg::ServiceEventInfo>::value> {};

template<>
struct is_message<chaos_interfaces::srv::ControlLease_Event>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace rosidl_generator_traits
{

template<>
inline const char * data_type<chaos_interfaces::srv::ControlLease>()
{
  return "chaos_interfaces::srv::ControlLease";
}

template<>
inline const char * name<chaos_interfaces::srv::ControlLease>()
{
  return "chaos_interfaces/srv/ControlLease";
}

template<>
struct has_fixed_size<chaos_interfaces::srv::ControlLease>
  : std::integral_constant<
    bool,
    has_fixed_size<chaos_interfaces::srv::ControlLease_Request>::value &&
    has_fixed_size<chaos_interfaces::srv::ControlLease_Response>::value
  >
{
};

template<>
struct has_bounded_size<chaos_interfaces::srv::ControlLease>
  : std::integral_constant<
    bool,
    has_bounded_size<chaos_interfaces::srv::ControlLease_Request>::value &&
    has_bounded_size<chaos_interfaces::srv::ControlLease_Response>::value
  >
{
};

template<>
struct is_service<chaos_interfaces::srv::ControlLease>
  : std::true_type
{
};

template<>
struct is_service_request<chaos_interfaces::srv::ControlLease_Request>
  : std::true_type
{
};

template<>
struct is_service_response<chaos_interfaces::srv::ControlLease_Response>
  : std::true_type
{
};

}  // namespace rosidl_generator_traits

#endif  // CHAOS_INTERFACES__SRV__DETAIL__CONTROL_LEASE__TRAITS_HPP_
