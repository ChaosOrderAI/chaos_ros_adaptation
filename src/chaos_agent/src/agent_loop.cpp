#include "chaos_agent/agent_loop.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace chaos_agent
{

namespace
{

/// How long one slice waits for a skill that is already running. A short
/// skill still finishes inside the slice; a long one does not block the
/// planner until a single long timeout.
constexpr std::chrono::seconds kReplanSlice{1};

/// Ask the model again at least this often while a skill is running, even
/// when nothing in the snapshot changed.
constexpr std::chrono::seconds kReplanPeriod{5};

/// Warn at most once per this period when a skill still has no terminal event.
constexpr std::chrono::seconds kInFlightLate{30};

/// A teammate progress message older than this is marked stale. It stays in
/// the snapshot: a silent teammate is itself a fact the planner can use.
constexpr std::chrono::seconds kTeammateStale{5};

/// Body motion that counts as a change. Smaller than this is odometry noise.
constexpr double kPoseMoveM = 0.05;
constexpr double kYawMoveRad = 0.05;

/// The control token that never reaches the skill layer.
constexpr const char * kDoneToken = "done";

/// Number of terminal events the loop keeps. The skill layer publishes the
/// events of every source in the asset namespace, so the loop must not keep
/// the ones it does not wait for.
constexpr std::size_t kTerminalHistory = 64;

/// Cap a teammate task string. The channel carries progress, not a document.
constexpr std::size_t kTaskChars = 512;

constexpr double kPi = 3.14159265358979323846;

std::string summarize_vocab(const nlohmann::json & doc)
{
  std::string out;
  for (const auto & skill : doc.value("skills", nlohmann::json::array())) {
    out += "- " + skill.value("token", "") + ": " + skill.value("description", "") + "\n";
  }
  return out;
}

/// The entry hub of a skill, as it is shown to the planner.
///
/// A skill that may start from every hub says so: telling the planner that
/// "observe" needs the hub "idle" would keep it from observing exactly when
/// the asset is in another hub.
std::string describe_entry(const nlohmann::json & skill)
{
  if (skill.value("any_entry_hub", false)) {
    return "any";
  }
  return skill.value("entry_hub", "");
}

double yaw_of(double qx, double qy, double qz, double qw)
{
  return std::atan2(
    2.0 * (qw * qz + qx * qy),
    1.0 - 2.0 * (qy * qy + qz * qz));
}

double wrap_angle(double angle)
{
  while (angle > kPi) {
    angle -= 2.0 * kPi;
  }
  while (angle < -kPi) {
    angle += 2.0 * kPi;
  }
  return angle;
}

/// Absolute `team_status` topic of one peer namespace.
///
/// A namespace ("/robots/dog_002" or "robots/dog_002") and an already
/// qualified topic both work. Empty input is not a peer.
std::string peer_status_topic(std::string ns)
{
  while (!ns.empty() && ns.back() == '/') {
    ns.pop_back();
  }
  if (ns.empty()) {
    return {};
  }
  if (ns.front() != '/') {
    ns.insert(ns.begin(), '/');
  }
  static const std::string suffix = "/team_status";
  if (ns.size() >= suffix.size() &&
    ns.compare(ns.size() - suffix.size(), suffix.size(), suffix) == 0)
  {
    return ns;
  }
  return ns + suffix;
}

nlohmann::json function_tool(const std::string & name, const std::string & description)
{
  return {
    {"type", "function"},
    {"function", {
        {"name", name},
        {"description", description},
        {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}},
      }},
  };
}

}  // namespace

InflightDecision decide_inflight(const std::string & token)
{
  if (token == "stop") {
    return InflightDecision::kStop;
  }
  if (token == "continue" || token == "hold") {
    return InflightDecision::kWait;
  }
  return InflightDecision::kIgnore;
}

AgentLoop::AgentLoop(
  rclcpp::Node::SharedPtr node,
  std::string asset_id,
  std::string skill_ns,
  nlohmann::json planner_document,
  std::string system_prompt,
  std::string llama_endpoint,
  std::string role,
  std::vector<std::string> peer_namespaces)
