// In-process ROS integration: a mock runtime (services, velocity ack,
// action servers) plus the native C++ gateway node, driven over the real DDS
// topics. Mirrors scripts/mqtt_smoke_test.py against the ELF library code.
//
// The mock runtime deliberately provides *genuinely* silent endpoints: the
// lease service uses the deferred-response callback of rclcpp and never sends
// a response for the "hang" owner, and the action server accepts goals it
// never completes. A callback that simply returns would answer with the
// default response and would therefore not exercise the gateway timeouts.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <std_msgs/msg/string.hpp>

#include "chaos_interfaces/action/flight.hpp"
#include "chaos_interfaces/action/navigate_pose.hpp"
#include "chaos_interfaces/msg/command_result.hpp"
#include "chaos_interfaces/msg/robot_state.hpp"
#include "chaos_interfaces/msg/velocity_command.hpp"
#include "chaos_interfaces/srv/control_lease.hpp"
#include "chaos_interfaces/srv/get_description.hpp"
#include "chaos_interfaces/srv/stop.hpp"

#include "chaos_mqtt/gateway_node.hpp"

namespace
{

using namespace std::chrono_literals;
using chaos_mqtt::GatewayNode;
using nlohmann::json;

std::int64_t epoch_ms()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
}

/// One gateway parameter override used by the fixture.
struct GatewayParameter
{
  GatewayParameter(std::string parameter_name, double parameter_value)
  : name(std::move(parameter_name)), value(parameter_value) {}
  GatewayParameter(std::string parameter_name, std::int64_t parameter_value)
  : name(std::move(parameter_name)), value(parameter_value) {}
  GatewayParameter(std::string parameter_name, int parameter_value)
  : name(std::move(parameter_name)), value(static_cast<std::int64_t>(parameter_value)) {}

  std::string name;
  rclcpp::ParameterValue value;
};

class MockRuntime : public rclcpp::Node
{
public:
  /// The mock runtime of the integration fixture.
  ///
  /// `with_navigation_server` decides whether the navigation action server is
  /// advertised from the start. A runtime without it lets a test send a
  /// navigation command before any action server exists.
  explicit MockRuntime(bool with_navigation_server = true)
  : rclcpp::Node("mock_runtime")
  {
    description_ = create_service<chaos_interfaces::srv::GetDescription>(
      "description",
      [](const std::shared_ptr<chaos_interfaces::srv::GetDescription::Request>,
        std::shared_ptr<chaos_interfaces::srv::GetDescription::Response> response) {
        response->asset_id = "drone_001";
        response->model = "mock";
        response->capabilities = {"state", "motion.stop", "motion.velocity", "navigation.pose",
          "flight.takeoff", "flight.land"};
        response->base_frame = "drone_001/base_link";
        response->command_timeout_s = 0.5;
      });
    // Deferred response callback: the request header is captured and the
    // answer is only sent explicitly. Requests of the "hang" owner are never
    // answered, which is a truly silent service.
    lease_ = create_service<chaos_interfaces::srv::ControlLease>(
      "control/lease",
      [this](const std::shared_ptr<rmw_request_id_t> request_header,
        std::shared_ptr<chaos_interfaces::srv::ControlLease::Request> request) {
        if (request->owner == "hang") {
          std::lock_guard<std::mutex> guard(mutex_);
          silent_requests_.push_back(request_header);
          return;
        }
        chaos_interfaces::srv::ControlLease::Response response;
        response.success = true;
        response.code = "OK";
        if (request->operation == "release") {
          response.token = "";
        } else {
          response.token = "tok_gateway_test";
        }
        response.remaining_s = request->duration_s;
        lease_->send_response(*request_header, response);
      });
    stop_ = create_service<chaos_interfaces::srv::Stop>(
      "motion/stop",
      [](const std::shared_ptr<chaos_interfaces::srv::Stop::Request>,
        std::shared_ptr<chaos_interfaces::srv::Stop::Response> response) {
        response->success = true;
        response->code = "OK";
      });
    velocity_sub_ = create_subscription<chaos_interfaces::msg::VelocityCommand>(
      "motion/velocity", 32,
      [this](const chaos_interfaces::msg::VelocityCommand::SharedPtr message) {
        chaos_interfaces::msg::CommandResult result;
        result.stamp = get_clock()->now();
        result.request_id = message->request_id;
        result.success = true;
        result.code = "OK";
        command_results_pub_->publish(result);
      });
    command_results_pub_ = create_publisher<chaos_interfaces::msg::CommandResult>(
      "command_results", 32);
    if (with_navigation_server) {
      start_navigation_server();
    }
    takeoff_server_ = make_flight_server("flight/takeoff");
    land_server_ = make_flight_server("flight/land");
  }

  /// Advertise the navigation action server.
  ///
  /// The server may be started *after* the gateway already received a
  /// navigation command: the gateway then has to defer the dispatch instead
  /// of rejecting the command. Every goal that reaches the server is
  /// counted, so a test can prove that a deferred goal was dispatched exactly
  /// once.
  void start_navigation_server()
  {
    if (navigation_server_) {
      return;
    }
    navigation_server_ = rclcpp_action::create_server<chaos_interfaces::action::NavigatePose>(
      this, "navigation/to_pose",
      [this](const rclcpp_action::GoalUUID &,
        std::shared_ptr<const chaos_interfaces::action::NavigatePose::Goal>) {
        const std::lock_guard<std::mutex> guard(mutex_);
        ++received_goals_;
        return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
      },
      [](const std::shared_ptr<rclcpp_action::ServerGoalHandle<chaos_interfaces::action::NavigatePose>>) {
        return rclcpp_action::CancelResponse::ACCEPT;
      },
      [this](const std::shared_ptr<rclcpp_action::ServerGoalHandle<chaos_interfaces::action::NavigatePose>> handle) {
        const double x = handle->get_goal()->target.pose.position.x;
        if (x == 42.0) {
          // Retain the goal handle: the goal stays EXECUTING forever and the
          // server never sends a result. The client therefore keeps a pending
          // result request and the gateway has to retire exactly this
          // observation. Releasing the handle would abort the goal and hand
          // an empty default result to the client.
          std::lock_guard<std::mutex> guard(mutex_);
          silent_goals_.push_back(handle);
          return;
        }
        if (x == 43.0) {
          // Held on purpose so the test can complete it *after* the gateway
          // already timed the observation out.
          std::lock_guard<std::mutex> guard(mutex_);
          held_goals_.push_back(handle);
          return;
        }
        // Barrier: the feedback is only published after the test observed the
        // acceptance, so the gateway has already mapped the goal UUID. The
        // wait is bounded; it never blocks the gateway executor because the
        // mock runtime owns a separate one.
        {
          std::unique_lock<std::mutex> lock(mutex_);
          feedback_goals_.push_back(handle);
          feedback_cv_.wait_for(lock, 20s, [this]() {return feedback_released_;});
        }
        for (int i = 0; i < 3; ++i) {
          auto feedback = std::make_shared<chaos_interfaces::action::NavigatePose::Feedback>();
          feedback->progress = 0.33f * static_cast<float>(i + 1);
          handle->publish_feedback(feedback);
          std::this_thread::sleep_for(10ms);
        }
        auto result = std::make_shared<chaos_interfaces::action::NavigatePose::Result>();
        result->success = true;
        result->code = "OK";
        result->message = "arrived";
        handle->succeed(result);
      });
  }

