// Native SDK contract tests: real mock ROS services and a real action server.
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include "chaos_interfaces/action/navigate_pose.hpp"
#include "chaos_interfaces/msg/velocity_command.hpp"
#include "chaos_interfaces/srv/control_lease.hpp"
#include "chaos_interfaces/srv/get_description.hpp"
#include "chaos_interfaces/srv/stop.hpp"

#include "chaos_sdk/action_slots.hpp"
#include "chaos_sdk/robot.hpp"

using namespace std::chrono_literals;

namespace
{

using NavigatePose = chaos_interfaces::action::NavigatePose;

void ensure_init()
{
  static std::once_flag flag;
  std::call_once(flag, []() {
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
  });
}

/// Compact description of a received target pose.
///
/// The mock server echoes the target, so a test can verify that the client
/// really sent the coordinates and the frame it was given.
std::string describe_target(const geometry_msgs::msg::PoseStamped & target)
{
  std::ostringstream out;
  out << "frame=" << target.header.frame_id
      << " x=" << target.pose.position.x
      << " y=" << target.pose.position.y
      << " z=" << target.pose.position.z
      << " qx=" << target.pose.orientation.x
      << " qy=" << target.pose.orientation.y
      << " qz=" << target.pose.orientation.z
      << " qw=" << target.pose.orientation.w;
  return out.str();
}

struct MockState
{
  std::atomic<bool> lease_success{true};
  std::atomic<bool> lease_stop_failed{false};
  std::atomic<bool> description_silent{false};
  std::atomic<bool> stop_silent{false};
  std::atomic<bool> lease_silent{false};
  std::atomic<int> action_mode{0};  // 0 succeed, 1 reject, 2 hang until canceled
  std::atomic<int> silent_goals{0};  // number of goals that never answer at once
  std::atomic<bool> slow_cancel{false};
  std::atomic<bool> publish_feedback{false};
  /// The goal finishes on its own even though a cancel request is pending.
  std::atomic<bool> succeed_despite_cancel{false};
  std::atomic<int> lease_calls{0};
};

class SdkTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    ensure_init();
    state_ = std::make_shared<MockState>();
    server_node_ = std::make_shared<rclcpp::Node>("mock_asset");
    client_node_ = std::make_shared<rclcpp::Node>("sdk_client");

    description_server_ = server_node_->create_service<chaos_interfaces::srv::GetDescription>(
      "/robots/test/description",
      [state = state_](
        const std::shared_ptr<chaos_interfaces::srv::GetDescription::Request>,
        std::shared_ptr<chaos_interfaces::srv::GetDescription::Response> response) {
        if (state->description_silent.load()) {
          std::this_thread::sleep_for(2s);
          return;
        }
        response->asset_id = "test";
        response->model = "mock";
        response->base_frame = "test/base";
        response->command_timeout_s = 0.2;
        response->capabilities = {"motion.velocity"};
      });

    lease_server_ = server_node_->create_service<chaos_interfaces::srv::ControlLease>(
      "/robots/test/control/lease",
      [state = state_](
        const std::shared_ptr<chaos_interfaces::srv::ControlLease::Request> request,
        std::shared_ptr<chaos_interfaces::srv::ControlLease::Response> response) {
        state->lease_calls.fetch_add(1);
        if (state->lease_silent.load()) {
          std::this_thread::sleep_for(1s);
          return;
        }
        if (request->operation == "release" && state->lease_stop_failed.load()) {
          response->success = false;
          response->code = "STOP_FAILED";
          response->message = "stop failed";
          response->token = "";
          return;
        }
        if (state->lease_success.load()) {
          response->success = true;
          response->code = "OK";
          response->token = "token-1";
          response->remaining_s = request->duration_s;
        } else {
          response->success = false;
          response->code = "BUSY";
          response->message = "busy";
        }
      });

    stop_server_ = server_node_->create_service<chaos_interfaces::srv::Stop>(
      "/robots/test/motion/stop",
      [state = state_](
        const std::shared_ptr<chaos_interfaces::srv::Stop::Request>,
        std::shared_ptr<chaos_interfaces::srv::Stop::Response> response) {
        if (state->stop_silent.load()) {
          std::this_thread::sleep_for(2s);
          return;
        }
        response->success = true;
        response->code = "OK";
      });

