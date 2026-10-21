// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from chaos_interfaces:action/NavigatePose.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/action/navigate_pose.hpp"


#ifndef CHAOS_INTERFACES__ACTION__DETAIL__NAVIGATE_POSE__STRUCT_HPP_
#define CHAOS_INTERFACES__ACTION__DETAIL__NAVIGATE_POSE__STRUCT_HPP_

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rosidl_runtime_cpp/bounded_vector.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


// Include directives for member types
// Member 'target'
#include "geometry_msgs/msg/detail/pose_stamped__struct.hpp"
// Member 'expires_at'
#include "builtin_interfaces/msg/detail/time__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__chaos_interfaces__action__NavigatePose_Goal __attribute__((deprecated))
#else
# define DEPRECATED__chaos_interfaces__action__NavigatePose_Goal __declspec(deprecated)
#endif

namespace chaos_interfaces
{

namespace action
{

// message struct
template<class ContainerAllocator>
struct NavigatePose_Goal_
{
  using Type = NavigatePose_Goal_<ContainerAllocator>;

  explicit NavigatePose_Goal_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : target(_init),
    expires_at(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->lease_token = "";
    }
  }

  explicit NavigatePose_Goal_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : lease_token(_alloc),
    target(_alloc, _init),
    expires_at(_alloc, _init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->lease_token = "";
    }
  }

  // field types and members
  using _lease_token_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _lease_token_type lease_token;
  using _target_type =
    geometry_msgs::msg::PoseStamped_<ContainerAllocator>;
  _target_type target;
  using _expires_at_type =
    builtin_interfaces::msg::Time_<ContainerAllocator>;
  _expires_at_type expires_at;

  // setters for named parameter idiom
  Type & set__lease_token(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->lease_token = _arg;
    return *this;
  }
  Type & set__target(
    const geometry_msgs::msg::PoseStamped_<ContainerAllocator> & _arg)
  {
    this->target = _arg;
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
    chaos_interfaces::action::NavigatePose_Goal_<ContainerAllocator> *;
  using ConstRawPtr =
    const chaos_interfaces::action::NavigatePose_Goal_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_Goal_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_Goal_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_Goal_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_Goal_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_Goal_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_Goal_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_Goal_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_Goal_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_Goal
    std::shared_ptr<chaos_interfaces::action::NavigatePose_Goal_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_Goal
    std::shared_ptr<chaos_interfaces::action::NavigatePose_Goal_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const NavigatePose_Goal_ & other) const
  {
    if (this->lease_token != other.lease_token) {
      return false;
    }
    if (this->target != other.target) {
      return false;
    }
    if (this->expires_at != other.expires_at) {
      return false;
    }
    return true;
  }
  bool operator!=(const NavigatePose_Goal_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct NavigatePose_Goal_

// alias to use template instance with default allocator
using NavigatePose_Goal =
  chaos_interfaces::action::NavigatePose_Goal_<std::allocator<void>>;

// constant definitions

}  // namespace action

}  // namespace chaos_interfaces


#ifndef _WIN32
# define DEPRECATED__chaos_interfaces__action__NavigatePose_Result __attribute__((deprecated))
#else
# define DEPRECATED__chaos_interfaces__action__NavigatePose_Result __declspec(deprecated)
#endif

namespace chaos_interfaces
{

namespace action
{

// message struct
template<class ContainerAllocator>
struct NavigatePose_Result_
{
  using Type = NavigatePose_Result_<ContainerAllocator>;

  explicit NavigatePose_Result_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->success = false;
      this->code = "";
      this->message = "";
    }
  }

  explicit NavigatePose_Result_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
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
    chaos_interfaces::action::NavigatePose_Result_<ContainerAllocator> *;
  using ConstRawPtr =
    const chaos_interfaces::action::NavigatePose_Result_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_Result_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_Result_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_Result_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_Result_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_Result_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_Result_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_Result_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_Result_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_Result
    std::shared_ptr<chaos_interfaces::action::NavigatePose_Result_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_Result
    std::shared_ptr<chaos_interfaces::action::NavigatePose_Result_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const NavigatePose_Result_ & other) const
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
  bool operator!=(const NavigatePose_Result_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct NavigatePose_Result_

// alias to use template instance with default allocator
using NavigatePose_Result =
  chaos_interfaces::action::NavigatePose_Result_<std::allocator<void>>;

// constant definitions

}  // namespace action

}  // namespace chaos_interfaces


