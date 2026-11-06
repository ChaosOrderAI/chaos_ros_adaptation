// Thin Python binding for the native C++ SDK.
//
// This module contains no business logic: every call forwards to the native
// chaos_sdk::Robot implementation. Responses are exposed as read-only
// attribute/dict views so the binding needs no ROS message marshalling.
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <geometry_msgs/msg/pose_stamped.hpp>

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "chaos_sdk/robot.hpp"

namespace py = pybind11;

namespace
{

using chaos_sdk::Robot;

/// Read-only view of a native response.
///
/// Both attribute access (``r.success``) and item access (``r["success"]``)
/// work, which keeps the legacy Python call sites working without
/// re-implementing anything in Python.
///
/// The view is a real Python object with its own type: a plain dictionary
/// would silently lose the attribute access, and a subclass of ``dict`` would
/// be a plain dictionary at runtime, because the C++ constructor of a native
/// dictionary creates a dictionary and not an instance of the subclass.
class Message
{
public:
  using Values = std::map<std::string, py::object>;

  explicit Message(Values values)
  : values_(std::move(values)) {}

  py::object __getattr__(const std::string & name) const
  {
    const auto it = values_.find(name);
    if (it == values_.end()) {
      throw py::attribute_error(name);
    }
    return it->second;
  }

  py::object __getitem__(const std::string & name) const
  {
    const auto it = values_.find(name);
    if (it == values_.end()) {
      throw py::key_error(name);
    }
    return it->second;
  }

  bool __contains__(const std::string & name) const {return values_.count(name) > 0;}

  py::list keys() const
  {
    py::list out;
    for (const auto & item : values_) {
      out.append(py::str(item.first));
    }
    return out;
  }

  std::size_t __len__() const {return values_.size();}

