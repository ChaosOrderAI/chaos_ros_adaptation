// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from chaos_interfaces:srv/Stop.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/srv/stop.hpp"


#ifndef CHAOS_INTERFACES__SRV__DETAIL__STOP__STRUCT_HPP_
#define CHAOS_INTERFACES__SRV__DETAIL__STOP__STRUCT_HPP_

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rosidl_runtime_cpp/bounded_vector.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


// Include directives for member types
// Member 'expires_at'
#include "builtin_interfaces/msg/detail/time__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__chaos_interfaces__srv__Stop_Request __attribute__((deprecated))
#else
# define DEPRECATED__chaos_interfaces__srv__Stop_Request __declspec(deprecated)
#endif

namespace chaos_interfaces
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct Stop_Request_
{
  using Type = Stop_Request_<ContainerAllocator>;

  explicit Stop_Request_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : expires_at(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->request_id = "";
      this->lease_token = "";
    }
  }

  explicit Stop_Request_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : request_id(_alloc),
    lease_token(_alloc),
    expires_at(_alloc, _init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->request_id = "";
      this->lease_token = "";
    }
  }

  // field types and members
  using _request_id_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _request_id_type request_id;
  using _lease_token_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _lease_token_type lease_token;
  using _expires_at_type =
    builtin_interfaces::msg::Time_<ContainerAllocator>;
  _expires_at_type expires_at;

  // setters for named parameter idiom
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
  Type & set__expires_at(
    const builtin_interfaces::msg::Time_<ContainerAllocator> & _arg)
  {
    this->expires_at = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    chaos_interfaces::srv::Stop_Request_<ContainerAllocator> *;
  using ConstRawPtr =
    const chaos_interfaces::srv::Stop_Request_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<chaos_interfaces::srv::Stop_Request_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<chaos_interfaces::srv::Stop_Request_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::srv::Stop_Request_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::srv::Stop_Request_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::srv::Stop_Request_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::srv::Stop_Request_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<chaos_interfaces::srv::Stop_Request_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<chaos_interfaces::srv::Stop_Request_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__chaos_interfaces__srv__Stop_Request
    std::shared_ptr<chaos_interfaces::srv::Stop_Request_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__chaos_interfaces__srv__Stop_Request
    std::shared_ptr<chaos_interfaces::srv::Stop_Request_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const Stop_Request_ & other) const
  {
    if (this->request_id != other.request_id) {
      return false;
    }
    if (this->lease_token != other.lease_token) {
      return false;
    }
    if (this->expires_at != other.expires_at) {
      return false;
    }
    return true;
  }
  bool operator!=(const Stop_Request_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct Stop_Request_

// alias to use template instance with default allocator
using Stop_Request =
  chaos_interfaces::srv::Stop_Request_<std::allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace chaos_interfaces


#ifndef _WIN32
# define DEPRECATED__chaos_interfaces__srv__Stop_Response __attribute__((deprecated))
#else
# define DEPRECATED__chaos_interfaces__srv__Stop_Response __declspec(deprecated)
#endif

namespace chaos_interfaces
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct Stop_Response_
{
  using Type = Stop_Response_<ContainerAllocator>;

  explicit Stop_Response_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->success = false;
      this->code = "";
      this->message = "";
    }
  }

  explicit Stop_Response_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : code(_alloc),
    message(_alloc)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->success = false;
      this->code = "";
      this->message = "";
    }
  }

  // field types and members
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
    chaos_interfaces::srv::Stop_Response_<ContainerAllocator> *;
  using ConstRawPtr =
    const chaos_interfaces::srv::Stop_Response_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<chaos_interfaces::srv::Stop_Response_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<chaos_interfaces::srv::Stop_Response_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::srv::Stop_Response_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::srv::Stop_Response_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::srv::Stop_Response_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::srv::Stop_Response_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<chaos_interfaces::srv::Stop_Response_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<chaos_interfaces::srv::Stop_Response_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__chaos_interfaces__srv__Stop_Response
    std::shared_ptr<chaos_interfaces::srv::Stop_Response_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__chaos_interfaces__srv__Stop_Response
    std::shared_ptr<chaos_interfaces::srv::Stop_Response_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const Stop_Response_ & other) const
  {
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
  bool operator!=(const Stop_Response_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct Stop_Response_

// alias to use template instance with default allocator
using Stop_Response =
  chaos_interfaces::srv::Stop_Response_<std::allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace chaos_interfaces


// Include directives for member types
// Member 'info'
#include "service_msgs/msg/detail/service_event_info__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__chaos_interfaces__srv__Stop_Event __attribute__((deprecated))
#else
# define DEPRECATED__chaos_interfaces__srv__Stop_Event __declspec(deprecated)
#endif

namespace chaos_interfaces
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct Stop_Event_
{
  using Type = Stop_Event_<ContainerAllocator>;

  explicit Stop_Event_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : info(_init)
  {
    (void)_init;
  }

  explicit Stop_Event_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : info(_alloc, _init)
  {
    (void)_init;
  }

  // field types and members
  using _info_type =
    service_msgs::msg::ServiceEventInfo_<ContainerAllocator>;
  _info_type info;
  using _request_type =
    rosidl_runtime_cpp::BoundedVector<chaos_interfaces::srv::Stop_Request_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<chaos_interfaces::srv::Stop_Request_<ContainerAllocator>>>;
  _request_type request;
  using _response_type =
    rosidl_runtime_cpp::BoundedVector<chaos_interfaces::srv::Stop_Response_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<chaos_interfaces::srv::Stop_Response_<ContainerAllocator>>>;
  _response_type response;

  // setters for named parameter idiom
  Type & set__info(
    const service_msgs::msg::ServiceEventInfo_<ContainerAllocator> & _arg)
  {
    this->info = _arg;
    return *this;
  }
  Type & set__request(
    const rosidl_runtime_cpp::BoundedVector<chaos_interfaces::srv::Stop_Request_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<chaos_interfaces::srv::Stop_Request_<ContainerAllocator>>> & _arg)
  {
    this->request = _arg;
    return *this;
  }
  Type & set__response(
    const rosidl_runtime_cpp::BoundedVector<chaos_interfaces::srv::Stop_Response_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<chaos_interfaces::srv::Stop_Response_<ContainerAllocator>>> & _arg)
  {
    this->response = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    chaos_interfaces::srv::Stop_Event_<ContainerAllocator> *;
  using ConstRawPtr =
    const chaos_interfaces::srv::Stop_Event_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<chaos_interfaces::srv::Stop_Event_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<chaos_interfaces::srv::Stop_Event_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::srv::Stop_Event_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::srv::Stop_Event_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::srv::Stop_Event_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::srv::Stop_Event_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<chaos_interfaces::srv::Stop_Event_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<chaos_interfaces::srv::Stop_Event_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__chaos_interfaces__srv__Stop_Event
    std::shared_ptr<chaos_interfaces::srv::Stop_Event_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__chaos_interfaces__srv__Stop_Event
    std::shared_ptr<chaos_interfaces::srv::Stop_Event_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const Stop_Event_ & other) const
  {
    if (this->info != other.info) {
      return false;
    }
    if (this->request != other.request) {
      return false;
    }
    if (this->response != other.response) {
      return false;
    }
    return true;
  }
  bool operator!=(const Stop_Event_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct Stop_Event_

// alias to use template instance with default allocator
using Stop_Event =
  chaos_interfaces::srv::Stop_Event_<std::allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace chaos_interfaces

namespace chaos_interfaces
{

namespace srv
{

struct Stop
{
  using Request = chaos_interfaces::srv::Stop_Request;
  using Response = chaos_interfaces::srv::Stop_Response;
  using Event = chaos_interfaces::srv::Stop_Event;
};

}  // namespace srv

}  // namespace chaos_interfaces

#endif  // CHAOS_INTERFACES__SRV__DETAIL__STOP__STRUCT_HPP_
