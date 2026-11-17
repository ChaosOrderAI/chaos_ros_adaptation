// Declare-and-read helper for ROS 2 parameters. Upstream YAML files write
// whole numbers for double parameters (img_point_cov: 100), which a strictly
// typed ROS 2 parameter rejects, so numbers are converted here.
#ifndef LIVO_PARAMS_H
#define LIVO_PARAMS_H

#include <rclcpp/rclcpp.hpp>

#include <cmath>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace livo
{
namespace detail
{

inline std::runtime_error bad_type(const std::string &name, const rclcpp::ParameterValue &value, const char *wanted)
{
  return std::runtime_error("Parameter '" + name + "' is " + rclcpp::to_string(value.get_type()) + ", expected " + wanted);
}

template <typename T> T convert(const std::string &name, const rclcpp::ParameterValue &v)
{
  using rclcpp::ParameterType;
  if constexpr (std::is_same_v<T, bool>)
  {
    if (v.get_type() == ParameterType::PARAMETER_BOOL) return v.get<bool>();
    if (v.get_type() == ParameterType::PARAMETER_INTEGER) return v.get<int64_t>() != 0;
    throw bad_type(name, v, "bool");
  }
  else if constexpr (std::is_integral_v<T>)
  {
    if (v.get_type() == ParameterType::PARAMETER_INTEGER) return static_cast<T>(v.get<int64_t>());
    if (v.get_type() == ParameterType::PARAMETER_DOUBLE && std::floor(v.get<double>()) == v.get<double>()) return static_cast<T>(v.get<double>());
    throw bad_type(name, v, "integer");
  }
  else if constexpr (std::is_floating_point_v<T>)
  {
    if (v.get_type() == ParameterType::PARAMETER_DOUBLE) return static_cast<T>(v.get<double>());
    if (v.get_type() == ParameterType::PARAMETER_INTEGER) return static_cast<T>(v.get<int64_t>());
    throw bad_type(name, v, "double");
  }
  else if constexpr (std::is_same_v<T, std::string>)
  {
    if (v.get_type() == ParameterType::PARAMETER_STRING) return v.get<std::string>();
    throw bad_type(name, v, "string");
  }
  else if constexpr (std::is_same_v<T, std::vector<double>>)
  {
    if (v.get_type() == ParameterType::PARAMETER_DOUBLE_ARRAY) return v.get<std::vector<double>>();
    if (v.get_type() == ParameterType::PARAMETER_INTEGER_ARRAY)
    {
      const auto ints = v.get<std::vector<int64_t>>();
      return std::vector<double>(ints.begin(), ints.end());
    }
    throw bad_type(name, v, "double array");
  }
  else if constexpr (std::is_same_v<T, std::vector<int>>)
  {
    if (v.get_type() == ParameterType::PARAMETER_INTEGER_ARRAY)
    {
      const auto ints = v.get<std::vector<int64_t>>();
      return std::vector<int>(ints.begin(), ints.end());
    }
    throw bad_type(name, v, "integer array");
  }
  else
  {
    static_assert(!sizeof(T), "unsupported parameter type");
  }
}

template <typename T> rclcpp::ParameterValue to_value(const T &value)
{
  if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::string> || std::is_same_v<T, std::vector<double>>)
    return rclcpp::ParameterValue(value);
  else if constexpr (std::is_integral_v<T>)
    return rclcpp::ParameterValue(static_cast<int64_t>(value));
  else if constexpr (std::is_floating_point_v<T>)
    return rclcpp::ParameterValue(static_cast<double>(value));
  else if constexpr (std::is_same_v<T, std::vector<int>>)
    return rclcpp::ParameterValue(std::vector<int64_t>(value.begin(), value.end()));
  else
    static_assert(!sizeof(T), "unsupported parameter type");
}

} // namespace detail

/// Same shape as ros::NodeHandle::param(name, out, default) in ROS 1.
template <typename T, typename D> void param(rclcpp::Node &node, const std::string &name, T &out, const D &default_value)
{
  const T fallback = static_cast<T>(default_value);
  rclcpp::ParameterValue value;
  if (node.has_parameter(name)) value = node.get_parameter(name).get_parameter_value();
  else
  {
    rcl_interfaces::msg::ParameterDescriptor descriptor;
    descriptor.dynamic_typing = true;
    value = node.declare_parameter(name, detail::to_value(fallback), descriptor);
  }
  out = value.get_type() == rclcpp::ParameterType::PARAMETER_NOT_SET ? fallback : detail::convert<T>(name, value);
}

} // namespace livo

#endif
