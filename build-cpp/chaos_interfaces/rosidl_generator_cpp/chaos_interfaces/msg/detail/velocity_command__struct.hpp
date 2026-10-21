// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from chaos_interfaces:msg/VelocityCommand.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/msg/velocity_command.hpp"


#ifndef CHAOS_INTERFACES__MSG__DETAIL__VELOCITY_COMMAND__STRUCT_HPP_
#define CHAOS_INTERFACES__MSG__DETAIL__VELOCITY_COMMAND__STRUCT_HPP_

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
# define DEPRECATED__chaos_interfaces__msg__VelocityCommand __attribute__((deprecated))
#else
# define DEPRECATED__chaos_interfaces__msg__VelocityCommand __declspec(deprecated)
#endif

namespace chaos_interfaces
{

namespace msg
{

// message struct
template<class ContainerAllocator>
struct VelocityCommand_
{
  using Type = VelocityCommand_<ContainerAllocator>;

  explicit VelocityCommand_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : header(_init),
    velocity(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->request_id = "";
      this->lease_token = "";
      this->valid_for_s = 0.0;
    }
  }

  explicit VelocityCommand_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : header(_alloc, _init),
    request_id(_alloc),
    lease_token(_alloc),
    velocity(_alloc, _init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->request_id = "";
      this->lease_token = "";
      this->valid_for_s = 0.0;
    }
  }

  // field types and members
  using _header_type =
    std_msgs::msg::Header_<ContainerAllocator>;
  _header_type header;
  using _request_id_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _request_id_type request_id;
  using _lease_token_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _lease_token_type lease_token;
  using _valid_for_s_type =
    double;
  _valid_for_s_type valid_for_s;
  using _velocity_type =
    geometry_msgs::msg::Twist_<ContainerAllocator>;
  _velocity_type velocity;

  // setters for named parameter idiom
  Type & set__header(
    const std_msgs::msg::Header_<ContainerAllocator> & _arg)
  {
    this->header = _arg;
    return *this;
  }
  Type & set__request_id(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->request_id = _arg;
    return *this;
  }
  Type & set__lease_token(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->lease_token = _arg;
    return *this;
  }
  Type & set__valid_for_s(
    const double & _arg)
  {
    this->valid_for_s = _arg;
    return *this;
  }
  Type & set__velocity(
    const geometry_msgs::msg::Twist_<ContainerAllocator> & _arg)
  {
    this->velocity = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    chaos_interfaces::msg::VelocityCommand_<ContainerAllocator> *;
  using ConstRawPtr =
    const chaos_interfaces::msg::VelocityCommand_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<chaos_interfaces::msg::VelocityCommand_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<chaos_interfaces::msg::VelocityCommand_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::msg::VelocityCommand_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::msg::VelocityCommand_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::msg::VelocityCommand_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::msg::VelocityCommand_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<chaos_interfaces::msg::VelocityCommand_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<chaos_interfaces::msg::VelocityCommand_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__chaos_interfaces__msg__VelocityCommand
    std::shared_ptr<chaos_interfaces::msg::VelocityCommand_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__chaos_interfaces__msg__VelocityCommand
    std::shared_ptr<chaos_interfaces::msg::VelocityCommand_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const VelocityCommand_ & other) const
  {
    if (this->header != other.header) {
      return false;
    }
    if (this->request_id != other.request_id) {
      return false;
    }
    if (this->lease_token != other.lease_token) {
      return false;
    }
    if (this->valid_for_s != other.valid_for_s) {
      return false;
    }
    if (this->velocity != other.velocity) {
      return false;
    }
    return true;
  }
  bool operator!=(const VelocityCommand_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct VelocityCommand_

// alias to use template instance with default allocator
using VelocityCommand =
  chaos_interfaces::msg::VelocityCommand_<std::allocator<void>>;

// constant definitions

}  // namespace msg

}  // namespace chaos_interfaces

#endif  // CHAOS_INTERFACES__MSG__DETAIL__VELOCITY_COMMAND__STRUCT_HPP_