  std::string __repr__() const
  {
    std::string out = "Message(";
    bool first = true;
    for (const auto & item : values_) {
      if (!first) {
        out += ", ";
      }
      first = false;
      out += item.first + "=" + py::str(item.second).cast<std::string>();
    }
    return out + ")";
  }

private:
  Values values_;
};

/// Build a message view from its fields.
py::object message_of(Message::Values values)
{
  return py::cast(Message(std::move(values)));
}

bool has_member(const py::object & root, const std::string & key)
{
  if (py::isinstance<py::dict>(root)) {
    return py::reinterpret_borrow<py::dict>(root).contains(key.c_str());
  }
  return py::hasattr(root, key.c_str());
}

py::object member_at(const py::object & root, const std::vector<std::string> & path)
{
  py::object current = root;
  for (const auto & key : path) {
    if (current.is_none()) {
      return py::none();
    }
    if (py::isinstance<py::dict>(current)) {
      py::dict view = py::reinterpret_borrow<py::dict>(current);
      if (!view.contains(key.c_str())) {
        return py::none();
      }
      current = view[key.c_str()];
    } else {
      if (!py::hasattr(current, key.c_str())) {
        return py::none();
      }
      current = py::getattr(current, key.c_str());
    }
  }
  return current;
}

double number_at(const py::object & root, const std::vector<std::string> & path, double fallback)
{
  const auto value = member_at(root, path);
  if (value.is_none()) {
    return fallback;
  }
  return value.cast<double>();
}

std::string string_at(
  const py::object & root, const std::vector<std::string> & path, const std::string & fallback)
{
  const auto value = member_at(root, path);
  if (value.is_none()) {
    return fallback;
  }
  return value.cast<std::string>();
}

/// Append a leaf to a lookup path.
///
/// The complete path is built before the lookup. Indexing a fixed size path
/// instead would read out of range for the flat dictionary layout, where the
/// path has one component less than the nested one.
std::vector<std::string> leaf(const std::vector<std::string> & prefix, const std::string & name)
{
  std::vector<std::string> path = prefix;
  path.push_back(name);
  return path;
}

/// Convert a Python target into a ROS pose.
///
/// Accepts a plain dict (``{"frame_id": ..., "position": {...},
/// "orientation": {...}}``) and any object with the rclpy message layout
/// (``header.frame_id`` / ``pose.position.x``), so an rclpy PoseStamped can be
/// handed over without the binding depending on rclpy.
geometry_msgs::msg::PoseStamped pose_from_python(const py::object & target)
{
  geometry_msgs::msg::PoseStamped pose;
  if (target.is_none()) {
    throw std::invalid_argument("navigate requires a target pose");
  }
  // The path components are separate: a dotted key would be looked up
  // literally and the position would silently stay at the default.
  // A ROS message carries a "pose" member, the flat dictionary does not.
  const bool nested = has_member(target, "pose");
  const std::vector<std::string> position = nested ?
    std::vector<std::string>{"pose", "position"} : std::vector<std::string>{"position"};
  const std::vector<std::string> orientation = nested ?
    std::vector<std::string>{"pose", "orientation"} : std::vector<std::string>{"orientation"};

  pose.header.frame_id = string_at(target, {"frame_id"}, "");
  if (pose.header.frame_id.empty()) {
    pose.header.frame_id = string_at(target, {"header", "frame_id"}, "");
  }
  pose.pose.position.x = number_at(target, leaf(position, "x"), 0.0);
  pose.pose.position.y = number_at(target, leaf(position, "y"), 0.0);
  pose.pose.position.z = number_at(target, leaf(position, "z"), 0.0);
  pose.pose.orientation.x = number_at(target, leaf(orientation, "x"), 0.0);
  pose.pose.orientation.y = number_at(target, leaf(orientation, "y"), 0.0);
  pose.pose.orientation.z = number_at(target, leaf(orientation, "z"), 0.0);
  pose.pose.orientation.w = number_at(target, leaf(orientation, "w"), 1.0);
  return pose;
}

py::object feedback_view(const chaos_interfaces::action::NavigatePose::Feedback & feedback);
py::object feedback_view(const chaos_interfaces::action::Flight::Feedback & feedback);

/// One stamped pose as the nested view the rest of the binding uses.
py::object pose_view(const geometry_msgs::msg::PoseStamped & pose)
{
  auto position = message_of({
    {"x", py::cast(pose.pose.position.x)},
    {"y", py::cast(pose.pose.position.y)},
    {"z", py::cast(pose.pose.position.z)},
  });
  auto orientation = message_of({
    {"x", py::cast(pose.pose.orientation.x)},
    {"y", py::cast(pose.pose.orientation.y)},
    {"z", py::cast(pose.pose.orientation.z)},
    {"w", py::cast(pose.pose.orientation.w)},
  });
  return message_of({
    {"header", message_of({{"frame_id", py::cast(pose.header.frame_id)}})},
    {"pose", message_of({{"position", position}, {"orientation", orientation}})},
  });
}

/// Wrap a Python feedback callable so it can run on the native feedback pool.
///
/// The pool calls it without the GIL and outside the executor of the client,
/// so the wrapper acquires the GIL itself and swallows anything the callable
/// raises: an exception must never escape into the worker.
template<typename ActionT>
std::function<void(typename ActionT::Feedback)> feedback_of(const py::object & callback)
{
  if (callback.is_none()) {
    return nullptr;
  }
  if (!py::isinstance<py::function>(callback)) {
    throw std::invalid_argument("feedback_callback must be callable or None");
  }
  py::function function = py::reinterpret_borrow<py::function>(callback);
  return [function](const typename ActionT::Feedback & feedback) {
    py::gil_scoped_acquire acquire;
    try {
      function(feedback_view(feedback));
    } catch (const py::error_already_set &) {
      // The sample is lost; the result of the goal stays untouched.
    }
  };
}

py::object feedback_view(const chaos_interfaces::action::NavigatePose::Feedback & feedback)
{
  return message_of({
    {"current_pose", pose_view(feedback.current_pose)},
    {"progress", py::cast(feedback.progress)},
  });
}

py::object feedback_view(const chaos_interfaces::action::Flight::Feedback & feedback)
{
  return message_of({
    {"altitude_m", py::cast(feedback.altitude_m)},
    {"progress", py::cast(feedback.progress)},
  });
}

/// Future wrapper around a native std::shared_future.
///
/// Waiting happens with the GIL released so that feedback or service
/// callbacks can make progress while Python blocks on the result.
class PyFuture
{
public:
  PyFuture(
    std::function<void ()> wait,
    std::function<py::object ()> convert,
    std::function<bool ()> done)
  : wait_(std::move(wait)), convert_(std::move(convert)), done_(std::move(done)) {}

  bool done() const {return done_();}

