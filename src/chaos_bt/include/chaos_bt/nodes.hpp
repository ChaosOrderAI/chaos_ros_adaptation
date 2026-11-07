#pragma once

#include <future>
#include <memory>
#include <string>

#include <behaviortree_cpp/action_node.h>
#include <behaviortree_cpp/bt_factory.h>

#include <geometry_msgs/msg/pose_stamped.hpp>

#include "chaos_sdk/robot.hpp"

namespace chaos_bt
{

/// Register the Navigate, Takeoff and Land nodes with one robot.
///
/// The factory keeps the robot alive for as long as the registered builders
/// live. The nodes send goals through that robot; they never acquire a lease,
/// spin an executor or stop the whole asset. The application keeps the robot's
/// executor running on a thread that does not tick the tree.
void register_nodes(
  BT::BehaviorTreeFactory & factory, std::shared_ptr<chaos_sdk::Robot> robot);

/// One action goal observed through the SDK, without blocking the tree.
///
/// BehaviorTree.CPP only remaps output ports that the XML names, and setOutput
/// then drops the value without an error. The node writes the blackboard entry
/// of the same name in that case, so a tree that omits the port still keeps the
/// result. A remap that setOutput rejects for any other reason is a broken tree
/// and is reported as an error instead of being published somewhere else.
class ActionNode : public BT::StatefulActionNode
{
public:
  using BT::StatefulActionNode::StatefulActionNode;

private:
  template<typename T>
  void write_port(const char * key, const T & value)
  {
    if (!config().blackboard) {
      throw BT::RuntimeError(std::string(key) + " has no blackboard");
    }
    const auto result = setOutput(key, value);
    if (result) {
      return;
    }
    if (config().output_ports.find(key) != config().output_ports.end()) {
      // The XML named this port, so the tree expects the value there. Writing
      // it to the blackboard instead would publish it to the wrong place.
      throw BT::RuntimeError(std::string("cannot publish ") + key + ": " + result.error());
    }
    config().blackboard->set(key, value);
  }

  template<typename Node>
  static void write_common(Node & node, const std::string & state, const std::string & code,
    const std::string & message);

  template<typename Node, typename Session>
  static void publish_feedback(Node & node, const std::shared_ptr<Session> & session);

  /// Report how the server answered a cancellation request.
  ///
  /// The request itself is sent without waiting for the answer. When the
  /// answer arrives, the next tick reports it: the server accepted it, refused
  /// it, or the request never reached it.
  template<typename Node, typename Session>
  static void report_cancel(Node & node, const std::shared_ptr<Session> & session);

  /// Reset the outputs that describe the goal that is running.
  ///
  /// A new goal starts without a result: the id is empty and the progress is
  /// zero until the first sample arrives. The values of the previous goal are
  /// never mixed into it.
  template<typename Node>
  static void clear_outputs(Node & node);

  template<typename Node>
  static BT::NodeStatus failure_of(Node & node, const std::exception & error);

  template<typename Node, typename Session, typename Task>
  static BT::NodeStatus poll_action(
    Node & node, const std::shared_ptr<Session> & session,
    const std::shared_future<std::shared_ptr<Task>> & acceptance);

  template<typename Node, typename Session, typename Task>
  static void request_cancel(
    Node & node, const std::shared_ptr<Session> & session,
    const std::shared_future<std::shared_ptr<Task>> & acceptance);

  /// Cancel the goal without letting an exception escape the tree.
  ///
  /// The tree may be destroyed while this runs, and an exception would then
  /// escape the tree destructor and abort the process.
  template<typename Node, typename Session, typename Task>
  static void safe_halt(
    Node & node, const std::shared_ptr<Session> & session,
    const std::shared_future<std::shared_ptr<Task>> & acceptance);

  /// Whether the goal reached a terminal state.
  ///
  /// A goal that was never sent, was rejected or whose result arrived is
  /// finished. A goal that the server has not acknowledged yet, or that is
  /// still running, is not.
  template<typename Task>
  static bool goal_finished(const std::shared_future<std::shared_ptr<Task>> & acceptance);

  friend class Navigate;
  friend class Takeoff;
  friend class Land;
};

/// One navigation goal, observed without blocking the tree.
///
/// Inputs: frame_id, x, y, z, yaw and an optional ttl_s. ttl_s bounds when the
/// goal may start, not when it must finish. Outputs: goal_id, task_state,
/// progress, code, message and current_pose.
class Navigate final : public ActionNode
{
public:
  Navigate(
    const std::string & name, const BT::NodeConfig & config,
    std::shared_ptr<chaos_sdk::Robot> robot);
  ~Navigate() override;

  static BT::PortsList providedPorts();

  BT::NodeStatus onStart() override;
  BT::NodeStatus onRunning() override;
  void onHalted() override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// One takeoff goal. Inputs: altitude_m and an optional ttl_s. Outputs:
/// goal_id, task_state, progress, code, message and current_altitude_m.
class Takeoff final : public ActionNode
{
public:
  Takeoff(
    const std::string & name, const BT::NodeConfig & config,
    std::shared_ptr<chaos_sdk::Robot> robot);
  ~Takeoff() override;

  static BT::PortsList providedPorts();

  BT::NodeStatus onStart() override;
  BT::NodeStatus onRunning() override;
  void onHalted() override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// One landing goal. Input: an optional ttl_s. Outputs: goal_id, task_state,
/// progress, code, message and current_altitude_m.
class Land final : public ActionNode
{
public:
  Land(
    const std::string & name, const BT::NodeConfig & config,
    std::shared_ptr<chaos_sdk::Robot> robot);
  ~Land() override;

  static BT::PortsList providedPorts();

  BT::NodeStatus onStart() override;
  BT::NodeStatus onRunning() override;
  void onHalted() override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace chaos_bt
