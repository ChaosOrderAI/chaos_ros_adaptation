// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from chaos_interfaces:msg/CommandResult.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/msg/command_result.hpp"


#ifndef CHAOS_INTERFACES__MSG__DETAIL__COMMAND_RESULT__STRUCT_HPP_
#define CHAOS_INTERFACES__MSG__DETAIL__COMMAND_RESULT__STRUCT_HPP_

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

#ifndef _WIN32
# define DEPRECATED__chaos_interfaces__msg__CommandResult __attribute__((deprecated))
#else
# define DEPRECATED__chaos_interfaces__msg__CommandResult __declspec(deprecated)
#endif

namespace chaos_interfaces
{

namespace msg
{

// message struct
template<class ContainerAllocator>
struct CommandResult_
{
  using Type = CommandResult_<ContainerAllocator>;

  explicit CommandResult_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : stamp(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->request_id = "";
      this->success = false;
      this->code = "";
      this->message = "";
    }
  }

  explicit CommandResult_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : stamp(_alloc, _init),
    request_id(_alloc),
    code(_alloc),
    message(_alloc)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->request_id = "";
      this->success = false;
      this->code = "";
      this->message = "";
    }
  }

  // field types and members
  using _stamp_type =
    builtin_interfaces::msg::Time_<ContainerAllocator>;
  _stamp_type stamp;
  using _request_id_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _request_id_type request_id;
  using _success_type =
    bool;
  _success_type success;
  using _code_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _code_type code;
  using _message_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _message_type message;

  // setters for named parameter idiom
  Type & set__stamp(
    const builtin_interfaces::msg::Time_<ContainerAllocator> & _arg)
  {
    this->stamp = _arg;
    return *this;
  }
  Type & set__request_id(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->request_id = _arg;
    return *this;
  }
  Type & set__success(
    const bool & _arg)
  {
    this->success = _arg;
    return *this;
  }
  Type & set__code(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->code = _arg;
    return *this;
  }
  Type & set__message(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->message = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    chaos_interfaces::msg::CommandResult_<ContainerAllocator> *;
  using ConstRawPtr =
    const chaos_interfaces::msg::CommandResult_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<chaos_interfaces::msg::CommandResult_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<chaos_interfaces::msg::CommandResult_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::msg::CommandResult_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::msg::CommandResult_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::msg::CommandResult_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::msg::CommandResult_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<chaos_interfaces::msg::CommandResult_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<chaos_interfaces::msg::CommandResult_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__chaos_interfaces__msg__CommandResult
    std::shared_ptr<chaos_interfaces::msg::CommandResult_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__chaos_interfaces__msg__CommandResult
    std::shared_ptr<chaos_interfaces::msg::CommandResult_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const CommandResult_ & other) const
  {
    if (this->stamp != other.stamp) {
      return false;
    }
    if (this->request_id != other.request_id) {
      return false;
    }
    if (this->success != other.success) {
      return false;
    }
    if (this->code != other.code) {
      return false;
    }
    if (this->message != other.message) {
      return false;
    }
    return true;
  }
  bool operator!=(const CommandResult_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct CommandResult_

// alias to use template instance with default allocator
using CommandResult =
  chaos_interfaces::msg::CommandResult_<std::allocator<void>>;

// constant definitions

}  // namespace msg

}  // namespace chaos_interfaces

#endif  // CHAOS_INTERFACES__MSG__DETAIL__COMMAND_RESULT__STRUCT_HPP_
