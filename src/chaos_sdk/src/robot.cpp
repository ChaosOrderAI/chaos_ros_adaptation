// Native C++ SDK client for the Chaos runtime.
//
// Ownership model
// ---------------
// * The Robot owns a shared ``Impl`` that holds the ROS endpoints, the
//   pending registry and the action client slots.
// * Every in-flight request is one ``Operation`` in the pending registry. The
//   registry owns the operation, the operation owns the user facing promises
//   and (for actions) the shared task state and the goal handle.
// * The ROS callbacks hold weak references only. A finished, timed out or
//   closed goal releases its client, its goal handle and its callbacks, so no
//   reference cycle keeps them alive.
// * Exactly one path may complete an operation: the path that removes it
//   from the registry under the implementation mutex ("claim"). The claim and
//   the state transition happen in the same critical section, so a late
//   response can never overwrite the state of an observation that already
//   timed out or was closed.
#include "chaos_sdk/robot.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "chaos_sdk/action_slots.hpp"
#include "chaos_sdk/action_task.hpp"

namespace chaos_sdk
{
namespace
{

constexpr double kDefaultResponseTimeoutS = 5.0;
constexpr double kDefaultActionTimeoutS = 300.0;
constexpr std::size_t kDefaultMaxPending = 64;
constexpr std::size_t kActionSlotsPerName = 8;
constexpr auto kWatchdogPeriod = std::chrono::milliseconds(50);

/// Why a local observation was stopped.
///
/// The distinction matters for the observable state: a timeout means that the
/// client gave up on its own observation window, a close means that the
/// client was shut down. Neither of them says anything about the remote
/// execution, which stays unknown.
enum class StopReason
{
  /// The local response timeout elapsed.
  Timeout,
  /// The client was closed.
  Closed,
};

/// Promise that settles exactly once, independent of which completion path
/// (response, timeout, close, rejection) wins the race.
template<typename T>
class Settler
{
public:
  Settler()
  : promise_(std::make_shared<std::promise<T>>()), future_(promise_->get_future().share()) {}

  std::shared_future<T> future() const {return future_;}

  void succeed(const T & value)
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    if (settled_) {
      return;
    }
    settled_ = true;
    try {
      promise_->set_value(value);
    } catch (const std::exception &) {
    }
  }

  void fail(const std::exception_ptr & error)
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    if (settled_) {
      return;
    }
    settled_ = true;
    try {
      promise_->set_exception(error);
    } catch (const std::exception &) {
    }
  }

private:
  mutable std::mutex mutex_;
  bool settled_ = false;
  std::shared_ptr<std::promise<T>> promise_;
  std::shared_future<T> future_;
};

std::chrono::steady_clock::time_point deadline_after(double seconds)
{
  return std::chrono::steady_clock::now() +
         std::chrono::duration_cast<std::chrono::steady_clock::duration>(
    std::chrono::duration<double>(seconds));
}

}  // namespace

const char * task_state_name(TaskState state)
{
  switch (state) {
    case TaskState::PENDING: return "pending";
    case TaskState::ACCEPTED: return "accepted";
    case TaskState::SUCCEEDED: return "succeeded";
    case TaskState::FAILED: return "failed";
    case TaskState::CANCELED: return "canceled";
    case TaskState::REJECTED: return "rejected";
    case TaskState::TIMED_OUT: return "timed_out";
    case TaskState::UNKNOWN: return "unknown";
  }
  return "unknown";
}

struct Robot::Impl
{
  /// One in-flight observation.
  ///
  /// The entry is removed from ``pending`` by whoever completes it. The
  /// removal (the claim) is the single completion token: a late response or
  /// a late goal acceptance finds no entry and must not change any state.
  struct Operation
  {
    std::uint64_t id = 0;
    std::chrono::steady_clock::time_point deadline;
    /// Set while the operation is still registered. The watchdog and
    /// close() set it under the mutex while claiming the entry, so a late
    /// response can detect that it lost the race.
    std::shared_ptr<std::atomic_bool> cancelled;
    /// Stop the local observation (drop rclcpp bookkeeping, release a slot).
    std::function<void (StopReason)> cancel_local;
    /// Settle the user facing promise(s) with an error.
    std::function<void (const std::exception_ptr &)> fail;
    /// Hand the request to the middleware, but only once the service is
    /// discovered.
    ///
    /// The middleware drops a request whose service is not known yet, so the
    /// SDK waits for the discovery instead of losing the request. The
    /// callback runs under the implementation mutex, so it must not take
    /// that mutex again.
    std::function<bool ()> send_when_ready;
    /// True once the request was handed to the middleware.
    bool sent = false;
  };

  rclcpp::Node::SharedPtr node;
  std::shared_ptr<rclcpp::Context> owned_context;
  rclcpp::executors::SingleThreadedExecutor::SharedPtr executor;
  std::thread spin_thread;

  mutable std::mutex mutex;
  std::string namespace_;
  std::string base_frame;
  double command_timeout_s = 0.5;
  std::string lease_token;
  std::string owner;
  chaos_interfaces::msg::RobotState::ConstSharedPtr last_state;
  bool closed = false;

  std::map<std::uint64_t, Operation> pending;
  std::uint64_t next_operation_id = 1;
  std::size_t max_pending = kDefaultMaxPending;
  double response_timeout_s = kDefaultResponseTimeoutS;
  double action_timeout_s = kDefaultActionTimeoutS;

  rclcpp::TimerBase::SharedPtr watchdog_timer;

