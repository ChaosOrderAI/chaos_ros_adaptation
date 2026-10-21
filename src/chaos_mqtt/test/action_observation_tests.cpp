// Narrow regression tests for the bounded action observation layer.
//
// These tests drive `ActionObserver` / `ActionSlotPool` directly, without the
// gateway node. They prove the two ownership rules that the pool counters
// alone cannot show:
//
//  1. A retired observation really releases its dedicated client. The client
//     of a goal that was accepted but never answered still owns a pending
//     result request; the observation has to destroy that client, otherwise
//     the middleware bookkeeping of the abandoned goal survives.
//  2. A deferred dispatch that expired before its action server appeared is
//     never sent - not even when the server shows up afterwards.
//
// The executor is driven explicitly by the test thread and is quiesced
// before any release happens, so the assertions cannot race with a callback
// of the client under test.
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include "chaos_interfaces/action/navigate_pose.hpp"
#include "chaos_mqtt/action_observer.hpp"

namespace
{

using namespace std::chrono_literals;
using chaos_mqtt::ActionObserver;
using chaos_mqtt::ActionOutcome;
using chaos_mqtt::ActionSink;
using chaos_mqtt::ActionSlotPool;
using NavigatePose = chaos_interfaces::action::NavigatePose;

/// Records the lifecycle events of the observation layer.
class RecordingSink : public ActionSink
{
public:
  void on_action_accepted(const std::string & request_id, const std::string & goal_id_hex) override
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    accepted_.push_back(request_id);
  }

  void on_action_rejected(const std::string & request_id) override
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    rejected_.push_back(request_id);
  }

  void on_action_result(const std::string & request_id, const ActionOutcome & outcome) override
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    results_.push_back(request_id);
  }

  bool has_accepted(const std::string & request_id)
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    for (const auto & accepted : accepted_) {
      if (accepted == request_id) {
        return true;
      }
    }
    return false;
  }

private:
  std::mutex mutex_;
  std::vector<std::string> accepted_;
  std::vector<std::string> rejected_;
  std::vector<std::string> results_;
};

/// Spin the executor until the predicate holds. The executor is only driven
/// from this thread, so no callback can run concurrently with the test.
bool spin_until(
  rclcpp::executors::SingleThreadedExecutor & executor, const std::function<bool()> & predicate,
  std::chrono::seconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    executor.spin_some(std::chrono::milliseconds(20));
    if (predicate()) {
      return true;
    }
  }
  return predicate();
}

class ActionObservation : public ::testing::Test
{
protected:
  void SetUp() override
  {
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
    node_ = std::make_shared<rclcpp::Node>("observation_node");
    executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    executor_->add_node(node_);
  }

  void TearDown() override
  {
    if (node_) {
      executor_->remove_node(node_);
      node_.reset();
    }
    executor_.reset();
  }

  std::shared_ptr<rclcpp::Node> node_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
};

/// A retired observation of a goal that was accepted but never answered must
/// release its dedicated client, and with it the pending result request.
TEST_F(ActionObservation, RetiredNeverAnsweredGoalReleasesItsClient)
{
  // The server accepts the goal and keeps it forever. The handle is retained:
  // a released handle would abort the goal and hand an empty default result
  // to the client.
  std::vector<std::shared_ptr<rclcpp_action::ServerGoalHandle<NavigatePose>>> retained;
  auto server = rclcpp_action::create_server<NavigatePose>(
    node_, "silent/to_pose",
    [](const rclcpp_action::GoalUUID &, std::shared_ptr<const NavigatePose::Goal>) {
      return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
    },
    [](const std::shared_ptr<rclcpp_action::ServerGoalHandle<NavigatePose>>) {
      return rclcpp_action::CancelResponse::ACCEPT;
    },
    [&retained](const std::shared_ptr<rclcpp_action::ServerGoalHandle<NavigatePose>> handle) {
      retained.push_back(handle);
    });

  RecordingSink sink;
  ActionSlotPool<NavigatePose> pool(
    node_.get(), "silent/to_pose", "silent.to_pose", &sink, 4);
  auto observer = pool.acquire("req_never_answered");
  ASSERT_NE(observer, nullptr);

  // The weak handle is the only view on the client that survives the
  // observation; the observation itself is the sole owner.
  std::weak_ptr<rclcpp_action::Client<NavigatePose>> weak_client = observer->weak_client();
  ASSERT_FALSE(weak_client.expired());

  NavigatePose::Goal goal;
  goal.target.pose.position.x = 1.0;
  observer->set_goal(std::move(goal));
  // The client may need a moment until the action server is discovered.
  ASSERT_TRUE(spin_until(
    *executor_, [observer]() {return observer->send_if_ready();}, 10s));
  ASSERT_TRUE(spin_until(*executor_, [&sink]() {return sink.has_accepted("req_never_answered");},
    10s));

  // The goal is accepted and never answered: the client owns a pending result
  // request now.
  EXPECT_EQ(observer->state(), ActionObserver<NavigatePose>::State::kAccepted);
  EXPECT_FALSE(weak_client.expired());

  // The executor is not spinning any more, so no callback of the client can
  // be on the stack while the client is released.
  pool.release("req_never_answered");
  EXPECT_EQ(pool.in_flight(), 0u);
  EXPECT_TRUE(weak_client.expired()) << "the retired client was not released";
  EXPECT_TRUE(observer->weak_client().expired());

  // The local strong reference of the observer must not keep the client
  // alive either.
  observer.reset();
  EXPECT_TRUE(weak_client.expired());
}