  /// Blocks until the native request completed and returns its value.
  py::object result() const
  {
    {
      py::gil_scoped_release release;
      wait_();
    }
    return convert_();
  }

private:
  std::function<void ()> wait_;
  std::function<py::object ()> convert_;
  std::function<bool ()> done_;
};

template<typename T, typename Converter>
PyFuture make_future(std::shared_future<T> future, Converter convert)
{
  std::function<py::object (const T &)> getter = convert;
  auto shared = std::make_shared<std::shared_future<T>>(std::move(future));
  return PyFuture(
    [shared]() {shared->wait();},
    [shared, getter]() {return getter(shared->get());},
    [shared]() {return shared->wait_for(std::chrono::seconds(0)) == std::future_status::ready;});
}

/// Task wrapper around the native typed action task.
class PyTask
{
public:
  explicit PyTask(std::shared_ptr<Robot::NavigationTask> task)
  : navigation_(std::move(task)) {}

  explicit PyTask(std::shared_ptr<Robot::FlightTask> task)
  : flight_(std::move(task)) {}

  bool accepted() const
  {
    if (navigation_) {
      return navigation_->accepted();
    }
    return flight_ && flight_->accepted();
  }

  std::string goal_id() const
  {
    if (navigation_) {
      return navigation_->goal_id();
    }
    return flight_ ? flight_->goal_id() : std::string();
  }

  std::string state() const
  {
    if (navigation_) {
      return chaos_sdk::task_state_name(navigation_->state());
    }
    return flight_ ? chaos_sdk::task_state_name(flight_->state()) : "unknown";
  }

  /// Terminal result of the action.
  ///
  /// The wait happens with the GIL released: a feedback callback of the same
  /// client must be able to run while Python waits for the result.
  py::object result() const
  {
    if (navigation_) {
      return convert(navigation_->result());
    }
    if (!flight_) {
      return py::none();
    }
    return convert(flight_->result());
  }

  /// Compatibility alias of result() for call sites that used the rclpy
  /// naming.
  ///
  /// It returns a real future: the caller may poll ``done()`` instead of
  /// blocking. The rclpy client is not exposed.
  py::object get_result_async() const
  {
    if (navigation_) {
      return py::cast(make_future(
        navigation_->result(),
        [](const auto & wrapped) {return convert_result(wrapped);}));
    }
    if (!flight_) {
      return py::none();
    }
    return py::cast(make_future(
      flight_->result(),
      [](const auto & wrapped) {return convert_result(wrapped);}));
  }

  /// Request cancellation through the owning action client.
  py::object cancel() const
  {
    if (navigation_) {
      return cancel_outcome(navigation_->cancel());
    }
    if (!flight_) {
      return py::none();
    }
    return cancel_outcome(flight_->cancel());
  }
  /// Compatibility alias of cancel() for call sites that used the rclpy
  /// naming.
  ///
  /// It returns a real future with the outcome of the cancel request. The
  /// rclpy client is not exposed.
  py::object cancel_goal_async() const
  {
    if (navigation_) {
      return py::cast(make_future(
        navigation_->cancel(),
        [](const chaos_sdk::CancelOutcome & outcome) {return cancel_outcome_value(outcome);}));
    }
    if (!flight_) {
      return py::none();
    }
    return py::cast(make_future(
      flight_->cancel(),
      [](const chaos_sdk::CancelOutcome & outcome) {return cancel_outcome_value(outcome);}));
  }

private:
  /// Convert an unwrapped terminal result.
  template<typename WrappedResult>
  static py::object convert_result(const WrappedResult & wrapped)
  {
    // The goal status decides, not only the payload: a canceled or aborted
    // goal with a successful payload is not a success.
    const auto state = chaos_sdk::classify_result(
      wrapped.code,
      wrapped.result != nullptr,
      wrapped.result ? wrapped.result->success : false);
    py::dict out;
    out["ok"] = state == chaos_sdk::TaskState::SUCCEEDED;
    out["code"] = wrapped.result ? wrapped.result->code : std::string("UNKNOWN");
    out["message"] = wrapped.result ? wrapped.result->message : std::string();
    out["ros_goal_status"] = static_cast<int>(wrapped.code);
    out["state"] = chaos_sdk::task_state_name(state);
    return out;
  }

  template<typename WrappedResult>
  static py::object convert(const std::shared_future<WrappedResult> & future)
  {
    WrappedResult wrapped;
    {
      // Waiting without the GIL keeps feedback callbacks alive.
      py::gil_scoped_release release;
      future.wait();
      wrapped = future.get();
    }
    return convert_result(wrapped);
  }