  rclcpp::Client<chaos_interfaces::srv::GetDescription>::SharedPtr description;
  rclcpp::Client<chaos_interfaces::srv::ControlLease>::SharedPtr lease;
  rclcpp::Client<chaos_interfaces::srv::Stop>::SharedPtr stop;
  rclcpp::Publisher<chaos_interfaces::msg::VelocityCommand>::SharedPtr velocity_pub;
  rclcpp::Subscription<chaos_interfaces::msg::RobotState>::SharedPtr state_sub;

  ActionSlotPool<chaos_interfaces::action::NavigatePose> navigation_slots{kActionSlotsPerName};
  ActionSlotPool<chaos_interfaces::action::Flight> flight_slots{kActionSlotsPerName};

  void apply_description(const chaos_interfaces::srv::GetDescription::Response & response)
  {
    const std::lock_guard<std::mutex> guard(mutex);
    // A callback may have claimed its response immediately before close().
    // Recheck under the cache lock so closing remains the final cache boundary.
    if (closed) return;
    base_frame = response.base_frame;
    command_timeout_s = response.command_timeout_s;
  }

  void apply_lease(
    const std::string & operation,
    const chaos_interfaces::srv::ControlLease::Response & response,
    const std::string & request_owner)
  {
    const std::lock_guard<std::mutex> guard(mutex);
    if (closed) return;
    const auto next = lease_transition(
      operation, response.success, response.code, response.token, request_owner,
      lease_token, owner);
    lease_token = next.first;
    owner = next.second;
  }

  /// Claim the completion of an operation. The caller holds the mutex.
  ///
  /// Returns true for the single caller that owns the completion; every
  /// other caller (a late response, a late goal acceptance) gets false and
  /// must not touch the cached state any more.
  bool claim_operation(std::uint64_t id)
  {
    auto it = pending.find(id);
    if (it == pending.end()) {
      return false;
    }
    pending.erase(it);
    return true;
  }

  /// Enforce the local response timeouts and hand the deferred requests to
  /// the middleware. Runs on the node's executor.
  void watchdog()
  {
    std::vector<Operation> expired;
    {
      const std::lock_guard<std::mutex> guard(mutex);
      const auto now = std::chrono::steady_clock::now();
      for (auto it = pending.begin(); it != pending.end(); ) {
        if (it->second.deadline <= now) {
          // Claim the entry and mark it cancelled under the same lock: a
          // response that arrives afterwards cannot update the cached state
          // any more.
          if (it->second.cancelled) {
            it->second.cancelled->store(true);
          }
          expired.push_back(it->second);
          it = pending.erase(it);
          continue;
        }
        if (!it->second.sent && it->second.send_when_ready) {
          // The send happens under the same lock as the claim, so a request
          // is never sent for an operation that is already gone.
          if (it->second.send_when_ready()) {
            it->second.sent = true;
          }
        }
        ++it;
      }
    }
    const auto error = std::make_exception_ptr(
      TimeoutError("no response before the local response timeout"));
    for (auto & operation : expired) {
      if (operation.cancel_local) {
        operation.cancel_local(StopReason::Timeout);
      }
      if (operation.fail) {
        operation.fail(error);
      }
    }
  }
};

Robot::Robot(
  rclcpp::Node::SharedPtr node,
  std::string asset_id,
  std::optional<std::string> namespace_,
  std::optional<std::string> base_frame,
  double command_timeout_s,
  double command_ttl_s)
: impl_(std::make_shared<Impl>()),
  command_ttl_s_(validate_ttl(command_ttl_s))
{
  if (!node) {
    throw std::invalid_argument("a ROS node is required");
  }
  if (asset_id.empty()) {
    throw std::invalid_argument("asset_id must not be empty");
  }
  impl_->node = node;
  impl_->base_frame = base_frame.value_or("");
  impl_->command_timeout_s = command_timeout_s;

  // The namespace is shared by every endpoint: services, publishers and
  // actions. An action client created with a relative name would resolve to
  // the root namespace instead of the asset namespace.
  std::string ns = (namespace_.has_value() && !namespace_->empty()) ? *namespace_
    : "/robots/" + asset_id;
  while (!ns.empty() && ns.back() == '/') {
    ns.pop_back();
  }
  const auto endpoint = [&ns](const std::string & name) {return ns + "/" + name;};
  impl_->namespace_ = ns;

  impl_->description = node->create_client<chaos_interfaces::srv::GetDescription>(
    endpoint("description"));
  impl_->lease = node->create_client<chaos_interfaces::srv::ControlLease>(
    endpoint("control/lease"));
  impl_->stop = node->create_client<chaos_interfaces::srv::Stop>(endpoint("motion/stop"));
  impl_->velocity_pub = node->create_publisher<chaos_interfaces::msg::VelocityCommand>(
    endpoint("motion/velocity"), 1);

  std::weak_ptr<Impl> weak = impl_;
  impl_->state_sub = node->create_subscription<chaos_interfaces::msg::RobotState>(
    endpoint("state"), 1,
    [weak](chaos_interfaces::msg::RobotState::SharedPtr message) {
      auto impl = weak.lock();
      if (!impl) {
        return;
      }
      const std::lock_guard<std::mutex> guard(impl->mutex);
      impl->last_state = message;
    });

  std::weak_ptr<Impl> timer_weak = impl_;
  impl_->watchdog_timer = node->create_wall_timer(
    kWatchdogPeriod, [timer_weak]() {
      auto impl = timer_weak.lock();
      if (impl) {
        impl->watchdog();
      }
    });
}