  /// Release the goals that wait for the feedback barrier. The mock runtime
  /// publishes the feedback and the result afterwards.
  void release_feedback_goals()
  {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      feedback_released_ = true;
    }
    feedback_cv_.notify_all();
  }

  /// Complete the goals that were held back, after the gateway retired them.
  void complete_held_goals()
  {
    std::vector<std::shared_ptr<rclcpp_action::ServerGoalHandle<chaos_interfaces::action::NavigatePose>>>
    held;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      held.swap(held_goals_);
    }
    for (const auto & handle : held) {
      auto result = std::make_shared<chaos_interfaces::action::NavigatePose::Result>();
      result->success = true;
      result->code = "LATE";
      result->message = "completed after the gateway stopped observing";
      handle->succeed(result);
    }
  }

  /// Number of goals the server accepted and keeps in EXECUTING state.
  std::size_t silent_goal_count()
  {
    std::lock_guard<std::mutex> guard(mutex_);
    return silent_goals_.size();
  }

  /// Number of goals the navigation server received in total.
  std::size_t received_goal_count()
  {
    std::lock_guard<std::mutex> guard(mutex_);
    return received_goals_;
  }

  /// Number of service requests that were never answered.
  std::size_t silent_request_count()
  {
    std::lock_guard<std::mutex> guard(mutex_);
    return silent_requests_.size();
  }

private:
  using Flight = chaos_interfaces::action::Flight;

  rclcpp_action::Server<Flight>::SharedPtr make_flight_server(const std::string & name)
  {
    return rclcpp_action::create_server<Flight>(
      this, name,
      [](const rclcpp_action::GoalUUID &, std::shared_ptr<const Flight::Goal>) {
        return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
      },
      [](const std::shared_ptr<rclcpp_action::ServerGoalHandle<Flight>>) {
        return rclcpp_action::CancelResponse::ACCEPT;
      },
      [](const std::shared_ptr<rclcpp_action::ServerGoalHandle<Flight>> handle) {
        auto feedback = std::make_shared<Flight::Feedback>();
        feedback->altitude_m = handle->get_goal()->altitude_m;
        handle->publish_feedback(feedback);
        std::this_thread::sleep_for(30ms);
        auto result = std::make_shared<Flight::Result>();
        result->success = true;
        result->code = "OK";
        result->message = "done";
        handle->succeed(result);
      });
  }

  rclcpp::Service<chaos_interfaces::srv::GetDescription>::SharedPtr description_;
  rclcpp::Service<chaos_interfaces::srv::ControlLease>::SharedPtr lease_;
  rclcpp::Service<chaos_interfaces::srv::Stop>::SharedPtr stop_;
  rclcpp::Subscription<chaos_interfaces::msg::VelocityCommand>::SharedPtr velocity_sub_;
  rclcpp::Publisher<chaos_interfaces::msg::CommandResult>::SharedPtr command_results_pub_;
  rclcpp_action::Server<chaos_interfaces::action::NavigatePose>::SharedPtr navigation_server_;
  rclcpp_action::Server<Flight>::SharedPtr takeoff_server_;
  rclcpp_action::Server<Flight>::SharedPtr land_server_;
  std::mutex mutex_;
  std::condition_variable feedback_cv_;
  bool feedback_released_ = false;
  std::vector<std::shared_ptr<rmw_request_id_t>> silent_requests_;
  std::size_t received_goals_ = 0;
  std::vector<std::shared_ptr<rclcpp_action::ServerGoalHandle<chaos_interfaces::action::NavigatePose>>>
  silent_goals_;
  std::vector<std::shared_ptr<rclcpp_action::ServerGoalHandle<chaos_interfaces::action::NavigatePose>>>
  feedback_goals_;
  std::vector<std::shared_ptr<rclcpp_action::ServerGoalHandle<chaos_interfaces::action::NavigatePose>>>
  held_goals_;
};

class GatewayIntegration : public ::testing::Test
{
protected:
  /// Whether the mock runtime advertises the navigation action server from
  /// the start. A derived fixture switches it off to exercise a navigation
  /// request that arrives before the action server exists.
  virtual bool with_navigation_server() const {return true;}

  void SetUp() override
  {
    setenv("ROS_DOMAIN_ID", "188", 1);
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
    runtime_ = std::make_shared<MockRuntime>(with_navigation_server());
    client_ = std::make_shared<rclcpp::Node>("gateway_test_client");
    // The test client uses a deeper queue than the public profile so that a
    // burst of test commands is not truncated by the *test* transport. The
    // gateway keeps the public depth.
    replies_sub_ = client_->create_subscription<std_msgs::msg::String>(
      "mqtt/command/result", 512,
      [this](const std_msgs::msg::String::SharedPtr message) {
        const std::lock_guard<std::mutex> guard(mutex_);
        replies_.push_back(json::parse(message->data));
      });
    events_sub_ = client_->create_subscription<std_msgs::msg::String>(
      "mqtt/task_events", 512,
      [this](const std_msgs::msg::String::SharedPtr message) {
        const std::lock_guard<std::mutex> guard(mutex_);
        events_.push_back(json::parse(message->data));
      });
    states_sub_ = client_->create_subscription<std_msgs::msg::String>(
      "mqtt/state", 512,
      [this](const std_msgs::msg::String::SharedPtr message) {
        const std::lock_guard<std::mutex> guard(mutex_);
        states_.push_back(json::parse(message->data));
      });
    command_pub_ = client_->create_publisher<std_msgs::msg::String>("mqtt/command", 512);
    executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    executor_->add_node(client_);
    // The mock runtime owns a separate executor: a blocking server callback
    // must never delay the gateway's goal acknowledgment.
    runtime_executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    runtime_executor_->add_node(runtime_);
    spin_thread_ = std::thread([this]() { executor_->spin(); });
    runtime_thread_ = std::thread([this]() { runtime_executor_->spin(); });
    create_gateway({{"request_timeout_s", 0.4}, {"action_timeout_s", 1.0}});
    // Wait for discovery between the fresh nodes.
    std::this_thread::sleep_for(500ms);
  }

