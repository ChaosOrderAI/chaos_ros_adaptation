#pragma once

/// Bounded, per-goal action observation for the MQTT gateway.
///
/// Contract of this layer (the reason it exists at all):
///
///  * Every in-flight goal owns a *dedicated* `rclcpp_action::Client`. Two
///    goals never share a client, so retiring one observation can never drop
///    the callbacks of a different, still valid goal.
///  * rclcpp_action exposes no API to remove a single pending goal request or
///    a single pending result request. The only way to drop that
///    middleware-side bookkeeping is to destroy the client that owns it.
///    Therefore every observation gets its own client and an abandoned
///    observation destroys exactly that client - nothing else.
///  * The ROS callbacks capture the observer *weakly*. The client therefore
///    never keeps the observer (and through it the gateway node) alive, and
///    the observer can always be destroyed. Destroying the observer destroys
///    the client, which drops the pending service requests with it.
///  * A client whose goal finished normally (rejected or result received) has
///    no pending service request left and is recycled through a small bounded
///    idle pool. A client of an abandoned observation is destroyed, because
///    its SendGoal/GetResult request may still be pending.
///  * The observer never touches its own client from inside a client
///    callback. Destroying a client while its result callback is on the stack
///    would free the callback's owner. Releases are therefore deferred to a
///    safe context (the gateway's expire timer or node destruction).
///
/// Threading: every method is called on the executor thread of the node that
/// owns the observation. The gateway runs a MutuallyExclusive executor, so
/// the callbacks are serialised; the gateway still guards its correlation
/// records with its own mutex.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>
#include <sstream>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include "chaos_core/action_result.hpp"

namespace chaos_mqtt
{

using chaos::ResultHasApplied;

/// Terminal information of one action goal, normalised from the ROS action
/// protocol so the gateway stays independent of the concrete action type.
struct ActionOutcome
{
  /// True when the server accepted the goal.
  bool accepted = false;
  /// True when the terminal status carried a real result payload.
  ///
  /// A goal that reached a terminal status without a result (or with an
  /// unknown status) leaves the *execution state unknown*: the gateway must
  /// not claim that the robot stopped, it only knows that it lost track of
  /// the goal.
  bool known = false;
  /// True only for a known result whose payload reports success.
  bool success = false;
  /// Application level result code (`result.code`).
  std::string code;
  /// Application level result message (`result.message`).
  std::string message;
  /// Set only when the action result has an `applied` field. Other actions
  /// leave it empty so their terminal JSON stays unchanged.
  std::optional<bool> applied;
  /// `action_msgs/GoalStatus` value of the terminal status.
  int ros_goal_status = 0;
  /// Hexadecimal UUID of the goal; empty while the goal is unacknowledged.
  std::string goal_id_hex;
};

/// Receiver of the action lifecycle events produced by `ActionObserver`.
///
/// The gateway implements this interface. Every method is invoked on the
/// executor thread of the node that owns the observation.
class ActionSink
{
public:
  virtual ~ActionSink() = default;

  /// The server accepted the goal. `goal_id_hex` correlates the feedback and
  /// the terminal result with the originating request.
  virtual void on_action_accepted(const std::string & request_id, const std::string & goal_id_hex) = 0;

  /// The server refused the goal; no goal was created on the server side.
  virtual void on_action_rejected(const std::string & request_id) = 0;

  /// The goal reached a terminal status.
  virtual void on_action_result(const std::string & request_id, const ActionOutcome & outcome) = 0;
};

/// Hexadecimal representation of a ROS 2 goal UUID (16 raw bytes).
inline std::string action_goal_id_hex(const std::array<uint8_t, 16> & uuid)
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

/// One observed action goal plus the dedicated client that serves it.
template<typename ActionT>
class ActionObserver : public std::enable_shared_from_this<ActionObserver<ActionT>>
{
public:
  using Client = rclcpp_action::Client<ActionT>;
  using GoalHandle = typename Client::GoalHandle;
  using WrappedResult = typename Client::WrappedResult;

  /// Lifecycle of the observation.
  enum class State
  {
    /// The goal request was sent, the server has not answered yet. The client
    /// may still own a pending SendGoal request.
    kPending,
    /// The goal was accepted; the client may still own a pending GetResult
    /// request.
    kAccepted,
    /// The server refused the goal. No request is pending any more.
    kRejected,
    /// The terminal result arrived. No request is pending any more.
    kCompleted,
  };