std::shared_ptr<Robot> Robot::create_standalone(
  std::string asset_id,
  std::optional<std::string> namespace_,
  std::optional<std::string> base_frame,
  double command_timeout_s,
  double command_ttl_s,
  double response_timeout_s)
{
  // The standalone client owns a dedicated ROS context. Closing one client
  // never shuts the context of another client down.
  auto context = std::make_shared<rclcpp::Context>();
  static char program_name[] = "chaos_sdk";
  char * argv[] = {program_name, nullptr};
  context->init(1, argv);
  rclcpp::NodeOptions options;
  options.context(context);
  auto node = std::make_shared<rclcpp::Node>("chaos_sdk_" + asset_id, options);
  auto robot = std::shared_ptr<Robot>(new Robot(
      node, asset_id, namespace_, base_frame, command_timeout_s, command_ttl_s));
  robot->impl_->owned_context = context;
  // The executor must use the owned context as well: the default executor
  // uses the global context, which is not initialized in a standalone
  // process (for example a Python process without rclpy.init()).
  rclcpp::ExecutorOptions executor_options;
  executor_options.context = context;
  robot->impl_->executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>(
    executor_options);
  robot->impl_->executor->add_node(node);
  robot->impl_->spin_thread = std::thread([executor = robot->impl_->executor]() {
      executor->spin();
    });
  robot->set_response_timeout(response_timeout_s);
  return robot;
}

Robot::~Robot()
{
  close();
}

chaos_interfaces::msg::RobotState::ConstSharedPtr Robot::state() const
{
  const std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->last_state;
}

bool Robot::wait_ready(std::chrono::nanoseconds timeout)
{
  rclcpp::Client<chaos_interfaces::srv::GetDescription>::SharedPtr client;
  {
    const std::lock_guard<std::mutex> guard(impl_->mutex);
    client = impl_->description;
  }
  if (!client) {
    return false;
  }
  return client->wait_for_service(timeout);
}

builtin_interfaces::msg::Time Robot::deadline(std::optional<double> ttl_s) const
{
  const double ttl = ttl_s.has_value() ? validate_ttl(*ttl_s) : command_ttl_s_;
  const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
  const auto spec = deadline_timespec(ttl, now_ns);
  builtin_interfaces::msg::Time time;
  time.sec = spec.first;
  time.nanosec = spec.second;
  return time;
}

std::string Robot::require_token() const
{
  const std::lock_guard<std::mutex> guard(impl_->mutex);
  if (impl_->lease_token.empty()) {
    throw std::invalid_argument("Acquire control and await its successful response first");
  }
  return impl_->lease_token;
}

std::size_t Robot::pending_count() const
{
  const std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->pending.size();
}

void Robot::set_response_timeout(double seconds)
{
  if (!std::isfinite(seconds) || seconds <= 0.0) {
    throw std::invalid_argument("response timeout must be finite and positive");
  }
  const std::lock_guard<std::mutex> guard(impl_->mutex);
  impl_->response_timeout_s = seconds;
}

double Robot::response_timeout_s() const
{
  const std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->response_timeout_s;
}

void Robot::set_action_timeout(double seconds)
{
  if (!std::isfinite(seconds) || seconds <= 0.0) {
    throw std::invalid_argument("action timeout must be finite and positive");
  }
  const std::lock_guard<std::mutex> guard(impl_->mutex);
  impl_->action_timeout_s = seconds;
}

double Robot::action_timeout_s() const
{
  const std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->action_timeout_s;
}

void Robot::set_max_pending(std::size_t maximum)
{
  if (maximum == 0) {
    throw std::invalid_argument("max pending must be positive");
  }
  const std::lock_guard<std::mutex> guard(impl_->mutex);
  impl_->max_pending = maximum;
}

std::size_t Robot::max_pending() const
{
  const std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->max_pending;
}

std::size_t Robot::action_client_count() const
{
  const std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->navigation_slots.live_clients() + impl_->flight_slots.live_clients();
}

std::size_t Robot::action_slot_count() const
{
  const std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->navigation_slots.size() + impl_->flight_slots.size();
}

std::shared_future<chaos_interfaces::srv::GetDescription::Response> Robot::describe()
{
  auto request = std::make_shared<chaos_interfaces::srv::GetDescription::Request>();
  std::weak_ptr<Impl> weak = impl_;
  rclcpp::Client<chaos_interfaces::srv::GetDescription>::SharedPtr client;
  {
    const std::lock_guard<std::mutex> guard(impl_->mutex);
    client = impl_->description;
  }
  return send_control<chaos_interfaces::srv::GetDescription>(
    client, *request,
    [weak](const chaos_interfaces::srv::GetDescription::Response & response) {
      if (auto impl = weak.lock()) {
        impl->apply_description(response);
      }
    });
}

std::shared_future<chaos_interfaces::srv::ControlLease::Response> Robot::acquire(
  const std::string & owner, double duration_s, std::optional<double> ttl_s)
{
  chaos_interfaces::srv::ControlLease::Request request;
  request.expires_at = deadline(ttl_s);
  request.operation = "acquire";
  request.owner = owner;
  request.token = "";
  request.duration_s = duration_s;
  std::weak_ptr<Impl> weak = impl_;
  rclcpp::Client<chaos_interfaces::srv::ControlLease>::SharedPtr client;
  {
    const std::lock_guard<std::mutex> guard(impl_->mutex);
    client = impl_->lease;
  }
  return send_control<chaos_interfaces::srv::ControlLease>(
    client, request,
    [weak, owner](const chaos_interfaces::srv::ControlLease::Response & response) {
      if (auto impl = weak.lock()) {
        impl->apply_lease("acquire", response, owner);
      }
    });
}