  static py::object cancel_outcome(const std::shared_future<chaos_sdk::CancelOutcome> & future)
  {
    chaos_sdk::CancelOutcome outcome;
    {
      py::gil_scoped_release release;
      future.wait();
      outcome = future.get();
    }
    return cancel_outcome_value(outcome);
  }

  static py::object cancel_outcome_value(const chaos_sdk::CancelOutcome & outcome)
  {
    py::dict out;
    out["accepted"] = outcome.accepted;
    out["return_code"] = outcome.return_code;
    out["goals_canceling"] = outcome.goals_canceling;
    return out;
  }

  std::shared_ptr<Robot::NavigationTask> navigation_;
  std::shared_ptr<Robot::FlightTask> flight_;
};

py::object describe_dict(const chaos_interfaces::srv::GetDescription::Response & response)
{
  return message_of({
    {"asset_id", py::cast(response.asset_id)},
    {"model", py::cast(response.model)},
    {"model_version", py::cast(response.model_version)},
    {"capabilities", py::cast(response.capabilities)},
    {"base_frame", py::cast(response.base_frame)},
    {"odom_frame", py::cast(response.odom_frame)},
    {"map_frame", py::cast(response.map_frame)},
    {"linear_limit_mps", py::cast(response.linear_limit_mps)},
    {"angular_limit_rps", py::cast(response.angular_limit_rps)},
    {"command_timeout_s", py::cast(response.command_timeout_s)},
    {"lease_max_s", py::cast(response.lease_max_s)},
  });
}

py::object lease_dict(const chaos_interfaces::srv::ControlLease::Response & response)
{
  return message_of({
    {"success", py::cast(response.success)},
    {"code", py::cast(response.code)},
    {"message", py::cast(response.message)},
    {"token", py::cast(response.token)},
    {"remaining_s", py::cast(response.remaining_s)},
  });
}

py::object stop_dict(const chaos_interfaces::srv::Stop::Response & response)
{
  return message_of({
    {"success", py::cast(response.success)},
    {"code", py::cast(response.code)},
    {"message", py::cast(response.message)},
  });
}

py::object state_dict(const chaos_interfaces::msg::RobotState & state)
{
  // The nested views are messages as well, so a nested field keeps the same
  // access style as a top level field.
  auto position = message_of({
    {"x", py::cast(state.pose.pose.position.x)},
    {"y", py::cast(state.pose.pose.position.y)},
    {"z", py::cast(state.pose.pose.position.z)},
  });
  auto orientation = message_of({
    {"x", py::cast(state.pose.pose.orientation.x)},
    {"y", py::cast(state.pose.pose.orientation.y)},
    {"z", py::cast(state.pose.pose.orientation.z)},
    {"w", py::cast(state.pose.pose.orientation.w)},
  });
  auto pose = message_of({
    {"position", position},
    {"orientation", orientation},
  });
  auto pose_stamped = message_of({
    {"header", message_of({})},
    {"pose", pose},
  });
  auto twist = message_of({
    {"linear", message_of({
        {"x", py::cast(state.twist.twist.linear.x)},
        {"y", py::cast(state.twist.twist.linear.y)},
        {"z", py::cast(state.twist.twist.linear.z)},
      })},
    {"angular", message_of({
        {"x", py::cast(state.twist.twist.angular.x)},
        {"y", py::cast(state.twist.twist.angular.y)},
        {"z", py::cast(state.twist.twist.angular.z)},
      })},
  });
  auto stamp = message_of({
    {"sec", py::cast(state.stamp.sec)},
    {"nanosec", py::cast(state.stamp.nanosec)},
  });
  return message_of({
    {"stamp", stamp},
    {"asset_id", py::cast(state.asset_id)},
    {"model", py::cast(state.model)},
    {"operational_state", py::cast(state.operational_state)},
    {"connected", py::cast(state.connected)},
    {"ready", py::cast(state.ready)},
    {"pose_valid", py::cast(state.pose_valid)},
    {"pose", pose_stamped},
    {"twist", message_of({{"twist", twist}})},
    {"battery_percentage", py::cast(state.battery_percentage)},
    {"detail", py::cast(state.detail)},
  });
}

using RobotFactory = std::shared_ptr<Robot> (*)(
  const std::string &, const std::optional<std::string> &, const std::optional<std::string> &,
  double, double, double);
RobotFactory create_robot =
  [](const std::string & asset_id,
  const std::optional<std::string> & namespace_,
  const std::optional<std::string> & base_frame,
  double command_timeout_s,
  double command_ttl_s,
  double response_timeout_s) {
    return Robot::create_standalone(
      asset_id, namespace_, base_frame, command_timeout_s, command_ttl_s, response_timeout_s);
  };

/// Legacy signature: the rclpy node cannot be shared with the native client,
/// so the native client owns its own node and context.
using LegacyRobotFactory = std::shared_ptr<Robot> (*)(
  py::object, const std::string &, const std::optional<std::string> &,
  const std::optional<std::string> &, double, double, double);
LegacyRobotFactory create_legacy_robot =
  [](py::object node, const std::string & asset_id,
  const std::optional<std::string> & namespace_,
  const std::optional<std::string> & base_frame,
  double command_timeout_s,
  double command_ttl_s,
  double response_timeout_s) {
    (void)node;
    return Robot::create_standalone(
      asset_id, namespace_, base_frame, command_timeout_s, command_ttl_s, response_timeout_s);
  };

}  // namespace