  void TearDown() override
  {
    // Release a goal that still waits for the feedback barrier: the runtime
    // executor must be able to stop even when the test failed early.
    if (runtime_) {
      runtime_->release_feedback_goals();
    }
    // Quiesce both executors before the nodes are destroyed.
    runtime_executor_->cancel();
    runtime_thread_.join();
    executor_->cancel();
    spin_thread_.join();
    if (gateway_) {
      executor_->remove_node(gateway_);
      gateway_.reset();
    }
    executor_->remove_node(client_);
    runtime_executor_->remove_node(runtime_);
    client_.reset();
    runtime_.reset();
  }

  /// (Re)create the gateway node with the given parameters.
  void create_gateway(const std::vector<GatewayParameter> & parameters)
  {
    if (gateway_) {
      executor_->remove_node(gateway_);
      gateway_.reset();
    }
    rclcpp::NodeOptions options;
    options.append_parameter_override("asset_json", std::string(
        R"({"id":"drone_001","capabilities":["state","motion.stop","motion.velocity",)"
        R"("navigation.pose","flight.takeoff","flight.land"],)"
        R"("frames":{"base":"drone_001/base_link"},"limits":{"command_timeout_s":0.5}})"));
    for (const auto & parameter : parameters) {
      options.append_parameter_override(parameter.name, parameter.value);
    }
    gateway_ = std::make_shared<GatewayNode>(options);
    executor_->add_node(gateway_);
  }

  json command(const std::string & name, const std::string & request_id,
    const json & fields = json::object(), std::int64_t age_ms = 0)
  {
    // The payload fields are merged explicitly: an initializer list that
    // mixes pairs with an object would be interpreted as a JSON *array* by
    // nlohmann and would never reach the gateway as a command.
    json payload = json{{"command", name}, {"ttl_ms", 2000}};
    for (const auto & item : fields.items()) {
      payload[item.key()] = item.value();
    }
    const json envelope{
      {"bridge", json{{"source", "mqtt"}, {"request_id", request_id},
          {"timestamp_ms", epoch_ms() - age_ms}}},
      {"payload", payload},
    };
    std_msgs::msg::String message;
    message.data = envelope.dump();
    command_pub_->publish(message);
    return envelope;
  }

  std::vector<json> replies_for(const std::string & request_id)
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    std::vector<json> result;
    for (const auto & reply : replies_) {
      if (reply["request_id"] == request_id) {
        result.push_back(reply);
      }
    }
    return result;
  }

  std::vector<json> events_for(const std::string & request_id)
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    std::vector<json> result;
    for (const auto & event : events_) {
      if (event["request_id"] == request_id) {
        result.push_back(event);
      }
    }
    return result;
  }

  /// The reply of a request with the given phase.
  ///
  /// A request produces an "accepted" reply first and a terminal reply
  /// afterwards; the last reply is therefore not necessarily the accepted
  /// one.
  json reply_with_phase(const std::string & request_id, const std::string & phase)
  {
    for (const auto & reply : replies_for(request_id)) {
      if (reply["response"].contains("phase") && reply["response"]["phase"] == phase) {
        return reply;
      }
    }
    return json();
  }

  /// True when a terminal reply (any phase) was published for the request.
  bool has_terminal_reply(const std::string & request_id)
  {
    for (const auto & reply : replies_for(request_id)) {
      if (reply["response"].contains("terminal") && reply["response"]["terminal"].get<bool>()) {
        return true;
      }
    }
    return false;
  }

  /// The last reply of a request as a diagnostic string.
  ///
  /// A request without any reply is a normal state (the gateway stays
  /// silent while it defers a dispatch), so the diagnostic must never read
  /// the last element of an empty list.
  std::string reply_diagnostics(const std::string & request_id)
  {
    const auto replies = replies_for(request_id);
    if (replies.empty()) {
      return "<no reply>";
    }
    return replies.back()["response"].dump();
  }

  bool wait_for(const std::function<bool()> & predicate, std::chrono::seconds timeout = 10s)
  {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (predicate()) {
        return true;
      }
      std::this_thread::sleep_for(5ms);
    }
    return predicate();
  }

  /// Wait until the gateway answers a probe command.
  ///
  /// The probe proves that a freshly created gateway was discovered by the
  /// test client and that its service clients are ready. The bounded retry
  /// replaces a fixed sleep: the discovery of a new node is not a fixed
  /// duration, and every attempt uses its own request id because a failed
  /// request is remembered as a replay.
  bool wait_for_gateway()
  {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    int attempt = 0;
    while (std::chrono::steady_clock::now() < deadline) {
      // The request id has to satisfy the wire contract (at least 8
      // characters), otherwise the gateway drops the probe uncorrelated.
      const std::string request_id = "gateway_probe_" + std::to_string(attempt++);
      command("system.describe", request_id);
      const bool answered = wait_for([&]() { return !replies_for(request_id).empty(); }, 1s);
      if (answered) {
        const auto replies = replies_for(request_id);
        if (!replies.empty() && replies.back()["response"]["ok"].get<bool>()) {
          return true;
        }
      }
      // The gateway answered but its service client is not ready yet: retry
      // with a fresh request id after a short pause.
      std::this_thread::sleep_for(50ms);
    }
    return false;
  }

  /// Advertise the navigation action server of the mock runtime.
  ///
  /// The runtime executor is stopped while the server is created: the entity
  /// list of a node must not change while its executor collects it. The
  /// server is discovered by the gateway's action client shortly afterwards.
  void start_runtime_navigation_server()
  {
    runtime_executor_->cancel();
    runtime_thread_.join();
    runtime_->start_navigation_server();
    runtime_executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    runtime_executor_->add_node(runtime_);
    runtime_thread_ = std::thread([this]() { runtime_executor_->spin(); });
  }

  std::shared_ptr<MockRuntime> runtime_;
  std::shared_ptr<GatewayNode> gateway_;
  std::shared_ptr<rclcpp::Node> client_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr replies_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr events_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr states_sub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr command_pub_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> runtime_executor_;
  std::thread spin_thread_;
  std::thread runtime_thread_;
  std::mutex mutex_;
  std::vector<json> replies_;
  std::vector<json> events_;
  std::vector<json> states_;
  std::vector<std::string> velocity_frames_;
  std::vector<double> velocity_valid_for_;
};

