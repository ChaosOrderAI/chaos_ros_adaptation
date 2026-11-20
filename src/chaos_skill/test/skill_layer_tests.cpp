// The skill layer is the only caller of the base capabilities. These tests
// drive it the way the top-level sources do: the executor directly, and the
// ingress through the intent topic.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include "chaos_interfaces/action/flight.hpp"
#include "chaos_interfaces/action/navigate_pose.hpp"
#include "chaos_interfaces/msg/announcement.hpp"
#include "chaos_interfaces/msg/robot_state.hpp"
#include "chaos_interfaces/msg/skill_event.hpp"
#include "chaos_interfaces/msg/skill_intent.hpp"
#include "chaos_interfaces/srv/control_lease.hpp"
#include "chaos_interfaces/srv/get_description.hpp"
#include "chaos_sdk/robot.hpp"
#include "chaos_skill/executor.hpp"
#include "chaos_skill/intent_ingress.hpp"
#include "chaos_skill/server_ports.hpp"
#include "chaos_skill/vocabulary.hpp"

namespace
{

using namespace std::chrono_literals;

/// The asset the skill layer drives: a description, a control lease and the
/// action servers of the goal types the skills can send.
class MockAsset : public rclcpp::Node
{
public:
  MockAsset()
  // The SDK resolves every endpoint below the asset namespace.
  : Node("mock_asset", "/robots/drone_001")
  {
    description_ = create_service<chaos_interfaces::srv::GetDescription>(
      "description",
      [](const std::shared_ptr<chaos_interfaces::srv::GetDescription::Request>,
        std::shared_ptr<chaos_interfaces::srv::GetDescription::Response> response) {
        response->asset_id = "drone_001";
        response->model = "mock";
        response->capabilities = {"state", "motion.stop", "navigation.pose",
          "flight.takeoff", "flight.land"};
        response->base_frame = "drone_001/base_link";
        response->map_frame = "drone_001/map";
        response->command_timeout_s = 0.5;
      });

    lease_ = create_service<chaos_interfaces::srv::ControlLease>(
      "control/lease",
      [this](
        const std::shared_ptr<chaos_interfaces::srv::ControlLease::Request> request,
        std::shared_ptr<chaos_interfaces::srv::ControlLease::Response> response) {
        if (request->operation == "acquire") {
          ++lease_acquires_;
        } else if (request->operation == "release") {
          ++lease_releases_;
        }
        response->success = true;
        response->code = "OK";
        response->token = request->operation == "release" ? "" : "mock-lease-token";
        response->remaining_s = request->duration_s;
      });

    // The runtime publishes the state as reliable and transient-local, and the
    // SDK subscribes the same way: a volatile publisher would never match.
    state_pub_ = create_publisher<chaos_interfaces::msg::RobotState>(
      "state", rclcpp::QoS(1).reliable().transient_local());
    state_timer_ = create_wall_timer(
      50ms, [this]() {
        chaos_interfaces::msg::RobotState state;
        state.stamp = get_clock()->now();
        state.asset_id = "drone_001";
        state.model = "mock";
        state.operational_state = "ready";
        state.connected = true;
        state.ready = true;
        state.pose_valid = true;
        state.battery_percentage = 0.75F;
        state_pub_->publish(state);
      });

    navigation_ = rclcpp_action::create_server<chaos_interfaces::action::NavigatePose>(
      this, "navigation/to_pose",
      [](const rclcpp_action::GoalUUID &,
        std::shared_ptr<const chaos_interfaces::action::NavigatePose::Goal>) {
        return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
      },
      [](const std::shared_ptr<rclcpp_action::ServerGoalHandle<chaos_interfaces::action::NavigatePose>>) {
        return rclcpp_action::CancelResponse::ACCEPT;
      },
      [this](const std::shared_ptr<rclcpp_action::ServerGoalHandle<chaos_interfaces::action::NavigatePose>> handle) {
        {
          const std::lock_guard<std::mutex> guard(mutex_);
          last_navigation_lease_ = handle->get_goal()->lease_token;
          last_navigation_frame_ = handle->get_goal()->target.header.frame_id;
          last_navigation_x_ = handle->get_goal()->target.pose.position.x;
        }
        auto result = std::make_shared<chaos_interfaces::action::NavigatePose::Result>();
        result->success = true;
        result->code = "OK";
        result->message = "arrived";
        handle->succeed(result);
      });

    takeoff_ = rclcpp_action::create_server<chaos_interfaces::action::Flight>(
      this, "flight/takeoff",
      [](const rclcpp_action::GoalUUID &,
        std::shared_ptr<const chaos_interfaces::action::Flight::Goal>) {
        return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
      },
      [](const std::shared_ptr<rclcpp_action::ServerGoalHandle<chaos_interfaces::action::Flight>>) {
        return rclcpp_action::CancelResponse::ACCEPT;
      },
      [this](const std::shared_ptr<rclcpp_action::ServerGoalHandle<chaos_interfaces::action::Flight>> handle) {
        {
          const std::lock_guard<std::mutex> guard(mutex_);
          last_flight_lease_ = handle->get_goal()->lease_token;
          last_altitude_ = handle->get_goal()->altitude_m;
        }
        auto result = std::make_shared<chaos_interfaces::action::Flight::Result>();
        result->success = true;
        result->code = "OK";
        result->message = "airborne";
        handle->succeed(result);
      });

    land_ = rclcpp_action::create_server<chaos_interfaces::action::Flight>(
      this, "flight/land",
      [](const rclcpp_action::GoalUUID &,
        std::shared_ptr<const chaos_interfaces::action::Flight::Goal>) {
        return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
      },
      [](const std::shared_ptr<rclcpp_action::ServerGoalHandle<chaos_interfaces::action::Flight>>) {
        return rclcpp_action::CancelResponse::ACCEPT;
      },
      [this](const std::shared_ptr<rclcpp_action::ServerGoalHandle<chaos_interfaces::action::Flight>> handle) {
        auto result = std::make_shared<chaos_interfaces::action::Flight::Result>();
        result->success = true;
        result->code = "OK";
        result->message = "landed";
        handle->succeed(result);
      });
  }

