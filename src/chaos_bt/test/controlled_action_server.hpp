// A real ROS action server whose acceptance, feedback and result the test
// releases by hand. The server runs on its own executor, so the tree can tick
// without spinning.
#pragma once

#include <atomic>
#include <chrono>
#include <type_traits>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include "chaos_interfaces/action/flight.hpp"
#include "chaos_interfaces/action/navigate_pose.hpp"
#include "chaos_interfaces/srv/control_lease.hpp"

namespace chaos_bt
{
namespace test
{

template<typename ActionT>
class ControlledAction
{
public:
  using GoalHandle = rclcpp_action::ServerGoalHandle<ActionT>;

  void accept(std::shared_ptr<GoalHandle> handle)
  {
    {
      const std::lock_guard<std::mutex> guard(mutex_);
      handle_ = std::move(handle);
      accepted_ = true;
    }
    changed_.notify_all();
  }

  bool wait_accepted(std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, timeout, [this]() {return accepted_;});
  }

  bool wait_canceling(std::chrono::milliseconds timeout)
  {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      auto handle = this->handle();
      if (handle && handle->is_canceling()) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
  }

  std::shared_ptr<GoalHandle> handle() const
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    return handle_;
  }

  void publish(const typename ActionT::Feedback & feedback)
  {
    auto handle = this->handle();
    if (handle) {
      handle->publish_feedback(std::make_shared<typename ActionT::Feedback>(feedback));
    }
  }

  void finish(bool success, const std::string & code, const std::string & message)
  {
    auto handle = this->handle();
    if (!handle) {
      return;
    }
    auto result = std::make_shared<typename ActionT::Result>();
    result->success = success;
    result->code = code;
    result->message = message;
    if (handle->is_canceling()) {
      handle->canceled(result);
    } else if (success) {
      handle->succeed(result);
    } else {
      handle->abort(result);
    }
    // The handle keeps the result. A second finish would ask rcl for a
    // transition the goal already left.
    {
      const std::lock_guard<std::mutex> guard(mutex_);
      handle_.reset();
    }
    release();
  }

  void release()
  {
    {
      const std::lock_guard<std::mutex> guard(mutex_);
      released_ = true;
    }
    changed_.notify_all();
  }

  bool await_release(std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, timeout, [this]() {return released_;});
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::shared_ptr<GoalHandle> handle_;
  bool accepted_ = false;
  bool released_ = false;
};

class ActionServer
{
public:
  ActionServer()
  {
    node_ = std::make_shared<rclcpp::Node>("chaos_bt_mock");
    group_ = node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
    lease_ = node_->create_service<chaos_interfaces::srv::ControlLease>(
      "/robots/test/control/lease",
      [this](
        const std::shared_ptr<chaos_interfaces::srv::ControlLease::Request>,
        std::shared_ptr<chaos_interfaces::srv::ControlLease::Response> response) {
        ++lease_calls_;
        response->success = lease_success_;
        response->code = lease_success_ ? "OK" : "NO_LEASE";
        response->token = lease_success_ ? "token-1" : "";
      },
      rclcpp::ServicesQoS(), group_);
    navigation_ = make_server<chaos_interfaces::action::NavigatePose>(
      "/robots/test/navigation/to_pose");
    takeoff_ = make_server<chaos_interfaces::action::Flight>("/robots/test/flight/takeoff");
    land_ = make_server<chaos_interfaces::action::Flight>("/robots/test/flight/land");
    executor_.add_node(node_);
    spin_ = std::thread([this]() {executor_.spin();});
  }

  ~ActionServer()
  {
    release_all();
    executor_.cancel();
    if (spin_.joinable()) {
      spin_.join();
    }
  }

  rclcpp::Node::SharedPtr node() const {return node_;}
  int lease_calls() const {return lease_calls_;}
  int goals() const {return goals_.load();}
  void set_lease_success(bool success) {lease_success_ = success;}
  void set_reject(bool reject) {reject_ = reject;}
  void set_hold_acceptance(bool hold) {hold_acceptance_ = hold;}
  void set_cancel_accept(bool accept) {cancel_accept_ = accept;}

  template<typename ActionT>
  std::shared_ptr<ControlledAction<ActionT>> install()
  {
    auto action = std::make_shared<ControlledAction<ActionT>>();
    if constexpr (std::is_same_v<ActionT, chaos_interfaces::action::NavigatePose>) {
      navigation_action_ = action;
    } else {
      flight_action_ = action;
    }
    return action;
  }

  void release_all()
  {
    hold_acceptance_ = false;
    if (navigation_action_) {
      navigation_action_->release();
    }
    if (flight_action_) {
      flight_action_->release();
    }
  }

private:
  template<typename ActionT>
  typename rclcpp_action::Server<ActionT>::SharedPtr make_server(const std::string & name)
  {
    return rclcpp_action::create_server<ActionT>(
      node_, name,
      [this](const rclcpp_action::GoalUUID &, std::shared_ptr<const typename ActionT::Goal>) {
        ++goals_;
        if (hold_acceptance_) {
          std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
        return reject_ ? rclcpp_action::GoalResponse::REJECT :
               rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
      },
      [this](const std::shared_ptr<rclcpp_action::ServerGoalHandle<ActionT>>) {
        return cancel_accept_ ? rclcpp_action::CancelResponse::ACCEPT :
               rclcpp_action::CancelResponse::REJECT;
      },
      [this](std::shared_ptr<rclcpp_action::ServerGoalHandle<ActionT>> handle) {
        std::shared_ptr<ControlledAction<ActionT>> controlled;
        if constexpr (std::is_same_v<ActionT, chaos_interfaces::action::NavigatePose>) {
          controlled = current_navigation();
        } else {
          controlled = current_flight();
        }
        if (!controlled) {
          return;
        }
        controlled->accept(handle);
        controlled->await_release(std::chrono::seconds(30));
      },
      rcl_action_server_get_default_options(), group_);
  }

  std::shared_ptr<ControlledAction<chaos_interfaces::action::NavigatePose>>
  current_navigation() const
  {
    return navigation_action_;
  }

  std::shared_ptr<ControlledAction<chaos_interfaces::action::Flight>> current_flight() const
  {
    return flight_action_;
  }

  rclcpp::Node::SharedPtr node_;
  rclcpp::CallbackGroup::SharedPtr group_;
  rclcpp::Service<chaos_interfaces::srv::ControlLease>::SharedPtr lease_;
  typename rclcpp_action::Server<chaos_interfaces::action::NavigatePose>::SharedPtr navigation_;
  typename rclcpp_action::Server<chaos_interfaces::action::Flight>::SharedPtr takeoff_;
  typename rclcpp_action::Server<chaos_interfaces::action::Flight>::SharedPtr land_;
  std::shared_ptr<ControlledAction<chaos_interfaces::action::NavigatePose>> navigation_action_;
  std::shared_ptr<ControlledAction<chaos_interfaces::action::Flight>> flight_action_;
  rclcpp::executors::MultiThreadedExecutor executor_{rclcpp::ExecutorOptions(), 4};
  std::thread spin_;
  std::atomic<int> lease_calls_{0};
  std::atomic<int> goals_{0};
  std::atomic<bool> lease_success_{true};
  std::atomic<bool> reject_{false};
  std::atomic<bool> hold_acceptance_{false};
  std::atomic<bool> cancel_accept_{true};
};

}  // namespace test
}  // namespace chaos_bt