: node_(std::move(node)),
  asset_id_(std::move(asset_id)),
  role_(std::move(role)),
  llama_(std::move(llama_endpoint)),
  planner_document_(std::move(planner_document)),
  system_prompt_(std::move(system_prompt))
{
  if (!node_) {
    throw std::invalid_argument("AgentLoop requires a node");
  }

  // The request ids of one agent process must not collide with the ids of an
  // earlier process: the skill layer replays the last outcome of a request id
  // it has seen, and a restarted agent would take that stale outcome for a
  // skill that never ran.
  run_id_ = static_cast<std::uint64_t>(
    std::chrono::steady_clock::now().time_since_epoch().count());

  // A caller-supplied prompt is kept as written. The snapshot still carries
  // the role and the teammates, so that prompt can use them.
  if (system_prompt_.empty()) {
    system_prompt_ =
      "You are the onboard planner for robot " + asset_id_ +
      ". At each step you are shown the robot's current hub state, its task role, "
      "and task progress reported by teammates. "
      "Choose exactly one skill tool call from the declared vocabulary. "
      "A skill only starts when its entry hub matches the current hub; "
      "if none fits, choose hold. Return done when the task is complete. "
      "While a skill is running, choose only skill_stop or skill_continue. "
      "skill_continue leaves the running skill alone. "
      "Do not start another skill until it finishes. "
      "A teammate entry is that robot's task progress, not its camera and not a shared controller. "
      "A role is a task function another robot may share; sharing a role does not merge "
      "observations or control.\n\n"
      "Available skills:\n" + summarize_vocab(planner_document_);
  }

  // The skill layer starts in the first hub of the vocabulary, so the planner
  // knows the hub of the asset before the first skill ran.
  const auto hubs = planner_document_.value("hub_states", nlohmann::json::array());
  if (!hubs.empty() && hubs.front().is_string()) {
    hub_ = hubs.front().get<std::string>();
  }

  // The agent node already lives in the asset's namespace, so the topics are
  // addressed relatively; skill_ns is only a fallback when the node is placed
  // outside it.
  std::string prefix;
  if (!skill_ns.empty() && skill_ns != "/") {
    prefix = skill_ns.front() == '/' ? skill_ns : (std::string("/") + skill_ns);
    if (prefix.back() != '/') {
      prefix += '/';
    }
    const std::string own = node_->get_namespace();
    if (own == prefix || own + "/" == prefix) {
      prefix.clear();
    }
  }
  intent_pub_ = node_->create_publisher<chaos_interfaces::msg::SkillIntent>(
    prefix + "skill_intent", rclcpp::QoS(10).reliable());
  event_sub_ = node_->create_subscription<chaos_interfaces::msg::SkillEvent>(
    prefix + "skill_event", rclcpp::QoS(10).reliable(),
    [this](chaos_interfaces::msg::SkillEvent::ConstSharedPtr event) {on_event(event);});
  state_sub_ = node_->create_subscription<chaos_interfaces::msg::RobotState>(
    prefix + "state", rclcpp::QoS(1).best_effort(),
    [this](chaos_interfaces::msg::RobotState::ConstSharedPtr msg) {
      const std::lock_guard<std::mutex> guard(state_mutex_);
      last_state_ = std::move(msg);
    });

  // Task progress only. This topic is not the announcement the external
  // adapter forwards, so a teammate update does not leave the robot as a
  // status post.
  team_pub_ = node_->create_publisher<chaos_interfaces::msg::Announcement>(
    prefix + "team_status", rclcpp::QoS(10).reliable());
  const std::string own_status = team_pub_->get_topic_name();
  for (const auto & peer : peer_namespaces) {
    const auto topic = peer_status_topic(peer);
    if (topic.empty() || topic == own_status) {
      continue;
    }
    team_subs_.push_back(
      node_->create_subscription<chaos_interfaces::msg::Announcement>(
        topic, rclcpp::QoS(10).reliable(),
        [this](chaos_interfaces::msg::Announcement::ConstSharedPtr msg) {
          on_team_status(msg);
        }));
  }
  publish_team_status();
}

chaos_interfaces::msg::RobotState::ConstSharedPtr AgentLoop::state() const
{
  const std::lock_guard<std::mutex> guard(state_mutex_);
  return last_state_;
}

std::string AgentLoop::current_hub() const
{
  const std::lock_guard<std::mutex> guard(mutex_);
  return hub_;
}

void AgentLoop::set_task(std::string task)
{
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    task_ = std::move(task);
  }
  publish_team_status();
}

