#include "chaos_mqtt/gateway_node.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <sstream>
#include <utility>

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "geometry_msgs/msg/twist_stamped.hpp"

namespace chaos_mqtt
{
namespace
{

std::int64_t epoch_ms_now()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
}

double monotonic_now_s()
{
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string goal_id_hex(const std::array<uint8_t, 16> & uuid)
{
  std::ostringstream hex;
  hex << std::hex;
  for (uint8_t byte : uuid) {
    hex.width(2);
    hex.fill('0');
    hex << static_cast<int>(byte);
  }
  return hex.str();
}

Json header_json(const std_msgs::msg::Header & header)
{
  return Json{
    {"stamp", Json{{"sec", header.stamp.sec}, {"nanosec", header.stamp.nanosec}}},
    {"frame_id", header.frame_id},
  };
}

Json pose_json(const geometry_msgs::msg::Pose & pose)
{
  return Json{
    {"position", Json{{"x", pose.position.x}, {"y", pose.position.y}, {"z", pose.position.z}}},
    {"orientation", Json{{"x", pose.orientation.x}, {"y", pose.orientation.y},
        {"z", pose.orientation.z}, {"w", pose.orientation.w}}},
  };
}

Json pose_stamped_json(const geometry_msgs::msg::PoseStamped & pose)
{
  return Json{{"header", header_json(pose.header)}, {"pose", pose_json(pose.pose)}};
}

Json twist_json(const geometry_msgs::msg::Twist & twist)
{
  return Json{
    {"linear", Json{{"x", twist.linear.x}, {"y", twist.linear.y}, {"z", twist.linear.z}}},
    {"angular", Json{{"x", twist.angular.x}, {"y", twist.angular.y}, {"z", twist.angular.z}}},
  };
}

Json state_json(const chaos_interfaces::msg::RobotState & state)
{
  return Json{
    {"stamp", Json{{"sec", state.stamp.sec}, {"nanosec", state.stamp.nanosec}}},
    {"asset_id", state.asset_id},
    {"model", state.model},
    {"operational_state", state.operational_state},
    {"connected", state.connected},
    {"ready", state.ready},
    {"pose_valid", state.pose_valid},
    {"pose", pose_stamped_json(state.pose)},
    {"twist", Json{{"header", header_json(state.twist.header)}, {"twist", twist_json(state.twist.twist)}}},
    {"battery_percentage", state.battery_percentage},
    {"detail", state.detail},
  };
}

Json response_data(const chaos_interfaces::srv::GetDescription::Response & result)
{
  // GetDescription carries no success/code/message; the gateway reports OK.
  Json capabilities = Json::array();
  for (const auto & capability : result.capabilities) {
    capabilities.push_back(capability);
  }
  return Json{
    {"asset_id", result.asset_id},
    {"model", result.model},
    {"model_version", result.model_version},
    {"capabilities", std::move(capabilities)},
    {"base_frame", result.base_frame},
    {"odom_frame", result.odom_frame},
    {"map_frame", result.map_frame},
    {"linear_limit_mps", result.linear_limit_mps},
    {"angular_limit_rps", result.angular_limit_rps},
    {"command_timeout_s", result.command_timeout_s},
    {"lease_max_s", result.lease_max_s},
  };
}

Json response_data(const chaos_interfaces::srv::ControlLease::Response & result)
{
  return Json{
    {"success", result.success},
    {"code", result.code},
    {"message", result.message},
    {"token", result.token},
    {"remaining_s", result.remaining_s},
  };
}

Json response_data(const chaos_interfaces::srv::Stop::Response & result)
{
  return Json{
    {"success", result.success},
    {"code", result.code},
    {"message", result.message},
  };
}

}  // namespace

builtin_interfaces::msg::Time GatewayNode::deadline_time(double deadline_ms)
{
  // Unix UTC, deliberately independent of /clock. Rounding down cannot grant
  // the remote command any additional validity.
  const auto deadline_ns = static_cast<std::int64_t>(deadline_ms) * 1000000;
  builtin_interfaces::msg::Time time;
  time.sec = static_cast<std::int32_t>(deadline_ns / 1000000000);
  time.nanosec = static_cast<std::uint32_t>(deadline_ns % 1000000000);
  if (time.sec <= 0 || time.sec >= (std::int32_t{1} << 31)) {
    throw CommandError("INVALID_ARGUMENT", "command deadline is outside ROS Time range");
  }
  return time;
}

GatewayNode::GatewayNode(const rclcpp::NodeOptions & options)
: rclcpp::Node("mqtt_gateway", options)
{
  declare_parameter<std::string>("asset_json", "{}");
  declare_parameter<std::int64_t>("maximum_clock_skew_ms", 5000);
  declare_parameter<double>("request_timeout_s", 5.0);
  declare_parameter<double>("action_timeout_s", 300.0);
  try {
    asset_ = Json::parse(get_parameter("asset_json").as_string());
  } catch (const std::exception & error) {
    throw std::invalid_argument(std::string("asset_json is not valid JSON: ") + error.what());
  }
  const auto id = asset_.contains("id") ? asset_.find("id") : asset_.find("asset_id");
  if (id == asset_.end() || !id->is_string() || id->get<std::string>().empty()) {
    throw std::invalid_argument("asset_json requires an id and capabilities");
  }
  asset_id_ = id->get<std::string>();
  const auto capabilities = asset_.find("capabilities");
  if (capabilities == asset_.end() || !capabilities->is_array()) {
    throw std::invalid_argument("asset_json requires an id and capabilities");
  }
  for (const auto & capability : *capabilities) {
    if (capability.is_string()) {
      capabilities_.push_back(capability.get<std::string>());
    }
  }
  clock_skew_ms_ = get_parameter("maximum_clock_skew_ms").as_int();
  request_timeout_s_ = get_parameter("request_timeout_s").as_double();
  action_timeout_s_ = get_parameter("action_timeout_s").as_double();
  if (clock_skew_ms_ < 100 || clock_skew_ms_ > 30000) {
    throw std::invalid_argument("maximum_clock_skew_ms must be in 100..30000");
  }
  if (request_timeout_s_ < 0.1 || request_timeout_s_ > 600 ||
    action_timeout_s_ < 1 || action_timeout_s_ > 86400)
  {
    throw std::invalid_argument("invalid gateway timeout");
  }

  result_pub_ = create_publisher<std_msgs::msg::String>("mqtt/command/result", 32);
  event_pub_ = create_publisher<std_msgs::msg::String>("mqtt/task_events", 32);
  state_pub_ = create_publisher<std_msgs::msg::String>("mqtt/state", 32);
  velocity_pub_ = create_publisher<chaos_interfaces::msg::VelocityCommand>("motion/velocity", 32);
  command_sub_ = create_subscription<std_msgs::msg::String>(
    "mqtt/command", 32, [this](std_msgs::msg::String::SharedPtr message) {
      command_callback(message);
    });
  velocity_result_sub_ = create_subscription<chaos_interfaces::msg::CommandResult>(
    "command_results", 32, [this](chaos_interfaces::msg::CommandResult::SharedPtr message) {
      const std::lock_guard<std::mutex> guard(records_mutex_);
      Pending * pending = records_.pending(message->request_id);
      if (!pending || pending->command.name != "motion.velocity") {
        return;
      }
      Json reply = response(
        message->request_id, pending->command.name, message->success, message->code,
        message->message);
      publish_json(result_pub_, reply);
      records_.finish(message->request_id, reply);
    });
  state_sub_ = create_subscription<chaos_interfaces::msg::RobotState>(
    "state", 32, [this](chaos_interfaces::msg::RobotState::SharedPtr message) {
      state_callback(message);
    });
  describe_client_ = create_client<chaos_interfaces::srv::GetDescription>("description");
  lease_client_ = create_client<chaos_interfaces::srv::ControlLease>("control/lease");
  stop_client_ = create_client<chaos_interfaces::srv::Stop>("motion/stop");
  navigation_client_ = rclcpp_action::create_client<NavigatePose>(this, "navigation/to_pose");
  takeoff_client_ = rclcpp_action::create_client<Flight>(this, "flight/takeoff");
  land_client_ = rclcpp_action::create_client<Flight>(this, "flight/land");
  // Subscribe directly so a missing goal acknowledgment does not leave a
  // per-request feedback callback registered in the shared action client.
  navigation_feedback_sub_ = create_subscription<NavigatePose::Impl::FeedbackMessage>(
    "navigation/to_pose/_action/feedback", 32,
    [this](NavigatePose::Impl::FeedbackMessage::SharedPtr message) {
      feedback_callback<NavigatePose>("navigation.to_pose", message);
    });
  takeoff_feedback_sub_ = create_subscription<Flight::Impl::FeedbackMessage>(
    "flight/takeoff/_action/feedback", 32,
    [this](Flight::Impl::FeedbackMessage::SharedPtr message) {
      feedback_callback<Flight>("flight.takeoff", message);
    });
  land_feedback_sub_ = create_subscription<Flight::Impl::FeedbackMessage>(
    "flight/land/_action/feedback", 32,
    [this](Flight::Impl::FeedbackMessage::SharedPtr message) {
      feedback_callback<Flight>("flight.land", message);
    });
  expire_timer_ = create_wall_timer(std::chrono::milliseconds(100), [this]() { expire(); });
}

GatewayNode::~GatewayNode()
{
  std::vector<std::string> request_ids;
  {
    const std::lock_guard<std::mutex> guard(records_mutex_);
    for (const auto & item : records_.pending_map()) {
      request_ids.push_back(item.first);
    }
  }
  for (const auto & request_id : request_ids) {
    if (rclcpp::ok()) {
      service_error(
        request_id, "GATEWAY_SHUTDOWN",
        "gateway shutting down; execution state is unknown");
    } else {
      const std::lock_guard<std::mutex> guard(records_mutex_);
      records_.finish(request_id, nullptr);
    }
  }
}

void GatewayNode::publish_json(
  const rclcpp::Publisher<std_msgs::msg::String>::SharedPtr & publisher, const Json & value)
{
  std_msgs::msg::String message;
  message.data = wire_value(value).dump();
  publisher->publish(message);
}

void GatewayNode::state_callback(const chaos_interfaces::msg::RobotState::SharedPtr message)
{
  publish_json(state_pub_, state_json(*message));
}

void GatewayNode::command_callback(const std_msgs::msg::String::SharedPtr message)
{
  const auto identity = recover_identity(message->data);
  if (identity) {
    const auto & request_id = identity->first;
    std::lock_guard<std::mutex> guard(records_mutex_);
    if (const auto * record = records_.completed(request_id)) {
      if (!record->first.is_null()) {
        publish_json(result_pub_, record->first);
      }
      if (!record->second.is_null()) {
        publish_json(event_pub_, record->second);
      }
      return;
    }
    if (const Pending * pending = records_.pending(request_id)) {
      if (pending->reply.has_value()) {
        publish_json(result_pub_, *pending->reply);
      }
      return;
    }
  }
  Command command;
  try {
    command = parse_command(message->data, capabilities_, epoch_ms_now(), clock_skew_ms_);
  } catch (const CommandError & error) {
    if (identity) {
      const std::lock_guard<std::mutex> guard(records_mutex_);
      Json reply = response(
        identity->first, identity->second, false, error.code(), error.what());
      publish_json(result_pub_, reply);
      records_.finish(identity->first, reply);
    } else {
      // Never log payloads or tokens; the error code is sufficient to debug.
      RCLCPP_WARN(get_logger(), "Dropped uncorrelatable MQTT command: %s", error.code().c_str());
    }
    return;
  }
  {
    const std::lock_guard<std::mutex> guard(records_mutex_);
    try {
      records_.start(command, monotonic_now_s(), request_timeout_s_);
    } catch (const CommandError & error) {
      Json reply = response(command.request_id, command.name, false, error.code(), error.what());
      publish_json(result_pub_, reply);
      records_.finish(command.request_id, reply);
      return;
    }
  }
  try {
    dispatch(command);
  } catch (const CommandError & error) {
    service_error(command.request_id, error.code(), error.what());
  } catch (const std::exception & error) {
    service_error(command.request_id, "DISPATCH_ERROR", error.what());
  }
}

void GatewayNode::dispatch(const Command & command)
{
  const auto & name = command.name;
  const auto & args = command.arguments;
  if (command.deadline_ms <= static_cast<double>(epoch_ms_now())) {
    throw CommandError("EXPIRED", "command expired before dispatch");
  }
  if (name == "system.describe") {
    send_service<chaos_interfaces::srv::GetDescription>(command, describe_client_,
      chaos_interfaces::srv::GetDescription::Request{});
  } else if (name.rfind("control.", 0) == 0) {
    chaos_interfaces::srv::ControlLease::Request request{};
    const auto operation = name.substr(std::string("control.").size());
    request.operation = operation;
    request.owner = args.at("owner").get<std::string>();
    request.token = args.at("token").get<std::string>();
    request.duration_s = args.at("duration_s").get<double>();
    request.expires_at = deadline_time(command.deadline_ms);
    send_service<chaos_interfaces::srv::ControlLease>(command, lease_client_, request);
  } else if (name == "motion.stop") {
    chaos_interfaces::srv::Stop::Request request{};
    request.request_id = command.request_id;
    request.lease_token = args.at("lease_token").get<std::string>();
    request.expires_at = deadline_time(command.deadline_ms);
    send_service<chaos_interfaces::srv::Stop>(command, stop_client_, request);
  } else if (name == "motion.velocity") {
    const double remaining = (command.deadline_ms - static_cast<double>(epoch_ms_now())) / 1000.0;
    if (remaining <= 0) {
      throw CommandError("EXPIRED", "velocity expired before dispatch");
    }
    chaos_interfaces::msg::VelocityCommand request;
    request.header.stamp = get_clock()->now();
    std::string frame_id = "base_link";
    const auto frames = asset_.find("frames");
    if (frames != asset_.end() && frames->is_object() && frames->contains("base") &&
      frames->at("base").is_string())
    {
      frame_id = frames->at("base").get<std::string>();
    }
    request.header.frame_id = frame_id;
    request.request_id = command.request_id;
    request.lease_token = args.at("lease_token").get<std::string>();
    double command_timeout = 0.5;
    const auto limits = asset_.find("limits");
    if (limits != asset_.end() && limits->is_object() && limits->contains("command_timeout_s") &&
      limits->at("command_timeout_s").is_number())
    {
      command_timeout = limits->at("command_timeout_s").get<double>();
    }
    const double valid_for_s = args.at("valid_for_s").get<double>();
    if (valid_for_s > command_timeout) {
      throw CommandError("INVALID_ARGUMENT", "valid_for_s exceeds asset command_timeout_s");
    }
    request.valid_for_s = std::min(remaining, valid_for_s);
    for (const char * kind : {"linear", "angular"}) {
      const Json & axes = args.at("velocity").at(kind);
      auto & target = kind[0] == 'l' ? request.velocity.linear : request.velocity.angular;
      target.x = axes.at("x").get<double>();
      target.y = axes.at("y").get<double>();
      target.z = axes.at("z").get<double>();
    }
    velocity_pub_->publish(request);
  } else {
    send_action(command);
  }
}

template<typename ServiceT>
void GatewayNode::send_service(
  const Command & command,
  typename rclcpp::Client<ServiceT>::SharedPtr client,
  typename ServiceT::Request request)
{
  const std::string request_id = command.request_id;
  if (!client->service_is_ready()) {
    service_error(request_id, "UNAVAILABLE", "runtime service is not ready");
    return;
  }
  auto cancelled = std::make_shared<std::atomic_bool>(false);
  {
    const std::lock_guard<std::mutex> guard(records_mutex_);
    if (Pending * pending = records_.pending(request_id)) {
      // Cancels local observation only; the runtime still executes.
      pending->cancel = [cancelled]() { cancelled->store(true); };
    }
  }
  client->async_send_request(
    std::make_shared<typename ServiceT::Request>(std::move(request)),
    [this, request_id, cancelled](typename rclcpp::Client<ServiceT>::SharedFutureWithRequest future) {
      if (cancelled->load()) {
        return;
      }
      Pending * pending = nullptr;
      {
        const std::lock_guard<std::mutex> guard(records_mutex_);
        pending = records_.pending(request_id);
      }
      if (!pending) {
        return;
      }
      try {
        Json data = response_data(*future.get().second);
        const bool success = data.value("success", true);
        const std::string code = data.value("code", "OK");
        const std::string message = data.value("message", "");
        data.erase("success");
        data.erase("code");
        data.erase("message");
        const std::string command_name = pending->command.name;
        Json reply = response(
          request_id, command_name, success, code, message, "result", data);
        publish_json(result_pub_, reply);
        const std::lock_guard<std::mutex> guard(records_mutex_);
        records_.finish(request_id, reply);
      } catch (const std::exception & error) {
        service_error(request_id, "RUNTIME_ERROR", error.what());
      }
    });
}

void GatewayNode::send_action(const Command & command)
{
  const auto & name = command.name;
  if (name == "navigation.to_pose") {
    send_typed_action<NavigatePose>(command, navigation_client_);
  } else if (name == "flight.takeoff") {
    send_typed_action<Flight>(command, takeoff_client_);
  } else if (name == "flight.land") {
    send_typed_action<Flight>(command, land_client_);
  } else {
    service_error(command.request_id, "UNSUPPORTED", "unknown command");
  }
}

template<typename ActionT>
void GatewayNode::send_typed_action(
  const Command & command,
  typename rclcpp_action::Client<ActionT>::SharedPtr client)
{
  const std::string request_id = command.request_id;
  const std::string command_name = command.name;
  if (!client->action_server_is_ready()) {
    service_error(request_id, "UNAVAILABLE", "runtime action is not ready");
    return;
  }
  typename ActionT::Goal goal;
  if constexpr (std::is_same_v<ActionT, NavigatePose>) {
    const Json & target = command.arguments.at("target");
    goal.target.header.stamp = get_clock()->now();
    goal.target.header.frame_id = target.at("frame_id").get<std::string>();
    goal.target.pose.position.x = target.at("position").at("x").get<double>();
    goal.target.pose.position.y = target.at("position").at("y").get<double>();
    goal.target.pose.position.z = target.at("position").at("z").get<double>();
    goal.target.pose.orientation.x = target.at("orientation").at("x").get<double>();
    goal.target.pose.orientation.y = target.at("orientation").at("y").get<double>();
    goal.target.pose.orientation.z = target.at("orientation").at("z").get<double>();
    goal.target.pose.orientation.w = target.at("orientation").at("w").get<double>();
  } else {
    goal.altitude_m = command.arguments.at("altitude_m").get<double>();
  }
  goal.lease_token = command.arguments.at("lease_token").get<std::string>();
  goal.expires_at = deadline_time(command.deadline_ms);

  typename rclcpp_action::Client<ActionT>::SendGoalOptions options;
  options.goal_response_callback =
    [this, request_id, command_name](
      typename rclcpp_action::ClientGoalHandle<ActionT>::SharedPtr handle) {
      if (!handle) {
        service_error(request_id, "REJECTED", "runtime rejected action goal");
        return;
      }
      try {
        std::lock_guard<std::mutex> guard(records_mutex_);
        Pending * pending = records_.pending(request_id);
        if (!pending) {
          return;
        }
        pending->accepted = true;
        pending->expires_at = monotonic_now_s() + action_timeout_s_;
        const std::string goal_hex = goal_id_hex(handle->get_goal_id());
        goal_ids_[request_id] = goal_hex;
        Json reply = response(
          request_id, command_name, true, "ACCEPTED",
          "action accepted; completion arrives on task_events", "accepted",
          Json{{"goal_id", goal_hex}});
        pending->reply = reply;
        publish_json(result_pub_, reply);
      } catch (const std::exception & error) {
        service_error(request_id, "RUNTIME_ERROR", error.what());
      }
    };
  options.result_callback =
    [this, request_id, command_name](
      const typename rclcpp_action::ClientGoalHandle<ActionT>::WrappedResult & wrapped) {
      // Jazzy delivers the result as a shared pointer; it is null when the
      // final status is UNKNOWN (server vanished before a result existed).
      const bool success = wrapped.code == rclcpp_action::ResultCode::SUCCEEDED &&
        wrapped.result && wrapped.result->success;
      Json data{
        {"ok", success},
        {"code", wrapped.result ? wrapped.result->code : ""},
        {"message", wrapped.result ? wrapped.result->message : ""},
        {"ros_goal_status", static_cast<int>(wrapped.code)},
        {"execution_state", "known"},
      };
      action_done(request_id, command_name, wrapped.code, data);
    };
  client->async_send_goal(goal, options);
}

template<typename ActionT>
void GatewayNode::feedback_callback(
  const std::string & command_name,
  const typename ActionT::Impl::FeedbackMessage::SharedPtr message)
{
  const std::string goal_hex = goal_id_hex(message->goal_id.uuid);
  Json feedback;
  if constexpr (std::is_same_v<ActionT, NavigatePose>) {
    feedback = Json{
      {"current_pose", pose_stamped_json(message->feedback.current_pose)},
      {"progress", message->feedback.progress},
    };
  } else {
    feedback = Json{
      {"altitude_m", message->feedback.altitude_m},
      {"progress", message->feedback.progress},
    };
  }
  (void)command_name;
  std::lock_guard<std::mutex> guard(records_mutex_);
  for (const auto & item : records_.pending_map()) {
    if (item.second.command.name != command_name) {
      continue;
    }
    const auto goal = goal_ids_.find(item.first);
    if (goal != goal_ids_.end() && goal->second == goal_hex) {
      event(item.first, "feedback", Json{{"feedback", std::move(feedback)}});
      break;
    }
  }
}

Json GatewayNode::event(const std::string & request_id, const std::string & phase, const Json & data)
{
  Pending * pending = records_.pending(request_id);
  if (!pending) {
    return nullptr;
  }
  Json value{
    {"version", 1},
    {"asset_id", asset_id_},
    {"request_id", request_id},
    {"command", pending->command.name},
    {"timestamp_ms", epoch_ms_now()},
    {"phase", phase},
    {"terminal", phase == "result"},
  };
  for (const auto & item : data.items()) {
    value[item.key()] = item.value();
  }
  const auto goal = goal_ids_.find(request_id);
  if (goal != goal_ids_.end()) {
    value["goal_id"] = goal->second;
  }
  publish_json(event_pub_, value);
  return value;
}

void GatewayNode::action_done(
  const std::string & request_id,
  const std::string & command_name,
  const rclcpp_action::ResultCode & code,
  const Json & data)
{
  (void)command_name;
  (void)code;
  std::lock_guard<std::mutex> guard(records_mutex_);
  Pending * pending = records_.pending(request_id);
  if (!pending) {
    return;
  }
  const Json terminal_event = event(request_id, "result", data);
  complete_action(*pending, terminal_event);
}

void GatewayNode::complete_action(Pending & pending, const Json & terminal_event)
{
  // Task events remain compatible, while the correlated response path also
  // receives completion. The Rust bridge can replace ACCEPTED in its replay
  // cache even if the task_events subscriber was offline.
  Json extra = Json::object();
  for (const char * key : {"goal_id", "ros_goal_status", "execution_state"}) {
    if (terminal_event.contains(key)) {
      extra[key] = terminal_event.at(key);
    }
  }
  const std::string request_id = pending.command.request_id;
  Json reply = response(
    request_id, pending.command.name, terminal_event.at("ok").get<bool>(),
    terminal_event.at("code").get<std::string>(), terminal_event.at("message").get<std::string>(),
    "result", extra);
  publish_json(result_pub_, reply);
  records_.finish(request_id, reply, terminal_event);
}

void GatewayNode::service_error(const std::string & request_id, const std::string & code,
  const std::string & message)
{
  std::lock_guard<std::mutex> guard(records_mutex_);
  Pending * pending = records_.pending(request_id);
  if (!pending) {
    return;
  }
  if (pending->accepted) {
    const Json terminal_event = event(
      request_id, "result",
      Json{{"ok", false}, {"code", code}, {"message", message}, {"execution_state", "unknown"}});
    complete_action(*pending, terminal_event);
  } else {
    Json reply = response(
      request_id, pending->command.name, false, code, message, "result",
      Json{{"execution_state", "unknown"}});
    publish_json(result_pub_, reply);
    records_.finish(request_id, reply);
  }
}

void GatewayNode::expire()
{
  std::vector<std::string> request_ids;
  {
    const std::lock_guard<std::mutex> guard(records_mutex_);
    request_ids = records_.expired(monotonic_now_s());
  }
  for (const auto & request_id : request_ids) {
    service_error(
      request_id, "RESPONSE_TIMEOUT",
      "runtime result timed out; execution state is unknown, not stopped");
  }
}

template void GatewayNode::send_service<chaos_interfaces::srv::GetDescription>(
  const Command &, rclcpp::Client<chaos_interfaces::srv::GetDescription>::SharedPtr,
  chaos_interfaces::srv::GetDescription::Request);
template void GatewayNode::send_service<chaos_interfaces::srv::ControlLease>(
  const Command &, rclcpp::Client<chaos_interfaces::srv::ControlLease>::SharedPtr,
  chaos_interfaces::srv::ControlLease::Request);
template void GatewayNode::send_service<chaos_interfaces::srv::Stop>(
  const Command &, rclcpp::Client<chaos_interfaces::srv::Stop>::SharedPtr,
  chaos_interfaces::srv::Stop::Request);
template void GatewayNode::send_typed_action<chaos_interfaces::action::NavigatePose>(
  const Command &, rclcpp_action::Client<chaos_interfaces::action::NavigatePose>::SharedPtr);
template void GatewayNode::send_typed_action<chaos_interfaces::action::Flight>(
  const Command &, rclcpp_action::Client<chaos_interfaces::action::Flight>::SharedPtr);
template void GatewayNode::feedback_callback<chaos_interfaces::action::NavigatePose>(
  const std::string &, chaos_interfaces::action::NavigatePose::Impl::FeedbackMessage::SharedPtr);
template void GatewayNode::feedback_callback<chaos_interfaces::action::Flight>(
  const std::string &, chaos_interfaces::action::Flight::Impl::FeedbackMessage::SharedPtr);

}  // namespace chaos_mqtt