  std::atomic<int> lease_acquires_{0};
  std::atomic<int> lease_releases_{0};

  std::string last_flight_lease()
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    return last_flight_lease_;
  }

  std::string last_navigation_lease()
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    return last_navigation_lease_;
  }

  std::string last_navigation_frame()
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    return last_navigation_frame_;
  }

  double last_navigation_x()
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    return last_navigation_x_;
  }

  double last_altitude()
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    return last_altitude_;
  }

private:
  rclcpp::Service<chaos_interfaces::srv::GetDescription>::SharedPtr description_;
  rclcpp::Service<chaos_interfaces::srv::ControlLease>::SharedPtr lease_;
  rclcpp::Publisher<chaos_interfaces::msg::RobotState>::SharedPtr state_pub_;
  rclcpp::TimerBase::SharedPtr state_timer_;
  rclcpp_action::Server<chaos_interfaces::action::NavigatePose>::SharedPtr navigation_;
  rclcpp_action::Server<chaos_interfaces::action::Flight>::SharedPtr takeoff_;
  rclcpp_action::Server<chaos_interfaces::action::Flight>::SharedPtr land_;
  std::mutex mutex_;
  std::string last_flight_lease_;
  std::string last_navigation_lease_;
  std::string last_navigation_frame_;
  double last_navigation_x_ = 0.0;
  double last_altitude_ = 0.0;
};

