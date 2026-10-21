#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "chaos_mqtt/error.hpp"

namespace chaos_mqtt
{

using Json = nlohmann::json;

// Command name -> required asset capability, or nullopt when any asset may
// use it (system.describe is informational, control.* manages the lease).
extern const std::unordered_map<std::string, std::optional<std::string>> kCommandCapabilities;

// JSON null replaces non-finite floating-point telemetry so outbound state is
// always strict JSON. Mirrors the former wire_value() recursion exactly.
Json wire_value(const Json & value);

struct Command
{
  std::string request_id;
  std::string name;
  double deadline_ms;
  Json arguments;
};

/// Validate the Rust String envelope and preserve the sender's deadline.
///
/// TTL uses Unix time, independent of ROS simulation time. Services and
/// actions receive that exact deadline so executor queueing never extends
/// validity. Throws CommandError on any violation.
Command parse_command(
  const std::string & data,
  const std::vector<std::string> & capabilities,
  std::int64_t now_ms,
  std::int64_t maximum_clock_skew_ms = 5000);

/// Recover only a valid bridge identity for a correlated validation error.
std::optional<std::pair<std::string, std::string>> recover_identity(const std::string & data);

/// Build a correlated response envelope.
Json response(
  const std::string & request_id,
  const std::string & command,
  bool success,
  std::string code,
  std::string message,
  const std::string & phase = "result",
  const Json & extra = nullptr);

struct Pending
{
  Command command;
  double expires_at = 0.0;
  bool accepted = false;
  std::optional<Json> reply;
  // Cancels local observation only, never robot execution (e.g. ignore an
  // in-flight service response after the request was finished elsewhere).
  std::function<void()> cancel;
};

/// Bounded active requests and completed replay records; monotonic timeouts.
class Correlations
{
public:
  explicit Correlations(std::size_t max_pending = 128, std::size_t max_completed = 512)
  : max_pending_(max_pending), max_completed_(max_completed)
  {}

  void start(const Command & command, double now, double timeout_s);
  void finish(const std::string & request_id, const Json & reply, const Json & event = nullptr);
  std::vector<std::string> expired(double now) const;

  Pending * pending(const std::string & request_id);
  const Pending * pending(const std::string & request_id) const;
  const std::map<std::string, Pending> & pending_map() const {return pending_;}
  bool is_completed(const std::string & request_id) const;
  /// Replay record: (reply, event). Reply may be null for shutdown drains.
  const std::pair<Json, Json> * completed(const std::string & request_id) const;
  std::size_t pending_size() const {return pending_.size();}
  std::size_t completed_size() const {return completed_.size();}

private:
  std::size_t max_pending_;
  std::size_t max_completed_;
  std::map<std::string, Pending> pending_;
  std::map<std::string, std::pair<Json, Json>> completed_;
  std::deque<std::string> completed_order_;
};

}  // namespace chaos_mqtt