void AgentLoop::set_role(std::string role)
{
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    role_ = std::move(role);
  }
  publish_team_status();
}

void AgentLoop::on_event(const chaos_interfaces::msg::SkillEvent::ConstSharedPtr & event)
{
  if (!event) {
    return;
  }
  // Progress of the skill this loop is waiting on. Other sources' feedback
  // is not this robot's task progress.
  if (event->phase == "feedback") {
    bool mine = false;
    {
      const std::lock_guard<std::mutex> guard(mutex_);
      if (!outstanding_.empty() && event->request_id == outstanding_) {
        in_flight_progress_ = event->progress;
        mine = true;
      }
    }
    if (mine) {
      publish_team_status();
    }
    return;
  }
  if (event->phase != "succeeded" && event->phase != "failed") {
    return;
  }
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    terminal_[event->request_id] = *event;
    terminal_order_.push_back(event->request_id);
    while (terminal_.size() > kTerminalHistory) {
      // Map order is lexicographic, and the request this loop is waiting on
      // must survive either way.
      std::string drop;
      for (const auto & id : terminal_order_) {
        if (id != outstanding_) {
          drop = id;
          break;
        }
      }
      if (drop.empty()) {
        break;
      }
      terminal_.erase(drop);
      terminal_order_.erase(
        std::remove(terminal_order_.begin(), terminal_order_.end(), drop),
        terminal_order_.end());
    }
  }
  signal_.notify_all();
}

void AgentLoop::on_team_status(
  const chaos_interfaces::msg::Announcement::ConstSharedPtr & msg)
{
  if (!msg || msg->payload_json.empty()) {
    return;
  }
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(msg->payload_json);
  } catch (const std::exception &) {
    return;
  }
  if (!body.is_object()) {
    return;
  }
  std::string id;
  if (body.contains("asset_id") && body["asset_id"].is_string()) {
    id = body["asset_id"].get<std::string>();
  }
  if (id.empty()) {
    id = msg->source;
  }
  // A peer that echoes this robot is not a teammate.
  if (id.empty() || id == asset_id_) {
    return;
  }

  // Copy the progress fields only. Anything else, including an image, is
  // dropped: this channel is not a camera.
  nlohmann::json picked = nlohmann::json::object();
  picked["asset_id"] = id;
  auto take_string = [&](const char * key, std::size_t limit) {
      if (!body.contains(key) || !body[key].is_string()) {
        return;
      }
      auto value = body[key].get<std::string>();
      if (value.size() > limit) {
        value.resize(limit);
      }
      picked[key] = std::move(value);
    };
  take_string("role", 128);
  take_string("hub", 128);
  take_string("task", kTaskChars);
  take_string("token", 128);
  take_string("phase", 32);
  if (body.contains("progress") && body["progress"].is_number()) {
    picked["progress"] = body["progress"].get<double>();
  }
  if (body.contains("success") && body["success"].is_boolean()) {
    picked["success"] = body["success"].get<bool>();
  }

  const std::lock_guard<std::mutex> guard(mutex_);
  teammates_[id] = Teammate{std::move(picked), std::chrono::steady_clock::now()};
}

std::optional<chaos_interfaces::msg::SkillEvent> AgentLoop::await_terminal(
  const std::string & request_id, std::chrono::seconds timeout)
{
  std::unique_lock<std::mutex> lock(mutex_);
  const auto ready = signal_.wait_for(
    lock, timeout, [this, &request_id]() {return terminal_.count(request_id) != 0;});
  if (!ready) {
    return std::nullopt;
  }
  const auto event = terminal_[request_id];
  terminal_.erase(request_id);
  terminal_order_.erase(
    std::remove(terminal_order_.begin(), terminal_order_.end(), request_id),
    terminal_order_.end());
  return event;
}

void AgentLoop::adopt(const chaos_interfaces::msg::SkillEvent & event)
{
  const std::lock_guard<std::mutex> guard(mutex_);
  // The hub of a failed attempt is the hub the asset rests in, so it is
  // taken over as well: a failure that left the asset somewhere else must
  // not leave the planner with a stale view.
  if (!event.hub_state.empty()) {
    hub_ = event.hub_state;
  }
  last_result_ = {
    {"token", event.token},
    {"success", event.success},
    {"code", event.code},
    {"message", event.message},
  };
  // observe reports the pose and the sensor stamps here. Dropping the payload
  // made the next decision look like the skill had returned nothing.
  if (!event.payload_json.empty()) {
    try {
      last_result_["payload"] = nlohmann::json::parse(event.payload_json);
    } catch (const std::exception &) {
      last_result_["payload"] = event.payload_json;
    }
  }
}

