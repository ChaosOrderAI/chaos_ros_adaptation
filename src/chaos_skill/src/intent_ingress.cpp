#include "chaos_skill/intent_ingress.hpp"

#include <chrono>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace chaos_skill
{

bool deadline_passed(const builtin_interfaces::msg::Time & deadline)
{
  if (deadline.sec == 0 && deadline.nanosec == 0) {
    return false;
  }
  const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
  const auto limit = static_cast<std::int64_t>(deadline.sec) * 1000000000LL +
    static_cast<std::int64_t>(deadline.nanosec);
  return now > limit;
}

std::int64_t deadline_to_ns(const builtin_interfaces::msg::Time & deadline)
{
  if (deadline.sec == 0 && deadline.nanosec == 0) {
    return 0;
  }
  return static_cast<std::int64_t>(deadline.sec) * 1000000000LL +
    static_cast<std::int64_t>(deadline.nanosec);
}

namespace
{

chaos_interfaces::msg::SkillEvent base_event(
  const chaos_interfaces::msg::SkillIntent & intent, const std::string & phase)
{
  chaos_interfaces::msg::SkillEvent event;
  event.request_id = intent.request_id;
  event.source = intent.token.source;
  event.token = intent.token.token;
  event.phase = phase;
  event.success = false;
  event.code = "";
  event.message = "";
  event.hub_state = "";
  event.progress = 0.0F;
  event.payload_json = "";
  return event;
}

}  // namespace

IntentIngress::IntentIngress(
  rclcpp::Node::SharedPtr node, Executor & executor, IntentIngressOptions options)
: node_(std::move(node)), executor_(executor), options_(std::move(options))
{
  if (!node_) {
    throw std::invalid_argument("IntentIngress requires a node");
  }
  publisher_ = node_->create_publisher<chaos_interfaces::msg::SkillEvent>(
    options_.event_topic, rclcpp::QoS(10).reliable());
  subscription_ = node_->create_subscription<chaos_interfaces::msg::SkillIntent>(
    options_.intent_topic, rclcpp::QoS(10).reliable(),
    [this](chaos_interfaces::msg::SkillIntent::ConstSharedPtr intent) {on_intent(intent);});
}

IntentIngress::~IntentIngress()
{
  stop();
}

void IntentIngress::start()
{
  if (running_.exchange(true)) {
    return;
  }
  worker_ = std::thread([this]() {run();});
}

void IntentIngress::stop()
{
  {
    // The worker waits on `signal_` with `mutex_` held and rechecks `running_`
    // under that lock. Clearing the flag without the lock loses the wakeup and
    // the join hangs.
    const std::lock_guard<std::mutex> guard(mutex_);
    if (!running_.exchange(false)) {
      return;
    }
    signal_.notify_all();
  }
  if (worker_.joinable()) {
    worker_.join();
  }
  // The queued intents never ran, so their sources get a terminal event
  // instead of waiting for their own timeout.
  std::deque<chaos_interfaces::msg::SkillIntent> abandoned;
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    abandoned.swap(queue_);
  }
  for (const auto & intent : abandoned) {
    auto event = base_event(intent, "failed");
    event.code = "skill.server_shutdown";
    event.message = "the skill server stopped before the intent ran";
    event.hub_state = executor_.current_hub();
    publish(event);
    release(intent.request_id);
  }
}

void IntentIngress::publish(const chaos_interfaces::msg::SkillEvent & event)
{
  publisher_->publish(event);
}


void IntentIngress::finish(
  const std::string & request_id, const chaos_interfaces::msg::SkillEvent & event)
{
  const std::lock_guard<std::mutex> guard(mutex_);
  pending_.erase(request_id);
  if (request_id.empty()) {
    return;
  }
  if (finished_.emplace(request_id, event).second) {
    finished_order_.push_back(request_id);
  }
  while (finished_order_.size() > options_.replay_capacity) {
    const auto oldest = finished_order_.front();
    finished_order_.pop_front();
    finished_.erase(oldest);
  }
}

void IntentIngress::release(const std::string & request_id)
{
  const std::lock_guard<std::mutex> guard(mutex_);
  pending_.erase(request_id);
}

