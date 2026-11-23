#include "chaos_mqtt/gateway_node.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <type_traits>
#include <utility>

#include <builtin_interfaces/msg/time.hpp>
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "geometry_msgs/msg/twist_stamped.hpp"

namespace chaos_mqtt
{
namespace
{

/// Seconds between two publications of an intent that was not accepted yet.
constexpr double kSkillRetryIntervalS = 0.5;

/// Unix milliseconds; the command deadline is an absolute UTC instant and is
/// never derived from the ROS clock (which may be simulated or paused).
std::int64_t epoch_ms_now()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
}

/// Monotonic seconds; used for every timeout and lease so that a wall clock
/// step can neither shorten nor extend a deadline.
double monotonic_now_s()
{
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// Seconds and nanoseconds of a ROS timestamp.
Json time_json(const builtin_interfaces::msg::Time & stamp)
{
  return Json{{"sec", stamp.sec}, {"nanosec", stamp.nanosec}};
}

Json header_json(const std_msgs::msg::Header & header)
{
  return Json{
    {"stamp", time_json(header.stamp)},
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
    {"stamp", time_json(state.stamp)},
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

// Keep response envelope fields separate from response.data.
struct ServiceReply
{
  bool success = true;
  std::string code = "OK";
  std::string message;
  Json data;
};

ServiceReply service_reply(const chaos_interfaces::srv::GetDescription::Response & result)
{
  // GetDescription carries no success/code/message; the gateway reports OK.
  return ServiceReply{true, "OK", "", Json{
      {"asset_id", result.asset_id},
      {"model", result.model},
      {"model_version", result.model_version},
      {"capabilities", result.capabilities},
      {"base_frame", result.base_frame},
      {"odom_frame", result.odom_frame},
      {"map_frame", result.map_frame},
      {"linear_limit_mps", result.linear_limit_mps},
      {"angular_limit_rps", result.angular_limit_rps},
      {"command_timeout_s", result.command_timeout_s},
      {"lease_max_s", result.lease_max_s},
  }};
}

ServiceReply service_reply(const chaos_interfaces::srv::ControlLease::Response & result)
{
  return ServiceReply{result.success, result.code, result.message, Json{
      {"token", result.token},
      {"remaining_s", result.remaining_s},
  }};
}

ServiceReply service_reply(const chaos_interfaces::srv::Stop::Response & result)
{
  return ServiceReply{result.success, result.code, result.message, Json::object()};
}

}  // namespace

builtin_interfaces::msg::Time GatewayNode::deadline_time(double deadline_ms)
{
  // Unix UTC, deliberately independent of /clock. Rounding down cannot grant
  // the remote command any additional validity. Validate in 64-bit before
  // narrowing into the ROS Time message: the upper bound is 2^31 seconds
  // (2038), and zero/negative deadlines are rejected per the wire contract.
  if (!std::isfinite(deadline_ms)) {
    throw CommandError("INVALID_ARGUMENT", "command deadline is outside ROS Time range");
  }
  const auto deadline_ns = static_cast<std::int64_t>(deadline_ms) * 1000000;
  const auto seconds = deadline_ns / 1000000000;
  if (seconds <= 0 || seconds >= (std::int64_t{1} << 31)) {
    throw CommandError("INVALID_ARGUMENT", "command deadline is outside ROS Time range");
  }
  builtin_interfaces::msg::Time time;
  time.sec = static_cast<std::int32_t>(seconds);
  time.nanosec = static_cast<std::uint32_t>(deadline_ns % 1000000000);
  return time;
}

GatewayNode::GatewayNode(const rclcpp::NodeOptions & options)
: rclcpp::Node("mqtt_gateway", options),
  navigation_pool_(this, "navigation/to_pose", "navigation.to_pose", this, 48),
  takeoff_pool_(this, "flight/takeoff", "flight.takeoff", this, 48),
  land_pool_(this, "flight/land", "flight.land", this, 48),
  ota_pool_(this, "system/ota", "system.ota", this, 48)
{
  declare_parameter<std::string>("asset_json", "{}");
  declare_parameter<std::int64_t>("maximum_clock_skew_ms", 5000);
  declare_parameter<double>("request_timeout_s", 5.0);
  declare_parameter<double>("action_timeout_s", 300.0);
  // Upper bound of goals that may be observed at the same time. Every
  // observation owns a dedicated action client, so this parameter bounds the
  // number of live action clients and of pending middleware requests.
  declare_parameter<int64_t>("action_slot_capacity", 48);
  // Upper bound of skill requests that are observed at the same time. The
  // skill layer runs one skill at a time, so the gateway does not need many
  // of them; the bound keeps the state of the gateway finite.
  declare_parameter<int64_t>("skill_request_capacity", 48);
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
  const auto slot_capacity = get_parameter("action_slot_capacity").as_int();
  if (clock_skew_ms_ < 100 || clock_skew_ms_ > 30000) {
    throw std::invalid_argument("maximum_clock_skew_ms must be in 100..30000");
  }
  if (!std::isfinite(request_timeout_s_) || !std::isfinite(action_timeout_s_) ||
    request_timeout_s_ < 0.1 || request_timeout_s_ > 600 ||
    action_timeout_s_ < 1 || action_timeout_s_ > 86400)
  {
    throw std::invalid_argument("invalid gateway timeout");
  }
  if (slot_capacity < 1 || slot_capacity > 256) {
    throw std::invalid_argument("action_slot_capacity must be in 1..256");
  }
  const auto skill_capacity = get_parameter("skill_request_capacity").as_int();
  if (skill_capacity < 1 || skill_capacity > 4096) {
    throw std::invalid_argument("skill_request_capacity must be in 1..4096");
  }
  skill_request_capacity_ = static_cast<std::size_t>(skill_capacity);
  navigation_pool_.set_capacity(static_cast<std::size_t>(slot_capacity));
  takeoff_pool_.set_capacity(static_cast<std::size_t>(slot_capacity));
  land_pool_.set_capacity(static_cast<std::size_t>(slot_capacity));
  ota_pool_.set_capacity(static_cast<std::size_t>(slot_capacity));

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
      Json reply;
      bool matched = false;
      {
        const std::lock_guard<std::mutex> guard(records_mutex_);
        const Pending * pending = records_.pending(message->request_id);
        if (pending && pending->command.name == "motion.velocity") {
          reply = response(
            message->request_id, pending->command.name, message->success, message->code,
            message->message);
          matched = true;
        }
      }
      if (!matched) {
        return;
      }
      // Publish outside of the lock: a publisher must never be called while
      // the correlation records are held, otherwise an intra-process
      // subscriber could re-enter this node and deadlock.
      publish_json(result_pub_, reply);
      const std::lock_guard<std::mutex> guard(records_mutex_);
      records_.finish(message->request_id, reply);
    });
  state_sub_ = create_subscription<chaos_interfaces::msg::RobotState>(
    "state", 32, [this](chaos_interfaces::msg::RobotState::SharedPtr message) {
      state_callback(message);
    });
  // The communication channel of the skill layer is a plain ROS topic. The
  // gateway encodes it for the wire; the skill layer knows nothing about
  // the transport.
  announcement_pub_ = create_publisher<std_msgs::msg::String>("mqtt/announcement", 32);
  announcement_sub_ = create_subscription<chaos_interfaces::msg::Announcement>(
    "communication/announcement", 32,
    [this](chaos_interfaces::msg::Announcement::SharedPtr message) {
      announcement_callback(message);
    });
  // The skill layer is the only caller of the base capabilities. The gateway
  // keeps the wire duties and hands the capability commands to the skill
  // layer as intents.
  skill_intent_pub_ = create_publisher<chaos_interfaces::msg::SkillIntent>(
    "skill_intent", 32);
  skill_event_sub_ = create_subscription<chaos_interfaces::msg::SkillEvent>(
    "skill_event", 32, [this](chaos_interfaces::msg::SkillEvent::SharedPtr message) {
      skill_event_callback(message);
    });
  describe_client_ = create_client<chaos_interfaces::srv::GetDescription>("description");
  lease_client_ = create_client<chaos_interfaces::srv::ControlLease>("control/lease");
  stop_client_ = create_client<chaos_interfaces::srv::Stop>("motion/stop");
  // Subscribe directly so a missing goal acknowledgment does not leave a
  // per-request feedback callback registered in an action client. The topic
  // is shared by every goal of that action, independent of the client that
  // sent it, so one subscription per action type is enough.
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
  ota_feedback_sub_ = create_subscription<SoftwareUpdate::Impl::FeedbackMessage>(
    "system/ota/_action/feedback", 32,
    [this](SoftwareUpdate::Impl::FeedbackMessage::SharedPtr message) {
      feedback_callback<SoftwareUpdate>("system.ota", message);
    });
  expire_timer_ = create_wall_timer(std::chrono::milliseconds(100), [this]() { expire(); });
}

GatewayNode::~GatewayNode()
{
  // Report the shutdown for every still pending correlation. The execution
  // state stays unknown: the gateway stops observing, it does not stop the
  // robot.
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
      goal_ids_.erase(request_id);
      records_.finish(request_id, nullptr);
    }
  }
  // Stop every action callback and destroy the dedicated clients. Destroying a
  // client drops the pending SendGoal/GetResult requests of exactly that
  // goal, so no middleware bookkeeping survives the gateway.
  navigation_pool_.shutdown();
  takeoff_pool_.shutdown();
  land_pool_.shutdown();
  ota_pool_.shutdown();
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