    action_group_ = server_node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
    action_server_ = rclcpp_action::create_server<NavigatePose>(
      server_node_, "/robots/test/navigation/to_pose",
      [state = state_](
        const rclcpp_action::GoalUUID &,
        std::shared_ptr<const NavigatePose::Goal>) {
        if (state->silent_goals.load() > 0) {
          // Simulate a server that does not answer the goal request.
          state->silent_goals.fetch_sub(1);
          std::this_thread::sleep_for(1s);
        }
        if (state->action_mode.load() == 1) {
          return rclcpp_action::GoalResponse::REJECT;
        }
        return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
      },
      [state = state_](const std::shared_ptr<rclcpp_action::ServerGoalHandle<NavigatePose>>) {
        // rclcpp_action calls the cancel callback unconditionally; an empty
        // callback would terminate the process.
        if (state->slow_cancel.load()) {
          // The server answers the cancel request far too late.
          std::this_thread::sleep_for(1s);
        }
        return rclcpp_action::CancelResponse::ACCEPT;
      },
      [state = state_](std::shared_ptr<rclcpp_action::ServerGoalHandle<NavigatePose>> handle) {
        if (state->action_mode.load() == 2) {
          while (rclcpp::ok() && !handle->is_canceling()) {
            std::this_thread::sleep_for(20ms);
          }
        }
        if (state->succeed_despite_cancel.load()) {
          // The goal finishes on its own while the cancel request is still
          // unanswered.
          std::this_thread::sleep_for(100ms);
        }
        if (state->publish_feedback.load()) {
          auto feedback = std::make_shared<NavigatePose::Feedback>();
          feedback->progress = 0.5f;
          handle->publish_feedback(feedback);
        }
        auto result = std::make_shared<NavigatePose::Result>();
        result->success = true;
        result->code = "OK";
        result->message = describe_target(handle->get_goal()->target);
        if (handle->is_canceling()) {
          handle->canceled(result);
        } else {
          handle->succeed(result);
        }
      },
      rcl_action_server_get_default_options(),
      action_group_);

    executor_ = std::make_shared<rclcpp::executors::MultiThreadedExecutor>();
    executor_->add_node(server_node_);
    executor_->add_node(client_node_);
    spin_ = std::thread([this]() {executor_->spin();});

    robot_ = std::make_shared<chaos_sdk::Robot>(client_node_, "test");
  }

  void TearDown() override
  {
    if (robot_) {
      robot_->close();
    }
    executor_->cancel();
    if (spin_.joinable()) {
      spin_.join();
    }
  }

  std::shared_ptr<MockState> state_;
  rclcpp::Node::SharedPtr server_node_;
  rclcpp::Node::SharedPtr client_node_;
  rclcpp::Service<chaos_interfaces::srv::GetDescription>::SharedPtr description_server_;
  rclcpp::Service<chaos_interfaces::srv::ControlLease>::SharedPtr lease_server_;
  rclcpp::Service<chaos_interfaces::srv::Stop>::SharedPtr stop_server_;
  rclcpp_action::Server<NavigatePose>::SharedPtr action_server_;
  rclcpp::CallbackGroup::SharedPtr action_group_;
  rclcpp::executors::MultiThreadedExecutor::SharedPtr executor_;
  std::thread spin_;
  std::shared_ptr<chaos_sdk::Robot> robot_;
};

TEST_F(SdkTest, describe_caches_frame_and_timeout)
{
  auto future = robot_->describe();
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
  const auto response = future.get();
  EXPECT_EQ("test/base", response.base_frame);
  EXPECT_EQ("test/base", robot_->base_frame());
  EXPECT_DOUBLE_EQ(0.2, robot_->command_timeout_s());
}

TEST_F(SdkTest, acquire_installs_token_and_failure_keeps_state)
{
  auto future = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
  EXPECT_TRUE(future.get().success);
  EXPECT_EQ("token-1", robot_->lease_token());
  EXPECT_EQ("operator", robot_->owner());

  state_->lease_success.store(false);
  auto failed = robot_->acquire("other", 5.0);
  ASSERT_EQ(std::future_status::ready, failed.wait_for(5s));
  EXPECT_FALSE(failed.get().success);
  // A failed acquire must not replace the live lease.
  EXPECT_EQ("token-1", robot_->lease_token());
}