void IntentIngress::on_intent(
  const chaos_interfaces::msg::SkillIntent::ConstSharedPtr & intent)
{
  if (!intent) {
    return;
  }

  if (deadline_passed(intent->expires_at)) {
    auto event = base_event(*intent, "failed");
    event.code = "skill.expired";
    event.message = "the intent expired before it was accepted";
    event.hub_state = executor_.current_hub();
    publish(event);
    refused_.fetch_add(1);
    return;
  }

  // A request that already finished replays its outcome; a request that is
  // still running is left to the run that owns it. Neither executes twice.
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    if (!intent->request_id.empty() && pending_.count(intent->request_id) != 0) {
      return;
    }
    const auto it = finished_.find(intent->request_id);
    if (!intent->request_id.empty() && it != finished_.end()) {
      const auto cached = it->second;
      publish(cached);
      return;
    }
  }

  // The vocabulary decides whether the token may start; the ingress only
  // checks that, so a refused intent never reaches the platform.
  const auto hub = executor_.current_hub();
  const auto * def = executor_.vocabulary().find(intent->token.token);
  if (!def) {
    auto event = base_event(*intent, "failed");
    event.code = "skill.unknown_token";
    event.message = "unknown skill token '" + intent->token.token + "'";
    event.hub_state = hub;
    publish(event);
    refused_.fetch_add(1);
    return;
  }
  // The hub precondition binds a planning decision only. A direct operator
  // command runs the skill as it is; the hub is the state machine of the
  // planning layer and must not veto it.
  if (intent->enforce_hub_precondition) {
    if (const auto err = executor_.vocabulary().check_entry(intent->token.token, hub)) {
      auto event = base_event(*intent, "failed");
      event.code = "skill.hub_mismatch";
      event.message = *err;
      event.hub_state = hub;
      publish(event);
      refused_.fetch_add(1);
      return;
    }
  }
  if (executor_.busy()) {
    auto event = base_event(*intent, "failed");
    event.code = "skill.busy";
    event.message = "another skill is already running";
    event.hub_state = hub;
    publish(event);
    refused_.fetch_add(1);
    return;
  }

  {
    const std::lock_guard<std::mutex> guard(mutex_);
    if (!intent->request_id.empty() && pending_.count(intent->request_id) != 0) {
      return;
    }
    if (queue_.size() >= options_.queue_capacity) {
      auto event = base_event(*intent, "failed");
      event.code = "skill.busy";
      event.message = "the skill queue is full";
      event.hub_state = hub;
      publish(event);
      refused_.fetch_add(1);
      return;
    }
    if (!intent->request_id.empty()) {
      pending_.insert(intent->request_id);
    }
    queue_.push_back(*intent);
  }
  signal_.notify_one();
}

void IntentIngress::run()
{
  while (running_.load()) {
    chaos_interfaces::msg::SkillIntent intent;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      signal_.wait(lock, [this]() {return !queue_.empty() || !running_.load();});
      if (queue_.empty()) {
        if (!running_.load()) {
          break;
        }
        continue;
      }
      intent = queue_.front();
      queue_.pop_front();
    }
    try {
      execute(intent);
    } catch (const std::exception & error) {
      // An escaping exception would take the whole skill server down.
      auto event = base_event(intent, "failed");
      event.code = "skill.internal_error";
      event.message = error.what();
      event.hub_state = executor_.current_hub();
      publish(event);
      release(intent.request_id);
    }
  }
}

void IntentIngress::execute(const chaos_interfaces::msg::SkillIntent & intent)
{
  nlohmann::json args = nlohmann::json::object();
  if (!intent.token.arguments_json.empty()) {
    try {
      args = nlohmann::json::parse(intent.token.arguments_json);
    } catch (const std::exception & e) {
      auto event = base_event(intent, "failed");
      event.code = "skill.args_invalid";
      event.message = e.what();
      event.hub_state = executor_.current_hub();
      publish(event);
      refused_.fetch_add(1);
      release(intent.request_id);
      return;
    }
  }

  // The deadline was checked when the intent was queued. It can pass while
  // the intent waits behind another skill, and the transport has by then
  // already told the operator the command expired.
  if (deadline_passed(intent.expires_at)) {
    auto event = base_event(intent, "failed");
    event.code = "skill.expired";
    event.message = "the intent expired before it was accepted";
    event.hub_state = executor_.current_hub();
    finish(intent.request_id, event);
    publish(event);
    refused_.fetch_add(1);
    return;
  }

  auto accepted = base_event(intent, "accepted");
  accepted.hub_state = executor_.current_hub();
  publish(accepted);

  SkillOrigin origin;
  origin.request_id = intent.request_id;
  origin.source = intent.token.source;
  origin.enforce_hub = intent.enforce_hub_precondition;
  origin.control_token = intent.control_token;
  origin.deadline_ns = deadline_to_ns(intent.expires_at);

  // Copy the intent into the callback. The SDK runs feedback later, on a
  // dispatcher thread, and that call can outlive this stack frame.
  const auto outcome = executor_.execute(
    intent.token.token, args,
    [this, intent](const SkillFeedback & feedback) {
      auto event = base_event(intent, "feedback");
      event.hub_state = feedback.current_hub;
      event.progress = feedback.progress;
      event.code = feedback.code;
      publish(event);
    },
    origin);

  auto event = base_event(intent, outcome.success ? "succeeded" : "failed");
  event.success = outcome.success;
  event.code = outcome.code;
  event.message = outcome.message;
  event.hub_state = outcome.hub_state;
  if (!outcome.payload.empty()) {
    event.payload_json = outcome.payload.dump();
  }

  finish(intent.request_id, event);
  publish(event);
  executed_.fetch_add(1);
}

}  // namespace chaos_skill