void GatewayNode::announcement_callback(
  const chaos_interfaces::msg::Announcement::SharedPtr message)
{
  if (!message) {
    return;
  }
  Json value{
    {"version", 1},
    {"asset_id", asset_id_},
    {"request_id", message->request_id},
    {"source", message->source},
    {"topic", message->topic},
    {"timestamp_ms", epoch_ms_now()},
    {"stamp_sec", message->stamp.sec},
    {"stamp_nanosec", message->stamp.nanosec},
  };
  // The payload is carried as a JSON string, like every other payload of the
  // wire protocol.
  Json payload = nlohmann::json::object();
  try {
    payload = message->payload_json.empty() ?
      nlohmann::json::object() : nlohmann::json::parse(message->payload_json);
  } catch (const std::exception &) {
    payload = nlohmann::json::object();
    payload["raw"] = message->payload_json;
  }
  value["payload"] = payload;
  publish_json(announcement_pub_, value);
}

void GatewayNode::command_callback(const std_msgs::msg::String::SharedPtr message)
{
  const auto identity = recover_identity(message->data);
  if (identity) {
    const auto & request_id = identity->first;
    Json replay_reply;
    Json replay_event;
    bool replay = false;
    {
      const std::lock_guard<std::mutex> guard(records_mutex_);
      if (const auto * record = records_.completed(request_id)) {
        replay = true;
        replay_reply = record->first;
        replay_event = record->second;
      } else if (const Pending * pending = records_.pending(request_id)) {
        replay = true;
        if (pending->reply.has_value()) {
          replay_reply = *pending->reply;
        }
      }
    }
    if (replay) {
      if (!replay_reply.is_null()) {
        publish_json(result_pub_, replay_reply);
      }
      if (!replay_event.is_null()) {
        publish_json(event_pub_, replay_event);
      }
      return;
    }
  }
  Command command;
  // Anchor first so parsing and clock sampling consume the dispatch budget.
  const double now_mono = monotonic_now_s();
  // The wall clock is read once: the parse and the remaining TTL of the
  // command have to describe the same instant, otherwise a clock step
  // between them could change the validity of the command.
  const std::int64_t now_ms = epoch_ms_now();
  try {
    command = parse_command(message->data, capabilities_, now_ms, clock_skew_ms_);
  } catch (const CommandError & error) {
    if (identity) {
      Json reply;
      {
        const std::lock_guard<std::mutex> guard(records_mutex_);
        reply = response(
          identity->first, identity->second, false, error.code(), error.what());
        records_.finish(identity->first, reply);
      }
      publish_json(result_pub_, reply);
    } else {
      // Never log payloads or tokens; the error code is sufficient to debug.
      RCLCPP_WARN(get_logger(), "Dropped uncorrelatable MQTT command: %s", error.code().c_str());
    }
    return;
  }
  // The original TTL of the command bounds its dispatch. The remaining TTL
  // is captured on the monotonic clock, so a later wall clock rollback can
  // neither shorten nor extend the deadline of a deferred dispatch.
  const double remaining_ttl_s =
    (command.deadline_ms - static_cast<double>(now_ms)) / 1000.0;
  const double dispatch_deadline = now_mono + std::max(0.0, remaining_ttl_s);
  {
    const std::lock_guard<std::mutex> guard(records_mutex_);
    try {
      records_.start(command, now_mono, request_timeout_s_, dispatch_deadline);
    } catch (const CommandError & error) {
      Json reply = response(command.request_id, command.name, false, error.code(), error.what());
      records_.finish(command.request_id, reply);
      publish_json(result_pub_, reply);
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
  } else if (name == "navigation.to_pose" || name == "flight.takeoff" ||
    name == "flight.land")
  {
    send_skill(command);
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
  // The gateway's executor runs MutuallyExclusive callbacks (default), so
  // state access is serialized; the mutex still guards against a future
  // Reentrant configuration. Never dereference Pending outside the lock.
  auto pending_call = client->async_send_request(
    std::make_shared<typename ServiceT::Request>(std::move(request)),
    [this, request_id, cancelled](typename rclcpp::Client<ServiceT>::SharedFutureWithRequest future) {
      if (cancelled->load()) {
        return;
      }
      std::string command_name;
      bool tracked = false;
      {
        const std::lock_guard<std::mutex> guard(records_mutex_);
        if (const Pending * pending = records_.pending(request_id)) {
          tracked = true;
          command_name = pending->command.name;
        }
      }
      if (!tracked) {
        return;
      }
      try {
        ServiceReply outcome = service_reply(*future.get().second);
        Json reply = response(
          request_id, command_name, outcome.success, outcome.code, outcome.message, "result",
          Json{{"data", std::move(outcome.data)}});
        publish_json(result_pub_, reply);
        const std::lock_guard<std::mutex> guard(records_mutex_);
        goal_ids_.erase(request_id);
        records_.finish(request_id, reply);
      } catch (const std::exception & error) {
        service_error(request_id, "RUNTIME_ERROR", error.what());
      }
    });
  // Bound rclcpp's internal pending-request bookkeeping: when the correlation
  // finishes elsewhere (timeout, replay, shutdown), drop the client's pending
  // entry so a permanently silent service cannot accumulate requests.
  const auto inner_request_id = pending_call.request_id;
  const std::lock_guard<std::mutex> guard(records_mutex_);
  if (Pending * pending = records_.pending(request_id)) {
    pending->cancel = [client, inner_request_id, cancelled]() {
      cancelled->store(true);
      try {
        client->remove_pending_request(inner_request_id);
      } catch (const std::exception &) {
        // The request may already be completed/removed; cancellation of the
        // local observation is best-effort and must never throw here.
      }
    };
  } else {
    try {
      client->remove_pending_request(inner_request_id);
    } catch (const std::exception &) {
    }
  }
}

bool GatewayNode::skill_for_command(
  const Command & command, std::string & token, Json & arguments, std::string & error) const
{
  const auto & name = command.name;
  const auto & args = command.arguments;
  if (name == "navigation.to_pose") {
    token = "navigate_to";
    const auto & target = args.at("target");
    arguments = Json{
      {"frame_id", target.at("frame_id").get<std::string>()},
      {"x", target.at("position").at("x").get<double>()},
      {"y", target.at("position").at("y").get<double>()},
      {"z", target.at("position").at("z").get<double>()},
      {"qx", target.at("orientation").at("x").get<double>()},
      {"qy", target.at("orientation").at("y").get<double>()},
      {"qz", target.at("orientation").at("z").get<double>()},
      {"qw", target.at("orientation").at("w").get<double>()},
    };
    return true;
  }
  if (name == "flight.takeoff") {
    token = "takeoff";
    arguments = Json{{"altitude_m", args.at("altitude_m").get<double>()}};
    return true;
  }
  if (name == "flight.land") {
    token = "land";
    arguments = Json::object();
    return true;
  }
  error = "command '" + name + "' has no skill binding";
  return false;
}

void GatewayNode::send_skill(const Command & command)
{
  // The runtime rejects a landing whose altitude is not zero. Dropping the
  // field here and sending a zero goal would turn that rejection into a landing.
  if (command.name == "flight.land") {
    const double altitude = command.arguments.value("altitude_m", 0.0);
    if (!std::isfinite(altitude) || altitude != 0.0) {
      service_error(
        command.request_id, "INVALID_ARGUMENT", "Landing altitude_m must be zero");
      return;
    }
  }
  std::string token;
  Json arguments;
  std::string error;
  if (!skill_for_command(command, token, arguments, error)) {
    service_error(command.request_id, "UNSUPPORTED", error);
    return;
  }
  if (dispatch_expired(command.request_id)) {
    service_error(
      command.request_id, "EXPIRED",
      "command TTL elapsed before the skill layer was reachable");
    return;
  }

  chaos_interfaces::msg::SkillIntent intent;
  intent.token.token = token;
  intent.token.arguments_json = arguments.dump();
  intent.token.decided_at = get_clock()->now();
  intent.token.source = "mqtt:" + asset_id_;
  intent.request_id = command.request_id;
  intent.expires_at = deadline_time(command.deadline_ms);
  // A remote command is not a planning decision: the hub of the planning
  // layer must not veto it.
  intent.enforce_hub_precondition = false;
  // The remote operator holds the lease; the skill layer runs with that
  // token instead of taking a second one.
  if (command.arguments.contains("lease_token") &&
    command.arguments.at("lease_token").is_string())
  {
    intent.control_token = command.arguments.at("lease_token").get<std::string>();
  }

  bool saturated = false;
  {
    const std::lock_guard<std::mutex> guard(records_mutex_);
    if (skill_requests_.size() >= skill_request_capacity_) {
      // The bound of the observed skill requests is reached: the request is
      // refused instead of growing the state of the gateway.
      saturated = true;
    } else {
      skill_requests_[command.request_id] = intent;
      skill_published_at_[command.request_id] = monotonic_now_s();
    }
  }
  if (saturated) {
    // The error is reported outside of the correlation lock.
    service_error(command.request_id, "BUSY", "too many concurrent skill requests");
    return;
  }
  skill_intent_pub_->publish(intent);
}

void GatewayNode::retry_skill_requests()
{
  // The dispatch deadline is checked outside of the correlation lock: the
  // helper takes it itself.
  std::vector<std::pair<std::string, chaos_interfaces::msg::SkillIntent>> awaiting;
  {
    const std::lock_guard<std::mutex> guard(records_mutex_);
    for (const auto & item : skill_requests_) {
      if (skill_published_at_.count(item.first) == 0) {
        // The skill layer already accepted the intent.
        continue;
      }
      awaiting.emplace_back(item.first, item.second);
    }
  }

  std::vector<chaos_interfaces::msg::SkillIntent> republish;
  std::vector<std::string> expired;
  for (const auto & item : awaiting) {
    {
      const std::lock_guard<std::mutex> guard(records_mutex_);
      // A request whose correlation was retired is never sent to the robot.
      if (records_.pending(item.first) == nullptr) {
        continue;
      }
    }
    if (dispatch_expired(item.first)) {
      expired.push_back(item.first);
      continue;
    }
    const std::lock_guard<std::mutex> guard(records_mutex_);
    const auto published = skill_published_at_.find(item.first);
    if (published == skill_published_at_.end()) {
      continue;
    }
    if (monotonic_now_s() - published->second < kSkillRetryIntervalS) {
      continue;
    }
    skill_published_at_[item.first] = monotonic_now_s();
    republish.push_back(item.second);
  }

  for (const auto & id : expired) {
    {
      const std::lock_guard<std::mutex> guard(records_mutex_);
      skill_requests_.erase(id);
      skill_published_at_.erase(id);
    }
    service_error(
      id, "EXPIRED",
      "command TTL elapsed before the skill layer was reachable");
  }
  for (const auto & intent : republish) {
    // The skill layer remembers the request id and replays the last outcome
    // instead of running the skill a second time.
    skill_intent_pub_->publish(intent);
  }
}

void GatewayNode::skill_event_callback(
  const chaos_interfaces::msg::SkillEvent::SharedPtr & message)
{
  if (!message) {
    return;
  }
  const auto & request_id = message->request_id;

  Json reply;
  bool publish_reply = false;
  Json event_value;
  bool publish_event = false;
  bool terminal = false;
  Json terminal_data;
  {
    const std::lock_guard<std::mutex> guard(records_mutex_);
    Pending * pending = records_.pending(request_id);
    if (!pending) {
      // The correlation was already retired (timeout). The skill may still
      // run on the robot, but the gateway no longer reports it. Drop the
      // bookkeeping too: the terminal event that would have erased it will
      // not be applied.
      skill_requests_.erase(request_id);
      skill_published_at_.erase(request_id);
      return;
    }
    if (message->phase == "accepted") {
      pending->accepted = true;
      // The skill deadline starts when the skill layer accepted the intent.
      pending->expires_at = monotonic_now_s() + action_timeout_s_;
      // The request stays in `skill_requests_` until its terminal event: the
      // capacity of the observed skill requests covers the whole lifetime of
      // a request, not only its dispatch.
      skill_published_at_.erase(request_id);
      reply = response(
        request_id, pending->command.name, true, "ACCEPTED",
        "skill accepted; completion arrives on task_events", "accepted",
        Json{{"skill", message->token}});
      pending->reply = reply;
      publish_reply = true;
    } else if (message->phase == "feedback") {
      event_value = build_event(request_id, "feedback", Json{
          {"skill", message->token},
          {"feedback", Json{
                {"progress", message->progress},
                {"hub_state", message->hub_state},
              }}});
      publish_event = !event_value.is_null();
    } else if (message->phase == "succeeded" || message->phase == "failed") {
      Json data{
        {"ok", message->success},
        {"code", message->code},
        {"message", message->message},
        {"hub_state", message->hub_state},
        {"execution_state", "known"},
        // The skill token replaces the goal id: a skill has no ROS action
        // goal, and a consumer still needs to know which skill ran.
        {"skill", message->token},
      };
      skill_requests_.erase(request_id);
      skill_published_at_.erase(request_id);
      terminal = true;
      terminal_data = std::move(data);
    } else {
      return;
    }
  }
  if (terminal) {
    // action_done takes the correlation lock itself, so it runs outside.
    action_done(request_id, terminal_data);
    return;
  }
  if (publish_reply) {
    publish_json(result_pub_, reply);
  }
  if (publish_event) {
    publish_json(event_pub_, event_value);
  }
}

void GatewayNode::send_action(const Command & command)
{
  const auto & name = command.name;
  if (name == "navigation.to_pose") {
    send_typed_action<NavigatePose>(command, navigation_pool_);
  } else if (name == "flight.takeoff") {
    send_typed_action<Flight>(command, takeoff_pool_);
  } else if (name == "flight.land") {
    send_typed_action<Flight>(command, land_pool_);
  } else if (name == "system.ota") {
    send_typed_action<SoftwareUpdate>(command, ota_pool_);
  } else {
    service_error(command.request_id, "UNSUPPORTED", "unknown command");
  }
}

template<typename ActionT>
void GatewayNode::send_typed_action(const Command & command, ActionSlotPool<ActionT> & pool)
{
  const std::string request_id = command.request_id;
  // Reserve an exclusive observation slot. Each slot owns a dedicated action
  // client, so a later timeout of a different goal can never remove the
  // callbacks of this goal.
  auto observer = pool.acquire(request_id);
  if (!observer) {
    service_error(request_id, "BUSY", "too many concurrent action goals");
    return;
  }
  // The action server of this dedicated client may not be discovered yet: the
  // client was created after the node graph was queried, so a runtime that
  // starts up (or appears) later is invisible to it for a while. The command
  // is therefore not rejected: the goal is remembered and the dispatch is
  // deferred until the client can reach a server. The original TTL of the
  // command bounds the wait, and an expired request is never sent to the
  // robot.
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
  } else if constexpr (std::is_same_v<ActionT, Flight>) {
    goal.altitude_m = command.arguments.at("altitude_m").get<double>();
  } else if constexpr (std::is_same_v<ActionT, SoftwareUpdate>) {
    goal.artifact_id = command.arguments.at("artifact_id").get<std::string>();
    goal.version = command.arguments.at("version").get<std::string>();
    goal.sha256 = command.arguments.at("sha256").get<std::string>();
    goal.size_bytes = command.arguments.at("size_bytes").get<std::uint64_t>();
  } else {
    static_assert(sizeof(ActionT) == 0, "unsupported action");
  }
  goal.lease_token = command.arguments.at("lease_token").get<std::string>();
  goal.expires_at = deadline_time(command.deadline_ms);
  try {
    if (dispatch_expired(request_id)) {
      service_error(request_id, "EXPIRED", "command TTL elapsed before action dispatch");
      return;
    }
    observer->set_goal(std::move(goal));
    if (!observer->send_if_ready()) {
      // The action server of this dedicated client is not discovered yet.
      // The dispatch is deferred instead of blocking the executor; the
      // original TTL of the command bounds the wait.
      pool.defer(request_id);
      return;
    }
  } catch (const std::exception & error) {
    service_error(request_id, "DISPATCH_ERROR", error.what());
  }
}

void GatewayNode::on_action_accepted(const std::string & request_id, const std::string & goal_id_hex)
{
  Json reply;
  {
    const std::lock_guard<std::mutex> guard(records_mutex_);
    Pending * pending = records_.pending(request_id);
    if (!pending) {
      // The correlation was already retired (timeout). The goal may still run
      // on the robot, but the gateway no longer reports it: an expired
      // observation never resurrects a finished record.
      return;
    }
    pending->accepted = true;
    // The action deadline starts when the server accepted the goal.
    pending->expires_at = monotonic_now_s() + action_timeout_s_;
    goal_ids_[request_id] = goal_id_hex;
    reply = response(
      request_id, pending->command.name, true, "ACCEPTED",
      "action accepted; completion arrives on task_events", "accepted",
      Json{{"goal_id", goal_id_hex}});
    pending->reply = reply;
  }
  publish_json(result_pub_, reply);
}

void GatewayNode::on_action_rejected(const std::string & request_id)
{
  service_error(request_id, "REJECTED", "runtime rejected action goal");
}

void GatewayNode::on_action_result(const std::string & request_id, const ActionOutcome & outcome)
{
  // A terminal status without a result payload keeps the execution state
  // unknown; only a known result may claim that the operation finished.
  Json data{
    {"ok", outcome.success},
    {"code", outcome.code},
    {"message", outcome.message},
    {"ros_goal_status", outcome.ros_goal_status},
    {"execution_state", outcome.known ? "known" : "unknown"},
  };
  if (outcome.applied.has_value()) {
    data["applied"] = *outcome.applied;
  }
  action_done(request_id, data);
}

template<typename ActionT>
void GatewayNode::feedback_callback(
  const std::string & command_name,
  const typename ActionT::Impl::FeedbackMessage::SharedPtr message)
{
  const std::string goal_hex = action_goal_id_hex(message->goal_id.uuid);
  Json feedback;
  if constexpr (std::is_same_v<ActionT, NavigatePose>) {
    feedback = Json{
      {"current_pose", pose_stamped_json(message->feedback.current_pose)},
      {"progress", message->feedback.progress},
    };
  } else if constexpr (std::is_same_v<ActionT, Flight>) {
    feedback = Json{
      {"altitude_m", message->feedback.altitude_m},
      {"progress", message->feedback.progress},
    };
  } else if constexpr (std::is_same_v<ActionT, SoftwareUpdate>) {
    feedback = Json{
      {"phase", message->feedback.phase},
      {"progress", message->feedback.progress},
    };
  } else {
    static_assert(sizeof(ActionT) == 0, "unsupported action");
  }
  Json event_value;
  {
    const std::lock_guard<std::mutex> guard(records_mutex_);
    for (const auto & item : records_.pending_map()) {
      if (item.second.command.name != command_name) {
        continue;
      }
      const auto goal = goal_ids_.find(item.first);
      if (goal != goal_ids_.end() && goal->second == goal_hex) {
        event_value = build_event(item.first, "feedback", Json{{"feedback", feedback}});
        break;
      }
    }
  }
  if (!event_value.is_null()) {
    publish_json(event_pub_, event_value);
  }
}

Json GatewayNode::build_event(const std::string & request_id, const std::string & phase,
  const Json & data)
{
  const Pending * pending = records_.pending(request_id);
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
  // The payload overrides the base fields, exactly like the original
  // ``{**base, **data}`` dictionary merge.
  for (const auto & item : data.items()) {
    value[item.key()] = item.value();
  }
  const auto goal = goal_ids_.find(request_id);
  if (goal != goal_ids_.end()) {
    value["goal_id"] = goal->second;
  }
  return value;
}

Json GatewayNode::build_action_reply(const Pending & pending, const Json & terminal_event)
{
  // Task events remain compatible, while the correlated response path also
  // receives completion. The Rust bridge can replace ACCEPTED in its replay
  // cache even if the task_events subscriber was offline.
  //
  // `applied` is part of the reply: a consumer of the result topic has to be
  // able to tell a simulated update from a committed one, exactly like a
  // consumer of the event topic. It is absent when the runtime never
  // reported it, for example when the goal timed out before it was accepted.
  Json extra = Json::object();
  for (const char * key : {"goal_id", "ros_goal_status", "execution_state", "applied", "skill",
      "hub_state"})
  {
    if (terminal_event.contains(key)) {
      extra[key] = terminal_event.at(key);
    }
  }
  return response(
    pending.command.request_id, pending.command.name, terminal_event.at("ok").get<bool>(),
    terminal_event.at("code").get<std::string>(), terminal_event.at("message").get<std::string>(),
    "result", extra);
}

void GatewayNode::action_done(const std::string & request_id, const Json & data)
{
  Json terminal_event;
  Json reply;
  {
    const std::lock_guard<std::mutex> guard(records_mutex_);
    Pending * pending = records_.pending(request_id);
    if (!pending) {
      // Late result of an observation that was already retired: it must not
      // publish a second terminal state.
      return;
    }
    terminal_event = build_event(request_id, "result", data);
    if (terminal_event.is_null()) {
      return;
    }
    reply = build_action_reply(*pending, terminal_event);
    goal_ids_.erase(request_id);
    records_.finish(request_id, reply, terminal_event);
  }
  publish_json(event_pub_, terminal_event);
  publish_json(result_pub_, reply);
}

void GatewayNode::service_error(const std::string & request_id, const std::string & code,
  const std::string & message)
{
  Json terminal_event;
  Json reply;
  {
    const std::lock_guard<std::mutex> guard(records_mutex_);
    Pending * pending = records_.pending(request_id);
    if (!pending) {
      skill_requests_.erase(request_id);
      skill_published_at_.erase(request_id);
      return;
    }
    if (pending->accepted) {
      // The goal was accepted, so the robot may still be executing it. The
      // terminal state stays unknown: the gateway lost the observation, it
      // did not stop the robot.
      terminal_event = build_event(
        request_id, "result",
        Json{{"ok", false}, {"code", code}, {"message", message}, {"execution_state", "unknown"}});
      if (terminal_event.is_null()) {
        return;
      }
      reply = build_action_reply(*pending, terminal_event);
      goal_ids_.erase(request_id);
      // A timed-out skill request is finished here and never sees its terminal
      // SkillEvent, which is the only other place that erases this map. Leaving
      // the id in it counts the request forever and eventually refuses every
      // later skill command.
      skill_requests_.erase(request_id);
      skill_published_at_.erase(request_id);
      records_.finish(request_id, reply, terminal_event);
    } else {
      reply = response(
        request_id, pending->command.name, false, code, message, "result",
        Json{{"execution_state", "unknown"}});
      goal_ids_.erase(request_id);
      skill_requests_.erase(request_id);
      skill_published_at_.erase(request_id);
      records_.finish(request_id, reply);
    }
  }
  if (!terminal_event.is_null()) {
    publish_json(event_pub_, terminal_event);
  }
  publish_json(result_pub_, reply);
}

bool GatewayNode::still_tracked(const std::string & request_id)
{
  const std::lock_guard<std::mutex> guard(records_mutex_);
  return records_.pending(request_id) != nullptr;
}

bool GatewayNode::dispatch_expired(const std::string & request_id)
{
  const std::lock_guard<std::mutex> guard(records_mutex_);
  const Pending * pending = records_.pending(request_id);
  if (!pending) {
    // The correlation is gone: the request may no longer be dispatched.
    return true;
  }
  if (pending->dispatch_deadline <= 0.0) {
    // No TTL bound was captured: only the response timeout applies.
    return false;
  }
  // The monotonic deadline is the cap: a wall clock rollback cannot extend
  // it. The deadline of the sender may expire the request earlier.
  return monotonic_now_s() >= pending->dispatch_deadline ||
         static_cast<double>(epoch_ms_now()) >= pending->command.deadline_ms;
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
  const auto tracked = [this](const std::string & request_id) {return still_tracked(request_id);};
  // A deferred goal whose original command TTL elapsed must never reach the
  // robot, even while its correlation is still alive. The check runs
  // immediately before the deferred dispatch.
  const auto dispatch_allowed = [this](const std::string & request_id) {
    if (!dispatch_expired(request_id)) {
      return true;
    }
    // The correlation is retired with EXPIRED; the observation is released
    // by `release_inactive` below.
    service_error(
      request_id, "EXPIRED",
      "command TTL elapsed before the action server was reachable");
    return false;
  };
  // Dispatch the goals whose action server became available in the meantime.
  // A request that already timed out is never sent to the robot.
  navigation_pool_.flush_ready(tracked, dispatch_allowed);
  takeoff_pool_.flush_ready(tracked, dispatch_allowed);
  land_pool_.flush_ready(tracked, dispatch_allowed);
  ota_pool_.flush_ready(tracked, dispatch_allowed);
  // Release the action observations that finished or whose correlation was
  // retired. This runs outside of every action client callback, so destroying
  // a dedicated client is safe here.
  navigation_pool_.release_inactive(tracked);
  takeoff_pool_.release_inactive(tracked);
  land_pool_.release_inactive(tracked);
  ota_pool_.release_inactive(tracked);
  // Republish the intents the skill layer did not accept yet. A request that
  // already timed out is never sent to the robot.
  retry_skill_requests();
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
  const Command &, ActionSlotPool<chaos_interfaces::action::NavigatePose> &);
template void GatewayNode::send_typed_action<chaos_interfaces::action::Flight>(
  const Command &, ActionSlotPool<chaos_interfaces::action::Flight> &);
template void GatewayNode::feedback_callback<chaos_interfaces::action::NavigatePose>(
  const std::string &, chaos_interfaces::action::NavigatePose::Impl::FeedbackMessage::SharedPtr);
template void GatewayNode::send_typed_action<chaos_interfaces::action::SoftwareUpdate>(
  const Command &, ActionSlotPool<chaos_interfaces::action::SoftwareUpdate> &);
template void GatewayNode::feedback_callback<chaos_interfaces::action::Flight>(
  const std::string &, chaos_interfaces::action::Flight::Impl::FeedbackMessage::SharedPtr);
template void GatewayNode::feedback_callback<chaos_interfaces::action::SoftwareUpdate>(
  const std::string &,
  chaos_interfaces::action::SoftwareUpdate::Impl::FeedbackMessage::SharedPtr);

}  // namespace chaos_mqtt