/// Several deferred dispatches that expire before their action server shows up
/// must never reach the robot, not even when the server appears afterwards.
TEST_F(ActionObservation, DeferredGoalsExpireWithoutDispatching)
{
  RecordingSink sink;
  // There is no action server for this action yet, so every dispatch is
  // deferred.
  ActionSlotPool<NavigatePose> pool(
    node_.get(), "late/to_pose", "late.to_pose", &sink, 8);
  constexpr int kGoals = 3;
  for (int i = 0; i < kGoals; ++i) {
    const std::string request_id = "deferred_" + std::to_string(i);
    const auto observer = pool.acquire(request_id);
    ASSERT_NE(observer, nullptr);
    NavigatePose::Goal goal;
    goal.target.pose.position.x = static_cast<double>(i);
    observer->set_goal(std::move(goal));
    ASSERT_FALSE(observer->send_if_ready());
    pool.defer(request_id);
  }
  EXPECT_EQ(pool.deferred_count(), static_cast<std::size_t>(kGoals));

  // The correlation of every request expires: the observations are retired
  // and their deferred dispatches are dropped.
  pool.release_inactive([](const std::string &) {return false;});
  EXPECT_EQ(pool.deferred_count(), 0u);
  EXPECT_EQ(pool.in_flight(), 0u);

  // The action server appears only now. A surviving deferred dispatch would
  // be sent by the next flush.
  std::atomic<int> received{0};
  std::vector<std::shared_ptr<rclcpp_action::ServerGoalHandle<NavigatePose>>> retained;
  auto server = rclcpp_action::create_server<NavigatePose>(
    node_, "late/to_pose",
    [](const rclcpp_action::GoalUUID &, std::shared_ptr<const NavigatePose::Goal>) {
      return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
    },
    [](const std::shared_ptr<rclcpp_action::ServerGoalHandle<NavigatePose>>) {
      return rclcpp_action::CancelResponse::ACCEPT;
    },
    [&retained, &received](
      const std::shared_ptr<rclcpp_action::ServerGoalHandle<NavigatePose>> handle) {
      received.fetch_add(1);
      retained.push_back(handle);
    });
  // A probe client proves that the server is discoverable now, so a surviving
  // deferred dispatch would really have been sent.
  auto probe = rclcpp_action::create_client<NavigatePose>(node_.get(), "late/to_pose");
  ASSERT_TRUE(spin_until(*executor_, [&probe]() {return probe->action_server_is_ready();}, 15s));
  pool.flush_ready([](const std::string &) {return false;});
  executor_->spin_some(std::chrono::seconds(1));
  EXPECT_EQ(received.load(), 0) << "an expired deferred dispatch was sent to the robot";
  EXPECT_EQ(pool.deferred_count(), 0u);
  EXPECT_EQ(pool.in_flight(), 0u);
}

}  // namespace

int main(int argc, char ** argv)
{
  setenv("ROS_DOMAIN_ID", "187", 1);
  setenv("ROS_AUTOMATIC_DISCOVERY_RANGE", "LOCALHOST", 1);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