void AgentLoop::clear_inflight()
{
  outstanding_.clear();
  in_flight_token_.clear();
  in_flight_progress_ = 0.F;
  stop_sent_ = false;
  last_late_warn_ = {};
  last_replan_ = {};
  replan_progress_ = 0.F;
  replan_digest_.clear();
  have_replan_pose_ = false;
}

std::string AgentLoop::phase_locked() const
{
  if (!outstanding_.empty()) {
    return "feedback";
  }
  if (last_result_.contains("success")) {
    return last_result_.value("success", false) ? "succeeded" : "failed";
  }
  return "idle";
}

std::string AgentLoop::next_request_id()
{
  return asset_id_ + ":agent:" + std::to_string(run_id_) + ":" +
    std::to_string(++sequence_);
}

void AgentLoop::publish_team_status()
{
  if (!team_pub_) {
    return;
  }
  nlohmann::json body;
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    std::string token = in_flight_token_;
    if (token.empty() && last_result_.contains("token") && last_result_["token"].is_string()) {
      token = last_result_["token"].get<std::string>();
    }
    const auto phase = phase_locked();
    double progress = in_flight_progress_;
    // The previous skill's success must not be reported as this skill's.
    bool success = false;
    if (outstanding_.empty()) {
      progress = phase == "succeeded" ? 1.0 : 0.0;
      success = last_result_.value("success", false);
    }
    body = {
      {"asset_id", asset_id_},
      {"role", role_.empty() ? nlohmann::json(nullptr) : nlohmann::json(role_)},
      {"hub", hub_},
      {"task", task_},
      {"token", token},
      {"progress", progress},
      {"phase", phase},
      {"success", success},
      {"stamp_unix_ns", std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count()},
    };
  }
  chaos_interfaces::msg::Announcement msg;
  msg.source = asset_id_;
  msg.topic = "team_progress";
  msg.payload_json = body.dump();
  msg.stamp = node_->now();
  team_pub_->publish(msg);
}

nlohmann::json AgentLoop::teammate_rows_locked() const
{
  nlohmann::json list = nlohmann::json::array();
  const auto now = std::chrono::steady_clock::now();
  const double stale_after = std::chrono::duration<double>(kTeammateStale).count();
  for (const auto & entry : teammates_) {
    const auto & mate = entry.second;
    const double age = std::chrono::duration<double>(now - mate.received).count();
    nlohmann::json row = mate.body;
    row["age_s"] = age;
    row["stale"] = age > stale_after;
    list.push_back(std::move(row));
  }
  return list;
}

std::string AgentLoop::teammate_digest_locked() const
{
  std::string out;
  for (const auto & entry : teammates_) {
    out += entry.first;
    out += '\n';
    out += entry.second.body.dump();
    out += '\n';
  }
  return out;
}

AgentLoop::PoseSample AgentLoop::sample_pose() const
{
  const auto state = this->state();
  PoseSample sample;
  if (!state || !state->pose_valid) {
    return sample;
  }
  sample.valid = true;
  sample.x = state->pose.pose.position.x;
  sample.y = state->pose.pose.position.y;
  sample.yaw = yaw_of(
    state->pose.pose.orientation.x, state->pose.pose.orientation.y,
    state->pose.pose.orientation.z, state->pose.pose.orientation.w);
  return sample;
}

bool AgentLoop::should_replan()
{
  const auto pose = sample_pose();
  const auto now = std::chrono::steady_clock::now();
  const std::lock_guard<std::mutex> guard(mutex_);
  // The first valid pose is the baseline, not a change.
  if (pose.valid && !have_replan_pose_) {
    replan_pose_ = pose;
    have_replan_pose_ = true;
  }
  const bool forced = last_replan_.time_since_epoch().count() != 0 &&
    now - last_replan_ >= kReplanPeriod;
  const bool progress = std::fabs(in_flight_progress_ - replan_progress_) > 1e-4F;
  const bool peers = teammate_digest_locked() != replan_digest_;
  bool moved = false;
  if (pose.valid && have_replan_pose_) {
    const double dx = pose.x - replan_pose_.x;
    const double dy = pose.y - replan_pose_.y;
    const double dyaw = wrap_angle(pose.yaw - replan_pose_.yaw);
    moved = std::hypot(dx, dy) > kPoseMoveM || std::fabs(dyaw) > kYawMoveRad;
  }
  return forced || progress || peers || moved;
}