  /// Create the observation and its dedicated action client.
  ///
  /// The client is created eagerly so that the readiness of the action server
  /// can be checked before the goal is sent. The readiness of a fresh client
  /// is *not* guaranteed to be immediate: a client that was created after the
  /// node graph was queried may not see its server yet. The gateway therefore
  /// defers the dispatch (`send_if_ready` / `ActionSlotPool::flush_ready`)
  /// instead of blocking the executor or failing the command.
  ActionObserver(
    rclcpp::Node * node,
    std::string action_name,
    ActionSink * sink,
    std::string request_id)
  : node_(node),
    action_name_(std::move(action_name)),
    sink_(sink),
    request_id_(std::move(request_id))
  {
    client_ = rclcpp_action::create_client<ActionT>(node_, action_name_);
  }

  ~ActionObserver()
  {
    destroy();
  }

  ActionObserver(const ActionObserver &) = delete;
  ActionObserver & operator=(const ActionObserver &) = delete;

  /// Re-target a recycled observer at a new request.
  void reset_for(std::string request_id)
  {
    request_id_ = std::move(request_id);
    state_ = State::kPending;
    goal_id_hex_.clear();
    goal_handle_.reset();
  }

  const std::string & request_id() const {return request_id_;}
  const std::string & goal_id_hex() const {return goal_id_hex_;}
  State state() const {return state_;}

  /// Weak view on the dedicated client.
  ///
  /// The observer itself is the only owner of the client. A caller that
  /// keeps this weak handle can therefore prove that a retired observation
  /// really released its client instead of only counting pool entries: after
  /// `destroy()` the weak handle must be expired.
  std::weak_ptr<Client> weak_client() const {return client_;}

  /// True when an action server for this action is reachable.
  bool server_ready() const
  {
    return client_ != nullptr && client_->action_server_is_ready();
  }

  /// Send the goal through the dedicated client.
  ///
  /// The callbacks capture this observer weakly, so the client never keeps
  /// the observer alive. A late acknowledgment or result of an observation
  /// that was already retired is dropped by the observer itself.
  void send(const typename ActionT::Goal & goal)
  {
    const std::weak_ptr<ActionObserver<ActionT>> weak_self = this->weak_from_this();
    typename Client::SendGoalOptions options;
    options.goal_response_callback =
      [weak_self](typename GoalHandle::SharedPtr handle) {
        const auto self = weak_self.lock();
        if (!self) {
          return;
        }
        self->handle_goal_response(std::move(handle));
      };
    options.result_callback =
      [weak_self](const WrappedResult & wrapped) {
        const auto self = weak_self.lock();
        if (!self) {
          return;
        }
        self->handle_result(wrapped);
      };
    client_->async_send_goal(goal, options);
  }

  /// Remember the goal for a later dispatch.
  ///
  /// A freshly created client may not see its server yet. The gateway then
  /// defers the dispatch instead of blocking the executor: the goal is sent
  /// as soon as the client becomes ready, and the correlation deadline bounds
  /// the wait.
  void set_goal(typename ActionT::Goal goal)
  {
    goal_ = std::move(goal);
    has_goal_ = true;
  }

  /// Send the remembered goal when the action server is reachable.
  ///
  /// Returns false when the server is not discovered yet; the caller then
  /// keeps the observation pending.
  bool send_if_ready()
  {
    if (!has_goal_) {
      return true;
    }
    if (!server_ready()) {
      return false;
    }
    has_goal_ = false;
    send(goal_);
    return true;
  }

  /// True when the observation reached a terminal state and its slot can be
  /// released.
  bool finished() const
  {
    return state_ == State::kRejected || state_ == State::kCompleted;
  }

  /// True when the client may still own a pending service request and
  /// therefore has to be destroyed instead of recycled.
  bool needs_destroy() const
  {
    return state_ == State::kPending || state_ == State::kAccepted;
  }

  /// Stop the ROS callbacks of the observed goal.
  ///
  /// `stop_callbacks` drops the per-goal callback registration and the goal
  /// handle entry of the client. It must not be called from inside the
  /// client's own callback.
  void stop_callbacks()
  {
    if (client_ && goal_handle_) {
      try {
        client_->stop_callbacks(goal_handle_);
      } catch (const std::exception &) {
        // The goal may already have been removed from the client; stopping the
        // callbacks is best effort and must never throw here.
      }
    }
    goal_handle_.reset();
  }