TEST_F(SdkTest, release_stop_failure_revokes_token_but_rejection_keeps_it)
{
  auto future = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));

  state_->lease_stop_failed.store(true);
  auto stopped = robot_->release();
  ASSERT_EQ(std::future_status::ready, stopped.wait_for(5s));
  EXPECT_FALSE(stopped.get().success);
  EXPECT_EQ("", robot_->lease_token());
  EXPECT_EQ("", robot_->owner());
}

TEST_F(SdkTest, velocity_requires_token_and_frame)
{
  EXPECT_THROW(robot_->velocity(1.0), std::invalid_argument);
  auto future = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
  // No describe() yet: the base frame is unknown.
  EXPECT_THROW(robot_->velocity(1.0), std::invalid_argument);
  auto described = robot_->describe();
  ASSERT_EQ(std::future_status::ready, described.wait_for(5s));
  const std::string request_id = robot_->velocity(0.5, 0.0, 0.0, 0.1);
  EXPECT_FALSE(request_id.empty());
}

TEST_F(SdkTest, silent_service_fails_with_local_timeout)
{
  robot_->set_response_timeout(0.3);
  state_->lease_silent.store(true);
  auto future = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
  EXPECT_THROW(future.get(), chaos_sdk::TimeoutError);
  // The pending slot is released, so the client accepts work again.
  EXPECT_EQ(0u, robot_->pending_count());
}

TEST_F(SdkTest, action_accepts_reports_result_and_cancels)
{
  auto future = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));

  auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  auto task = goal.get();
  ASSERT_NE(nullptr, task);
  EXPECT_TRUE(task->accepted());
  EXPECT_FALSE(task->goal_id().empty());
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  const auto wrapped = task->result().get();
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, wrapped.code);
  EXPECT_EQ(chaos_sdk::TaskState::SUCCEEDED, task->state());
}

TEST_F(SdkTest, rejected_action_resolves_without_task)
{
  auto future = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
  state_->action_mode.store(1);
  auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  EXPECT_EQ(nullptr, goal.get());
}

TEST_F(SdkTest, hanging_action_can_be_canceled_through_the_task)
{
  auto future = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
  state_->action_mode.store(2);
  auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  auto task = goal.get();
  ASSERT_NE(nullptr, task);
  ASSERT_TRUE(task->accepted());
  auto canceled = task->cancel();
  ASSERT_EQ(std::future_status::ready, canceled.wait_for(5s));
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  EXPECT_EQ(rclcpp_action::ResultCode::CANCELED, task->result().get().code);
}

TEST_F(SdkTest, close_resolves_pending_and_rejects_later_calls)
{
  robot_->set_response_timeout(30.0);
  state_->lease_silent.store(true);
  auto future = robot_->acquire("operator", 5.0);
  std::this_thread::sleep_for(200ms);
  robot_->close();
  EXPECT_THROW(future.get(), chaos_sdk::ClientClosedError);
  EXPECT_THROW(robot_->describe(), chaos_sdk::ClientClosedError);
  robot_->close();  // idempotent
}

TEST_F(SdkTest, pending_requests_are_bounded)
{
  robot_->set_max_pending(1);
  robot_->set_response_timeout(30.0);
  state_->lease_silent.store(true);
  auto first = robot_->acquire("operator", 5.0);
  EXPECT_THROW(robot_->acquire("operator", 5.0), chaos_sdk::PendingOverflowError);
  robot_->close();
  EXPECT_THROW(first.get(), chaos_sdk::ClientClosedError);
}

TEST_F(SdkTest, sequential_actions_do_not_exhaust_the_pending_budget)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  // More goals than the pending budget of 64: every completed goal must
  // release its slot, otherwise the budget would be exhausted.
  for (int index = 0; index < 70; ++index) {
    auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
    ASSERT_EQ(std::future_status::ready, goal.wait_for(5s)) << "goal " << index;
    auto task = goal.get();
    ASSERT_NE(nullptr, task) << "goal " << index;
    ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s)) << "goal " << index;
    EXPECT_EQ(chaos_sdk::TaskState::SUCCEEDED, task->state()) << "goal " << index;
  }
  EXPECT_EQ(0u, robot_->pending_count());
}

TEST_F(SdkTest, late_goal_response_does_not_resurrect_the_task)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  robot_->set_response_timeout(0.3);
  state_->silent_goals.store(1);
  auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  EXPECT_THROW(goal.get(), chaos_sdk::TimeoutError);
  // The late goal response must not create a task and must not keep the
  // pending budget occupied.
  std::this_thread::sleep_for(1500ms);
  EXPECT_EQ(0u, robot_->pending_count());
}

