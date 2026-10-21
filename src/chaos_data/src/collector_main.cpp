// Native collector entry point.
//
// Keeps the behaviour of the original Python collector:
//  * the asset configuration is read from the "asset_json" parameter;
//  * the status and diagnostics are published below the asset namespace;
//  * recording only starts when the clock gate admits the sample, and a
//    failure closes the session with a non zero exit code;
//  * the session metadata keeps the recovery semantics of the original;
//  * SIGINT requests a graceful shutdown that still finalises the writer and
//    the durable metadata, SIGTERM keeps its hard stop semantics.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_msgs/msg/key_value.hpp>
#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include "chaos_data/controller.hpp"

namespace {

std::atomic<bool> g_shutdown_requested{false};

void requestShutdown(int signal_number)
{
  // A terminal or process group SIGINT can be followed by another SIGINT.
  // Never raise an exception into a native flush or an atomic manifest
  // update: the first signal only requests the shutdown.
  g_shutdown_requested.store(true);
  std::signal(SIGINT, SIG_IGN);
}

}  // namespace

int main(int argc, char ** argv)
{
  // This process owns SIGINT for its whole lifetime: rclcpp must not
  // replace the handler, and a second signal must not interrupt the writer
  // or the durable metadata update.
  std::signal(SIGINT, requestShutdown);
  std::signal(SIGTERM, SIG_DFL);

  rclcpp::init(argc, argv, rclcpp::InitOptions(), rclcpp::SignalHandlerOptions::None);

  int exit_code = 0;
  std::unique_ptr<chaos::data::CollectionController> controller;
  try {
    auto node = std::make_shared<rclcpp::Node>("data_collector");
    if (!node->has_parameter("use_sim_time")) {
      node->declare_parameter("use_sim_time", false);
    }
    if (node->get_parameter("use_sim_time").as_bool()) {
      throw std::invalid_argument(
        "data collection currently requires the system clock");
    }
    if (!node->has_parameter("asset_json")) {
      node->declare_parameter("asset_json", std::string("{}"));
    }
    const std::string raw = node->get_parameter("asset_json").as_string();
    if (raw.size() > 1048576) {
      throw std::invalid_argument(
        "asset_json must be a JSON string of at most 1 MiB");
    }
    const nlohmann::json asset = nlohmann::json::parse(raw);
    if (!asset.at("data_collection").at("enabled").get<bool>()) {
      throw std::invalid_argument("data_collection must be explicitly enabled");
    }
    const std::string space = asset.at("namespace").get<std::string>();
    const std::string namespace_prefix =
      space.empty() || space == "/" ? std::string()
                                    : space.back() == '/' ? space.substr(0, space.size() - 1)
                                                          : space;

    auto status = node->create_publisher<std_msgs::msg::String>(
      namespace_prefix + "/data_collection/status",
      rclcpp::QoS(1).reliable().transient_local());
    auto diagnostics =
      node->create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
        namespace_prefix + "/data_collection/diagnostics", 10);

    controller = std::make_unique<chaos::data::CollectionController>(asset);
    RCLCPP_INFO(node->get_logger(), "Data collection session: %s",
                controller->session().path().c_str());

    const double poll_interval =
      asset.at("data_collection").at("time_sync").at("poll_interval_s").get<double>();
    auto timer = node->create_wall_timer(
      std::chrono::duration<double>(poll_interval), [&]() {
        const nlohmann::json snapshot = controller->poll();
        if (!rclcpp::ok()) {
          return;
        }
        std_msgs::msg::String status_message;
        status_message.data = snapshot.dump();
        status->publish(status_message);
        const nlohmann::json & clock =
          snapshot.at("clock").is_null() ? nlohmann::json() : snapshot.at("clock");
        const bool synchronized =
          clock.contains("time_sample") &&
          clock.at("time_sample").value("status", std::string()) == "synced" &&
          clock.value("reason", std::string()) == "ready";
        std::uint8_t level = 2;
        if (snapshot.value("state", std::string()) == "recording" && synchronized) {
          level = 0;
        } else if (snapshot.value("state", std::string()) != "failed") {
          level = 1;
        }
        diagnostic_msgs::msg::DiagnosticArray report;
        report.header.stamp = node->now();
        diagnostic_msgs::msg::DiagnosticStatus entry;
        entry.level = level;
        entry.name = controller->assetId() + "/data_collection";
        entry.hardware_id = controller->assetId();
        entry.message = snapshot.value("reason", std::string());
        for (const char * key : {"state", "session_id", "segment_count", "free_bytes"}) {
          diagnostic_msgs::msg::KeyValue value;
          value.key = key;
          if (snapshot.contains(key) && !snapshot.at(key).is_null()) {
            value.value = snapshot.at(key).dump();
          } else {
            value.value = "null";
          }
          entry.values.push_back(value);
        }
        report.status.push_back(entry);
        diagnostics->publish(report);
        if (controller->exitCode() != 0) {
          RCLCPP_ERROR(node->get_logger(), "%s",
                       controller->reason().c_str());
        }
      });

    while (rclcpp::ok() && !g_shutdown_requested.load() &&
           controller->exitCode() == 0) {
      rclcpp::spin_some(node);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  } catch (const std::exception & error) {
    std::fprintf(stderr, "Collector failed: %s\n", error.what());
    if (controller) {
      controller->fail(std::string("collector exception: ") + error.what());
    }
    exit_code = 1;
  }

  if (controller) {
    controller->close();
    exit_code = controller->exitCode();
  }
  if (rclcpp::ok()) {
    rclcpp::shutdown();
  }
  return exit_code;
}