// Include directives for member types
// Member 'current_pose'
// already included above
// #include "geometry_msgs/msg/detail/pose_stamped__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__chaos_interfaces__action__NavigatePose_Feedback __attribute__((deprecated))
#else
# define DEPRECATED__chaos_interfaces__action__NavigatePose_Feedback __declspec(deprecated)
#endif

namespace chaos_interfaces
{

namespace action
{

// message struct
template<class ContainerAllocator>
struct NavigatePose_Feedback_
{
  using Type = NavigatePose_Feedback_<ContainerAllocator>;

  explicit NavigatePose_Feedback_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : current_pose(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->progress = 0.0f;
    }
  }

  explicit NavigatePose_Feedback_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : current_pose(_alloc, _init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->progress = 0.0f;
    }
  }

  // field types and members
  using _current_pose_type =
    geometry_msgs::msg::PoseStamped_<ContainerAllocator>;
  _current_pose_type current_pose;
  using _progress_type =
    float;
  _progress_type progress;

  // setters for named parameter idiom
  Type & set__current_pose(
    const geometry_msgs::msg::PoseStamped_<ContainerAllocator> & _arg)
  {
    this->current_pose = _arg;
    return *this;
  }
  Type & set__progress(
    const float & _arg)
  {
    this->progress = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    chaos_interfaces::action::NavigatePose_Feedback_<ContainerAllocator> *;
  using ConstRawPtr =
    const chaos_interfaces::action::NavigatePose_Feedback_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_Feedback_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_Feedback_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_Feedback_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_Feedback_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_Feedback_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_Feedback_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_Feedback_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_Feedback_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_Feedback
    std::shared_ptr<chaos_interfaces::action::NavigatePose_Feedback_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_Feedback
    std::shared_ptr<chaos_interfaces::action::NavigatePose_Feedback_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const NavigatePose_Feedback_ & other) const
  {
    if (this->current_pose != other.current_pose) {
      return false;
    }
    if (this->progress != other.progress) {
      return false;
    }
    return true;
  }
  bool operator!=(const NavigatePose_Feedback_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct NavigatePose_Feedback_

// alias to use template instance with default allocator
using NavigatePose_Feedback =
  chaos_interfaces::action::NavigatePose_Feedback_<std::allocator<void>>;

// constant definitions

}  // namespace action

}  // namespace chaos_interfaces


// Include directives for member types
// Member 'goal_id'
#include "unique_identifier_msgs/msg/detail/uuid__struct.hpp"
// Member 'goal'
#include "chaos_interfaces/action/detail/navigate_pose__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__chaos_interfaces__action__NavigatePose_SendGoal_Request __attribute__((deprecated))
#else
# define DEPRECATED__chaos_interfaces__action__NavigatePose_SendGoal_Request __declspec(deprecated)
#endif

namespace chaos_interfaces
{

namespace action
{

// message struct
template<class ContainerAllocator>
struct NavigatePose_SendGoal_Request_
{
  using Type = NavigatePose_SendGoal_Request_<ContainerAllocator>;

  explicit NavigatePose_SendGoal_Request_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : goal_id(_init),
    goal(_init)
  {
    (void)_init;
  }

  explicit NavigatePose_SendGoal_Request_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : goal_id(_alloc, _init),
    goal(_alloc, _init)
  {
    (void)_init;
  }

  // field types and members
  using _goal_id_type =
    unique_identifier_msgs::msg::UUID_<ContainerAllocator>;
  _goal_id_type goal_id;
  using _goal_type =
    chaos_interfaces::action::NavigatePose_Goal_<ContainerAllocator>;
  _goal_type goal;