std::shared_future<chaos_interfaces::srv::ControlLease::Response> Robot::renew(
  double duration_s, std::optional<double> ttl_s)
{
  // The token and the owner are read atomically: a concurrent release must
  // not be able to mix a new token with an old owner.
  std::string token;
  std::string owner;
  {
    const std::lock_guard<std::mutex> guard(impl_->mutex);
    if (impl_->lease_token.empty()) {
      throw std::invalid_argument("Acquire control and await its successful response first");
    }
    token = impl_->lease_token;
    owner = impl_->owner;
  }
  chaos_interfaces::srv::ControlLease::Request request;
  request.expires_at = deadline(ttl_s);
  request.operation = "renew";
  request.owner = owner;
  request.token = token;
  request.duration_s = duration_s;
  std::weak_ptr<Impl> weak = impl_;
  rclcpp::Client<chaos_interfaces::srv::ControlLease>::SharedPtr client;
  {
    const std::lock_guard<std::mutex> guard(impl_->mutex);
    client = impl_->lease;
  }
  return send_control<chaos_interfaces::srv::ControlLease>(
    client, request,
    [weak, owner](const chaos_interfaces::srv::ControlLease::Response & response) {
      if (auto impl = weak.lock()) {
        impl->apply_lease("renew", response, owner);
      }
    });
}

std::shared_future<chaos_interfaces::srv::ControlLease::Response> Robot::release(
  std::optional<double> ttl_s)
{
  std::string token;
  std::string owner;
  {
    const std::lock_guard<std::mutex> guard(impl_->mutex);
    if (impl_->lease_token.empty()) {
      throw std::invalid_argument("Acquire control and await its successful response first");
    }
    token = impl_->lease_token;
    owner = impl_->owner;
  }
  chaos_interfaces::srv::ControlLease::Request request;
  request.expires_at = deadline(ttl_s);
  request.operation = "release";
  request.owner = owner;
  request.token = token;
  request.duration_s = 0.0;
  std::weak_ptr<Impl> weak = impl_;
  rclcpp::Client<chaos_interfaces::srv::ControlLease>::SharedPtr client;
  {
    const std::lock_guard<std::mutex> guard(impl_->mutex);
    client = impl_->lease;
  }
  return send_control<chaos_interfaces::srv::ControlLease>(
    client, request,
    [weak, owner](const chaos_interfaces::srv::ControlLease::Response & response) {
      if (auto impl = weak.lock()) {
        impl->apply_lease("release", response, owner);
      }
    });
}

std::shared_future<chaos_interfaces::srv::Stop::Response> Robot::stop(
  std::optional<std::string> request_id, std::optional<double> ttl_s)
{
  chaos_interfaces::srv::Stop::Request request;
  request.expires_at = deadline(ttl_s);
  request.request_id = request_id.has_value() && !request_id->empty() ?
    *request_id : new_request_id();
  request.lease_token = require_token();
  rclcpp::Client<chaos_interfaces::srv::Stop>::SharedPtr client;
  {
    const std::lock_guard<std::mutex> guard(impl_->mutex);
    client = impl_->stop;
  }
  return send_control<chaos_interfaces::srv::Stop>(client, request);
}

std::string Robot::velocity(
  double x, double y, double z, double yaw,
  std::optional<double> valid_for_s,
  std::optional<std::string> request_id)
{
  std::string token;
  std::string frame;
  double command_timeout;
  rclcpp::Publisher<chaos_interfaces::msg::VelocityCommand>::SharedPtr publisher;
  rclcpp::Node::SharedPtr node;
  {
    const std::lock_guard<std::mutex> guard(impl_->mutex);
    if (impl_->closed) {
      throw ClientClosedError("the SDK client is closed");
    }
    if (impl_->lease_token.empty()) {
      throw std::invalid_argument("Acquire control and await its successful response first");
    }
    token = impl_->lease_token;
    frame = impl_->base_frame;
    command_timeout = impl_->command_timeout_s;
    publisher = impl_->velocity_pub;
    node = impl_->node;
  }
  if (frame.empty()) {
    throw std::invalid_argument("Call describe() first or supply base_frame");
  }
  validate_velocity(x, y, z, yaw);
  chaos_interfaces::msg::VelocityCommand message;
  message.header.stamp = node->get_clock()->now();
  message.header.frame_id = frame;
  message.request_id = request_id.has_value() && !request_id->empty() ?
    *request_id : new_request_id();
  message.lease_token = token;
  message.valid_for_s = valid_for_s.has_value() ? *valid_for_s : command_timeout;
  message.velocity.linear.x = x;
  message.velocity.linear.y = y;
  message.velocity.linear.z = z;
  message.velocity.angular.z = yaw;
  publisher->publish(message);
  return message.request_id;
}

std::shared_future<std::shared_ptr<Robot::NavigationTask>> Robot::navigate(
  const geometry_msgs::msg::PoseStamped & target,
  std::function<void (NavigatePose::Feedback)> feedback_callback,
  std::optional<double> ttl_s)
{
  NavigatePose::Goal goal;
  goal.expires_at = deadline(ttl_s);
  goal.lease_token = require_token();
  goal.target = target;
  return send_goal<NavigatePose>(
    impl_->namespace_ + "/navigation/to_pose", goal, std::move(feedback_callback));
}

std::shared_future<std::shared_ptr<Robot::FlightTask>> Robot::takeoff(
  double altitude_m,
  std::function<void (Flight::Feedback)> feedback_callback,
  std::optional<double> ttl_s)
{
  Flight::Goal goal;
  goal.expires_at = deadline(ttl_s);
  goal.lease_token = require_token();
  goal.altitude_m = altitude_m;
  return send_goal<Flight>(
    impl_->namespace_ + "/flight/takeoff", goal, std::move(feedback_callback));
}

std::shared_future<std::shared_ptr<Robot::FlightTask>> Robot::land(
  std::function<void (Flight::Feedback)> feedback_callback,
  std::optional<double> ttl_s)
{
  Flight::Goal goal;
  goal.expires_at = deadline(ttl_s);
  goal.lease_token = require_token();
  goal.altitude_m = 0.0;
  return send_goal<Flight>(
    impl_->namespace_ + "/flight/land", goal, std::move(feedback_callback));
}