TEST_F(GatewayIntegration, DescribeLeaseVelocityStopAndReplayOverDds)
{
  const std::string describe_id = "describe_0001";
  command("system.describe", describe_id);
  ASSERT_TRUE(wait_for([&]() { return !replies_for(describe_id).empty(); }));
  const json described = replies_for(describe_id).back();
  EXPECT_TRUE(described["response"]["ok"].get<bool>());
  EXPECT_EQ(described["response"]["data"]["asset_id"], "drone_001");

  const std::string acquire_id = "acquire_0001";
  command("control.acquire", acquire_id, {{"owner", "cpp_smoke"}, {"duration_s", 15.0}});
  ASSERT_TRUE(wait_for([&]() { return !replies_for(acquire_id).empty(); }));
  const json acquired = replies_for(acquire_id).back();
  ASSERT_TRUE(acquired["response"]["ok"].get<bool>());
  const std::string token = acquired["response"]["data"]["token"];
  EXPECT_FALSE(token.empty());

  // Replaying the identical envelope returns the cached terminal reply.
  const auto cached = acquired;
  command("control.acquire", acquire_id, {{"owner", "cpp_smoke"}, {"duration_s", 15.0}});
  ASSERT_TRUE(wait_for([&]() { return replies_for(acquire_id).size() >= 2; }));
  EXPECT_EQ(replies_for(acquire_id).back(), cached);

  // Velocity: runtime ack arrives on command_results and is correlated.
  const std::string velocity_id = "velocity_0001";
  command("motion.velocity", velocity_id,
    {{"lease_token", token}, {"valid_for_s", 0.3}, {"velocity", {{"linear", {{"x", 0.1}}}}}});
  ASSERT_TRUE(wait_for([&]() { return !replies_for(velocity_id).empty(); }));
  EXPECT_TRUE(replies_for(velocity_id).back()["response"]["ok"].get<bool>());

  // Stale command is rejected EXPIRED with a correlated reply.
  const std::string stale_id = "stale_0001";
  command("motion.velocity", stale_id,
    {{"lease_token", token}, {"valid_for_s", 0.3}, {"velocity", json::object()}}, 5000);
  ASSERT_TRUE(wait_for([&]() { return !replies_for(stale_id).empty(); }));
  EXPECT_EQ(replies_for(stale_id).back()["response"]["code"], "EXPIRED");

  // Unknown fields are rejected without killing the gateway.
  const std::string bad_id = "bad_0001";
  command("motion.velocity", bad_id,
    {{"lease_token", token}, {"valid_for_s", 0.3}, {"velocity", json::object()},
      {"topic", "/hardware/cmd_vel"}});
  ASSERT_TRUE(wait_for([&]() { return !replies_for(bad_id).empty(); }));
  EXPECT_EQ(replies_for(bad_id).back()["response"]["code"], "INVALID_ARGUMENT");

  const std::string stop_id = "stop_0001";
  command("motion.stop", stop_id, {{"lease_token", token}});
  ASSERT_TRUE(wait_for([&]() { return !replies_for(stop_id).empty(); }));
  EXPECT_TRUE(replies_for(stop_id).back()["response"]["ok"].get<bool>());
}

TEST_F(GatewayIntegration, ActionLifecycleAcceptFeedbackTerminalAndReplay)
{
  const std::string acquire_id = "acquire_action1";
  command("control.acquire", acquire_id, {{"owner", "cpp_smoke"}, {"duration_s", 30.0}});
  ASSERT_TRUE(wait_for([&]() { return !replies_for(acquire_id).empty(); }));
  const std::string token = replies_for(acquire_id).back()["response"]["data"]["token"];

  const std::string goal_id = "goal_0001";
  command("navigation.to_pose", goal_id,
    {{"lease_token", token},
      {"target", {{"frame_id", "map"}, {"position", {{"x", 0.1}}}}}});
  ASSERT_TRUE(wait_for([&]() { return !reply_with_phase(goal_id, "accepted").is_null(); }));
  const json accepted = reply_with_phase(goal_id, "accepted");
  EXPECT_TRUE(accepted["response"]["ok"].get<bool>());
  EXPECT_EQ(accepted["response"]["phase"], "accepted");
  EXPECT_FALSE(accepted["response"]["terminal"].get<bool>());
  const std::string goal = accepted["response"]["goal_id"];

  // The mock publishes the feedback only after the acceptance was observed:
  // the gateway has mapped the goal UUID before the feedback arrives.
  runtime_->release_feedback_goals();

  // Feedback and terminal events arrive on task_events with goal correlation.
  ASSERT_TRUE(wait_for([&]() {
    const std::lock_guard<std::mutex> guard(mutex_);
    return std::any_of(events_.begin(), events_.end(), [&](const json & event) {
      return event["request_id"] == goal_id && event["phase"] == "feedback" &&
        event["goal_id"] == goal;
    });
  }));
  ASSERT_TRUE(wait_for([&]() {
    const std::lock_guard<std::mutex> guard(mutex_);
    return std::any_of(events_.begin(), events_.end(), [&](const json & event) {
      return event["request_id"] == goal_id && event["terminal"].get<bool>() &&
        event["ok"].get<bool>() && event["execution_state"] == "known" &&
        event["goal_id"] == goal;
    });
  }));

  // Completion also produces a terminal correlated reply, then replay is
  // byte-identical (both reply and event) for the original envelope.
  ASSERT_TRUE(wait_for([&]() { return has_terminal_reply(goal_id); }));
  const json completed = replies_for(goal_id).back();
  EXPECT_EQ(completed["response"]["phase"], "result");
  EXPECT_TRUE(completed["response"]["terminal"].get<bool>());
  EXPECT_TRUE(completed["response"]["ok"].get<bool>());
  EXPECT_EQ(completed["response"]["goal_id"], goal);
  json terminal_event;
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    const auto it = std::find_if(events_.begin(), events_.end(), [&](const json & event) {
      return event["request_id"] == goal_id && event["terminal"].get<bool>();
    });
    ASSERT_NE(it, events_.end());
    terminal_event = *it;
  }
  const std::size_t events_before = events_.size();
  command("navigation.to_pose", goal_id,
    {{"lease_token", token},
      {"target", {{"frame_id", "map"}, {"position", {{"x", 0.1}}}}}});
  ASSERT_TRUE(wait_for([&]() { return replies_for(goal_id).size() >= 3; }));
  EXPECT_EQ(replies_for(goal_id).back()["response"], completed["response"]);
  ASSERT_TRUE(wait_for([&]() {
    const std::lock_guard<std::mutex> guard(mutex_);
    return events_.size() > events_before;
  }));
  const std::lock_guard<std::mutex> guard(mutex_);
  EXPECT_EQ(events_.back(), terminal_event);
}