nlohmann::json vocabulary()
{
  return nlohmann::json::parse(R"json(
  {
    "hub_states": ["idle", "airborne"],
    "skills": [
      {"token": "takeoff", "entry_hub": "idle", "exit_hub": "airborne",
       "capability": "motion", "needs_lease": true,
       "arguments_schema": {"altitude_m": {"type": "number"}}},
      {"token": "land", "entry_hub": "airborne", "exit_hub": "idle",
       "capability": "motion", "needs_lease": true, "arguments_schema": {}},
      {"token": "navigate_to", "entry_hub": "idle", "exit_hub": "idle",
       "capability": "navigation", "needs_lease": true,
       "arguments_schema": {"x": {"type": "number"}}},
      {"token": "observe", "entry_hub": "idle", "exit_hub": "idle",
       "capability": "sensor", "needs_lease": false, "any_entry_hub": true,
       "arguments_schema": {}},
      {"token": "announce", "entry_hub": "idle", "exit_hub": "idle",
       "capability": "communication", "needs_lease": false, "any_entry_hub": true,
       "arguments_schema": {"topic": {"type": "string"}}}
    ]
  })json");
}

class SkillLayer : public ::testing::Test
{
protected:
  void SetUp() override
  {
    setenv("ROS_DOMAIN_ID", "187", 1);
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
    asset_ = std::make_shared<MockAsset>();
    asset_executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    asset_executor_->add_node(asset_);
    asset_thread_ = std::thread([this]() {asset_executor_->spin();});

    node_ = std::make_shared<rclcpp::Node>("skill_test");
    executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    executor_->add_node(node_);
    executor_thread_ = std::thread([this]() {executor_->spin();});

    robot_ = chaos_sdk::Robot::create_standalone("drone_001");
    ASSERT_TRUE(robot_->wait_ready(10s));
  }

  void TearDown() override
  {
    if (robot_) {
      robot_->close();
    }
    executor_->cancel();
    asset_executor_->cancel();
    if (executor_thread_.joinable()) {
      executor_thread_.join();
    }
    if (asset_thread_.joinable()) {
      asset_thread_.join();
    }
  }

  std::shared_ptr<chaos_skill::Executor> make_executor(
    std::shared_ptr<chaos_skill::CapabilityPorts> ports = nullptr,
    const std::string & initial_hub = "idle")
  {
    return std::make_shared<chaos_skill::Executor>(
      robot_, chaos_skill::Vocabulary::from_json(vocabulary()), initial_hub,
      "drone_001/map", ports);
  }

  std::shared_ptr<MockAsset> asset_;
  std::shared_ptr<rclcpp::Node> node_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> asset_executor_;
  std::thread executor_thread_;
  std::thread asset_thread_;
  std::shared_ptr<chaos_sdk::Robot> robot_;
};

TEST_F(SkillLayer, AMotionSkillTakesALeaseAndGivesItBack)
{
  auto skill = make_executor();

  const auto outcome = skill->execute("takeoff", {{"altitude_m", 1.5}});

  EXPECT_TRUE(outcome.success) << outcome.message;
  EXPECT_EQ(outcome.hub_state, "airborne");
  EXPECT_EQ(asset_->lease_acquires_.load(), 1);
  EXPECT_EQ(asset_->lease_releases_.load(), 1);
  EXPECT_DOUBLE_EQ(asset_->last_altitude(), 1.5);
}

TEST_F(SkillLayer, TheHubPreconditionRefusesAnIllegalSkill)
{
  auto skill = make_executor();

  const auto outcome = skill->execute("land", nlohmann::json::object());

  EXPECT_FALSE(outcome.success);
  EXPECT_EQ(outcome.code, "skill.hub_mismatch");
  // The refusal happens before the platform is touched.
  EXPECT_EQ(asset_->lease_acquires_.load(), 0);
}

TEST_F(SkillLayer, ACallerThatHoldsTheLeaseRunsWithItsOwnToken)
{
  auto skill = make_executor();

  chaos_skill::SkillOrigin origin;
  origin.source = "mqtt:drone_001";
  origin.control_token = "remote-lease-token";

  const auto outcome = skill->execute(
    "takeoff", {{"altitude_m", 2.0}}, nullptr, origin);

  EXPECT_TRUE(outcome.success) << outcome.message;
  // The skill layer did not take a second lease: the runtime admits one
  // holder at a time.
  EXPECT_EQ(asset_->lease_acquires_.load(), 0);
  EXPECT_EQ(asset_->lease_releases_.load(), 0);
  EXPECT_EQ(asset_->last_flight_lease(), "remote-lease-token");
}