std::string Robot::lease_token() const
{
  const std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->lease_token;
}

std::string Robot::owner() const
{
  const std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->owner;
}

std::string Robot::base_frame() const
{
  const std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->base_frame;
}

double Robot::command_timeout_s() const
{
  const std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->command_timeout_s;
}

template<typename ServiceT>
std::shared_future<typename ServiceT::Response> Robot::send_control(
  typename rclcpp::Client<ServiceT>::SharedPtr client,
  typename ServiceT::Request request,
  std::function<void (const typename ServiceT::Response &)> update)
{
  if (!client) {
    throw ClientClosedError("the SDK client is closed");
  }
  auto settler = std::make_shared<Settler<typename ServiceT::Response>>();
  auto cancelled = std::make_shared<std::atomic_bool>(false);
  // The rclcpp request id is only known after the send; the watchdog reads it
  // through this holder so a late response can be dropped from the client's
  // internal pending map.
  auto request_id = std::make_shared<std::atomic<std::int64_t>>(-1);
  // The payload is kept in a shared object: the request may be handed to the
  // middleware later, when the service is discovered.
  auto payload = std::make_shared<typename ServiceT::Request>(std::move(request));
  std::weak_ptr<Impl> weak = impl_;
  // The id is shared with the callbacks: they run later, on the executor.
  auto operation_id = std::make_shared<std::uint64_t>(0);

  // The response callback claims the completion before it touches the cached
  // state: a response that arrives after the local timeout or after close()
  // must not update the cache.
  auto on_response = [weak, settler, update, operation_id](
    typename rclcpp::Client<ServiceT>::SharedFutureWithRequest result) {
      auto impl = weak.lock();
      if (!impl) {
        settler->fail(std::make_exception_ptr(
            ClientClosedError("the SDK client was destroyed")));
        return;
      }
      bool claimed = false;
      {
        const std::lock_guard<std::mutex> guard(impl->mutex);
        claimed = impl->claim_operation(*operation_id);
      }
      if (!claimed) {
        return;
      }
      try {
        const auto response = result.get().second;
        if (response && update) {
          update(*response);
        }
        if (response) {
          settler->succeed(*response);
        } else {
          settler->fail(std::make_exception_ptr(
              std::runtime_error("service response was null")));
        }
      } catch (const std::exception &) {
        settler->fail(std::current_exception());
      }
    };

  // Hand the request to the middleware, but only once the service is
  // discovered. A request that is sent earlier is dropped by the middleware
  // and would be lost until the local timeout.
  //
  // The callback runs under the implementation mutex: the watchdog and the
  // send share the lock, so a request is never sent for an operation that is
  // already gone. It must therefore not take that mutex again.
  auto send_when_ready = [client, payload, request_id, cancelled, settler, on_response]() -> bool {
      if (!client->service_is_ready()) {
        return false;
      }
      try {
        auto pending_call = client->async_send_request(payload, on_response);
        request_id->store(pending_call.request_id);
        if (cancelled->load()) {
          // The local timeout fired while the request was being sent: the
          // rclcpp pending entry must be dropped explicitly, otherwise it
          // would stay in the client forever.
          try {
            client->remove_pending_request(pending_call.request_id);
          } catch (const std::exception &) {
          }
        }
        return true;
      } catch (const std::exception &) {
        // The request could not be handed to the middleware at all.
        settler->fail(std::current_exception());
        return true;
      }
    };

  {
    const std::lock_guard<std::mutex> guard(impl_->mutex);
    if (impl_->closed) {
      throw ClientClosedError("the SDK client is closed");
    }
    if (impl_->pending.size() >= impl_->max_pending) {
      throw PendingOverflowError("too many in-flight requests; await a response first");
    }
    const std::uint64_t id = impl_->next_operation_id++;
    *operation_id = id;
    Impl::Operation operation;
    operation.id = id;
    operation.deadline = deadline_after(impl_->response_timeout_s);
    operation.cancelled = cancelled;
    operation.cancel_local = [client, cancelled, request_id](StopReason) {
        cancelled->store(true);
        const std::int64_t inner = request_id->load();
        if (inner >= 0) {
          try {
            client->remove_pending_request(inner);
          } catch (const std::exception &) {
            // The request may already be completed; cleanup is best effort.
          }
        }
      };
    operation.fail = [settler](const std::exception_ptr & error) {settler->fail(error);};
    operation.send_when_ready = send_when_ready;
    impl_->pending.emplace(id, std::move(operation));
    // Send it right away when the service is already known; otherwise the
    // watchdog retries until the discovery succeeded.
    auto entry = impl_->pending.find(id);
    if (entry != impl_->pending.end() && entry->second.send_when_ready()) {
      entry->second.sent = true;
    }
  }
  return settler->future();
}

