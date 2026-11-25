// The nodes against a real action server.
//
// The server decides when a goal is accepted, when it reports feedback and
// when it finishes. The tree only ticks, so a tick that waited would block the
// test as well.
#include <gtest/gtest.h>

#include <chrono>
#include <exception>
#include <memory>
#include <thread>

#include <behaviortree_cpp/action_node.h>
#include <behaviortree_cpp/bt_factory.h>

#include "chaos_sdk/bt/nodes.hpp"
#include "chaos_sdk/robot.hpp"
#include "controlled_action_server.hpp"

using namespace std::chrono_literals;

namespace
{

class NodeTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
  }

  void SetUp() override
  {
    server_ = std::make_unique<chaos_sdk::bt::test::ActionServer>();
    robot_ = std::make_shared<chaos_sdk::Robot>(server_->node(), "test");
    auto lease = robot_->acquire("operator", 5.0);
    ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
    ASSERT_TRUE(lease.get().success);
    chaos_sdk::bt::register_nodes(factory_, robot_);
  }

  void TearDown() override
  {
    tree_ = BT::Tree();
    server_->release_all();
    robot_->close();
    server_.reset();
  }

  BT::Tree make(const std::string & xml)
  {
    tree_ = factory_.createTreeFromText(xml);
    return std::move(tree_);
  }

  std::unique_ptr<chaos_sdk::bt::test::ActionServer> server_;
  std::shared_ptr<chaos_sdk::Robot> robot_;
  BT::BehaviorTreeFactory factory_;
  BT::Tree tree_;
};

TEST_F(NodeTest, NavigationSucceedsAndReportsTheResult)
{
  auto goal = server_->install<chaos_interfaces::action::NavigatePose>();
  auto tree = make(
    R"(<root BTCPP_format="4"><BehaviorTree ID="Main"><Navigate frame_id="map" )"
    R"(x="1" y="2" z="0" yaw="0"/></BehaviorTree></root>)");
  ASSERT_EQ(BT::NodeStatus::RUNNING, tree.tickOnce());
  ASSERT_TRUE(goal->wait_accepted(5s));
  goal->finish(true, "ARRIVED", "done");
  BT::NodeStatus status = BT::NodeStatus::RUNNING;
  for (int i = 0; i < 50 && status == BT::NodeStatus::RUNNING; ++i) {
    status = tree.tickOnce();
    std::this_thread::sleep_for(20ms);
  }
  EXPECT_EQ(BT::NodeStatus::SUCCESS, status);
  EXPECT_EQ("succeeded", tree.rootBlackboard()->get<std::string>("task_state"));
  EXPECT_EQ("ARRIVED", tree.rootBlackboard()->get<std::string>("code"));
}

TEST_F(NodeTest, ARejectedGoalFails)
{
  server_->set_reject(true);
  auto tree = make(
    R"(<root BTCPP_format="4"><BehaviorTree ID="Main"><Navigate frame_id="map" )"
    R"(x="0" y="0" z="0" yaw="0"/></BehaviorTree></root>)");
  BT::NodeStatus status = BT::NodeStatus::RUNNING;
  for (int i = 0; i < 50 && status == BT::NodeStatus::RUNNING; ++i) {
    status = tree.tickOnce();
    std::this_thread::sleep_for(20ms);
  }
  EXPECT_EQ(BT::NodeStatus::FAILURE, status);
  EXPECT_EQ("rejected", tree.rootBlackboard()->get<std::string>("task_state"));
}