  /// Stop the callbacks and destroy the dedicated client.
  ///
  /// Destroying the client removes its service clients and subscriptions from
  /// the node and drops every pending service request it still owns.
  void destroy()
  {
    stop_callbacks();
    client_.reset();
  }

private:
  /// The server answered the goal request.
  void handle_goal_response(typename GoalHandle::SharedPtr handle)
  {
    if (!handle) {
      // The server refused the goal: no goal exists on the server side.
      state_ = State::kRejected;
      sink_->on_action_rejected(request_id_);
      return;
    }
    goal_handle_ = handle;
    goal_id_hex_ = action_goal_id_hex(handle->get_goal_id());
    state_ = State::kAccepted;
    sink_->on_action_accepted(request_id_, goal_id_hex_);
  }

  /// The goal reached a terminal status.
  void handle_result(const WrappedResult & wrapped)
  {
    ActionOutcome outcome;
    outcome.accepted = true;
    // A terminal status without a result payload (or with an unknown status)
    // means the gateway lost track of the goal: the execution state stays
    // unknown, the robot may still be executing the goal.
    outcome.known = wrapped.code != rclcpp_action::ResultCode::UNKNOWN && wrapped.result != nullptr;
    outcome.success = outcome.known &&
      wrapped.code == rclcpp_action::ResultCode::SUCCEEDED && wrapped.result->success;
    outcome.code = outcome.known ? wrapped.result->code : std::string("UNKNOWN");
    outcome.message = outcome.known ? wrapped.result->message : std::string();
    if (outcome.known) {
      if constexpr (ResultHasApplied<typename ActionT::Result>::value) {
        outcome.applied = wrapped.result->applied;
      }
    }
    // rclcpp_action::ResultCode mirrors the action_msgs/GoalStatus values.
    outcome.ros_goal_status = static_cast<int>(wrapped.code);
    outcome.goal_id_hex = goal_id_hex_;
    state_ = State::kCompleted;
    sink_->on_action_result(request_id_, outcome);
  }

  rclcpp::Node * node_;
  std::string action_name_;
  ActionSink * sink_;
  std::string request_id_;
  State state_ = State::kPending;
  std::string goal_id_hex_;
  typename GoalHandle::SharedPtr goal_handle_;
  typename Client::SharedPtr client_;
  typename ActionT::Goal goal_;
  bool has_goal_ = false;
};
/// Bounded pool of action observations for one action type.
///
/// The pool owns the exclusive right to use an observation slot. The number of
/// simultaneously observed goals is bounded by the pool capacity, so neither
/// the gateway's own bookkeeping nor the middleware's pending requests can
/// grow without limit.
template<typename ActionT>
class ActionSlotPool
{
public:
  using Observer = ActionObserver<ActionT>;

  /// Upper bound of clients kept alive for reuse. Recycling avoids the graph
  /// churn of recreating a client per goal; the bound keeps the number of
  /// dormant DDS entities small.
  static constexpr std::size_t kMaxIdleObservers = 4;

  ActionSlotPool(
    rclcpp::Node * node,
    std::string action_name,
    std::string command_name,
    ActionSink * sink,
    std::size_t capacity)
  : node_(node),
    action_name_(std::move(action_name)),
    command_name_(std::move(command_name)),
    sink_(sink),
    capacity_(std::max<std::size_t>(1, capacity))
  {}

  /// Reserve an observation slot for `request_id`.
  ///
  /// Returns nullptr when the bounded capacity is exhausted; the caller then
  /// rejects the command instead of growing the observation state.
  std::shared_ptr<Observer> acquire(const std::string & request_id)
  {
    if (busy_.size() >= capacity_) {
      return nullptr;
    }
    std::shared_ptr<Observer> observer;
    if (!idle_.empty()) {
      observer = idle_.back();
      idle_.pop_back();
      observer->reset_for(request_id);
    } else {
      observer = std::make_shared<Observer>(node_, action_name_, sink_, request_id);
    }
    busy_[request_id] = observer;
    return observer;
  }

  /// Release the observation of `request_id`.
  ///
  /// A clean client (rejected or completed goal) is recycled through the idle
  /// pool; a client that may still own a pending service request is destroyed
  /// so that the middleware bookkeeping of exactly that goal disappears.
  void release(const std::string & request_id)
  {
    const auto it = busy_.find(request_id);
    if (it == busy_.end()) {
      return;
    }
    const std::shared_ptr<Observer> observer = it->second;
    busy_.erase(it);
    if (observer->needs_destroy()) {
      observer->destroy();
      return;
    }
    observer->stop_callbacks();
    if (idle_.size() >= kMaxIdleObservers) {
      observer->destroy();
      return;
    }
    idle_.push_back(observer);
  }