template<typename ActionT>
std::shared_future<std::shared_ptr<ActionTask<ActionT>>> Robot::send_goal(
  const std::string & action_name,
  typename ActionT::Goal goal,
  std::function<void (typename ActionT::Feedback)> feedback_callback)
{
  using ClientT = rclcpp_action::Client<ActionT>;
  using TaskT = ActionTask<ActionT>;
  using WrappedResult = typename ClientT::GoalHandle::WrappedResult;

  auto accept = std::make_shared<Settler<std::shared_ptr<TaskT>>>();
  auto result = std::make_shared<Settler<WrappedResult>>();
  // The shared state is owned by the pending operation while the goal is
  // observed and by the user afterwards. Every callback references it
  // weakly, so no callback keeps the state alive.
  auto shared = std::make_shared<typename TaskT::Shared>();
  std::weak_ptr<Impl> weak = impl_;
  std::weak_ptr<typename TaskT::Shared> weak_shared = shared;

  // --- Phase 1: reserve a client slot and register the operation ---------
  std::uint64_t operation_id = 0;
  std::size_t slot_index = 0;
  std::uint64_t slot_generation = 0;
  typename ClientT::SharedPtr client;
  ActionSlotPool<ActionT> * pool = nullptr;
  {
    const std::lock_guard<std::mutex> guard(impl_->mutex);
    if (impl_->closed) {
      throw ClientClosedError("the SDK client is closed");
    }
    if (impl_->pending.size() >= impl_->max_pending) {
      throw PendingOverflowError("too many in-flight requests; await a response first");
    }
    if constexpr (std::is_same_v<ActionT, chaos_interfaces::action::NavigatePose>) {
      pool = &impl_->navigation_slots;
    } else {
      pool = &impl_->flight_slots;
    }
    const auto lease = pool->acquire(impl_->node, action_name);
    client = lease.client;
    slot_index = lease.index;
    slot_generation = lease.generation;

    operation_id = impl_->next_operation_id++;
    Impl::Operation operation;
    operation.id = operation_id;
    operation.deadline = deadline_after(impl_->response_timeout_s);
    operation.cancelled = std::make_shared<std::atomic_bool>(false);
    // The pending operation owns the shared state: it must stay alive until
    // the goal completes, times out or is closed.
    operation.cancel_local = [weak, weak_client = std::weak_ptr<ClientT>(client), weak_shared,
      shared, release_slot = std::function<void (bool)>(
        [weak, pool, slot_index, slot_generation](bool retire) {
          if (auto impl = weak.lock()) {
            const std::lock_guard<std::mutex> impl_guard(impl->mutex);
            pool->release(slot_index, slot_generation, retire);
          }
        })](StopReason reason) {
        // Stop the local observation only: the remote execution state stays
        // unknown, the goal is not claimed to be stopped.
        auto impl = weak.lock();
        if (!impl) {
          return;
        }
        auto client_locked = weak_client.lock();
        if (client_locked) {
          typename ClientT::GoalHandle::SharedPtr handle;
          {
            const std::lock_guard<std::mutex> state_guard(shared->mutex);
            handle = shared->handle;
          }
          if (handle) {
            // Drop the callbacks of this goal only; the other goals that
            // share the client keep their observation.
            try {
              client_locked->stop_callbacks(handle);
            } catch (const std::exception &) {
            }
          }
        }
        {
          const std::lock_guard<std::mutex> state_guard(shared->mutex);
          // A timeout and a close are different observations: only the
          // timeout is a local deadline, the close says nothing about the
          // remote goal.
          shared->task_state.store(static_cast<int>(
              reason == StopReason::Timeout ? TaskState::TIMED_OUT : TaskState::UNKNOWN));
          shared->handle.reset();
          shared->cancel_requester = nullptr;
        }
        // Retire this slot only: the ROS client of the slot is released, the
        // clients of the other slots keep serving their goals.
        release_slot(true);
      };
    operation.fail = [accept, result](const std::exception_ptr & error) {
        accept->fail(error);
        result->fail(error);
      };
    impl_->pending.emplace(operation_id, std::move(operation));
  }

  // Releasing the slot must not happen while the implementation mutex is
  // held: the release helper takes the same mutex.
  const auto release_slot = [weak, pool, slot_index, slot_generation](bool retire) {
      if (auto impl = weak.lock()) {
        const std::lock_guard<std::mutex> impl_guard(impl->mutex);
        pool->release(slot_index, slot_generation, retire);
      }
    };

  // --- Phase 2: send the goal ---------------------------------------------
  typename ClientT::SendGoalOptions options;
  const std::weak_ptr<ClientT> weak_client = client;
  options.goal_response_callback =
    [weak, weak_client, weak_shared, accept, result, release_slot, operation_id](
      typename ClientT::GoalHandle::SharedPtr handle) {
      auto impl = weak.lock();
      if (!impl) {
        accept->fail(std::make_exception_ptr(ClientClosedError("the SDK client was destroyed")));
        result->fail(std::make_exception_ptr(ClientClosedError("the SDK client was destroyed")));
        return;
      }
      auto client_locked = weak_client.lock();
      auto state = weak_shared.lock();
      if (!client_locked || !state) {
        accept->fail(std::make_exception_ptr(ClientClosedError("the SDK client was destroyed")));
        result->fail(std::make_exception_ptr(ClientClosedError("the SDK client was destroyed")));
        return;
      }
      // The acceptance and the state transition happen in the same critical
      // section as the claim. A late acceptance after a timeout or a close
      // finds no entry and must not resurrect the task.
      bool accepted = false;
      bool rejected = false;
      {
        const std::lock_guard<std::mutex> guard(impl->mutex);
        auto entry = impl->pending.find(operation_id);
        if (entry != impl->pending.end()) {
          if (handle) {
            // The goal may run far longer than a service response: the
            // observation window becomes the action timeout.
            entry->second.deadline = deadline_after(impl->action_timeout_s);
            {
              const std::lock_guard<std::mutex> state_guard(state->mutex);
              state->handle = handle;
              state->result_future = result->future();
              // The acceptance and the goal id are captured once and kept
              // afterwards: the readable history must survive the release of
              // the ROS resources.
              state->was_accepted = true;
              state->goal_id = goal_id_hex(handle->get_goal_id());
              state->task_state.store(static_cast<int>(TaskState::ACCEPTED));
            }
            accepted = true;
          } else {
            // The server rejected the goal: the observation ends here. The
            // claim decides whether the rejection is still relevant, a late
            // rejection must not overwrite a terminal state.
            rejected = impl->claim_operation(operation_id);
            if (rejected) {
              const std::lock_guard<std::mutex> state_guard(state->mutex);
              state->task_state.store(static_cast<int>(TaskState::REJECTED));
              state->handle.reset();
              state->cancel_requester = nullptr;
            }
          }
        }
      }
      if (!accepted && !rejected) {
        // Late response after the local timeout or after close: the task
        // must not be resurrected.
        result->fail(std::make_exception_ptr(
            TimeoutError("the goal response arrived after the local timeout")));
        release_slot(true);
        return;
      }
      if (rejected) {
        release_slot(false);
        accept->succeed(nullptr);
        result->fail(std::make_exception_ptr(std::runtime_error("the action goal was rejected")));
        return;
      }
      try {
        // The goal handle alone has no public result API in C++; the client
        // owns it, so the client is asked for the terminal result.
        client_locked->async_get_result(
          handle, [weak, weak_shared, result, release_slot, operation_id](
            const WrappedResult & wrapped) {
            auto impl = weak.lock();
            auto state = weak_shared.lock();
            if (!impl || !state) {
              return;
            }
            TaskState final_state = TaskState::UNKNOWN;
            bool claimed = false;
            {
              // Claim the completion and compute the terminal state under the
              // same lock: a late result must not overwrite the state of an
              // observation that already timed out or was closed.
              const std::lock_guard<std::mutex> guard(impl->mutex);
              claimed = impl->claim_operation(operation_id);
              if (claimed) {
                // A terminal status without a result payload never proves
                // success: the payload carries the business outcome.
                final_state = classify_result(
                  wrapped.code,
                  wrapped.result != nullptr,
                  wrapped.result ? wrapped.result->success : false);
                {
                  // Release the ROS resources of this goal: the task keeps
                  // the readable outcome, but no client and no handle.
                  const std::lock_guard<std::mutex> state_guard(state->mutex);
                  state->handle.reset();
                  state->cancel_requester = nullptr;
                }
              }
            }
            if (!claimed) {
              return;
            }
            // The slot is released outside of the implementation mutex.
            release_slot(false);
            state->task_state.store(static_cast<int>(final_state));
            result->succeed(wrapped);
          });
      } catch (const std::exception &) {
        {
          const std::lock_guard<std::mutex> guard(impl->mutex);
          impl->claim_operation(operation_id);
        }
        release_slot(true);
        accept->fail(std::current_exception());
        result->fail(std::current_exception());
        return;
      }
      // The cancel requester references the action client weakly and is
      // cleared on the terminal state, so a finished task does not keep a
      // client alive.
      {
        const std::lock_guard<std::mutex> state_guard(state->mutex);
        state->cancel_requester =
          [weak, weak_client, weak_shared, result, release_slot, operation_id]()
          -> std::shared_future<CancelOutcome> {
            auto impl = weak.lock();
            auto client_locked = weak_client.lock();
            auto state = weak_shared.lock();
            if (!impl || !client_locked || !state) {
              auto promise = std::make_shared<std::promise<CancelOutcome>>();
              promise->set_exception(std::make_exception_ptr(
                  ClientClosedError("the SDK client is not available any more")));
              return promise->get_future().share();
            }
            typename ClientT::GoalHandle::SharedPtr handle;
            {
              const std::lock_guard<std::mutex> state_guard(state->mutex);
              handle = state->handle;
            }
            if (!handle) {
              auto promise = std::make_shared<std::promise<CancelOutcome>>();
              promise->set_exception(std::make_exception_ptr(
                  std::runtime_error("the action goal is no longer observed")));
              return promise->get_future().share();
            }
            auto settler = std::make_shared<Settler<CancelOutcome>>();
            const std::uint64_t cancel_id = [&]() {
                const std::lock_guard<std::mutex> guard(impl->mutex);
                if (impl->closed) {
                  throw ClientClosedError("the SDK client is closed");
                }
                if (impl->pending.size() >= impl->max_pending) {
                  throw PendingOverflowError("too many in-flight requests");
                }
                const std::uint64_t id = impl->next_operation_id++;
                Impl::Operation operation;
                operation.id = id;
                operation.deadline = deadline_after(impl->response_timeout_s);
                operation.cancelled = std::make_shared<std::atomic_bool>(false);
                // The cancel request has no per request cleanup in
                // rclcpp_action. On timeout the observation of the goal
                // itself is stopped, the goal is settled and the slot of
                // this goal is retired, which releases the client and with it
                // the unanswered cancel request. Only this slot is affected.
                operation.cancel_local = [weak, weak_shared, result, release_slot,
                  operation_id](StopReason reason) {
                    auto impl = weak.lock();
                    auto state = weak_shared.lock();
                    // The goal observation is only stopped when this request
                    // still owns it. A goal that already reached a terminal
                    // state keeps that state: the unanswered cancel request
                    // must not overwrite a completed result.
                    bool goal_claimed = false;
                    if (impl) {
                      // Stop the observation of the goal itself.
                      const std::lock_guard<std::mutex> guard(impl->mutex);
                      goal_claimed = impl->claim_operation(operation_id);
                    }
                    if (goal_claimed && state) {
                      const std::lock_guard<std::mutex> state_guard(state->mutex);
                      state->task_state.store(static_cast<int>(
                          reason == StopReason::Timeout ? TaskState::TIMED_OUT : TaskState::UNKNOWN));
                      state->handle.reset();
                      state->cancel_requester = nullptr;
                    }
                    if (goal_claimed && result) {
                      result->fail(std::make_exception_ptr(
                          TimeoutError("the cancel request timed out")));
                    }
                    // The slot of the goal is retired: the unanswered cancel
                    // request lives in that client and is dropped with it.
                    // The generation check keeps a newer goal of the same
                    // slot safe.
                    release_slot(true);
                  };
                operation.fail = [settler](const std::exception_ptr & error) {settler->fail(error);};
                impl->pending.emplace(id, std::move(operation));
                return id;
              }();
            try {
              client_locked->async_cancel_goal(
                handle, [weak, settler, cancel_id](
                  const typename ClientT::CancelResponse::SharedPtr & response) {
                  auto impl = weak.lock();
                  if (!impl) {
                    settler->fail(std::make_exception_ptr(
                        ClientClosedError("the SDK client was destroyed")));
                    return;
                  }
                  // Only the owner of the pending cancel request may settle
                  // it. A response that arrives after the local timeout or
                  // after close() must not report a successful cancellation.
                  bool claimed = false;
                  {
                    const std::lock_guard<std::mutex> guard(impl->mutex);
                    claimed = impl->claim_operation(cancel_id);
                  }
                  if (!claimed) {
                    return;
                  }
                  CancelOutcome outcome;
                  if (response) {
                    // The server confirms the cancellation with return_code
                    // == 0 and the list of the goals that are cancelling.
                    outcome.return_code = response->return_code;
                    outcome.goals_canceling = response->goals_canceling.size();
                    outcome.accepted = response->return_code == 0 &&
                      !response->goals_canceling.empty();
                  }
                  settler->succeed(outcome);
                });
            } catch (const std::exception &) {
              {
                const std::lock_guard<std::mutex> guard(impl->mutex);
                impl->claim_operation(cancel_id);
              }
              settler->fail(std::current_exception());
            }
            return settler->future();
          };
      }
      accept->succeed(std::make_shared<TaskT>(state));
    };
  if (feedback_callback) {
    options.feedback_callback =
      [feedback_callback](typename ClientT::GoalHandle::SharedPtr,
      const std::shared_ptr<const typename ActionT::Feedback> feedback) {
        if (!feedback) {
          return;
        }
        try {
          feedback_callback(*feedback);
        } catch (const std::exception &) {
          // A user callback must never kill the executor thread.
        } catch (...) {
        }
      };
  }

  try {
    client->async_send_goal(goal, options);
  } catch (...) {
    {
      const std::lock_guard<std::mutex> guard(impl_->mutex);
      impl_->claim_operation(operation_id);
    }
    release_slot(true);
    throw;
  }
  return accept->future();
}