TEST_F(GatewayIntegration, SilentServiceTimesOutWithUnknownExecutionState)
{
  // The mock lease service uses the deferred response callback and never
  // answers the "hang" owner: this is a genuinely silent endpoint.
  const std::string hang_id = "hang_0001";
  command("control.acquire", hang_id, {{"owner", "hang"}, {"duration_s", 5.0}});
  ASSERT_TRUE(wait_for([&]() { return !replies_for(hang_id).empty(); }, 5s));
  EXPECT_EQ(replies_for(hang_id).back()["response"]["code"], "RESPONSE_TIMEOUT");
  EXPECT_EQ(replies_for(hang_id).back()["response"]["execution_state"], "unknown");
  EXPECT_EQ(runtime_->silent_request_count(), 1u);
}

TEST_F(GatewayIntegration, AcceptedActionTimeoutKeepsUnknownExecutionState)
{
  const std::string acquire_id = "acquire_hang";
  command("control.acquire", acquire_id, {{"owner", "cpp_smoke"}, {"duration_s", 30.0}});
  ASSERT_TRUE(wait_for([&]() { return !replies_for(acquire_id).empty(); }));
  const std::string token = replies_for(acquire_id).back()["response"]["data"]["token"];

  const std::string hang_goal = "hang_goal1";
  command("navigation.to_pose", hang_goal,
    {{"lease_token", token},
      {"target", {{"frame_id", "map"}, {"position", {{"x", 42.0}}}}}});
  ASSERT_TRUE(wait_for([&]() { return !reply_with_phase(hang_goal, "accepted").is_null(); }));
  EXPECT_EQ(reply_with_phase(hang_goal, "accepted")["response"]["phase"], "accepted");
  // The server keeps the goal in EXECUTING state forever.
  ASSERT_TRUE(wait_for([&]() { return runtime_->silent_goal_count() >= 1; }));
  // action_timeout_s is 1.0 s in the fixture; the mock never finishes.
  ASSERT_TRUE(wait_for([&]() {
    const std::lock_guard<std::mutex> guard(mutex_);
    return std::any_of(events_.begin(), events_.end(), [&](const json & event) {
      return event["request_id"] == hang_goal && event["terminal"].get<bool>() &&
        event["code"] == "RESPONSE_TIMEOUT" && event["execution_state"] == "unknown";
    });
  }, 10s));
  // The correlated reply path also receives the terminal unknown result.
  ASSERT_TRUE(wait_for([&]() {
    return has_terminal_reply(hang_goal) &&
      replies_for(hang_goal).back()["response"]["code"] == "RESPONSE_TIMEOUT";
  }, 10s));
}

TEST_F(GatewayIntegration, UnknownBatterySerializesToNullAndVelocityFrameIsAssetBase)
{
  auto state_pub = client_->create_publisher<chaos_interfaces::msg::RobotState>("state", 10);
  auto velocity_sub = client_->create_subscription<chaos_interfaces::msg::VelocityCommand>(
    "motion/velocity", 32,
    [this](const chaos_interfaces::msg::VelocityCommand::SharedPtr message) {
      const std::lock_guard<std::mutex> guard(mutex_);
      velocity_frames_.push_back(message->header.frame_id);
      velocity_valid_for_.push_back(message->valid_for_s);
    });
  (void)velocity_sub;

  chaos_interfaces::msg::RobotState state;
  state.asset_id = "synthetic_unknown";
  state.battery_percentage = std::nan("");
  state_pub->publish(state);
  ASSERT_TRUE(wait_for([&]() {
    const std::lock_guard<std::mutex> guard(mutex_);
    return std::any_of(states_.begin(), states_.end(), [](const json & item) {
      return item["asset_id"] == "synthetic_unknown";
    });
  }));
  json telemetry;
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    telemetry = *std::find_if(states_.begin(), states_.end(), [](const json & item) {
      return item["asset_id"] == "synthetic_unknown";
    });
  }
  EXPECT_TRUE(telemetry["battery_percentage"].is_null());

  const std::string acquire_id = "acquire_frame";
  command("control.acquire", acquire_id, {{"owner", "cpp_smoke"}, {"duration_s", 15.0}});
  ASSERT_TRUE(wait_for([&]() { return !replies_for(acquire_id).empty(); }));
  const std::string token = replies_for(acquire_id).back()["response"]["data"]["token"];
  const std::string velocity_id = "velocity_frame";
  command("motion.velocity", velocity_id,
    {{"lease_token", token}, {"valid_for_s", 0.3}, {"velocity", {{"linear", {{"x", 0.1}}}}}});
  ASSERT_TRUE(wait_for([&]() { return !velocity_frames_.empty(); }));
  EXPECT_EQ(velocity_frames_.front(), "drone_001/base_link");
  ASSERT_TRUE(wait_for([&]() { return !replies_for(velocity_id).empty(); }));
}

