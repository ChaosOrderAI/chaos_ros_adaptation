#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include "chaos_sdk/sdk_core.hpp"

namespace chaos_sdk
{

/// Bounded pool of action clients for one action type.
///
/// rclcpp_action does not expose a way to drop a single unanswered goal
/// request or goal handle. Instead of resetting a shared client (which would
/// destroy the observation of every other in-flight goal), every action type
/// owns a bounded number of client slots:
///
/// * a slot is leased for the duration of one goal,
/// * a slot is reused after a goal finished,
/// * a slot is *retired* when its observation timed out; the client is then
///   released (``stop_callbacks`` + shared pointer reset) and a fresh client
///   is created the next time the slot is used.
///
/// Every lease carries a generation counter. A late callback of an old lease
/// can only release its own lease: the slot refuses a release with a stale
/// generation, so a timed out goal can never retire the client of a newer
/// goal that reused the same slot.
template<typename ActionT>
class ActionSlotPool
{
public:
  using ClientT = rclcpp_action::Client<ActionT>;
  using SharedPtr = typename ClientT::SharedPtr;

  /// Lease of one slot. The index and the generation identify the lease
  /// without keeping the client alive.
  struct Lease
  {
    std::size_t index = 0;
    std::uint64_t generation = 0;
    SharedPtr client;
  };

  explicit ActionSlotPool(std::size_t capacity)
  : capacity_(capacity) {}

  /// Take a client slot. Throws PendingOverflowError when every slot is busy.
  Lease acquire(const rclcpp::Node::SharedPtr & node, const std::string & action_name)
  {
    node_ = node;
    // Prefer an idle slot that already serves the requested action.
    for (std::size_t index = 0; index < slots_.size(); ++index) {
      auto & slot = slots_[index];
      if (slot.busy || slot.action_name != action_name) {
        continue;
      }
      if (!slot.client) {
        slot.client = rclcpp_action::create_client<ActionT>(node, action_name);
      }
      slot.busy = true;
      slot.generation++;
      return Lease{index, slot.generation, slot.client};
    }
    if (slots_.size() < capacity_) {
      Slot slot;
      slot.client = rclcpp_action::create_client<ActionT>(node, action_name);
      slot.action_name = action_name;
      slot.busy = true;
      slot.generation = 1;
      slots_.push_back(std::move(slot));
      return Lease{slots_.size() - 1, 1, slots_.back().client};
    }
    // At capacity: reuse an idle slot for another action name. Busy slots are
    // never touched, so the observation of other goals stays intact.
    for (std::size_t index = 0; index < slots_.size(); ++index) {
      auto & slot = slots_[index];
      if (slot.busy) {
        continue;
      }
      slot.client.reset();
      slot.action_name = action_name;
      slot.client = rclcpp_action::create_client<ActionT>(node, action_name);
      slot.busy = true;
      slot.generation++;
      return Lease{index, slot.generation, slot.client};
    }
    throw PendingOverflowError("no free action client slot for " + action_name);
  }

  /// Give the slot back. A stale generation is ignored: the slot already
  /// belongs to a newer goal.
  ///
  /// A retired slot drops its client, so the ROS resources of that slot are
  /// actually released. The client is also unregistered from the node:
  /// rclcpp_action registers every action client as a waitable of the node,
  /// and the executor would keep the client alive through that registration.
  void release(std::size_t index, std::uint64_t generation, bool retire)
  {
    if (index >= slots_.size()) {
      return;
    }
    auto & slot = slots_[index];
    if (slot.generation != generation) {
      // The slot was leased again after this lease ended.
      return;
    }
    slot.busy = false;
    if (retire) {
      if (node_ && slot.client) {
        try {
          node_->get_node_waitables_interface()->remove_waitable(slot.client, nullptr);
        } catch (const std::exception &) {
          // The registration may already be gone; the release below still
          // drops the reference of the pool.
        }
      }
      slot.client.reset();
    }
  }

  std::size_t size() const {return slots_.size();}
  std::size_t capacity() const {return capacity_;}

  /// Number of slots that currently hold a ROS client.
  std::size_t live_clients() const
  {
    std::size_t live = 0;
    for (const auto & slot : slots_) {
      if (slot.client) {
        ++live;
      }
    }
    return live;
  }

  /// Weak reference to the ROS client of one slot.
  ///
  /// The reference never keeps the client alive. It exists so that the native
  /// tests can prove that a retired slot really released its client instead
  /// of only counting the slots.
  std::weak_ptr<ClientT> weak_client(std::size_t index) const
  {
    if (index >= slots_.size()) {
      return std::weak_ptr<ClientT>();
    }
    return slots_[index].client;
  }

private:
  struct Slot
  {
    SharedPtr client;
    std::string action_name;
    bool busy = false;
    std::uint64_t generation = 0;
  };

  std::size_t capacity_;
  std::vector<Slot> slots_;
  /// The node the clients were created on. It is needed to unregister a
  /// retired client from the waitables of the node.
  rclcpp::Node::SharedPtr node_;
};

}  // namespace chaos_sdk