void Robot::close()
{
  auto impl = impl_;
  std::vector<Impl::Operation> pending;
  {
    const std::lock_guard<std::mutex> guard(impl->mutex);
    if (impl->closed) {
      return;
    }
    impl->closed = true;
    for (const auto & item : impl->pending) {
      // Mark the observation as cancelled under the lock: a response or a
      // goal acceptance that arrives after close() must not update the cached
      // state any more.
      if (item.second.cancelled) {
        item.second.cancelled->store(true);
      }
      pending.push_back(item.second);
    }
    impl->pending.clear();
  }
  const auto error = std::make_exception_ptr(
    ClientClosedError("the SDK client was closed before a response arrived"));
  for (auto & operation : pending) {
    if (operation.cancel_local) {
      operation.cancel_local(StopReason::Closed);
    }
    if (operation.fail) {
      operation.fail(error);
    }
  }
  {
    const std::lock_guard<std::mutex> guard(impl->mutex);
    impl->watchdog_timer.reset();
    impl->description.reset();
    impl->lease.reset();
    impl->stop.reset();
    impl->velocity_pub.reset();
    impl->state_sub.reset();
  }
  if (impl->executor) {
    impl->executor->cancel();
  }
  if (impl->spin_thread.joinable()) {
    if (impl->spin_thread.get_id() == std::this_thread::get_id()) {
      impl->spin_thread.detach();
    } else {
      impl->spin_thread.join();
    }
  }
  // Only a context owned by this client is shut down; other clients keep
  // their own context alive.
  if (impl->owned_context && impl->owned_context->is_valid()) {
    impl->owned_context->shutdown("chaos_sdk client closed");
  }
}