void AgentLoop::note_replan_baseline()
{
  const auto pose = sample_pose();
  const std::lock_guard<std::mutex> guard(mutex_);
  last_replan_ = std::chrono::steady_clock::now();
  replan_progress_ = in_flight_progress_;
  replan_digest_ = teammate_digest_locked();
  if (pose.valid) {
    replan_pose_ = pose;
    have_replan_pose_ = true;
  }
}

void AgentLoop::warn_if_late(const std::string & waiting)
{
  const auto now = std::chrono::steady_clock::now();
  bool late = false;
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    if (in_flight_since_.time_since_epoch().count() == 0 ||
      now - in_flight_since_ < kInFlightLate)
    {
      return;
    }
    if (last_late_warn_.time_since_epoch().count() != 0 &&
      now - last_late_warn_ < kInFlightLate)
    {
      return;
    }
    last_late_warn_ = now;
    late = true;
  }
  if (late) {
    RCLCPP_WARN(node_->get_logger(),
      "skill %s still has no terminal event; not sending another intent",
      waiting.c_str());
  }
}

bool AgentLoop::vocabulary_has(const std::string & token) const
{
  for (const auto & skill : planner_document_.value("skills", nlohmann::json::array())) {
    if (skill.value("token", "") == token) {
      return true;
    }
  }
  return false;
}

void AgentLoop::publish_stop()
{
  chaos_interfaces::msg::SkillIntent intent;
  intent.token.token = "stop";
  intent.token.arguments_json = "{}";
  intent.token.decided_at = node_->now();
  intent.token.source = "agent:" + asset_id_;
  intent.expires_at = builtin_interfaces::msg::Time();
  intent.enforce_hub_precondition = true;
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    // The outstanding skill stays outstanding. Replacing it would make the
    // loop adopt the stop's own result and lose the cancelled skill.
    if (stop_sent_ || outstanding_.empty()) {
      return;
    }
    intent.request_id = next_request_id();
    stop_sent_ = true;
  }
  intent_pub_->publish(intent);
}

bool AgentLoop::replan_while_running(const std::string & waiting)
{
  publish_team_status();
  warn_if_late(waiting);
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    if (stop_sent_) {
      return true;
    }
  }
  // Unchanged world: keep waiting. The model is not called on every slice.
  if (!should_replan()) {
    return true;
  }
  note_replan_baseline();

  const auto response = llama_.chat(
    system_prompt_, build_user_content(), build_inflight_tools(),
    {{"temperature", 0.2}, {"max_tokens", 64}});
  last_latency_ms_ = response.latency.count();
  if (!response.ok) {
    RCLCPP_WARN(node_->get_logger(), "llama call failed during %s: %s %s",
      waiting.c_str(), response.code.c_str(), response.message.c_str());
    return true;
  }
  if (response.tool_calls.empty()) {
    RCLCPP_WARN(node_->get_logger(),
      "llama returned no in-flight tool call (content=%s)", response.content.c_str());
    return true;
  }

  const auto & call = response.tool_calls.front();
  const std::string token = token_from_tool(call);
  switch (decide_inflight(token)) {
    case InflightDecision::kStop:
      if (!vocabulary_has("stop")) {
        RCLCPP_WARN(node_->get_logger(),
          "model chose stop but the vocabulary has no stop skill");
        return true;
      }
      publish_stop();
      RCLCPP_INFO(node_->get_logger(), "stopping the running skill %s", waiting.c_str());
      return true;
    case InflightDecision::kWait:
      return true;
    case InflightDecision::kIgnore:
      RCLCPP_WARN(node_->get_logger(),
        "ignoring %s while a skill is still running", token.c_str());
      return true;
  }
  return true;
}

