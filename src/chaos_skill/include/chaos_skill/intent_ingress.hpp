#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>

#include "chaos_interfaces/msg/skill_event.hpp"
#include "chaos_interfaces/msg/skill_intent.hpp"
#include "chaos_skill/executor.hpp"

namespace chaos_skill
{

/// Whether a Unix UTC deadline has passed.
///
/// The deadline is compared against the wall clock, never against the ROS
/// clock: a simulated or paused /clock must not extend the acceptance window
/// of a request that a transport already gave up on. A zero deadline means
/// that the request may be accepted at any time.
bool deadline_passed(const builtin_interfaces::msg::Time & deadline);

/// Absolute Unix nanoseconds of a deadline, or zero when the deadline is unset.
std::int64_t deadline_to_ns(const builtin_interfaces::msg::Time & deadline);

/// The topics and the bookkeeping limits of one skill ingress.
struct IntentIngressOptions
{
  /// Topic the intents arrive on, relative to the node namespace.
  std::string intent_topic = "skill_intent";
  /// Topic the lifecycle events are published on.
  std::string event_topic = "skill_event";
  /// Number of finished requests remembered for replay.
  std::size_t replay_capacity = 256;
  /// Number of intents that may wait for the executor.
  std::size_t queue_capacity = 8;
};

/// The ingress of the skill layer: the one point where the top-level sources
/// hand their work to the skill layer.
///
/// The onboard agent and the external communication adapter are peers above
/// this layer; both publish a SkillIntent. The ingress executes the intent
/// through the one Executor and publishes the lifecycle as SkillEvent
/// messages, so the source that produced the intent can follow it.
///
/// The ingress owns the request-id bookkeeping: a repeated request id never
/// executes a skill twice. A request that is still running is ignored, and a
/// request that already finished replays its last outcome. That is what a
/// transport needs when it retries a request whose acknowledgement was lost.
///
/// The ingress never decides what a skill means; the vocabulary and the
/// executor own that. It only decides whether an intent may start.
class IntentIngress
{
public:
  IntentIngress(
    rclcpp::Node::SharedPtr node, Executor & executor,
    IntentIngressOptions options = {});
  ~IntentIngress();

  IntentIngress(const IntentIngress &) = delete;
  IntentIngress & operator=(const IntentIngress &) = delete;

  /// Start the worker that executes the intents.
  void start();

  /// Stop the worker and wait for it to leave.
  void stop();

  /// Number of intents executed since the ingress started.
  std::uint64_t executed() const {return executed_.load();}

  /// Number of intents that were refused before execution.
  std::uint64_t refused() const {return refused_.load();}

private:
  void on_intent(const chaos_interfaces::msg::SkillIntent::ConstSharedPtr & intent);
  void run();
  void execute(const chaos_interfaces::msg::SkillIntent & intent);
  void publish(const chaos_interfaces::msg::SkillEvent & event);
  /// Record the terminal event of a request and release its pending slot.
  void finish(const std::string & request_id, const chaos_interfaces::msg::SkillEvent & event);
  /// Release the pending slot of a request that never reached a terminal
  /// event.
  void release(const std::string & request_id);

  rclcpp::Node::SharedPtr node_;
  Executor & executor_;
  IntentIngressOptions options_;
  rclcpp::Subscription<chaos_interfaces::msg::SkillIntent>::SharedPtr subscription_;
  rclcpp::Publisher<chaos_interfaces::msg::SkillEvent>::SharedPtr publisher_;
  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable signal_;
  std::deque<chaos_interfaces::msg::SkillIntent> queue_;
  /// Request ids that are still running.
  std::set<std::string> pending_;
  /// Terminal events of the requests that finished, by request id.
  std::map<std::string, chaos_interfaces::msg::SkillEvent> finished_;
  /// Insertion order of `finished_`, so the oldest entry can be dropped.
  std::deque<std::string> finished_order_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> executed_{0};
  std::atomic<std::uint64_t> refused_{0};
};

}  // namespace chaos_skill