  // setters for named parameter idiom
  Type & set__goal_id(
    const unique_identifier_msgs::msg::UUID_<ContainerAllocator> & _arg)
  {
    this->goal_id = _arg;
    return *this;
  }
  Type & set__goal(
    const chaos_interfaces::action::NavigatePose_Goal_<ContainerAllocator> & _arg)
  {
    this->goal = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    chaos_interfaces::action::NavigatePose_SendGoal_Request_<ContainerAllocator> *;
  using ConstRawPtr =
    const chaos_interfaces::action::NavigatePose_SendGoal_Request_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Request_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Request_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_SendGoal_Request_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Request_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_SendGoal_Request_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Request_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Request_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Request_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_SendGoal_Request
    std::shared_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Request_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_SendGoal_Request
    std::shared_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Request_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const NavigatePose_SendGoal_Request_ & other) const
  {
    if (this->goal_id != other.goal_id) {
      return false;
    }
    if (this->goal != other.goal) {
      return false;
    }
    return true;
  }
  bool operator!=(const NavigatePose_SendGoal_Request_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct NavigatePose_SendGoal_Request_

// alias to use template instance with default allocator
using NavigatePose_SendGoal_Request =
  chaos_interfaces::action::NavigatePose_SendGoal_Request_<std::allocator<void>>;

// constant definitions

}  // namespace action

}  // namespace chaos_interfaces


// Include directives for member types
// Member 'stamp'
// already included above
// #include "builtin_interfaces/msg/detail/time__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__chaos_interfaces__action__NavigatePose_SendGoal_Response __attribute__((deprecated))
#else
# define DEPRECATED__chaos_interfaces__action__NavigatePose_SendGoal_Response __declspec(deprecated)
#endif

namespace chaos_interfaces
{

namespace action
{

// message struct
template<class ContainerAllocator>
struct NavigatePose_SendGoal_Response_
{
  using Type = NavigatePose_SendGoal_Response_<ContainerAllocator>;

  explicit NavigatePose_SendGoal_Response_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : stamp(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->accepted = false;
    }
  }

  explicit NavigatePose_SendGoal_Response_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : stamp(_alloc, _init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->accepted = false;
    }
  }

  // field types and members
  using _accepted_type =
    bool;
  _accepted_type accepted;
  using _stamp_type =
    builtin_interfaces::msg::Time_<ContainerAllocator>;
  _stamp_type stamp;

  // setters for named parameter idiom
  Type & set__accepted(
    const bool & _arg)
  {
    this->accepted = _arg;
    return *this;
  }
  Type & set__stamp(
    const builtin_interfaces::msg::Time_<ContainerAllocator> & _arg)
  {
    this->stamp = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    chaos_interfaces::action::NavigatePose_SendGoal_Response_<ContainerAllocator> *;
  using ConstRawPtr =
    const chaos_interfaces::action::NavigatePose_SendGoal_Response_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Response_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Response_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_SendGoal_Response_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Response_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_SendGoal_Response_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Response_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Response_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Response_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_SendGoal_Response
    std::shared_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Response_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_SendGoal_Response
    std::shared_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Response_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const NavigatePose_SendGoal_Response_ & other) const
  {
    if (this->accepted != other.accepted) {
      return false;
    }
    if (this->stamp != other.stamp) {
      return false;
    }
    return true;
  }
  bool operator!=(const NavigatePose_SendGoal_Response_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct NavigatePose_SendGoal_Response_

// alias to use template instance with default allocator
using NavigatePose_SendGoal_Response =
  chaos_interfaces::action::NavigatePose_SendGoal_Response_<std::allocator<void>>;

// constant definitions

}  // namespace action

}  // namespace chaos_interfaces


// Include directives for member types
// Member 'info'
#include "service_msgs/msg/detail/service_event_info__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__chaos_interfaces__action__NavigatePose_SendGoal_Event __attribute__((deprecated))
#else
# define DEPRECATED__chaos_interfaces__action__NavigatePose_SendGoal_Event __declspec(deprecated)
#endif

namespace chaos_interfaces
{

namespace action
{

// message struct
template<class ContainerAllocator>
struct NavigatePose_SendGoal_Event_
{
  using Type = NavigatePose_SendGoal_Event_<ContainerAllocator>;