TEST_F(SkillLayer, ADirectCommandIsNotBoundByTheHubPrecondition)
{
  auto skill = make_executor();

  chaos_skill::SkillOrigin origin;
  origin.source = "mqtt:drone_001";
  origin.enforce_hub = false;

  const auto outcome = skill->execute(
    "land", nlohmann::json::object(), nullptr, origin);

  EXPECT_TRUE(outcome.success) << outcome.message;
}

TEST_F(SkillLayer, TheObserveSkillReportsTheStateAndTheSensorStreams)
{
  chaos_skill::ServerPortsOptions options;
  auto ports = std::make_shared<chaos_skill::ServerPorts>(node_.get(), options);
  auto skill = make_executor(ports);

  // The asset publishes its state; the skill layer reads the latest sample.
  auto outcome = skill->execute("observe", nlohmann::json::object());
  for (int i = 0; i < 40 && !outcome.success; ++i) {
    std::this_thread::sleep_for(50ms);
    outcome = skill->execute("observe", nlohmann::json::object());
  }

  ASSERT_TRUE(outcome.success) << outcome.message;
  ASSERT_TRUE(outcome.payload.contains("state"));
  EXPECT_EQ(outcome.payload["state"].value("asset_id", ""), "drone_001");
  EXPECT_TRUE(outcome.payload.contains("aux"));
}

TEST_F(SkillLayer, TheAnnounceSkillWritesToTheCommunicationChannel)
{
  auto announcements = std::vector<chaos_interfaces::msg::Announcement>();
  std::mutex mutex;
  std::condition_variable signal;
  auto sub = node_->create_subscription<chaos_interfaces::msg::Announcement>(
    "communication/announcement", 10,
    [&](chaos_interfaces::msg::Announcement::ConstSharedPtr message) {
      {
        const std::lock_guard<std::mutex> guard(mutex);
        announcements.push_back(*message);
      }
      signal.notify_all();
    });

  chaos_skill::ServerPortsOptions options;
  auto ports = std::make_shared<chaos_skill::ServerPorts>(node_.get(), options);
  auto skill = make_executor(ports);

  chaos_skill::SkillOrigin origin;
  origin.source = "agent:drone_001";
  origin.request_id = "req-1";

  const auto outcome = skill->execute(
    "announce", {{"topic", "status"}, {"payload", {{"battery", 0.5}}}}, nullptr, origin);
  EXPECT_TRUE(outcome.success) << outcome.message;

  std::unique_lock<std::mutex> lock(mutex);
  ASSERT_TRUE(signal.wait_for(lock, 5s, [&]() {return !announcements.empty();}));
  EXPECT_EQ(announcements.front().topic, "status");
  EXPECT_EQ(announcements.front().source, "agent:drone_001");
  EXPECT_EQ(announcements.front().request_id, "req-1");
}

TEST_F(SkillLayer, TheIngressRunsAnIntentAndReportsTheLifecycle)
{
  auto events = std::vector<chaos_interfaces::msg::SkillEvent>();
  std::mutex mutex;
  std::condition_variable signal;
  auto sub = node_->create_subscription<chaos_interfaces::msg::SkillEvent>(
    "skill_event", 10,
    [&](chaos_interfaces::msg::SkillEvent::ConstSharedPtr message) {
      {
        const std::lock_guard<std::mutex> guard(mutex);
        events.push_back(*message);
      }
      signal.notify_all();
    });

  auto skill = make_executor();
  chaos_skill::IntentIngress ingress(node_, *skill);
  ingress.start();

  auto intent = chaos_interfaces::msg::SkillIntent();
  intent.token.token = "takeoff";
  intent.token.arguments_json = R"({"altitude_m": 3.0})";
  intent.token.source = "agent:drone_001";
  intent.request_id = "req-1";
  intent.enforce_hub_precondition = true;
  node_->create_publisher<chaos_interfaces::msg::SkillIntent>("skill_intent", 10)
  ->publish(intent);

  std::unique_lock<std::mutex> lock(mutex);
  ASSERT_TRUE(signal.wait_for(
      lock, 10s, [&]() {
        return events.size() >= 2 && events.back().phase == "succeeded";
      }));
  EXPECT_EQ(events.front().phase, "accepted");
  EXPECT_TRUE(events.back().success);
  EXPECT_EQ(events.back().hub_state, "airborne");
  ingress.stop();
}

