// The in-flight decision is a pure function. The snapshot is what the model
// is shown: role, yaw, and teammate task progress. Neither test calls a model.
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

#include "chaos_agent/agent_loop.hpp"
#include "chaos_interfaces/msg/announcement.hpp"
#include "chaos_interfaces/msg/robot_state.hpp"

namespace
{

using namespace std::chrono_literals;

TEST(InflightDecision, StopCancelsAndEverythingElseWaitsOrIsIgnored)
{
  using chaos_agent::InflightDecision;
  EXPECT_EQ(chaos_agent::decide_inflight("stop"), InflightDecision::kStop);
  EXPECT_EQ(chaos_agent::decide_inflight("continue"), InflightDecision::kWait);
  EXPECT_EQ(chaos_agent::decide_inflight("hold"), InflightDecision::kWait);
  // done would end the task while the body is still moving.
  EXPECT_EQ(chaos_agent::decide_inflight("done"), InflightDecision::kIgnore);
  EXPECT_EQ(chaos_agent::decide_inflight("navigate_to"), InflightDecision::kIgnore);
  // The tool prefix is stripped before this function. A raw tool name is not
  // a stop.
  EXPECT_EQ(chaos_agent::decide_inflight("skill_stop"), InflightDecision::kIgnore);
  EXPECT_EQ(chaos_agent::decide_inflight(""), InflightDecision::kIgnore);
}

nlohmann::json test_vocabulary()
{
  return {
    {"hub_states", nlohmann::json::array({"idle"})},
    {"skills", nlohmann::json::array({
        {{"token", "navigate_to"}, {"description", "go"},
          {"entry_hub", "idle"}, {"exit_hub", "idle"}},
        {{"token", "stop"}, {"description", "stop"},
          {"entry_hub", "idle"}, {"exit_hub", "idle"}, {"any_entry_hub", true}},
      })},
  };
}

class ObservationTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
  }

  static void TearDownTestSuite()
  {
    if (rclcpp::ok()) {
      rclcpp::shutdown();
    }
  }

  void SetUp() override
  {
    node_ = std::make_shared<rclcpp::Node>("chaos_agent", "/robots/dog_001");
    executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    executor_->add_node(node_);
    spin_ = std::thread([this]() {executor_->spin();});
  }

  void TearDown() override
  {
    loop_.reset();
    executor_->cancel();
    if (spin_.joinable()) {
      spin_.join();
    }
    executor_->remove_node(node_);
    node_.reset();
  }

  template<typename Pred>
  bool wait_until(Pred pred, std::chrono::milliseconds budget = 2000ms)
  {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      if (pred()) {
        return true;
      }
      std::this_thread::sleep_for(20ms);
    }
    return pred();
  }

  rclcpp::Node::SharedPtr node_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  std::thread spin_;
  std::shared_ptr<chaos_agent::AgentLoop> loop_;
};