  explicit NavigatePose_SendGoal_Event_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : info(_init)
  {
    (void)_init;
  }

  explicit NavigatePose_SendGoal_Event_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : info(_alloc, _init)
  {
    (void)_init;
  }

  // field types and members
  using _info_type =
    service_msgs::msg::ServiceEventInfo_<ContainerAllocator>;
  _info_type info;
  using _request_type =
    rosidl_runtime_cpp::BoundedVector<chaos_interfaces::action::NavigatePose_SendGoal_Request_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<chaos_interfaces::action::NavigatePose_SendGoal_Request_<ContainerAllocator>>>;
  _request_type request;
  using _response_type =
    rosidl_runtime_cpp::BoundedVector<chaos_interfaces::action::NavigatePose_SendGoal_Response_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<chaos_interfaces::action::NavigatePose_SendGoal_Response_<ContainerAllocator>>>;
  _response_type response;

  // setters for named parameter idiom
  Type & set__info(
    const service_msgs::msg::ServiceEventInfo_<ContainerAllocator> & _arg)
  {
    this->info = _arg;
    return *this;
  }
  Type & set__request(
    const rosidl_runtime_cpp::BoundedVector<chaos_interfaces::action::NavigatePose_SendGoal_Request_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<chaos_interfaces::action::NavigatePose_SendGoal_Request_<ContainerAllocator>>> & _arg)
  {
    this->request = _arg;
    return *this;
  }
  Type & set__response(
    const rosidl_runtime_cpp::BoundedVector<chaos_interfaces::action::NavigatePose_SendGoal_Response_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<chaos_interfaces::action::NavigatePose_SendGoal_Response_<ContainerAllocator>>> & _arg)
  {
    this->response = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    chaos_interfaces::action::NavigatePose_SendGoal_Event_<ContainerAllocator> *;
  using ConstRawPtr =
    const chaos_interfaces::action::NavigatePose_SendGoal_Event_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Event_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Event_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_SendGoal_Event_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Event_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_SendGoal_Event_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Event_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Event_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Event_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_SendGoal_Event
    std::shared_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Event_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_SendGoal_Event
    std::shared_ptr<chaos_interfaces::action::NavigatePose_SendGoal_Event_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const NavigatePose_SendGoal_Event_ & other) const
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
  bool operator!=(const NavigatePose_SendGoal_Event_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct NavigatePose_SendGoal_Event_

// alias to use template instance with default allocator
using NavigatePose_SendGoal_Event =
  chaos_interfaces::action::NavigatePose_SendGoal_Event_<std::allocator<void>>;

// constant definitions

}  // namespace action

}  // namespace chaos_interfaces

namespace chaos_interfaces
{

namespace action
{

struct NavigatePose_SendGoal
{
  using Request = chaos_interfaces::action::NavigatePose_SendGoal_Request;
  using Response = chaos_interfaces::action::NavigatePose_SendGoal_Response;
  using Event = chaos_interfaces::action::NavigatePose_SendGoal_Event;
};

}  // namespace action

}  // namespace chaos_interfaces


// Include directives for member types
// Member 'goal_id'
// already included above
// #include "unique_identifier_msgs/msg/detail/uuid__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__chaos_interfaces__action__NavigatePose_GetResult_Request __attribute__((deprecated))
#else
# define DEPRECATED__chaos_interfaces__action__NavigatePose_GetResult_Request __declspec(deprecated)
#endif

namespace chaos_interfaces
{

namespace action
{

// message struct
template<class ContainerAllocator>
struct NavigatePose_GetResult_Request_
{
  using Type = NavigatePose_GetResult_Request_<ContainerAllocator>;

  explicit NavigatePose_GetResult_Request_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : goal_id(_init)
  {
    (void)_init;
  }

  explicit NavigatePose_GetResult_Request_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : goal_id(_alloc, _init)
  {
    (void)_init;
  }