TEST_F(NodeTest, HaltingARunningGoalRequestsCancellation)
{
  auto goal = server_->install<chaos_interfaces::action::NavigatePose>();
  auto tree = make(
    R"(<root BTCPP_format="4"><BehaviorTree ID="Main"><Navigate frame_id="map" )"
    R"(x="0" y="0" z="0" yaw="0"/></BehaviorTree></root>)");
  ASSERT_EQ(BT::NodeStatus::RUNNING, tree.tickOnce());
  ASSERT_TRUE(goal->wait_accepted(5s));
  // The node learns about the acceptance on a later tick, and that tick can
  // land before the result is ready. Halt only once the node reports it.
  bool accepted = false;
  for (int i = 0; i < 50 && !accepted; ++i) {
    ASSERT_EQ(BT::NodeStatus::RUNNING, tree.tickOnce());
    try {
      accepted = tree.rootBlackboard()->get<std::string>("task_state") == "accepted";
    } catch (const std::exception &) {
      accepted = false;
    }
    std::this_thread::sleep_for(20ms);
  }
  ASSERT_TRUE(accepted);
  const auto started = std::chrono::steady_clock::now();
  tree.haltTree();
  EXPECT_LT(std::chrono::steady_clock::now() - started, 200ms);
  EXPECT_EQ("CANCEL_REQUESTED", tree.rootBlackboard()->get<std::string>("code"));
  EXPECT_TRUE(goal->wait_canceling(5s));
}

TEST_F(NodeTest, HaltingBeforeAcceptanceStillCancels)
{
  server_->set_hold_acceptance(true);
  auto goal = server_->install<chaos_interfaces::action::NavigatePose>();
  auto tree = make(
    R"(<root BTCPP_format="4"><BehaviorTree ID="Main"><Navigate frame_id="map" )"
    R"(x="0" y="0" z="0" yaw="0"/></BehaviorTree></root>)");
  ASSERT_EQ(BT::NodeStatus::RUNNING, tree.tickOnce());
  tree.haltTree();
  EXPECT_EQ("CANCEL_DEFERRED", tree.rootBlackboard()->get<std::string>("code"));
  EXPECT_TRUE(goal->wait_canceling(5s));
}

TEST_F(NodeTest, FeedbackReachesTheOutputPorts)
{
  auto goal = server_->install<chaos_interfaces::action::NavigatePose>();
  auto tree = make(
    R"(<root BTCPP_format="4"><BehaviorTree ID="Main"><Navigate frame_id="map" )"
    R"(x="0" y="0" z="0" yaw="0"/></BehaviorTree></root>)");
  ASSERT_EQ(BT::NodeStatus::RUNNING, tree.tickOnce());
  ASSERT_TRUE(goal->wait_accepted(5s));
  chaos_interfaces::action::NavigatePose::Feedback feedback;
  feedback.progress = 0.5f;
  feedback.current_pose.pose.position.x = 3.0;
  bool seen = false;
  for (int i = 0; i < 20 && !seen; ++i) {
    goal->publish(feedback);
    tree.tickOnce();
    try {
      seen = tree.rootBlackboard()->get<double>("progress") == 0.5;
    } catch (const std::exception &) {
      seen = false;
    }
    std::this_thread::sleep_for(50ms);
  }
  EXPECT_TRUE(seen);
  auto pose = tree.rootBlackboard()->get<geometry_msgs::msg::PoseStamped>("current_pose");
  EXPECT_DOUBLE_EQ(3.0, pose.pose.position.x);
}

TEST_F(NodeTest, ALocalTimeoutDoesNotClaimThatTheGoalStopped)
{
  auto goal = server_->install<chaos_interfaces::action::NavigatePose>();
  robot_->set_action_timeout(0.3);
  auto tree = make(
    R"(<root BTCPP_format="4"><BehaviorTree ID="Main"><Navigate frame_id="map" )"
    R"(x="0" y="0" z="0" yaw="0"/></BehaviorTree></root>)");
  BT::NodeStatus status = BT::NodeStatus::RUNNING;
  for (int i = 0; i < 50 && status == BT::NodeStatus::RUNNING; ++i) {
    status = tree.tickOnce();
    std::this_thread::sleep_for(50ms);
  }
  EXPECT_EQ(BT::NodeStatus::FAILURE, status);
  EXPECT_EQ("timed_out", tree.rootBlackboard()->get<std::string>("task_state"));
  EXPECT_EQ("TIMED_OUT", tree.rootBlackboard()->get<std::string>("code"));
  auto handle = goal->handle();
  ASSERT_TRUE(handle);
  EXPECT_FALSE(handle->is_canceling());
}