TEST_F(SkillLayer, ARepeatedRequestIdRunsTheSkillOnlyOnce)
{
  auto events = std::vector<chaos_interfaces::msg::SkillEvent>();
  std::mutex mutex;
  std::condition_variable signal;
  auto sub = node_->create_subscription<chaos_interfaces::msg::SkillEvent>(
    "skill_event", 10,
    [&](chaos_interfaces::msg::SkillEvent::ConstSharedPtr message) {
      {
        const std::lock_guard<std::mutex> guard(mutex);
        events.push_back(*message);
      }
      signal.notify_all();
    });

  auto skill = make_executor();
  chaos_skill::IntentIngress ingress(node_, *skill);
  ingress.start();

  auto publisher = node_->create_publisher<chaos_interfaces::msg::SkillIntent>("skill_intent", 10);
  auto intent = chaos_interfaces::msg::SkillIntent();
  intent.token.token = "takeoff";
  intent.token.arguments_json = R"({"altitude_m": 3.0})";
  intent.token.source = "mqtt:drone_001";
  intent.request_id = "req-repeat";
  intent.enforce_hub_precondition = false;

  publisher->publish(intent);
  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(signal.wait_for(
        lock, 10s, [&]() {
          return events.size() >= 2 && events.back().phase == "succeeded";
        }));
  }
  const auto acquired_after_first = asset_->lease_acquires_.load();

  // The same request id again: the skill must not run a second time.
  publisher->publish(intent);
  {
    std::unique_lock<std::mutex> lock(mutex);
    signal.wait_for(lock, 5s, [&]() {return events.size() >= 4;});
  }
  std::this_thread::sleep_for(300ms);

  EXPECT_EQ(asset_->lease_acquires_.load(), acquired_after_first);
  EXPECT_EQ(ingress.executed(), 1u);
  ingress.stop();
}

TEST_F(SkillLayer, AnExpiredIntentIsRefused)
{
  auto events = std::vector<chaos_interfaces::msg::SkillEvent>();
  std::mutex mutex;
  std::condition_variable signal;
  auto sub = node_->create_subscription<chaos_interfaces::msg::SkillEvent>(
    "skill_event", 10,
    [&](chaos_interfaces::msg::SkillEvent::ConstSharedPtr message) {
      {
        const std::lock_guard<std::mutex> guard(mutex);
        events.push_back(*message);
      }
      signal.notify_all();
    });

  auto skill = make_executor();
  chaos_skill::IntentIngress ingress(node_, *skill);
  ingress.start();

  auto intent = chaos_interfaces::msg::SkillIntent();
  intent.token.token = "takeoff";
  intent.token.arguments_json = R"({"altitude_m": 3.0})";
  intent.request_id = "req-expired";
  // One second in the past: the intent may no longer be accepted.
  intent.expires_at.sec = 1;
  node_->create_publisher<chaos_interfaces::msg::SkillIntent>("skill_intent", 10)
  ->publish(intent);

  std::unique_lock<std::mutex> lock(mutex);
  ASSERT_TRUE(signal.wait_for(lock, 10s, [&]() {return !events.empty();}));
  EXPECT_EQ(events.front().phase, "failed");
  EXPECT_EQ(events.front().code, "skill.expired");
  EXPECT_EQ(ingress.executed(), 0u);
  ingress.stop();
}

}  // namespace
