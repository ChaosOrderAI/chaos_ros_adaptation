#include "chaos_agent/agent_loop.hpp"

#include <chrono>
#include <utility>

namespace chaos_agent
{

namespace
{

/// How long the loop waits for the skill layer to answer one intent.
constexpr std::chrono::seconds kSkillResultTimeout{30};

/// How often the loop waits for an outstanding intent before it gives the
/// observation up. The skill may still be running, so the loop does not send
/// a second intent: it would only be refused as busy.
constexpr int kMaxOutstandingWaits = 3;

/// The control token that never reaches the skill layer.
constexpr const char * kDoneToken = "done";

/// Number of terminal events the loop keeps. The skill layer publishes the
/// events of every source in the asset namespace, so the loop must not keep
/// the ones it does not wait for.
constexpr std::size_t kTerminalHistory = 64;

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

}  // namespace

AgentLoop::AgentLoop(
  rclcpp::Node::SharedPtr node,
  std::string asset_id,
  std::string skill_ns,
  nlohmann::json planner_document,
  std::string system_prompt,
  std::string llama_endpoint)
: node_(std::move(node)),
  asset_id_(std::move(asset_id)),
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

  if (system_prompt_.empty()) {
    system_prompt_ =
      "You are the onboard planner for robot " + asset_id_ +
      ". At each step you are shown the robot's current hub state and task. "
      "Choose exactly one skill tool call from the declared vocabulary. "
      "A skill only starts when its entry hub matches the current hub; "
      "if none fits, choose hold. Return done when the task is complete.\n\n"
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

void AgentLoop::on_event(const chaos_interfaces::msg::SkillEvent::ConstSharedPtr & event)
{
  if (!event) {
    return;
  }
  if (event->phase != "succeeded" && event->phase != "failed") {
    return;
  }
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    terminal_[event->request_id] = *event;
    while (terminal_.size() > kTerminalHistory) {
      terminal_.erase(terminal_.begin());
    }
  }
  signal_.notify_all();
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
}

nlohmann::json AgentLoop::build_user_content() const
{
  nlohmann::json snapshot = nlohmann::json::object();
  snapshot["asset_id"] = asset_id_;
  snapshot["current_hub"] = hub_.empty() ? nlohmann::json(nullptr) : nlohmann::json(hub_);
  snapshot["task"] = task_;

  const auto state = this->state();
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
        }},
    };
  } else {
    snapshot["state"] = nullptr;
  }

  // The outcome of the last skill belongs into the prompt: without it the
  // planner cannot tell a failed attempt from a step that never happened.
  const std::lock_guard<std::mutex> guard(mutex_);
  if (!last_result_.empty()) {
    snapshot["last_result"] = last_result_;
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
  tools.push_back({
    {"type", "function"},
    {"function", {
        {"name", "skill_done"},
        {"description", "the task is complete; stop deciding"},
        {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}},
      }},
  });
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

  // One intent at a time: the skill layer runs one skill at a time, so a
  // second intent would only be refused as busy.
  if (!outstanding_.empty()) {
    const auto event = await_terminal(outstanding_, kSkillResultTimeout);
    if (event) {
      adopt(*event);
      outstanding_.clear();
      outstanding_waits_ = 0;
      return true;
    }
    if (++outstanding_waits_ >= kMaxOutstandingWaits) {
      // The outcome never arrived. The skill may still be running, so the
      // loop gives the observation up instead of piling another intent on
      // top of it.
      outstanding_.clear();
      outstanding_waits_ = 0;
    }
    return true;
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
  intent.request_id = asset_id_ + ":agent:" + std::to_string(run_id_) + ":" +
    std::to_string(++sequence_);
  intent.expires_at = builtin_interfaces::msg::Time();
  // The agent produces planning decisions, so the hub precondition of the
  // vocabulary binds them.
  intent.enforce_hub_precondition = true;

  intent_pub_->publish(intent);

  const auto event = await_terminal(intent.request_id, kSkillResultTimeout);
  if (!event) {
    // The outcome did not arrive in time. The skill may still be running, so
    // the loop waits for it in the next iteration instead of sending a
    // second intent.
    outstanding_ = intent.request_id;
    outstanding_waits_ = 0;
    RCLCPP_WARN(node_->get_logger(), "skill %s produced no terminal event", token.c_str());
    return true;
  }
  adopt(*event);
  RCLCPP_INFO(node_->get_logger(), "skill %s -> %s (hub=%s, code=%s)",
    token.c_str(), event->success ? "ok" : "fail",
    event->hub_state.c_str(), event->code.c_str());
  return true;
}

}  // namespace chaos_agent