  // field types and members
  using _goal_id_type =
    unique_identifier_msgs::msg::UUID_<ContainerAllocator>;
  _goal_id_type goal_id;

  // setters for named parameter idiom
  Type & set__goal_id(
    const unique_identifier_msgs::msg::UUID_<ContainerAllocator> & _arg)
  {
    this->goal_id = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    chaos_interfaces::action::NavigatePose_GetResult_Request_<ContainerAllocator> *;
  using ConstRawPtr =
    const chaos_interfaces::action::NavigatePose_GetResult_Request_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_GetResult_Request_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_GetResult_Request_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_GetResult_Request_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_GetResult_Request_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_GetResult_Request_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_GetResult_Request_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_GetResult_Request_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_GetResult_Request_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_GetResult_Request
    std::shared_ptr<chaos_interfaces::action::NavigatePose_GetResult_Request_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_GetResult_Request
    std::shared_ptr<chaos_interfaces::action::NavigatePose_GetResult_Request_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const NavigatePose_GetResult_Request_ & other) const
  {
    if (this->goal_id != other.goal_id) {
      return false;
    }
    return true;
  }
  bool operator!=(const NavigatePose_GetResult_Request_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct NavigatePose_GetResult_Request_

// alias to use template instance with default allocator
using NavigatePose_GetResult_Request =
  chaos_interfaces::action::NavigatePose_GetResult_Request_<std::allocator<void>>;

// constant definitions

}  // namespace action

}  // namespace chaos_interfaces


// Include directives for member types
// Member 'result'
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__chaos_interfaces__action__NavigatePose_GetResult_Response __attribute__((deprecated))
#else
# define DEPRECATED__chaos_interfaces__action__NavigatePose_GetResult_Response __declspec(deprecated)
#endif

namespace chaos_interfaces
{

namespace action
{

// message struct
template<class ContainerAllocator>
struct NavigatePose_GetResult_Response_
{
  using Type = NavigatePose_GetResult_Response_<ContainerAllocator>;

  explicit NavigatePose_GetResult_Response_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : result(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->status = 0;
    }
  }

  explicit NavigatePose_GetResult_Response_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : result(_alloc, _init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->status = 0;
    }
  }

  // field types and members
  using _status_type =
    int8_t;
  _status_type status;
  using _result_type =
    chaos_interfaces::action::NavigatePose_Result_<ContainerAllocator>;
  _result_type result;

  // setters for named parameter idiom
  Type & set__status(
    const int8_t & _arg)
  {
    this->status = _arg;
    return *this;
  }
  Type & set__result(
    const chaos_interfaces::action::NavigatePose_Result_<ContainerAllocator> & _arg)
  {
    this->result = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    chaos_interfaces::action::NavigatePose_GetResult_Response_<ContainerAllocator> *;
  using ConstRawPtr =
    const chaos_interfaces::action::NavigatePose_GetResult_Response_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_GetResult_Response_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_GetResult_Response_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_GetResult_Response_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_GetResult_Response_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_GetResult_Response_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_GetResult_Response_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_GetResult_Response_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_GetResult_Response_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_GetResult_Response
    std::shared_ptr<chaos_interfaces::action::NavigatePose_GetResult_Response_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_GetResult_Response
    std::shared_ptr<chaos_interfaces::action::NavigatePose_GetResult_Response_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const NavigatePose_GetResult_Response_ & other) const
  {
    if (this->status != other.status) {
      return false;
    }
    if (this->result != other.result) {
      return false;
    }
    return true;
  }
  bool operator!=(const NavigatePose_GetResult_Response_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct NavigatePose_GetResult_Response_

// alias to use template instance with default allocator
using NavigatePose_GetResult_Response =
  chaos_interfaces::action::NavigatePose_GetResult_Response_<std::allocator<void>>;

// constant definitions

}  // namespace action

}  // namespace chaos_interfaces


// Include directives for member types
// Member 'info'
// already included above
// #include "service_msgs/msg/detail/service_event_info__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__chaos_interfaces__action__NavigatePose_GetResult_Event __attribute__((deprecated))
#else
# define DEPRECATED__chaos_interfaces__action__NavigatePose_GetResult_Event __declspec(deprecated)
#endif

namespace chaos_interfaces
{

namespace action
{

// message struct
template<class ContainerAllocator>
struct NavigatePose_GetResult_Event_
{
  using Type = NavigatePose_GetResult_Event_<ContainerAllocator>;