TEST_F(NodeTest, SequentialActionsReuseTheLease)
{
  auto takeoff = server_->install<chaos_interfaces::action::Flight>();
  auto tree = make(
    R"(<root BTCPP_format="4"><BehaviorTree ID="Main"><Sequence>)"
    R"(<Takeoff altitude_m="1.5"/><Land/></Sequence></BehaviorTree></root>)");
  ASSERT_EQ(BT::NodeStatus::RUNNING, tree.tickOnce());
  ASSERT_TRUE(takeoff->wait_accepted(5s));
  takeoff->finish(true, "OK", "airborne");
  // Takeoff already captured its handle. The next flight goal is the landing.
  auto land = server_->install<chaos_interfaces::action::Flight>();
  BT::NodeStatus status = BT::NodeStatus::RUNNING;
  for (int i = 0; i < 50 && status == BT::NodeStatus::RUNNING; ++i) {
    if (land->wait_accepted(0s)) {
      land->finish(true, "OK", "landed");
    }
    status = tree.tickOnce();
    std::this_thread::sleep_for(20ms);
  }
  EXPECT_EQ(BT::NodeStatus::SUCCESS, status);
  EXPECT_EQ(1, server_->lease_calls());
}

TEST_F(NodeTest, AHaltedNodeObservesItsGoalUntilItIsTerminal)
{
  auto goal = server_->install<chaos_interfaces::action::NavigatePose>();
  auto tree = make(
    R"(<root BTCPP_format="4"><BehaviorTree ID="Main"><Navigate frame_id="map" )"
    R"(x="0" y="0" z="0" yaw="0"/></BehaviorTree></root>)");
  ASSERT_EQ(BT::NodeStatus::RUNNING, tree.tickOnce());
  ASSERT_TRUE(goal->wait_accepted(5s));
  tree.haltTree();
  ASSERT_TRUE(goal->wait_canceling(5s));
  goal->finish(false, "CANCELED", "canceled by the tree");
  BT::NodeStatus status = BT::NodeStatus::RUNNING;
  for (int i = 0; i < 50 && status == BT::NodeStatus::RUNNING; ++i) {
    status = tree.tickOnce();
    std::this_thread::sleep_for(20ms);
  }
  // The halted node observed the goal it had sent. It did not start a second
  // goal for the same asset.
  EXPECT_EQ(1, server_->goals());
  EXPECT_EQ(BT::NodeStatus::FAILURE, status);
  EXPECT_EQ("canceled", tree.rootBlackboard()->get<std::string>("task_state"));
}

TEST_F(NodeTest, AHaltedNodeSendsANewGoalAfterTheOldOneFinished)
{
  auto goal = server_->install<chaos_interfaces::action::NavigatePose>();
  auto tree = make(
    R"(<root BTCPP_format="4"><BehaviorTree ID="Main"><Navigate frame_id="map" )"
    R"(x="0" y="0" z="0" yaw="0"/></BehaviorTree></root>)");
  ASSERT_EQ(BT::NodeStatus::RUNNING, tree.tickOnce());
  ASSERT_TRUE(goal->wait_accepted(5s));
  tree.haltTree();
  ASSERT_TRUE(goal->wait_canceling(5s));

  // The old goal is not terminal yet: the node keeps observing it and does
  // not send a second goal for the same asset.
  ASSERT_EQ(BT::NodeStatus::RUNNING, tree.tickOnce());
  EXPECT_EQ(1, server_->goals());

  goal->finish(false, "CANCELED", "canceled by the tree");
  BT::NodeStatus status = BT::NodeStatus::RUNNING;
  for (int i = 0; i < 50 && status == BT::NodeStatus::RUNNING; ++i) {
    status = tree.tickOnce();
    std::this_thread::sleep_for(20ms);
  }
  ASSERT_EQ(BT::NodeStatus::FAILURE, status);
  EXPECT_EQ("canceled", tree.rootBlackboard()->get<std::string>("task_state"));
  EXPECT_EQ(1, server_->goals());

  // The node is activated again while the old goal is terminal. It sends a
  // new goal instead of consuming the finished one a second time.
  tree.haltTree();
  tree.tickOnce();
  // The goal travels to the server asynchronously.
  for (int i = 0; i < 100 && server_->goals() < 2; ++i) {
    std::this_thread::sleep_for(20ms);
  }
  EXPECT_EQ(2, server_->goals());
}

