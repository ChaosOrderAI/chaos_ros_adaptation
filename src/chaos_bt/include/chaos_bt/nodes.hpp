#pragma once

#include <future>
#include <memory>
#include <string>

#include <behaviortree_cpp/action_node.h>
#include <behaviortree_cpp/bt_factory.h>

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
/// result.
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
    if (!result) {
      config().blackboard->set(key, value);
    }
  }

  template<typename Node>
  static void write_common(Node & node, const std::string & state, const std::string & code,
    const std::string & message);

  template<typename Node, typename Session>
  static void publish_feedback(Node & node, const std::shared_ptr<Session> & session);

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