TEST_F(ObservationTest, SnapshotCarriesRoleYawAndTeammateProgress)
{
  chaos_interfaces::msg::Announcement::SharedPtr own;
  std::mutex own_mutex;
  auto own_sub = node_->create_subscription<chaos_interfaces::msg::Announcement>(
    "team_status", rclcpp::QoS(10).reliable(),
    [&](chaos_interfaces::msg::Announcement::SharedPtr msg) {
      const std::lock_guard<std::mutex> guard(own_mutex);
      own = std::move(msg);
    });

  loop_ = std::make_shared<chaos_agent::AgentLoop>(
    node_, "dog_001", "", test_vocabulary(), "",
    "http://127.0.0.1:9", "receiver", std::vector<std::string>{"/robots/dog_002"});
  EXPECT_NE(loop_->system_prompt().find("skill_continue"), std::string::npos);
  EXPECT_NE(loop_->system_prompt().find("not its camera"), std::string::npos);
  loop_->set_task("receive the box");

  // Let the graph match, then publish again. The constructor's first message
  // can be lost before the subscription connects.
  std::this_thread::sleep_for(200ms);
  loop_->set_role("receiver");

  ASSERT_TRUE(wait_until([&]() {
      const std::lock_guard<std::mutex> guard(own_mutex);
      return static_cast<bool>(own);
    }));
  {
    const std::lock_guard<std::mutex> guard(own_mutex);
    EXPECT_EQ(own->source, "dog_001");
    EXPECT_EQ(own->topic, "team_progress");
    const auto body = nlohmann::json::parse(own->payload_json);
    EXPECT_EQ(body["asset_id"], "dog_001");
    EXPECT_EQ(body["role"], "receiver");
    EXPECT_EQ(body["phase"], "idle");
    EXPECT_EQ(body["task"], "receive the box");
    EXPECT_FALSE(body.contains("image"));
  }

  auto state_pub = node_->create_publisher<chaos_interfaces::msg::RobotState>(
    "state", rclcpp::QoS(1).best_effort());
  chaos_interfaces::msg::RobotState state;
  state.asset_id = "dog_001";
  state.pose_valid = true;
  state.pose.pose.position.x = 1.5;
  state.pose.pose.position.y = -0.25;
  // Yaw of pi/2: z = w = sqrt(1/2), x = y = 0.
  state.pose.pose.orientation.z = std::sqrt(0.5);
  state.pose.pose.orientation.w = std::sqrt(0.5);
  state.twist.twist.linear.x = 0.1;

  auto peer_pub = node_->create_publisher<chaos_interfaces::msg::Announcement>(
    "/robots/dog_002/team_status", rclcpp::QoS(10).reliable());
  chaos_interfaces::msg::Announcement garbage;
  garbage.source = "dog_002";
  garbage.topic = "team_progress";
  garbage.payload_json = "not-json";
  chaos_interfaces::msg::Announcement peer;
  peer.source = "dog_002";
  peer.topic = "team_progress";
  peer.payload_json = R"({
    "asset_id": "dog_002",
    "role": "giver",
    "hub": "idle",
    "task": "hand the box",
    "token": "navigate_to",
    "progress": 0.4,
    "phase": "feedback",
    "success": false,
    "image": "not-a-camera"
  })";

  ASSERT_TRUE(wait_until([&]() {
      state_pub->publish(state);
      peer_pub->publish(garbage);
      peer_pub->publish(peer);
      const auto obs = loop_->observation();
      return !obs["state"].is_null() && obs["teammates"].size() == 1;
    }));

  const auto obs = loop_->observation();
  EXPECT_EQ(obs["asset_id"], "dog_001");
  EXPECT_EQ(obs["role"], "receiver");
  EXPECT_EQ(obs["task"], "receive the box");
  EXPECT_NEAR(obs["state"]["pose"]["yaw"].get<double>(), 1.5707963267948966, 1e-6);
  EXPECT_DOUBLE_EQ(obs["state"]["pose"]["x"].get<double>(), 1.5);
  EXPECT_DOUBLE_EQ(obs["state"]["twist"]["linear_x"].get<double>(), 0.1);
  ASSERT_EQ(obs["teammates"].size(), 1u);
  const auto & mate = obs["teammates"].front();
  EXPECT_EQ(mate["asset_id"], "dog_002");
  EXPECT_EQ(mate["role"], "giver");
  EXPECT_EQ(mate["hub"], "idle");
  EXPECT_EQ(mate["token"], "navigate_to");
  EXPECT_EQ(mate["task"], "hand the box");
  EXPECT_EQ(mate["phase"], "feedback");
  EXPECT_DOUBLE_EQ(mate["progress"].get<double>(), 0.4);
  EXPECT_FALSE(mate["success"].get<bool>());
  EXPECT_FALSE(mate["stale"].get<bool>());
  EXPECT_LT(mate["age_s"].get<double>(), 5.0);
  EXPECT_FALSE(mate.contains("image"));
  EXPECT_TRUE(obs["system_prompt"].is_null() || !obs.contains("system_prompt"));
}

}  // namespace