TEST_F(NodeTest, ARetrySendsANewGoalAfterAHalt)
{
  auto goal = server_->install<chaos_interfaces::action::NavigatePose>();
  auto tree = make(
    R"(<root BTCPP_format="4"><BehaviorTree ID="Main">)"
    R"(<RetryUntilSuccessful num_attempts="3">)"
    R"(<Parallel success_count="2" failure_count="2">)"
    R"(<Navigate frame_id="map" x="0" y="0" z="0" yaw="0"/><AlwaysFailure/>)"
    R"(</Parallel></RetryUntilSuccessful></BehaviorTree></root>)");
  tree.tickOnce();
  // The failing sibling halts the navigation that is still running, so the
  // cancellation is on its way.
  ASSERT_TRUE(goal->wait_canceling(5s));
  ASSERT_EQ(1, server_->goals());

  goal->finish(false, "CANCELED", "canceled by the tree");
  // The retry must not consume the old goal again: once the old goal is
  // terminal, the next attempt starts a new one. The client needs a moment
  // to observe the terminal result, so the tree is halted and ticked again
  // until the new goal is on its way.
  for (int i = 0; i < 20 && server_->goals() < 2; ++i) {
    tree.haltTree();
    tree.tickOnce();
    std::this_thread::sleep_for(50ms);
  }
  EXPECT_EQ(2, server_->goals());
}

TEST_F(NodeTest, ARefusedCancellationIsReported)
{
  server_->set_cancel_accept(false);
  auto goal = server_->install<chaos_interfaces::action::NavigatePose>();
  auto tree = make(
    R"(<root BTCPP_format="4"><BehaviorTree ID="Main"><Navigate frame_id="map" )"
    R"(x="0" y="0" z="0" yaw="0"/></BehaviorTree></root>)");
  ASSERT_EQ(BT::NodeStatus::RUNNING, tree.tickOnce());
  ASSERT_TRUE(goal->wait_accepted(5s));
  tree.haltTree();
  std::string code;
  for (int i = 0; i < 100 && code != "CANCEL_REJECTED"; ++i) {
    tree.tickOnce();
    try {
      code = tree.rootBlackboard()->get<std::string>("code");
    } catch (const std::exception &) {
      code.clear();
    }
    std::this_thread::sleep_for(20ms);
  }
  EXPECT_EQ("CANCEL_REJECTED", code);
}

TEST_F(NodeTest, ANonFiniteTtlIsAConfigurationError)
{
  auto goal = server_->install<chaos_interfaces::action::NavigatePose>();
  auto tree = make(
    R"(<root BTCPP_format="4"><BehaviorTree ID="Main"><Navigate frame_id="map" )"
    R"(x="0" y="0" z="0" yaw="0" ttl_s="nan"/></BehaviorTree></root>)");
  EXPECT_THROW(tree.tickOnce(), std::exception);
  // The goal was never sent.
  EXPECT_EQ(0, server_->goals());
}

TEST_F(NodeTest, AMissingLeaseIsAConfigurationError)
{
  // A robot without a lease cannot send a goal. The node reports the mistake
  // instead of claiming that the remote action failed.
  auto robot = std::make_shared<chaos_sdk::Robot>(server_->node(), "no_lease");
  BT::BehaviorTreeFactory factory;
  chaos_sdk::bt::register_nodes(factory, robot);
  auto tree = factory.createTreeFromText(
    R"(<root BTCPP_format="4"><BehaviorTree ID="Main"><Navigate frame_id="map" )"
    R"(x="0" y="0" z="0" yaw="0"/></BehaviorTree></root>)");
  EXPECT_THROW(tree.tickOnce(), std::exception);
  robot->close();
}

TEST_F(NodeTest, ABrokenOutputRemapIsAConfigurationError)
{
  // The port is remapped to something that is not a blackboard entry. The node
  // must not publish the value somewhere else.
  auto tree = make(
    R"(<root BTCPP_format="4"><BehaviorTree ID="Main"><Navigate frame_id="map" )"
    R"(x="0" y="0" z="0" yaw="0" code="not_a_blackboard_entry"/></BehaviorTree></root>)");
  EXPECT_THROW(tree.tickOnce(), std::exception);
}

}  // namespace
