// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from chaos_interfaces:srv/Stop.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/srv/stop.hpp"


#ifndef CHAOS_INTERFACES__SRV__DETAIL__STOP__TRAITS_HPP_
#define CHAOS_INTERFACES__SRV__DETAIL__STOP__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "chaos_interfaces/srv/detail/stop__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

// Include directives for member types
// Member 'expires_at'
#include "builtin_interfaces/msg/detail/time__traits.hpp"

namespace chaos_interfaces
{

namespace srv
{

inline void to_flow_style_yaml(
  const Stop_Request & msg,
  std::ostream & out)
{
  out << "{";
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

  // member: expires_at
  {
    out << "expires_at: ";
    to_flow_style_yaml(msg.expires_at, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const Stop_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
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

  // member: expires_at
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "expires_at:\n";
    to_block_style_yaml(msg.expires_at, out, indentation + 2);
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const Stop_Request & msg, bool use_flow_style = false)
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
  const chaos_interfaces::srv::Stop_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  chaos_interfaces::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use chaos_interfaces::srv::to_yaml() instead")]]
inline std::string to_yaml(const chaos_interfaces::srv::Stop_Request & msg)
{
  return chaos_interfaces::srv::to_yaml(msg);
}

template<>
inline const char * data_type<chaos_interfaces::srv::Stop_Request>()
{
  return "chaos_interfaces::srv::Stop_Request";
}

template<>
inline const char * name<chaos_interfaces::srv::Stop_Request>()
{
  return "chaos_interfaces/srv/Stop_Request";
}

template<>
struct has_fixed_size<chaos_interfaces::srv::Stop_Request>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<chaos_interfaces::srv::Stop_Request>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<chaos_interfaces::srv::Stop_Request>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace chaos_interfaces
{

namespace srv
{

inline void to_flow_style_yaml(
  const Stop_Response & msg,
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
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const Stop_Response & msg,
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
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const Stop_Response & msg, bool use_flow_style = false)
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
  const chaos_interfaces::srv::Stop_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  chaos_interfaces::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use chaos_interfaces::srv::to_yaml() instead")]]
inline std::string to_yaml(const chaos_interfaces::srv::Stop_Response & msg)
{
  return chaos_interfaces::srv::to_yaml(msg);
}

template<>
inline const char * data_type<chaos_interfaces::srv::Stop_Response>()
{
  return "chaos_interfaces::srv::Stop_Response";
}

template<>
inline const char * name<chaos_interfaces::srv::Stop_Response>()
{
  return "chaos_interfaces/srv/Stop_Response";
}

template<>
struct has_fixed_size<chaos_interfaces::srv::Stop_Response>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<chaos_interfaces::srv::Stop_Response>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<chaos_interfaces::srv::Stop_Response>
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
  const Stop_Event & msg,
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
  const Stop_Event & msg,
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

inline std::string to_yaml(const Stop_Event & msg, bool use_flow_style = false)
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
  const chaos_interfaces::srv::Stop_Event & msg,
  std::ostream & out, size_t indentation = 0)
{
  chaos_interfaces::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use chaos_interfaces::srv::to_yaml() instead")]]
inline std::string to_yaml(const chaos_interfaces::srv::Stop_Event & msg)
{
  return chaos_interfaces::srv::to_yaml(msg);
}

template<>
inline const char * data_type<chaos_interfaces::srv::Stop_Event>()
{
  return "chaos_interfaces::srv::Stop_Event";
}

template<>
inline const char * name<chaos_interfaces::srv::Stop_Event>()
{
  return "chaos_interfaces/srv/Stop_Event";
}

template<>
struct has_fixed_size<chaos_interfaces::srv::Stop_Event>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<chaos_interfaces::srv::Stop_Event>
  : std::integral_constant<bool, has_bounded_size<chaos_interfaces::srv::Stop_Request>::value && has_bounded_size<chaos_interfaces::srv::Stop_Response>::value && has_bounded_size<service_msgs::msg::ServiceEventInfo>::value> {};

template<>
struct is_message<chaos_interfaces::srv::Stop_Event>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace rosidl_generator_traits
{

template<>
inline const char * data_type<chaos_interfaces::srv::Stop>()
{
  return "chaos_interfaces::srv::Stop";
}

template<>
inline const char * name<chaos_interfaces::srv::Stop>()
{
  return "chaos_interfaces/srv/Stop";
}

template<>
struct has_fixed_size<chaos_interfaces::srv::Stop>
  : std::integral_constant<
    bool,
    has_fixed_size<chaos_interfaces::srv::Stop_Request>::value &&
    has_fixed_size<chaos_interfaces::srv::Stop_Response>::value
  >
{
};

template<>
struct has_bounded_size<chaos_interfaces::srv::Stop>
  : std::integral_constant<
    bool,
    has_bounded_size<chaos_interfaces::srv::Stop_Request>::value &&
    has_bounded_size<chaos_interfaces::srv::Stop_Response>::value
  >
{
};

template<>
struct is_service<chaos_interfaces::srv::Stop>
  : std::true_type
{
};

template<>
struct is_service_request<chaos_interfaces::srv::Stop_Request>
  : std::true_type
{
};

template<>
struct is_service_response<chaos_interfaces::srv::Stop_Response>
  : std::true_type
{
};

}  // namespace rosidl_generator_traits

#endif  // CHAOS_INTERFACES__SRV__DETAIL__STOP__TRAITS_HPP_