template std::shared_future<chaos_interfaces::srv::GetDescription::Response>
Robot::send_control<chaos_interfaces::srv::GetDescription>(
  rclcpp::Client<chaos_interfaces::srv::GetDescription>::SharedPtr,
  chaos_interfaces::srv::GetDescription::Request,
  std::function<void (const chaos_interfaces::srv::GetDescription::Response &)>);
template std::shared_future<chaos_interfaces::srv::ControlLease::Response>
Robot::send_control<chaos_interfaces::srv::ControlLease>(
  rclcpp::Client<chaos_interfaces::srv::ControlLease>::SharedPtr,
  chaos_interfaces::srv::ControlLease::Request,
  std::function<void (const chaos_interfaces::srv::ControlLease::Response &)>);
template std::shared_future<chaos_interfaces::srv::Stop::Response>
Robot::send_control<chaos_interfaces::srv::Stop>(
  rclcpp::Client<chaos_interfaces::srv::Stop>::SharedPtr,
  chaos_interfaces::srv::Stop::Request,
  std::function<void (const chaos_interfaces::srv::Stop::Response &)>);

template std::shared_future<std::shared_ptr<Robot::NavigationTask>>
Robot::send_goal<chaos_interfaces::action::NavigatePose>(
  const std::string &, chaos_interfaces::action::NavigatePose::Goal,
  std::function<void (chaos_interfaces::action::NavigatePose::Feedback)>);
template std::shared_future<std::shared_ptr<Robot::FlightTask>>
Robot::send_goal<chaos_interfaces::action::Flight>(
  const std::string &, chaos_interfaces::action::Flight::Goal,
  std::function<void (chaos_interfaces::action::Flight::Feedback)>);

}  // namespace chaos_sdk