  explicit NavigatePose_GetResult_Event_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : info(_init)
  {
    (void)_init;
  }

  explicit NavigatePose_GetResult_Event_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : info(_alloc, _init)
  {
    (void)_init;
  }

  // field types and members
  using _info_type =
    service_msgs::msg::ServiceEventInfo_<ContainerAllocator>;
  _info_type info;
  using _request_type =
    rosidl_runtime_cpp::BoundedVector<chaos_interfaces::action::NavigatePose_GetResult_Request_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<chaos_interfaces::action::NavigatePose_GetResult_Request_<ContainerAllocator>>>;
  _request_type request;
  using _response_type =
    rosidl_runtime_cpp::BoundedVector<chaos_interfaces::action::NavigatePose_GetResult_Response_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<chaos_interfaces::action::NavigatePose_GetResult_Response_<ContainerAllocator>>>;
  _response_type response;

  // setters for named parameter idiom
  Type & set__info(
    const service_msgs::msg::ServiceEventInfo_<ContainerAllocator> & _arg)
  {
    this->info = _arg;
    return *this;
  }
  Type & set__request(
    const rosidl_runtime_cpp::BoundedVector<chaos_interfaces::action::NavigatePose_GetResult_Request_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<chaos_interfaces::action::NavigatePose_GetResult_Request_<ContainerAllocator>>> & _arg)
  {
    this->request = _arg;
    return *this;
  }
  Type & set__response(
    const rosidl_runtime_cpp::BoundedVector<chaos_interfaces::action::NavigatePose_GetResult_Response_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<chaos_interfaces::action::NavigatePose_GetResult_Response_<ContainerAllocator>>> & _arg)
  {
    this->response = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    chaos_interfaces::action::NavigatePose_GetResult_Event_<ContainerAllocator> *;
  using ConstRawPtr =
    const chaos_interfaces::action::NavigatePose_GetResult_Event_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_GetResult_Event_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_GetResult_Event_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_GetResult_Event_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_GetResult_Event_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_GetResult_Event_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_GetResult_Event_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_GetResult_Event_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_GetResult_Event_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_GetResult_Event
    std::shared_ptr<chaos_interfaces::action::NavigatePose_GetResult_Event_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_GetResult_Event
    std::shared_ptr<chaos_interfaces::action::NavigatePose_GetResult_Event_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const NavigatePose_GetResult_Event_ & other) const
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
  bool operator!=(const NavigatePose_GetResult_Event_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct NavigatePose_GetResult_Event_

// alias to use template instance with default allocator
using NavigatePose_GetResult_Event =
  chaos_interfaces::action::NavigatePose_GetResult_Event_<std::allocator<void>>;

// constant definitions

}  // namespace action

}  // namespace chaos_interfaces

namespace chaos_interfaces
{

namespace action
{

struct NavigatePose_GetResult
{
  using Request = chaos_interfaces::action::NavigatePose_GetResult_Request;
  using Response = chaos_interfaces::action::NavigatePose_GetResult_Response;
  using Event = chaos_interfaces::action::NavigatePose_GetResult_Event;
};

}  // namespace action

}  // namespace chaos_interfaces


// Include directives for member types
// Member 'goal_id'
// already included above
// #include "unique_identifier_msgs/msg/detail/uuid__struct.hpp"
// Member 'feedback'
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__chaos_interfaces__action__NavigatePose_FeedbackMessage __attribute__((deprecated))
#else
# define DEPRECATED__chaos_interfaces__action__NavigatePose_FeedbackMessage __declspec(deprecated)
#endif

namespace chaos_interfaces
{

namespace action
{

// message struct
template<class ContainerAllocator>
struct NavigatePose_FeedbackMessage_
{
  using Type = NavigatePose_FeedbackMessage_<ContainerAllocator>;