// M13 acceptance: more than the historical 32 timed-out observations must not
// disturb a different, still valid goal. Every in-flight goal owns a
// dedicated action client, so retiring one observation cannot remove the
// callbacks of another goal.
TEST_F(GatewayIntegration, ManySilentGoalsDoNotStarveAValidGoal)
{
  create_gateway({{"request_timeout_s", 0.4}, {"action_timeout_s", 30.0}});
  std::this_thread::sleep_for(500ms);

  const std::string acquire_id = "acquire_many";
  command("control.acquire", acquire_id, {{"owner", "cpp_smoke"}, {"duration_s", 120.0}});
  ASSERT_TRUE(wait_for([&]() { return !replies_for(acquire_id).empty(); }, 15s));
  const std::string token = replies_for(acquire_id).back()["response"]["data"]["token"];

  // 40 goals that the server accepts but never completes. This is above the
  // 32 observations that used to trigger a shared client reset. Every goal is
  // published only after the gateway accepted the previous one, so the test
  // proves that all of them really entered the gateway instead of being
  // truncated by the queue depth of the public QoS profile.
  constexpr int kSilentGoals = 40;
  for (int i = 0; i < kSilentGoals; ++i) {
    const std::string request_id = "silent_goal_" + std::to_string(i);
    command("navigation.to_pose", request_id,
      {{"lease_token", token},
        {"target", {{"frame_id", "map"}, {"position", {{"x", 42.0}}}}}});
    ASSERT_TRUE(wait_for([&]() { return !reply_with_phase(request_id, "accepted").is_null(); }, 30s))
      << "goal " << i << " was never accepted";
  }
  // The server really holds 40 goals in EXECUTING state.
  ASSERT_TRUE(wait_for([&]() { return runtime_->silent_goal_count() >= kSilentGoals; }, 30s));

  // While all silent goals are still observed, a normal goal must keep
  // receiving feedback and finally succeed.
  const std::string valid_goal = "valid_goal_during_silence";
  command("navigation.to_pose", valid_goal,
    {{"lease_token", token},
      {"target", {{"frame_id", "map"}, {"position", {{"x", 0.25}}}}}});
  ASSERT_TRUE(wait_for([&]() { return !reply_with_phase(valid_goal, "accepted").is_null(); }, 30s));
  runtime_->release_feedback_goals();
  ASSERT_TRUE(wait_for([&]() {
    const std::lock_guard<std::mutex> guard(mutex_);
    return std::any_of(events_.begin(), events_.end(), [&](const json & event) {
      return event["request_id"] == valid_goal && event["phase"] == "feedback";
    });
  }, 30s));
  ASSERT_TRUE(wait_for([&]() {
    const std::lock_guard<std::mutex> guard(mutex_);
    return std::any_of(events_.begin(), events_.end(), [&](const json & event) {
      return event["request_id"] == valid_goal && event["terminal"].get<bool>() &&
        event["ok"].get<bool>() && event["execution_state"] == "known";
    });
  }, 30s));

  // The silent goals are still pending: their observation was not disturbed
  // by the successful goal.
  for (int i = 0; i < kSilentGoals; ++i) {
    const auto events = events_for("silent_goal_" + std::to_string(i));
    for (const auto & event : events) {
      EXPECT_FALSE(event["terminal"].get<bool>());
    }
  }
  const auto valid_replies = replies_for(valid_goal);
  ASSERT_TRUE(has_terminal_reply(valid_goal));
  EXPECT_TRUE(valid_replies.back()["response"]["ok"].get<bool>());
  EXPECT_EQ(valid_replies.back()["response"]["phase"], "result");
}

// A goal that is abandoned by the timeout must not be resurrected by a late
// result: no second terminal state, no changed replay cache.
TEST_F(GatewayIntegration, LateGoalResultAfterTimeoutIsIgnored)
{
  const std::string acquire_id = "acquire_late";
  command("control.acquire", acquire_id, {{"owner", "cpp_smoke"}, {"duration_s", 60.0}});
  ASSERT_TRUE(wait_for([&]() { return !replies_for(acquire_id).empty(); }));
  const std::string token = replies_for(acquire_id).back()["response"]["data"]["token"];

  const std::string goal_id = "late_goal";
  command("navigation.to_pose", goal_id,
    {{"lease_token", token},
      {"target", {{"frame_id", "map"}, {"position", {{"x", 43.0}}}}}});
  ASSERT_TRUE(wait_for([&]() { return !reply_with_phase(goal_id, "accepted").is_null(); }));
  EXPECT_EQ(reply_with_phase(goal_id, "accepted")["response"]["phase"], "accepted");

  // The observation times out (action_timeout_s is 1.0 s).
  ASSERT_TRUE(wait_for([&]() {
    return has_terminal_reply(goal_id) &&
      replies_for(goal_id).back()["response"]["code"] == "RESPONSE_TIMEOUT";
  }, 15s));
  const json timeout_reply = replies_for(goal_id).back();
  const std::size_t reply_count = replies_for(goal_id).size();
  const std::size_t event_count = events_for(goal_id).size();

  // The server now completes the goal. The gateway already retired the
  // observation, so nothing may be published.
  runtime_->complete_held_goals();
  std::this_thread::sleep_for(1500ms);
  EXPECT_EQ(replies_for(goal_id).size(), reply_count);
  EXPECT_EQ(events_for(goal_id).size(), event_count);
  EXPECT_EQ(replies_for(goal_id).back()["response"], timeout_reply["response"]);

  // The replay cache still holds the timeout reply.
  command("navigation.to_pose", goal_id,
    {{"lease_token", token},
      {"target", {{"frame_id", "map"}, {"position", {{"x", 43.0}}}}}});
  ASSERT_TRUE(wait_for([&]() { return replies_for(goal_id).size() > reply_count; }));
  EXPECT_EQ(replies_for(goal_id).back()["response"], timeout_reply["response"]);
}

// The number of simultaneously observed goals is bounded. Exceeding the
// capacity is rejected instead of growing the observation state.
TEST_F(GatewayIntegration, ActionSlotCapacityRejectsExcessGoals)
{
  create_gateway(
    {{"request_timeout_s", 0.4}, {"action_timeout_s", 30.0}, {"action_slot_capacity", 4}});
  std::this_thread::sleep_for(500ms);

  const std::string acquire_id = "acquire_capacity";
  command("control.acquire", acquire_id, {{"owner", "cpp_smoke"}, {"duration_s", 120.0}});
  ASSERT_TRUE(wait_for([&]() { return !replies_for(acquire_id).empty(); }, 15s));
  const std::string token = replies_for(acquire_id).back()["response"]["data"]["token"];

  for (int i = 0; i < 8; ++i) {
    const std::string request_id = "capacity_goal_" + std::to_string(i);
    command("navigation.to_pose", request_id,
      {{"lease_token", token},
        {"target", {{"frame_id", "map"}, {"position", {{"x", 42.0}}}}}});
    ASSERT_TRUE(wait_for([&]() { return !replies_for(request_id).empty(); }, 20s))
      << "goal " << i << " produced no reply";
  }
  int accepted = 0;
  int rejected = 0;
  for (int i = 0; i < 8; ++i) {
    const std::string request_id = "capacity_goal_" + std::to_string(i);
    const auto replies = replies_for(request_id);
    ASSERT_FALSE(replies.empty());
    const json accepted_reply = reply_with_phase(request_id, "accepted");
    if (!accepted_reply.is_null()) {
      ++accepted;
      continue;
    }
    // The excess observation is rejected with BUSY and a correlated reply.
    const json & reply = replies.back();
    EXPECT_EQ(reply["response"]["code"], "BUSY");
    EXPECT_FALSE(reply["response"]["ok"].get<bool>());
    ++rejected;
  }
  EXPECT_EQ(accepted, 4);
  EXPECT_EQ(rejected, 4);
  // Exactly the accepted goals are observed by the server.
  ASSERT_TRUE(wait_for([&]() { return runtime_->silent_goal_count() >= 4; }, 10s));
  EXPECT_EQ(runtime_->silent_goal_count(), 4u);
}