TEST_F(SdkTest, a_silent_goal_does_not_block_other_actions)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  state_->silent_goals.store(1);
  auto silent = robot_->navigate(geometry_msgs::msg::PoseStamped());
  auto healthy = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, healthy.wait_for(5s));
  auto task = healthy.get();
  ASSERT_NE(nullptr, task);
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, task->result().get().code);
  // The silent goal is still observed, but it did not disturb the other one.
  EXPECT_EQ(1u, robot_->pending_count());
  EXPECT_EQ(std::future_status::timeout, silent.wait_for(200ms));
}

TEST_F(SdkTest, close_during_send_is_safe)
{
  std::atomic<bool> stop{false};
  std::atomic<int> failures{0};
  std::thread sender([this, &stop, &failures]() {
      while (!stop.load()) {
        try {
          auto future = robot_->acquire("operator", 5.0);
          std::future_status status = future.wait_for(std::chrono::milliseconds(5));
          if (status == std::future_status::ready) {
            try {
              future.get();
            } catch (const chaos_sdk::ClientClosedError &) {
              failures.fetch_add(1);
              return;
            } catch (const std::exception &) {
            }
          }
        } catch (const chaos_sdk::ClientClosedError &) {
          failures.fetch_add(1);
          return;
        } catch (const std::exception &) {
        }
      }
    });
  std::this_thread::sleep_for(50ms);
  robot_->close();
  stop.store(true);
  sender.join();
  EXPECT_GT(failures.load(), 0);
  EXPECT_THROW(robot_->describe(), chaos_sdk::ClientClosedError);
}

TEST_F(SdkTest, feedback_callback_exception_does_not_kill_the_executor)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  state_->publish_feedback.store(true);
  std::atomic<int> calls{0};
  auto accepted = robot_->navigate(
    geometry_msgs::msg::PoseStamped(),
    [&calls](NavigatePose::Feedback feedback) {
      calls.fetch_add(1);
      throw std::runtime_error("user callback failure");
    });
  ASSERT_EQ(std::future_status::ready, accepted.wait_for(5s));
  auto task = accepted.get();
  ASSERT_NE(nullptr, task);
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  // The executor is still alive: a following request still works.
  auto described = robot_->describe();
  EXPECT_EQ(std::future_status::ready, described.wait_for(5s));
}

TEST_F(SdkTest, a_successful_result_is_reported_as_succeeded)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  auto task = goal.get();
  ASSERT_NE(nullptr, task);
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  const auto wrapped = task->result().get();
  ASSERT_NE(nullptr, wrapped.result);
  // The mock server answers with success == true; the SDK must not claim a
  // business failure as a success and vice versa.
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, wrapped.code);
  EXPECT_EQ(chaos_sdk::TaskState::SUCCEEDED, task->state());
}

TEST(TaskStatePolicy, a_terminal_status_without_a_result_is_never_a_success)
{
  // The payload carries the business outcome. A terminal status without a
  // payload therefore never proves success, not even with success == true.
  EXPECT_EQ(
    chaos_sdk::TaskState::UNKNOWN,
    chaos_sdk::classify_result(rclcpp_action::ResultCode::SUCCEEDED, false, true));
  EXPECT_EQ(
    chaos_sdk::TaskState::UNKNOWN,
    chaos_sdk::classify_result(rclcpp_action::ResultCode::SUCCEEDED, false, false));
  EXPECT_EQ(
    chaos_sdk::TaskState::UNKNOWN,
    chaos_sdk::classify_result(rclcpp_action::ResultCode::ABORTED, false, true));
  // With a payload the classification follows the status and the business
  // outcome.
  EXPECT_EQ(
    chaos_sdk::TaskState::SUCCEEDED,
    chaos_sdk::classify_result(rclcpp_action::ResultCode::SUCCEEDED, true, true));
  EXPECT_EQ(
    chaos_sdk::TaskState::FAILED,
    chaos_sdk::classify_result(rclcpp_action::ResultCode::SUCCEEDED, true, false));
  EXPECT_EQ(
    chaos_sdk::TaskState::CANCELED,
    chaos_sdk::classify_result(rclcpp_action::ResultCode::CANCELED, true, true));
  EXPECT_EQ(
    chaos_sdk::TaskState::UNKNOWN,
    chaos_sdk::classify_result(rclcpp_action::ResultCode::UNKNOWN, true, true));
  EXPECT_EQ(
    chaos_sdk::TaskState::FAILED,
    chaos_sdk::classify_result(rclcpp_action::ResultCode::ABORTED, true, false));
}

