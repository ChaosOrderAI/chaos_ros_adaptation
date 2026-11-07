#include "chaos_bt/nodes.hpp"

#include <stdexcept>
#include <utility>

namespace chaos_bt
{

void register_nodes(
  BT::BehaviorTreeFactory & factory, std::shared_ptr<chaos_sdk::Robot> robot)
{
  if (!robot) {
    throw std::invalid_argument("register_nodes requires a robot");
  }
  factory.registerBuilder<Navigate>(
    "Navigate",
    [robot](const std::string & name, const BT::NodeConfig & config) {
      return std::make_unique<Navigate>(name, config, robot);
    });
  factory.registerBuilder<Takeoff>(
    "Takeoff",
    [robot](const std::string & name, const BT::NodeConfig & config) {
      return std::make_unique<Takeoff>(name, config, robot);
    });
  factory.registerBuilder<Land>(
    "Land",
    [robot](const std::string & name, const BT::NodeConfig & config) {
      return std::make_unique<Land>(name, config, robot);
    });
}

}  // namespace chaos_bt
