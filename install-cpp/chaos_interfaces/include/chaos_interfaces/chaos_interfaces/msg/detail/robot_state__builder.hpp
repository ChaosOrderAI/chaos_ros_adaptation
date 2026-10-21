// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from chaos_interfaces:msg/RobotState.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/msg/robot_state.hpp"


#ifndef CHAOS_INTERFACES__MSG__DETAIL__ROBOT_STATE__BUILDER_HPP_
#define CHAOS_INTERFACES__MSG__DETAIL__ROBOT_STATE__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "chaos_interfaces/msg/detail/robot_state__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace chaos_interfaces
{

namespace msg
{

namespace builder
{

class Init_RobotState_detail
{
public:
  explicit Init_RobotState_detail(::chaos_interfaces::msg::RobotState & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::msg::RobotState detail(::chaos_interfaces::msg::RobotState::_detail_type arg)
  {
    msg_.detail = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::msg::RobotState msg_;
};

class Init_RobotState_battery_percentage
{
public:
  explicit Init_RobotState_battery_percentage(::chaos_interfaces::msg::RobotState & msg)
  : msg_(msg)
  {}
  Init_RobotState_detail battery_percentage(::chaos_interfaces::msg::RobotState::_battery_percentage_type arg)
  {
    msg_.battery_percentage = std::move(arg);
    return Init_RobotState_detail(msg_);
  }

private:
  ::chaos_interfaces::msg::RobotState msg_;
};

class Init_RobotState_twist
{
public:
  explicit Init_RobotState_twist(::chaos_interfaces::msg::RobotState & msg)
  : msg_(msg)
  {}
  Init_RobotState_battery_percentage twist(::chaos_interfaces::msg::RobotState::_twist_type arg)
  {
    msg_.twist = std::move(arg);
    return Init_RobotState_battery_percentage(msg_);
  }

private:
  ::chaos_interfaces::msg::RobotState msg_;
};

class Init_RobotState_pose
{
public:
  explicit Init_RobotState_pose(::chaos_interfaces::msg::RobotState & msg)
  : msg_(msg)
  {}
  Init_RobotState_twist pose(::chaos_interfaces::msg::RobotState::_pose_type arg)
  {
    msg_.pose = std::move(arg);
    return Init_RobotState_twist(msg_);
  }

private:
  ::chaos_interfaces::msg::RobotState msg_;
};

class Init_RobotState_pose_valid
{
public:
  explicit Init_RobotState_pose_valid(::chaos_interfaces::msg::RobotState & msg)
  : msg_(msg)
  {}
  Init_RobotState_pose pose_valid(::chaos_interfaces::msg::RobotState::_pose_valid_type arg)
  {
    msg_.pose_valid = std::move(arg);
    return Init_RobotState_pose(msg_);
  }

private:
  ::chaos_interfaces::msg::RobotState msg_;
};

class Init_RobotState_ready
{
public:
  explicit Init_RobotState_ready(::chaos_interfaces::msg::RobotState & msg)
  : msg_(msg)
  {}
  Init_RobotState_pose_valid ready(::chaos_interfaces::msg::RobotState::_ready_type arg)
  {
    msg_.ready = std::move(arg);
    return Init_RobotState_pose_valid(msg_);
  }

private:
  ::chaos_interfaces::msg::RobotState msg_;
};

class Init_RobotState_connected
{
public:
  explicit Init_RobotState_connected(::chaos_interfaces::msg::RobotState & msg)
  : msg_(msg)
  {}
  Init_RobotState_ready connected(::chaos_interfaces::msg::RobotState::_connected_type arg)
  {
    msg_.connected = std::move(arg);
    return Init_RobotState_ready(msg_);
  }

private:
  ::chaos_interfaces::msg::RobotState msg_;
};

class Init_RobotState_operational_state
{
public:
  explicit Init_RobotState_operational_state(::chaos_interfaces::msg::RobotState & msg)
  : msg_(msg)
  {}
  Init_RobotState_connected operational_state(::chaos_interfaces::msg::RobotState::_operational_state_type arg)
  {
    msg_.operational_state = std::move(arg);
    return Init_RobotState_connected(msg_);
  }

private:
  ::chaos_interfaces::msg::RobotState msg_;
};

class Init_RobotState_model
{
public:
  explicit Init_RobotState_model(::chaos_interfaces::msg::RobotState & msg)
  : msg_(msg)
  {}
  Init_RobotState_operational_state model(::chaos_interfaces::msg::RobotState::_model_type arg)
  {
    msg_.model = std::move(arg);
    return Init_RobotState_operational_state(msg_);
  }

private:
  ::chaos_interfaces::msg::RobotState msg_;
};

class Init_RobotState_asset_id
{
public:
  explicit Init_RobotState_asset_id(::chaos_interfaces::msg::RobotState & msg)
  : msg_(msg)
  {}
  Init_RobotState_model asset_id(::chaos_interfaces::msg::RobotState::_asset_id_type arg)
  {
    msg_.asset_id = std::move(arg);
    return Init_RobotState_model(msg_);
  }

private:
  ::chaos_interfaces::msg::RobotState msg_;
};

class Init_RobotState_stamp
{
public:
  Init_RobotState_stamp()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_RobotState_asset_id stamp(::chaos_interfaces::msg::RobotState::_stamp_type arg)
  {
    msg_.stamp = std::move(arg);
    return Init_RobotState_asset_id(msg_);
  }

private:
  ::chaos_interfaces::msg::RobotState msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::msg::RobotState>()
{
  return chaos_interfaces::msg::builder::Init_RobotState_stamp();
}

}  // namespace chaos_interfaces

#endif  // CHAOS_INTERFACES__MSG__DETAIL__ROBOT_STATE__BUILDER_HPP_