TEST_F(SdkTest, the_acceptance_and_the_goal_id_survive_the_release_of_the_handle)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  auto task = goal.get();
  ASSERT_NE(nullptr, task);
  ASSERT_TRUE(task->accepted());
  const std::string goal_id = task->goal_id();
  ASSERT_FALSE(goal_id.empty());
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  // The ROS resources of the goal are released with the terminal state, but
  // the readable history must survive that cleanup.
  EXPECT_EQ(chaos_sdk::TaskState::SUCCEEDED, task->state());
  EXPECT_TRUE(task->accepted());
  EXPECT_EQ(goal_id, task->goal_id());
}

TEST_F(SdkTest, the_target_pose_reaches_the_server_unchanged)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  geometry_msgs::msg::PoseStamped target;
  target.header.frame_id = "test/map";
  target.pose.position.x = 1.5;
  target.pose.position.y = -2.5;
  target.pose.position.z = 0.25;
  target.pose.orientation.w = 1.0;
  auto goal = robot_->navigate(target);
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  auto task = goal.get();
  ASSERT_NE(nullptr, task);
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  const auto wrapped = task->result().get();
  ASSERT_NE(nullptr, wrapped.result);
  EXPECT_EQ("frame=test/map x=1.5 y=-2.5 z=0.25 qx=0 qy=0 qz=0 qw=1", wrapped.result->message);
}

TEST_F(SdkTest, a_cancel_timeout_does_not_overwrite_a_completed_goal)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  robot_->set_response_timeout(0.3);
  robot_->set_action_timeout(5.0);
  // The goal finishes on its own while the cancel request stays unanswered.
  state_->succeed_despite_cancel.store(true);
  state_->slow_cancel.store(true);
  auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  auto task = goal.get();
  ASSERT_NE(nullptr, task);
  ASSERT_TRUE(task->accepted());

  auto canceled = task->cancel();
  ASSERT_EQ(std::future_status::ready, canceled.wait_for(5s));
  // The server answers the cancel request far too late.
  EXPECT_THROW(canceled.get(), chaos_sdk::TimeoutError);

  // The goal reached its terminal state before the cancel timed out. The
  // unanswered cancel must not overwrite that terminal state and must not
  // fail the result of a goal that already finished.
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  const auto wrapped = task->result().get();
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, wrapped.code);
  EXPECT_EQ(chaos_sdk::TaskState::SUCCEEDED, task->state());
  EXPECT_EQ(0u, robot_->pending_count());
}

TEST_F(SdkTest, a_retired_slot_releases_its_ros_client)
{
  // The pool is the unit that owns the ROS clients of the goals. The weak
  // reference proves that a retired slot really destroys its client instead
  // of only counting the slots.
  //
  // The node of the pool is deliberately not spun: the goal request stays
  // unanswered and the client is owned by the pool alone, so the weak
  // reference shows exactly what the pool releases.
  auto pool_node = std::make_shared<rclcpp::Node>("pool_probe");
  chaos_sdk::ActionSlotPool<NavigatePose> pool(2);
  auto lease = pool.acquire(pool_node, "/robots/test/navigation/to_pose");
  auto weak_client = pool.weak_client(lease.index);
  ASSERT_FALSE(weak_client.expired());

  // The goal request stays unanswered while the slot is retired.
  NavigatePose::Goal goal;
  goal.lease_token = "token-1";
  auto answered = std::make_shared<std::atomic<bool>>(false);
  using Client = rclcpp_action::Client<NavigatePose>;
  Client::SendGoalOptions options;
  options.goal_response_callback =
    [answered](Client::GoalHandle::SharedPtr) {answered->store(true);};
  {
    // Neither the lease nor the local reference may keep the client alive:
    // the pool owns it.
    auto client = lease.client;
    lease.client.reset();
    client->async_send_goal(goal, options);
  }
  pool.release(lease.index, lease.generation, true);
  // The client of the retired slot is gone: the weak reference expired.
  EXPECT_TRUE(weak_client.expired());
  EXPECT_EQ(0u, pool.live_clients());

  // The late goal response must not resurrect the client.
  std::this_thread::sleep_for(1500ms);
  EXPECT_TRUE(weak_client.expired());
  EXPECT_FALSE(answered->load());
}