nlohmann::json AgentLoop::build_user_content() const
{
  const auto state = this->state();
  std::string hub;
  std::string task;
  std::string role;
  nlohmann::json last = nlohmann::json::object();
  nlohmann::json inflight;
  nlohmann::json teammates = nlohmann::json::array();
  {
    // hub_ and the outcome used to be read outside this lock.
    const std::lock_guard<std::mutex> guard(mutex_);
    hub = hub_;
    task = task_;
    role = role_;
    last = last_result_;
    if (!outstanding_.empty()) {
      inflight = {
        {"token", in_flight_token_},
        {"progress", in_flight_progress_},
        {"stop_sent", stop_sent_},
      };
    }
    teammates = teammate_rows_locked();
  }

  nlohmann::json snapshot = nlohmann::json::object();
  snapshot["asset_id"] = asset_id_;
  snapshot["role"] = role.empty() ? nlohmann::json(nullptr) : nlohmann::json(role);
  snapshot["current_hub"] = hub.empty() ? nlohmann::json(nullptr) : nlohmann::json(hub);
  snapshot["task"] = task;
  snapshot["teammates"] = std::move(teammates);
  if (!inflight.empty()) {
    snapshot["in_flight"] = std::move(inflight);
  }

  if (state) {
    snapshot["state"] = {
      {"operational_state", state->operational_state},
      {"connected", state->connected},
      {"ready", state->ready},
      {"pose_valid", state->pose_valid},
      {"battery_percentage", state->battery_percentage},
      {"pose", {
          {"frame_id", state->pose.header.frame_id},
          {"x", state->pose.pose.position.x},
          {"y", state->pose.pose.position.y},
          {"z", state->pose.pose.position.z},
          {"qx", state->pose.pose.orientation.x},
          {"qy", state->pose.pose.orientation.y},
          {"qz", state->pose.pose.orientation.z},
          {"qw", state->pose.pose.orientation.w},
          // navigate_to takes a yaw. The quaternion alone leaves the planner to
          // re-derive the one number the skill actually consumes.
          {"yaw", yaw_of(
              state->pose.pose.orientation.x, state->pose.pose.orientation.y,
              state->pose.pose.orientation.z, state->pose.pose.orientation.w)},
        }},
      {"twist", {
          {"frame_id", state->twist.header.frame_id},
          {"linear_x", state->twist.twist.linear.x},
          {"linear_y", state->twist.twist.linear.y},
          {"linear_z", state->twist.twist.linear.z},
          {"angular_z", state->twist.twist.angular.z},
        }},
    };
  } else {
    snapshot["state"] = nullptr;
  }

  // The outcome of the last skill belongs into the prompt: without it the
  // planner cannot tell a failed attempt from a step that never happened.
  if (!last.empty()) {
    snapshot["last_result"] = std::move(last);
  }
  return snapshot;
}

nlohmann::json AgentLoop::build_tools() const
{
  nlohmann::json tools = nlohmann::json::array();
  for (const auto & skill : planner_document_.value("skills", nlohmann::json::array())) {
    tools.push_back({
      {"type", "function"},
      {"function", {
          {"name", "skill_" + skill.value("token", "")},
          {"description",
            std::string("[") + describe_entry(skill) + " -> " +
              skill.value("exit_hub", std::string{}) +
              "] " + skill.value("description", std::string{})},
          {"parameters", {
              {"type", "object"},
              {"properties", skill.value("arguments_schema", nlohmann::json::object())},
            }},
        }},
    });
  }
  // The control token, same shape so the model picks uniformly. It is the
  // only control token: "hold" is an ordinary skill of the vocabulary, so
  // naming it twice would produce an invalid function list.
  tools.push_back(function_tool("skill_done", "the task is complete; stop deciding"));
  return tools;
}

nlohmann::json AgentLoop::build_inflight_tools() const
{
  nlohmann::json tools = nlohmann::json::array();
  if (vocabulary_has("stop")) {
    tools.push_back(function_tool(
        "skill_stop",
        "Cancel the running skill and stop motion. The running skill is not replaced."));
  }
  tools.push_back(function_tool(
      "skill_continue",
      "Leave the running skill alone. Do not start another skill."));
  return tools;
}

std::string AgentLoop::token_from_tool(const LlamaClient::ToolCall & call) const
{
  static const std::string prefix = "skill_";
  if (call.name.rfind(prefix, 0) == 0) {
    return call.name.substr(prefix.size());
  }
  return call.name;
}

