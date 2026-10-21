// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from chaos_interfaces:msg/ControlTrace.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/msg/control_trace.hpp"


#ifndef CHAOS_INTERFACES__MSG__DETAIL__CONTROL_TRACE__STRUCT_HPP_
#define CHAOS_INTERFACES__MSG__DETAIL__CONTROL_TRACE__STRUCT_HPP_

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rosidl_runtime_cpp/bounded_vector.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__struct.hpp"
// Member 'velocity'
#include "geometry_msgs/msg/detail/twist__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__chaos_interfaces__msg__ControlTrace __attribute__((deprecated))
#else
# define DEPRECATED__chaos_interfaces__msg__ControlTrace __declspec(deprecated)
#endif

namespace chaos_interfaces
{

namespace msg
{

// message struct
template<class ContainerAllocator>
struct ControlTrace_
{
  using Type = ControlTrace_<ContainerAllocator>;

  explicit ControlTrace_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : header(_init),
    velocity(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->asset_id = "";
      this->sequence = 0ull;
      this->monotonic_ns = 0ull;
      this->request_id = "";
      this->command = "";
      this->stage = "";
      this->velocity_valid = false;
      this->valid_for_s = 0.0;
      this->code = "";
    }
  }

  explicit ControlTrace_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : header(_alloc, _init),
    asset_id(_alloc),
    request_id(_alloc),
    command(_alloc),
    stage(_alloc),
    velocity(_alloc, _init),
    code(_alloc)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->asset_id = "";
      this->sequence = 0ull;
      this->monotonic_ns = 0ull;
      this->request_id = "";
      this->command = "";
      this->stage = "";
      this->velocity_valid = false;
      this->valid_for_s = 0.0;
      this->code = "";
    }
  }

  // field types and members
  using _header_type =
    std_msgs::msg::Header_<ContainerAllocator>;
  _header_type header;
  using _asset_id_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _asset_id_type asset_id;
  using _sequence_type =
    uint64_t;
  _sequence_type sequence;
  using _monotonic_ns_type =
    uint64_t;
  _monotonic_ns_type monotonic_ns;
  using _request_id_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _request_id_type request_id;
  using _command_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _command_type command;
  using _stage_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _stage_type stage;
  using _velocity_valid_type =
    bool;
  _velocity_valid_type velocity_valid;
  using _velocity_type =
    geometry_msgs::msg::Twist_<ContainerAllocator>;
  _velocity_type velocity;
  using _valid_for_s_type =
    double;
  _valid_for_s_type valid_for_s;
  using _code_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _code_type code;

  // setters for named parameter idiom
  Type & set__header(
    const std_msgs::msg::Header_<ContainerAllocator> & _arg)
  {
    this->header = _arg;
    return *this;
  }
  Type & set__asset_id(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->asset_id = _arg;
    return *this;
  }
  Type & set__sequence(
    const uint64_t & _arg)
  {
    this->sequence = _arg;
    return *this;
  }
  Type & set__monotonic_ns(
    const uint64_t & _arg)
  {
    this->monotonic_ns = _arg;
    return *this;
  }
  Type & set__request_id(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->request_id = _arg;
    return *this;
  }
  Type & set__command(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->command = _arg;
    return *this;
  }
  Type & set__stage(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->stage = _arg;
    return *this;
  }
  Type & set__velocity_valid(
    const bool & _arg)
  {
    this->velocity_valid = _arg;
    return *this;
  }
  Type & set__velocity(
    const geometry_msgs::msg::Twist_<ContainerAllocator> & _arg)
  {
    this->velocity = _arg;
    return *this;
  }
  Type & set__valid_for_s(
    const double & _arg)
  {
    this->valid_for_s = _arg;
    return *this;
  }
  Type & set__code(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->code = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    chaos_interfaces::msg::ControlTrace_<ContainerAllocator> *;
  using ConstRawPtr =
    const chaos_interfaces::msg::ControlTrace_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<chaos_interfaces::msg::ControlTrace_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<chaos_interfaces::msg::ControlTrace_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::msg::ControlTrace_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::msg::ControlTrace_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::msg::ControlTrace_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::msg::ControlTrace_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<chaos_interfaces::msg::ControlTrace_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<chaos_interfaces::msg::ControlTrace_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__chaos_interfaces__msg__ControlTrace
    std::shared_ptr<chaos_interfaces::msg::ControlTrace_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__chaos_interfaces__msg__ControlTrace
    std::shared_ptr<chaos_interfaces::msg::ControlTrace_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const ControlTrace_ & other) const
  {
    if (this->header != other.header) {
      return false;
    }
    if (this->asset_id != other.asset_id) {
      return false;
    }
    if (this->sequence != other.sequence) {
      return false;
    }
    if (this->monotonic_ns != other.monotonic_ns) {
      return false;
    }
    if (this->request_id != other.request_id) {
      return false;
    }
    if (this->command != other.command) {
      return false;
    }
    if (this->stage != other.stage) {
      return false;
    }
    if (this->velocity_valid != other.velocity_valid) {
      return false;
    }
    if (this->velocity != other.velocity) {
      return false;
    }
    if (this->valid_for_s != other.valid_for_s) {
      return false;
    }
    if (this->code != other.code) {
      return false;
    }
    return true;
  }
  bool operator!=(const ControlTrace_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct ControlTrace_

// alias to use template instance with default allocator
using ControlTrace =
  chaos_interfaces::msg::ControlTrace_<std::allocator<void>>;

// constant definitions

}  // namespace msg

}  // namespace chaos_interfaces

#endif  // CHAOS_INTERFACES__MSG__DETAIL__CONTROL_TRACE__STRUCT_HPP_