PYBIND11_MODULE(_chaos_sdk, module)
{
  module.doc() = "Thin binding for the native chaos_sdk C++ client.";

  py::class_<Message>(module, "Message")
  .def("__getattr__", &Message::__getattr__)
  .def("__getitem__", &Message::__getitem__)
  .def("__contains__", &Message::__contains__)
  .def("keys", &Message::keys)
  .def("__len__", &Message::__len__)
  .def("__repr__", &Message::__repr__);

  py::class_<PyFuture>(module, "Future")
  .def("done", &PyFuture::done)
  .def("result", &PyFuture::result,
    "Blocks until the native request completed and returns its value.");

  py::class_<PyTask>(module, "Task")
  .def_property_readonly("accepted", &PyTask::accepted)
  .def_property_readonly("goal_id", &PyTask::goal_id)
  .def_property_readonly("state", &PyTask::state)
  .def("result", &PyTask::result)
  .def("cancel", &PyTask::cancel)
  .def("get_result_async", &PyTask::get_result_async)
  .def("cancel_goal_async", &PyTask::cancel_goal_async);

  py::class_<Robot, std::shared_ptr<Robot>>(module, "Robot")
  .def(
    py::init(create_robot),
    py::arg("asset_id"),
    py::arg("namespace") = std::nullopt,
    py::arg("base_frame") = std::nullopt,
    py::arg("command_timeout_s") = 0.5,
    py::arg("command_ttl_s") = 5.0,
    py::arg("response_timeout_s") = 5.0,
    "Create a client that owns its ROS context, node and executor thread.")
  .def(
    py::init(create_legacy_robot),
    py::arg("node"),
    py::arg("asset_id"),
    py::arg("namespace") = std::nullopt,
    py::arg("base_frame") = std::nullopt,
    py::arg("command_timeout_s") = 0.5,
    py::arg("command_ttl_s") = 5.0,
    py::arg("response_timeout_s") = 5.0,
    "Legacy signature; the passed node is ignored because the native client "
    "owns its own context.")
  .def(
    "describe", [](Robot & self) {
      return make_future(self.describe(), [](const auto & response) {return describe_dict(response);});
    })
  .def(
    "acquire", [](Robot & self, const std::string & owner, double duration_s,
      const std::optional<double> & ttl_s) {
      return make_future(
        self.acquire(owner, duration_s, ttl_s),
        [](const auto & response) {return lease_dict(response);});
    },
    py::arg("owner"), py::arg("duration_s") = 5.0, py::arg("ttl_s") = std::nullopt)
  .def(
    "renew", [](Robot & self, double duration_s, const std::optional<double> & ttl_s) {
      return make_future(
        self.renew(duration_s, ttl_s),
        [](const auto & response) {return lease_dict(response);});
    },
    py::arg("duration_s") = 5.0, py::arg("ttl_s") = std::nullopt)
  .def(
    "release", [](Robot & self, const std::optional<double> & ttl_s) {
      return make_future(
        self.release(ttl_s),
        [](const auto & response) {return lease_dict(response);});
    },
    py::arg("ttl_s") = std::nullopt)
  .def(
    "stop", [](Robot & self, const std::optional<std::string> & request_id,
      const std::optional<double> & ttl_s) {
      return make_future(
        self.stop(request_id, ttl_s),
        [](const auto & response) {return stop_dict(response);});
    },
    py::arg("request_id") = std::nullopt, py::arg("ttl_s") = std::nullopt)
  .def(
    "velocity", [](Robot & self, double x, double y, double z, double yaw,
      const std::optional<double> & valid_for_s, const std::optional<std::string> & request_id) {
      return self.velocity(x, y, z, yaw, valid_for_s, request_id);
    },
    py::arg("x") = 0.0, py::arg("y") = 0.0, py::arg("z") = 0.0, py::arg("yaw") = 0.0,
    py::arg("valid_for_s") = std::nullopt, py::arg("request_id") = std::nullopt)
  .def(
    "navigate", [](Robot & self, const py::object & target, const py::object & feedback_callback,
      const std::optional<double> & ttl_s) {
      const auto pose = pose_from_python(target);
      return make_future(
        self.navigate(pose, feedback_of<chaos_interfaces::action::NavigatePose>(feedback_callback),
          ttl_s),
        [](const std::shared_ptr<Robot::NavigationTask> & task) {
          return py::cast(PyTask(task));
        });
    },
    py::arg("target"), py::arg("feedback_callback") = py::none(), py::kw_only(),
    py::arg("ttl_s") = std::nullopt,
    "Send a navigation goal. The target may be a dict or an object with the "
    "rclpy message layout (header.frame_id / pose.position). feedback_callback "
    "receives current_pose and progress and runs off the executor thread.")
  .def(
    "takeoff", [](Robot & self, double altitude_m, const py::object & feedback_callback,
      const std::optional<double> & ttl_s) {
      return make_future(
        self.takeoff(altitude_m,
          feedback_of<chaos_interfaces::action::Flight>(feedback_callback), ttl_s),
        [](const std::shared_ptr<Robot::FlightTask> & task) {
          return py::cast(PyTask(task));
        });
    },
    py::arg("altitude_m"), py::arg("feedback_callback") = py::none(), py::kw_only(),
    py::arg("ttl_s") = std::nullopt)
  .def(
    "land", [](Robot & self, const py::object & feedback_callback,
      const std::optional<double> & ttl_s) {
      return make_future(
        self.land(feedback_of<chaos_interfaces::action::Flight>(feedback_callback), ttl_s),
        [](const std::shared_ptr<Robot::FlightTask> & task) {
          return py::cast(PyTask(task));
        });
    },
    py::arg("feedback_callback") = py::none(), py::kw_only(),
    py::arg("ttl_s") = std::nullopt)
  .def("state", [](Robot & self) -> py::object {
      const auto state = self.state();
      if (!state) {
        return py::none();
      }
      return state_dict(*state);
    })
  .def("wait_ready", [](Robot & self, double timeout_s) {
      return self.wait_ready(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(timeout_s)));
    }, py::arg("timeout_s") = 5.0)
  // Read only accessors are properties: the existing call sites use them as
  // attributes (``sdk.lease_token``).
  .def_property_readonly("lease_token", &Robot::lease_token)
  .def_property_readonly("owner", &Robot::owner)
  .def_property_readonly("base_frame", &Robot::base_frame)
  .def_property_readonly("command_timeout_s", &Robot::command_timeout_s)
  .def_property_readonly("command_ttl_s", &Robot::command_ttl_s)
  .def_property_readonly("pending_count", &Robot::pending_count)
  .def_property_readonly("feedback_dropped_count", &Robot::feedback_dropped_count)
  .def_property_readonly("response_timeout_s", &Robot::response_timeout_s)
  .def_property_readonly("action_timeout_s", &Robot::action_timeout_s)
  .def_property_readonly("max_pending", &Robot::max_pending)
  .def_property_readonly("action_client_count", &Robot::action_client_count)
  .def_property_readonly("action_slot_count", &Robot::action_slot_count)
  .def("set_response_timeout", &Robot::set_response_timeout)
  .def("set_action_timeout", &Robot::set_action_timeout)
  .def("set_max_pending", &Robot::set_max_pending)
  .def("close", [](Robot & self) {
      // close() joins the executor thread. A feedback callback of this client
      // may be waiting for the GIL, so the join must not hold it.
      py::gil_scoped_release release;
      self.close();
    });
}
