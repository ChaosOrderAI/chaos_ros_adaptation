// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from chaos_interfaces:msg/RobotState.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/msg/robot_state.hpp"


#ifndef CHAOS_INTERFACES__MSG__DETAIL__ROBOT_STATE__STRUCT_HPP_
#define CHAOS_INTERFACES__MSG__DETAIL__ROBOT_STATE__STRUCT_HPP_

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rosidl_runtime_cpp/bounded_vector.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


// Include directives for member types
// Member 'stamp'
#include "builtin_interfaces/msg/detail/time__struct.hpp"
// Member 'pose'
#include "geometry_msgs/msg/detail/pose_stamped__struct.hpp"
// Member 'twist'
#include "geometry_msgs/msg/detail/twist_stamped__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__chaos_interfaces__msg__RobotState __attribute__((deprecated))
#else
# define DEPRECATED__chaos_interfaces__msg__RobotState __declspec(deprecated)
#endif

namespace chaos_interfaces
{

namespace msg
{

// message struct
template<class ContainerAllocator>
struct RobotState_
{
  using Type = RobotState_<ContainerAllocator>;

  explicit RobotState_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : stamp(_init),
    pose(_init),
    twist(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->asset_id = "";
      this->model = "";
      this->operational_state = "";
      this->connected = false;
      this->ready = false;
      this->pose_valid = false;
      this->battery_percentage = 0.0f;
      this->detail = "";
    }
  }

  explicit RobotState_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : stamp(_alloc, _init),
    asset_id(_alloc),
    model(_alloc),
    operational_state(_alloc),
    pose(_alloc, _init),
    twist(_alloc, _init),
    detail(_alloc)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->asset_id = "";
      this->model = "";
      this->operational_state = "";
      this->connected = false;
      this->ready = false;
      this->pose_valid = false;
      this->battery_percentage = 0.0f;
      this->detail = "";
    }
  }

  // field types and members
  using _stamp_type =
    builtin_interfaces::msg::Time_<ContainerAllocator>;
  _stamp_type stamp;
  using _asset_id_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _asset_id_type asset_id;
  using _model_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _model_type model;
  using _operational_state_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _operational_state_type operational_state;
  using _connected_type =
    bool;
  _connected_type connected;
  using _ready_type =
    bool;
  _ready_type ready;
  using _pose_valid_type =
    bool;
  _pose_valid_type pose_valid;
  using _pose_type =
    geometry_msgs::msg::PoseStamped_<ContainerAllocator>;
  _pose_type pose;
  using _twist_type =
    geometry_msgs::msg::TwistStamped_<ContainerAllocator>;
  _twist_type twist;
  using _battery_percentage_type =
    float;
  _battery_percentage_type battery_percentage;
  using _detail_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _detail_type detail;

  // setters for named parameter idiom
  Type & set__stamp(
    const builtin_interfaces::msg::Time_<ContainerAllocator> & _arg)
  {
    this->stamp = _arg;
    return *this;
  }
  Type & set__asset_id(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->asset_id = _arg;
    return *this;
  }
  Type & set__model(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->model = _arg;
    return *this;
  }
  Type & set__operational_state(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->operational_state = _arg;
    return *this;
  }
  Type & set__connected(
    const bool & _arg)
  {
    this->connected = _arg;
    return *this;
  }
  Type & set__ready(
    const bool & _arg)
  {
    this->ready = _arg;
    return *this;
  }
  Type & set__pose_valid(
    const bool & _arg)
  {
    this->pose_valid = _arg;
    return *this;
  }
  Type & set__pose(
    const geometry_msgs::msg::PoseStamped_<ContainerAllocator> & _arg)
  {
    this->pose = _arg;
    return *this;
  }
  Type & set__twist(
    const geometry_msgs::msg::TwistStamped_<ContainerAllocator> & _arg)
  {
    this->twist = _arg;
    return *this;
  }
  Type & set__battery_percentage(
    const float & _arg)
  {
    this->battery_percentage = _arg;
    return *this;
  }
  Type & set__detail(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->detail = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    chaos_interfaces::msg::RobotState_<ContainerAllocator> *;
  using ConstRawPtr =
    const chaos_interfaces::msg::RobotState_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<chaos_interfaces::msg::RobotState_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<chaos_interfaces::msg::RobotState_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::msg::RobotState_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::msg::RobotState_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::msg::RobotState_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::msg::RobotState_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<chaos_interfaces::msg::RobotState_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<chaos_interfaces::msg::RobotState_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__chaos_interfaces__msg__RobotState
    std::shared_ptr<chaos_interfaces::msg::RobotState_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__chaos_interfaces__msg__RobotState
    std::shared_ptr<chaos_interfaces::msg::RobotState_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const RobotState_ & other) const
  {
    if (this->stamp != other.stamp) {
      return false;
    }
    if (this->asset_id != other.asset_id) {
      return false;
    }
    if (this->model != other.model) {
      return false;
    }
    if (this->operational_state != other.operational_state) {
      return false;
    }
    if (this->connected != other.connected) {
      return false;
    }
    if (this->ready != other.ready) {
      return false;
    }
    if (this->pose_valid != other.pose_valid) {
      return false;
    }
    if (this->pose != other.pose) {
      return false;
    }
    if (this->twist != other.twist) {
      return false;
    }
    if (this->battery_percentage != other.battery_percentage) {
      return false;
    }
    if (this->detail != other.detail) {
      return false;
    }
    return true;
  }
  bool operator!=(const RobotState_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct RobotState_

// alias to use template instance with default allocator
using RobotState =
  chaos_interfaces::msg::RobotState_<std::allocator<void>>;

// constant definitions

}  // namespace msg

}  // namespace chaos_interfaces

#endif  // CHAOS_INTERFACES__MSG__DETAIL__ROBOT_STATE__STRUCT_HPP_