  /// Mark the observation as waiting for its action server to be discovered.
  ///
  /// The goal is dispatched by `flush_ready` as soon as the dedicated client
  /// can reach a server. The correlation record keeps the overall deadline.
  void defer(const std::string & request_id)
  {
    deferred_.insert(request_id);
  }

  /// Dispatch the deferred goals whose client became ready.
  ///
  /// `still_tracked` answers whether the gateway still correlates the
  /// request; a request that already timed out must never be sent to the
  /// robot.
  ///
  /// `dispatch_allowed` answers whether the request may still reach the
  /// robot at all: a deferred goal whose original command TTL elapsed must
  /// not be sent, even while its correlation is still alive. The predicate is
  /// optional - without it every tracked request may be dispatched, which
  /// keeps the direct users of the pool working unchanged.
  void flush_ready(
    const std::function<bool(const std::string &)> & still_tracked,
    const std::function<bool(const std::string &)> & dispatch_allowed = nullptr)
  {
    std::vector<std::string> dispatched;
    std::vector<std::string> blocked;
    for (const auto & request_id : deferred_) {
      const auto it = busy_.find(request_id);
      if (it == busy_.end() || !still_tracked(request_id)) {
        continue;
      }
      if (dispatch_allowed && !dispatch_allowed(request_id)) {
        // The request may no longer be sent to the robot. The caller retires
        // the correlation and releases the observation.
        blocked.push_back(request_id);
        continue;
      }
      if (it->second->send_if_ready()) {
        dispatched.push_back(request_id);
      }
    }
    for (const auto & request_id : dispatched) {
      deferred_.erase(request_id);
    }
    for (const auto & request_id : blocked) {
      deferred_.erase(request_id);
    }
  }

  /// Release every observation that reached a terminal state, plus every
  /// observation whose correlation record disappeared (a retired goal).
  ///
  /// `still_tracked` answers whether the gateway still correlates the
  /// request. A missing correlation means the observation was abandoned, for
  /// example by a timeout, and its client has to be destroyed.
  void release_inactive(const std::function<bool(const std::string &)> & still_tracked)
  {
    std::vector<std::string> finished;
    for (const auto & item : busy_) {
      if (item.second->finished() || !still_tracked(item.first)) {
        finished.push_back(item.first);
      }
    }
    for (const auto & request_id : finished) {
      release(request_id);
    }
    // Deferred dispatches of retired requests are dropped as well. The
    // erasure happens through the iterator: erasing the element the range
    // loop currently visits would invalidate that iterator and the loop
    // would continue on a dangling one.
    for (auto it = deferred_.begin(); it != deferred_.end(); ) {
      if (!still_tracked(*it)) {
        it = deferred_.erase(it);
      } else {
        ++it;
      }
    }
  }

  /// Drop every observation; used when the gateway shuts down.
  void shutdown()
  {
    for (auto & item : busy_) {
      item.second->destroy();
    }
    busy_.clear();
    deferred_.clear();
    for (auto & observer : idle_) {
      observer->destroy();
    }
    idle_.clear();
  }

  /// Set the maximum number of simultaneously observed goals.
  ///
  /// Must be called before the first acquisition; the capacity is read from a
  /// ROS parameter, which is only available once the node is constructed.
  void set_capacity(std::size_t capacity) {capacity_ = std::max<std::size_t>(1, capacity);}

  std::size_t capacity() const {return capacity_;}
  std::size_t in_flight() const {return busy_.size();}
  /// Number of observations that wait for their action server.
  std::size_t deferred_count() const {return deferred_.size();}
  std::size_t idle_observers() const {return idle_.size();}
  /// Number of live action clients (in flight plus recycled).
  std::size_t live_clients() const {return busy_.size() + idle_.size();}
  const std::string & command_name() const {return command_name_;}

private:
  rclcpp::Node * node_;
  std::string action_name_;
  std::string command_name_;
  ActionSink * sink_;
  std::size_t capacity_;
  std::map<std::string, std::shared_ptr<Observer>> busy_;
  std::vector<std::shared_ptr<Observer>> idle_;
  /// Observations whose goal waits for their action server to be discovered.
  std::set<std::string> deferred_;
};

}  // namespace chaos_mqtt