// Destroying the gateway while goals are still observed must not leave
// dangling callbacks: the dedicated clients are stopped and destroyed.
TEST_F(GatewayIntegration, GatewayDestructionWithPendingGoalIsClean)
{
  const std::string acquire_id = "acquire_destroy";
  command("control.acquire", acquire_id, {{"owner", "cpp_smoke"}, {"duration_s", 60.0}});
  ASSERT_TRUE(wait_for([&]() { return !replies_for(acquire_id).empty(); }));
  const std::string token = replies_for(acquire_id).back()["response"]["data"]["token"];

  const std::string goal_id = "destroy_goal";
  command("navigation.to_pose", goal_id,
    {{"lease_token", token},
      {"target", {{"frame_id", "map"}, {"position", {{"x", 42.0}}}}}});
  ASSERT_TRUE(wait_for([&]() { return !reply_with_phase(goal_id, "accepted").is_null(); }));
  EXPECT_EQ(reply_with_phase(goal_id, "accepted")["response"]["phase"], "accepted");

  // Remove the node from the executor first: after that no callback of the
  // gateway may run any more.
  executor_->remove_node(gateway_);
  gateway_.reset();
  std::this_thread::sleep_for(300ms);
  SUCCEED() << "gateway destroyed with a pending goal";
}

// Silent service requests and silent goals must not accumulate unbounded
// state, and the gateway must keep serving normal traffic afterwards.
TEST_F(GatewayIntegration, SilentEndpointsDoNotAccumulateUnboundedState)
{
  // 40 never-answered service requests: each expires via request_timeout_s
  // and must also drop rclcpp's internal pending request entry. The requests
  // are published in small batches and every batch is confirmed before the
  // next one is sent: the public queue depth of 32 must not silently drop
  // part of the burst.
  constexpr int kSilentRequests = 40;
  constexpr int kBatch = 8;
  for (int base = 0; base < kSilentRequests; base += kBatch) {
    for (int i = base; i < base + kBatch; ++i) {
      command("control.acquire", "hang_svc_" + std::to_string(i),
        {{"owner", "hang"}, {"duration_s", 5.0}});
    }
    ASSERT_TRUE(wait_for([&]() {
      for (int i = base; i < base + kBatch; ++i) {
        if (replies_for("hang_svc_" + std::to_string(i)).empty()) {
          return false;
        }
      }
      return true;
    }, 20s)) << "batch starting at " << base << " was not fully admitted";
  }
  for (int i = 0; i < kSilentRequests; ++i) {
    ASSERT_TRUE(has_terminal_reply("hang_svc_" + std::to_string(i)));
    EXPECT_EQ(replies_for("hang_svc_" + std::to_string(i)).back()["response"]["code"],
      "RESPONSE_TIMEOUT");
  }
  EXPECT_EQ(runtime_->silent_request_count(), static_cast<std::size_t>(kSilentRequests));

  // 40 accepted action goals that never finish: each expires via
  // action_timeout_s. Every observation owns a dedicated client, so the
  // middleware bookkeeping of exactly that goal disappears.
  const std::string acquire_id = "acquire_pressure";
  command("control.acquire", acquire_id, {{"owner", "cpp_smoke"}, {"duration_s", 60.0}});
  ASSERT_TRUE(wait_for([&]() { return !replies_for(acquire_id).empty(); }));
  const std::string token = replies_for(acquire_id).back()["response"]["data"]["token"];
  for (int i = 0; i < 40; ++i) {
    const std::string request_id = "hang_goal_" + std::to_string(i);
    command("navigation.to_pose", request_id,
      {{"lease_token", token},
        {"target", {{"frame_id", "map"}, {"position", {{"x", 42.0}}}}}});
    ASSERT_TRUE(wait_for([&]() { return !reply_with_phase(request_id, "accepted").is_null(); }, 30s))
      << "goal " << i << " was never accepted";
  }
  // All 40 goals are really observed by the server.
  ASSERT_TRUE(wait_for([&]() { return runtime_->silent_goal_count() >= 40; }, 30s));
  ASSERT_TRUE(wait_for([&]() {
    const std::lock_guard<std::mutex> guard(mutex_);
    return std::count_if(events_.begin(), events_.end(), [&](const json & event) {
      return event["terminal"].get<bool>() && event["code"] == "RESPONSE_TIMEOUT";
    }) >= 40;
  }, 40s));

  // After all the churn the gateway must still serve normal traffic.
  const std::string after_id = "after_pressure";
  command("system.describe", after_id);
  ASSERT_TRUE(wait_for([&]() { return !replies_for(after_id).empty(); }, 15s));
  EXPECT_TRUE(replies_for(after_id).back()["response"]["ok"].get<bool>());

  const std::string goal_id = "goal_after_pressure";
  command("navigation.to_pose", goal_id,
    {{"lease_token", token},
      {"target", {{"frame_id", "map"}, {"position", {{"x", 0.5}}}}}});
  ASSERT_TRUE(wait_for([&]() { return !reply_with_phase(goal_id, "accepted").is_null(); }, 20s));
  runtime_->release_feedback_goals();
  ASSERT_TRUE(wait_for([&]() {
    const std::lock_guard<std::mutex> guard(mutex_);
    return std::any_of(events_.begin(), events_.end(), [&](const json & event) {
      return event["request_id"] == goal_id && event["terminal"].get<bool>() &&
        event["ok"].get<bool>() && event["execution_state"] == "known";
    });
  }, 20s));
}

// The mock runtime of this fixture starts *without* the navigation action
// server. A navigation request therefore arrives before any server is
// discoverable: the gateway has to defer the dispatch instead of rejecting
// the command.
class GatewayIntegrationLateNavigation : public GatewayIntegration
{
protected:
  bool with_navigation_server() const override {return false;}
};

