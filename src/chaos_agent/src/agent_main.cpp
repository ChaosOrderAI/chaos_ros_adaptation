// chaos_agent_node: drives AgentLoop for one asset against a local llama-server.
//
// The node holds the ROS executor, subscribes to the asset's RobotState, and
// runs the decide→execute→observe loop on a worker thread. The LLM endpoint,
// the task text and the vocabulary JSON are parameters; the vocabulary is the
// same document the skill server was started with, so the planner and the
// executor always agree on the token set.

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>

#include "chaos_agent/agent_loop.hpp"

namespace
{

class AgentNode : public rclcpp::Node
{
public:
  AgentNode()
  : Node("chaos_agent")
  {
    declare_parameter("asset_id", std::string{});
    declare_parameter("skill_ns", std::string{});
    declare_parameter("task", std::string{});
    declare_parameter("vocabulary_json", std::string{});
    declare_parameter("llama_endpoint", std::string{"http://127.0.0.1:8000"});
    declare_parameter("system_prompt", std::string{});
    declare_parameter("iteration_hz", 0.5);
  }

  /// Called once after the node is inside a shared_ptr. shared_from_this is
  /// not usable in the constructor.
  void init()
  {
    const auto asset_id = get_parameter("asset_id").as_string();
    const auto skill_ns = get_parameter("skill_ns").as_string();
    const auto task = get_parameter("task").as_string();
    const auto vocab_json = get_parameter("vocabulary_json").as_string();
    const auto endpoint = get_parameter("llama_endpoint").as_string();
    const auto system_prompt = get_parameter("system_prompt").as_string();
    const auto iteration_hz = get_parameter("iteration_hz").as_double();

    if (asset_id.empty() || vocab_json.empty()) {
      throw std::invalid_argument("asset_id and vocabulary_json are required");
    }
    const auto planner_document = nlohmann::json::parse(vocab_json);

    loop_ = std::make_shared<chaos_agent::AgentLoop>(
      shared_from_this(), asset_id, skill_ns, planner_document, system_prompt, endpoint);
    loop_->set_task(task);

    const auto period = std::chrono::duration<double>(1.0 / std::max(0.05, iteration_hz));
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      [this] {tick();});
  }

private:
  void tick()
  {
    if (running_->exchange(true)) {
      return;  // previous iteration still in flight
    }
    // The worker must not touch the node: the node may be destroyed while
    // the iteration is still in flight. Everything it needs is held by a
    // shared pointer.
    const auto loop = loop_;
    const auto timer = timer_;
    const auto running = running_;
    std::thread([loop, timer, running] {
        const auto cont = loop->run_once();
        if (!cont) {
          timer->cancel();
        }
        running->store(false);
      }).detach();
  }

  std::shared_ptr<chaos_agent::AgentLoop> loop_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::shared_ptr<std::atomic<bool>> running_ = std::make_shared<std::atomic<bool>>(false);
};

}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<AgentNode>();
  node->init();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