TEST_F(SdkTest, more_goals_than_the_pool_capacity_release_every_client)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  robot_->set_response_timeout(0.3);
  robot_->set_action_timeout(1.0);
  // More goals than the pool capacity of eight: every timed out goal must
  // retire its slot and release its ROS client, otherwise the pool would be
  // exhausted and the next goal would be refused.
  state_->silent_goals.store(10);
  for (int index = 0; index < 10; ++index) {
    auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
    ASSERT_EQ(std::future_status::ready, goal.wait_for(5s)) << "goal " << index;
    EXPECT_THROW(goal.get(), chaos_sdk::TimeoutError) << "goal " << index;
  }
  // The timed out observations are gone and their clients were released.
  EXPECT_EQ(0u, robot_->pending_count());
  EXPECT_EQ(0u, robot_->action_client_count());
  EXPECT_LE(robot_->action_slot_count(), 8u);
  // A healthy goal still works after the retired slots.
  state_->silent_goals.store(0);
  auto healthy = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, healthy.wait_for(5s));
  auto task = healthy.get();
  ASSERT_NE(nullptr, task);
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, task->result().get().code);
}

TEST_F(SdkTest, silent_goals_release_their_clients_and_keep_the_pool_bounded)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  robot_->set_response_timeout(0.3);
  robot_->set_action_timeout(1.0);
  // Six goals that the server never answers at once: every one of them must
  // time out, release its slot and retire its ROS client.
  state_->silent_goals.store(6);
  std::vector<std::shared_future<std::shared_ptr<chaos_sdk::Robot::NavigationTask>>> goals;
  for (int index = 0; index < 6; ++index) {
    goals.push_back(robot_->navigate(geometry_msgs::msg::PoseStamped()));
  }
  const std::size_t peak_clients = robot_->action_client_count();
  EXPECT_LE(peak_clients, 8u);
  for (auto & goal : goals) {
    ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
    EXPECT_THROW(goal.get(), chaos_sdk::TimeoutError);
  }
  // The timed out observations are gone and their clients were released.
  EXPECT_EQ(0u, robot_->pending_count());
  EXPECT_LE(robot_->action_client_count(), peak_clients);
  EXPECT_LE(robot_->action_slot_count(), 8u);
  // A healthy goal still works after the retired slots.
  state_->silent_goals.store(0);
  auto healthy = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, healthy.wait_for(5s));
  auto task = healthy.get();
  ASSERT_NE(nullptr, task);
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, task->result().get().code);
}

TEST_F(SdkTest, a_late_service_response_does_not_restore_the_lease_after_close)
{
  robot_->set_response_timeout(0.3);
  state_->lease_silent.store(true);
  auto future = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
  EXPECT_THROW(future.get(), chaos_sdk::TimeoutError);
  EXPECT_EQ("", robot_->lease_token());
  robot_->close();
  // The server answers after the timeout and after close(): the claim fails,
  // so the cached lease state stays untouched.
  std::this_thread::sleep_for(1200ms);
  EXPECT_EQ("", robot_->lease_token());
  EXPECT_EQ("", robot_->owner());
}

TEST_F(SdkTest, a_cancel_that_is_never_answered_stops_the_goal_observation)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  robot_->set_response_timeout(0.3);
  robot_->set_action_timeout(5.0);
  state_->action_mode.store(2);  // the goal never finishes
  state_->slow_cancel.store(true);
  auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  auto task = goal.get();
  ASSERT_NE(nullptr, task);
  ASSERT_TRUE(task->accepted());
  auto canceled = task->cancel();
  ASSERT_EQ(std::future_status::ready, canceled.wait_for(5s));
  // The cancel request was not answered in time.
  EXPECT_THROW(canceled.get(), chaos_sdk::TimeoutError);
  // The observation of the goal itself stopped and everything is settled.
  EXPECT_EQ(std::future_status::ready, task->result().wait_for(5s));
  EXPECT_THROW(task->result().get(), chaos_sdk::TimeoutError);
  EXPECT_EQ(0u, robot_->pending_count());
}

}  // namespace