  explicit NavigatePose_FeedbackMessage_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : goal_id(_init),
    feedback(_init)
  {
    (void)_init;
  }

  explicit NavigatePose_FeedbackMessage_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : goal_id(_alloc, _init),
    feedback(_alloc, _init)
  {
    (void)_init;
  }

  // field types and members
  using _goal_id_type =
    unique_identifier_msgs::msg::UUID_<ContainerAllocator>;
  _goal_id_type goal_id;
  using _feedback_type =
    chaos_interfaces::action::NavigatePose_Feedback_<ContainerAllocator>;
  _feedback_type feedback;

  // setters for named parameter idiom
  Type & set__goal_id(
    const unique_identifier_msgs::msg::UUID_<ContainerAllocator> & _arg)
  {
    this->goal_id = _arg;
    return *this;
  }
  Type & set__feedback(
    const chaos_interfaces::action::NavigatePose_Feedback_<ContainerAllocator> & _arg)
  {
    this->feedback = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    chaos_interfaces::action::NavigatePose_FeedbackMessage_<ContainerAllocator> *;
  using ConstRawPtr =
    const chaos_interfaces::action::NavigatePose_FeedbackMessage_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_FeedbackMessage_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<chaos_interfaces::action::NavigatePose_FeedbackMessage_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_FeedbackMessage_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_FeedbackMessage_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      chaos_interfaces::action::NavigatePose_FeedbackMessage_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<chaos_interfaces::action::NavigatePose_FeedbackMessage_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_FeedbackMessage_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<chaos_interfaces::action::NavigatePose_FeedbackMessage_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_FeedbackMessage
    std::shared_ptr<chaos_interfaces::action::NavigatePose_FeedbackMessage_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__chaos_interfaces__action__NavigatePose_FeedbackMessage
    std::shared_ptr<chaos_interfaces::action::NavigatePose_FeedbackMessage_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const NavigatePose_FeedbackMessage_ & other) const
  {
    if (this->goal_id != other.goal_id) {
      return false;
    }
    if (this->feedback != other.feedback) {
      return false;
    }
    return true;
  }
  bool operator!=(const NavigatePose_FeedbackMessage_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct NavigatePose_FeedbackMessage_

// alias to use template instance with default allocator
using NavigatePose_FeedbackMessage =
  chaos_interfaces::action::NavigatePose_FeedbackMessage_<std::allocator<void>>;

// constant definitions

}  // namespace action

}  // namespace chaos_interfaces

#include "action_msgs/srv/cancel_goal.hpp"
#include "action_msgs/msg/goal_info.hpp"
#include "action_msgs/msg/goal_status_array.hpp"

namespace chaos_interfaces
{

namespace action
{

struct NavigatePose
{
  /// The goal message defined in the action definition.
  using Goal = chaos_interfaces::action::NavigatePose_Goal;
  /// The result message defined in the action definition.
  using Result = chaos_interfaces::action::NavigatePose_Result;
  /// The feedback message defined in the action definition.
  using Feedback = chaos_interfaces::action::NavigatePose_Feedback;

  struct Impl
  {
    /// The send_goal service using a wrapped version of the goal message as a request.
    using SendGoalService = chaos_interfaces::action::NavigatePose_SendGoal;
    /// The get_result service using a wrapped version of the result message as a response.
    using GetResultService = chaos_interfaces::action::NavigatePose_GetResult;
    /// The feedback message with generic fields which wraps the feedback message.
    using FeedbackMessage = chaos_interfaces::action::NavigatePose_FeedbackMessage;

    /// The generic service to cancel a goal.
    using CancelGoalService = action_msgs::srv::CancelGoal;
    /// The generic message for the status of a goal.
    using GoalStatusMessage = action_msgs::msg::GoalStatusArray;
  };
};

typedef struct NavigatePose NavigatePose;

}  // namespace action

}  // namespace chaos_interfaces

#endif  // CHAOS_INTERFACES__ACTION__DETAIL__NAVIGATE_POSE__STRUCT_HPP_