bool AgentLoop::run_once()
{
  if (stop_requested_) {
    return false;
  }

  // Heartbeat. A peer marks progress stale after a few seconds, so an idle
  // robot still has to say it is idle. The constructor publish happens before
  // the node spins and is easy to miss.
  publish_team_status();

  // One intent at a time: the skill layer runs one skill at a time, so a
  // second intent would only be refused as busy. `outstanding_` is also read
  // by the event callback, so it is only touched under the mutex.
  std::string waiting;
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    waiting = outstanding_;
  }
  if (!waiting.empty()) {
    const auto event = await_terminal(waiting, kReplanSlice);
    if (event) {
      adopt(*event);
      {
        const std::lock_guard<std::mutex> guard(mutex_);
        if (outstanding_ == waiting) {
          clear_inflight();
        }
      }
      publish_team_status();
      RCLCPP_INFO(node_->get_logger(), "skill %s -> %s (hub=%s, code=%s)",
        event->token.c_str(), event->success ? "ok" : "fail",
        event->hub_state.c_str(), event->code.c_str());
      return true;
    }
    // The slice that finds the skill still running may stop it or leave it.
    // It does not publish another motion skill.
    return replan_while_running(waiting);
  }

  const auto response = llama_.chat(
    system_prompt_, build_user_content(), build_tools(),
    {{"temperature", 0.2}, {"max_tokens", 64}});
  last_latency_ms_ = response.latency.count();
  if (!response.ok) {
    RCLCPP_WARN(node_->get_logger(), "llama call failed: %s %s",
      response.code.c_str(), response.message.c_str());
    return true;  // transient: try again next iteration
  }
  if (response.tool_calls.empty()) {
    RCLCPP_WARN(node_->get_logger(), "llama returned no tool call (content=%s)",
      response.content.c_str());
    return true;
  }

  const auto & call = response.tool_calls.front();
  const std::string token = token_from_tool(call);
  if (token == kDoneToken) {
    RCLCPP_INFO(node_->get_logger(), "agent reported done");
    return false;
  }
  if (!call.arguments_valid) {
    // The model produced something that is not a JSON object. That is not a
    // missing argument, so it is reported as what it is.
    RCLCPP_WARN(node_->get_logger(), "the model produced invalid arguments for %s: %s",
      token.c_str(), call.raw_arguments.c_str());
    return true;
  }

  // The agent publishes an intent and follows the events of the skill layer,
  // exactly like the external communication adapter does.
  chaos_interfaces::msg::SkillIntent intent;
  intent.token.token = token;
  intent.token.arguments_json = call.arguments.dump();
  intent.token.decided_at = node_->now();
  intent.token.source = "agent:" + asset_id_;
  intent.expires_at = builtin_interfaces::msg::Time();
  // The agent produces planning decisions, so the hub precondition of the
  // vocabulary binds them.
  intent.enforce_hub_precondition = true;

  const auto pose = sample_pose();
  {
    // Protect this id from eviction before the publish, so a burst of other
    // sources' events cannot drop it before await_terminal looks.
    const std::lock_guard<std::mutex> guard(mutex_);
    intent.request_id = next_request_id();
    outstanding_ = intent.request_id;
    in_flight_token_ = token;
    in_flight_progress_ = 0.F;
    in_flight_since_ = std::chrono::steady_clock::now();
    stop_sent_ = false;
    last_late_warn_ = {};
    // This slice started the skill. The baseline is the world now, so the
    // next slice does not call the model unless something actually changed.
    last_replan_ = in_flight_since_;
    replan_progress_ = 0.F;
    replan_digest_ = teammate_digest_locked();
    replan_pose_ = pose;
    have_replan_pose_ = pose.valid;
  }
  intent_pub_->publish(intent);

  const auto event = await_terminal(intent.request_id, kReplanSlice);
  if (!event) {
    // Still running. The next iteration waits another slice and may replan.
    // This slice does not: it is the one that just started the skill.
    return true;
  }
  adopt(*event);
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    if (outstanding_ == intent.request_id) {
      clear_inflight();
    }
  }
  publish_team_status();
  RCLCPP_INFO(node_->get_logger(), "skill %s -> %s (hub=%s, code=%s)",
    token.c_str(), event->success ? "ok" : "fail",
    event->hub_state.c_str(), event->code.c_str());
  return true;
}

}  // namespace chaos_agent