// Regression for the early UNAVAILABLE: a navigation request that arrives
// before the action server exists must not be rejected. The gateway defers
// the dispatch and sends the goal exactly once as soon as the server is
// reachable.
TEST_F(GatewayIntegrationLateNavigation, NavigationBeforeServerAppearsIsDeferredAndDispatchedOnce)
{
  // The lease is held by the mock runtime, so the token stays valid when the
  // gateway is recreated below.
  const std::string acquire_id = "acquire_late_nav";
  command("control.acquire", acquire_id, {{"owner", "cpp_smoke"}, {"duration_s", 60.0}});
  ASSERT_TRUE(wait_for([&]() { return !replies_for(acquire_id).empty(); }));
  const json acquired = replies_for(acquire_id).back();
  ASSERT_TRUE(acquired["response"]["ok"].get<bool>()) << acquired["response"].dump();
  const std::string token = acquired["response"]["data"]["token"].get<std::string>();

  // The deferred request has to survive until the action server appears, so
  // the correlation timeout of this gateway is generous.
  create_gateway({{"request_timeout_s", 10.0}, {"action_timeout_s", 5.0}});
  ASSERT_TRUE(wait_for_gateway()) << "the recreated gateway never answered a probe";

  const std::string goal_id = "late_nav_goal";
  command("navigation.to_pose", goal_id,
    {{"ttl_ms", 15000}, {"lease_token", token},
      {"target", {{"frame_id", "map"}, {"position", {{"x", 0.1}}}}}});

  // A duplicate of the still pending request is a replay of the correlation:
  // it must not create a second goal and it must not disturb the deferred
  // dispatch.
  command("navigation.to_pose", goal_id,
    {{"ttl_ms", 15000}, {"lease_token", token},
      {"target", {{"frame_id", "map"}, {"position", {{"x", 0.1}}}}}});

  // The action server does not exist yet. The gateway has to stay silent:
  // the old behaviour answered UNAVAILABLE immediately instead of deferring
  // the dispatch.
  ASSERT_FALSE(wait_for([&]() { return !replies_for(goal_id).empty(); }, 1s))
    << "the gateway answered before the action server existed: "
    << reply_diagnostics(goal_id);
  EXPECT_EQ(runtime_->received_goal_count(), 0u) << "the goal was sent without a server";

  // The server appears now: the deferred goal is dispatched and accepted.
  start_runtime_navigation_server();
  ASSERT_TRUE(wait_for([&]() { return !reply_with_phase(goal_id, "accepted").is_null(); }, 15s));
  const json accepted = reply_with_phase(goal_id, "accepted");
  EXPECT_TRUE(accepted["response"]["ok"].get<bool>());
  EXPECT_EQ(accepted["response"]["phase"], "accepted");
  EXPECT_FALSE(accepted["response"]["terminal"].get<bool>());

  // The goal runs to completion: the mock publishes feedback and a result.
  runtime_->release_feedback_goals();
  ASSERT_TRUE(wait_for([&]() { return has_terminal_reply(goal_id); }, 15s));
  const json completed = replies_for(goal_id).back();
  EXPECT_EQ(completed["response"]["phase"], "result");
  EXPECT_TRUE(completed["response"]["ok"].get<bool>());

  // The goal was dispatched exactly once: the server received a single goal
  // and the gateway reported a single acceptance. The duplicate of the
  // pending request did not create a second goal.
  EXPECT_EQ(runtime_->received_goal_count(), 1u);
  int acceptances = 0;
  for (const auto & reply : replies_for(goal_id)) {
    if (reply["response"].contains("phase") && reply["response"]["phase"] == "accepted") {
      ++acceptances;
    }
  }
  EXPECT_EQ(acceptances, 1);
}

// The other half of the deferral contract: a request whose correlation
// expires while the action server is still missing must never reach the
// robot, not even when the server appears afterwards.
TEST_F(GatewayIntegrationLateNavigation, NavigationThatExpiresBeforeTheServerAppearsIsNeverSent)
{
  // The fixture gateway correlates a request for 0.4 s: the request below
  // expires while the action server is still missing.
  const std::string goal_id = "expired_late_nav";
  command("navigation.to_pose", goal_id,
    {{"lease_token", "tok_late_nav"},
      {"target", {{"frame_id", "map"}, {"position", {{"x", 0.1}}}}}});

  ASSERT_TRUE(wait_for([&]() { return has_terminal_reply(goal_id); }, 10s));
  const json expired = replies_for(goal_id).back();
  EXPECT_EQ(expired["response"]["code"], "RESPONSE_TIMEOUT");
  EXPECT_TRUE(expired["response"]["terminal"].get<bool>());
  EXPECT_EQ(runtime_->received_goal_count(), 0u);

  // The server appears only now. The expired request must not be sent.
  start_runtime_navigation_server();
  ASSERT_FALSE(wait_for([&]() { return !reply_with_phase(goal_id, "accepted").is_null(); }, 2s))
    << "an expired request was dispatched after the server appeared";
  EXPECT_EQ(runtime_->received_goal_count(), 0u);
  EXPECT_EQ(replies_for(goal_id).back()["response"], expired["response"]);
}

// The original TTL of the command bounds the dispatch, not only the response
// timeout of the correlation: a goal whose TTL elapsed while the action
// server was missing must never reach the robot.
TEST_F(GatewayIntegrationLateNavigation, NavigationWithElapsedTtlIsNeverSent)
{
  // The correlation of this gateway lives far longer than the TTL of the
  // command below, so only the original TTL can stop the deferred dispatch.
  create_gateway({{"request_timeout_s", 10.0}, {"action_timeout_s", 5.0}});
  ASSERT_TRUE(wait_for_gateway()) << "the recreated gateway never answered a probe";

  const std::string goal_id = "ttl_expired_nav";
  command("navigation.to_pose", goal_id,
    {{"ttl_ms", 100},
      {"lease_token", "tok_ttl_nav"},
      {"target", {{"frame_id", "map"}, {"position", {{"x", 0.1}}}}}});

  // The action server does not exist, so the dispatch is deferred. The TTL
  // elapses long before the response timeout of the correlation.
  ASSERT_TRUE(wait_for([&]() { return has_terminal_reply(goal_id); }, 5s))
    << "the deferred request was neither expired nor answered";
  const json expired = replies_for(goal_id).back();
  EXPECT_EQ(expired["response"]["code"], "EXPIRED");
  EXPECT_TRUE(expired["response"]["terminal"].get<bool>());
  EXPECT_FALSE(expired["response"]["ok"].get<bool>());
  EXPECT_EQ(runtime_->received_goal_count(), 0u) << "the goal was sent without a server";

  // The server appears only now. The expired request must not be sent.
  start_runtime_navigation_server();
  ASSERT_FALSE(wait_for([&]() { return !reply_with_phase(goal_id, "accepted").is_null(); }, 2s))
    << "a request with an elapsed TTL was dispatched after the server appeared";
  EXPECT_EQ(runtime_->received_goal_count(), 0u);
  EXPECT_EQ(replies_for(goal_id).back()["response"], expired["response"]);
}

}  // namespace

int main(int argc, char ** argv)
{
  setenv("ROS_DOMAIN_ID", "188", 1);
  setenv("ROS_AUTOMATIC_DISCOVERY_RANGE", "LOCALHOST", 1);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
