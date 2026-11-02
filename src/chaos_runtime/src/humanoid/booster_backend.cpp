#include "chaos_runtime/humanoid/booster_backend.hpp"

#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "chaos_runtime/humanoid/telemetry_gate.hpp"

#if defined(CHAOS_HUMANOID_WITH_BOOSTER_SDK)
#include <booster/idl/b1/RobotStates.h>
#include <booster/idl/nav_msgs/Odometry.h>
#include <booster/robot/b1/b1_loco_client.hpp>
#include <booster/robot/channel/channel_factory.hpp>
#include <booster/robot/channel/channel_subscriber.hpp>

namespace chaos::humanoid {
namespace {

class BoosterBackend final : public HumanoidBackend {
public:
  BoosterBackend(const BackendConfig & config, OdometryCallback on_odometry,
                 MotionModeCallback on_mode)
      : callbacks_(std::make_shared<TelemetryGate>(
          std::move(on_odometry), std::move(on_mode))) {
    try {
      booster::robot::ChannelFactory::Instance()->Init(
        config.dds_domain_id, config.network_interface);
      loco_.Init();
      // MoveCommand is fire-and-forget, so only the request endpoint is needed.
      if (!loco_.WaitForService(5000, false)) {
        throw std::runtime_error("Booster locomotion request service is unavailable");
      }
      odom_sub_ = std::make_unique<booster::robot::ChannelSubscriber<nav_msgs::msg::Odometry>>(
        config.state_topic);
      mode_sub_ = std::make_unique<booster::robot::ChannelSubscriber<
        booster_interface::msg::RobotStatesMsg>>(config.robot_state_topic);
      odom_sub_->InitChannel([callbacks = callbacks_](const void * data) {
        if (data == nullptr) {
          return;
        }
        const auto & source = *static_cast<const nav_msgs::msg::Odometry *>(data);
        const auto & pose = source.pose().pose();
        const auto & position = pose.position();
        const auto & orientation = pose.orientation();
        const auto & twist = source.twist().twist();
        Odometry snapshot;
        snapshot.stamp = Stamp{source.header().stamp().sec(),
                               source.header().stamp().nanosec()};
        snapshot.frame_id = source.header().frame_id();
        snapshot.child_frame_id = source.child_frame_id();
        snapshot.position = {position.x(), position.y(), position.z()};
        snapshot.quaternion_xyzw = {orientation.x(), orientation.y(),
                                    orientation.z(), orientation.w()};
        snapshot.twist = {twist.linear().x(), twist.linear().y(), twist.linear().z(),
          twist.angular().x(), twist.angular().y(), twist.angular().z()};
        callbacks->odometry(snapshot);
      });
      mode_sub_->InitChannel([callbacks = callbacks_](const void * data) {
        if (data == nullptr) {
          return;
        }
        const auto & source =
          *static_cast<const booster_interface::msg::RobotStatesMsg *>(data);
        // Translation stays in this vendor TU; the generic driver never learns
        // Booster's mode numbering or transition API.
        using Mode = decltype(source.current_mode());
        callbacks->mode(source.current_mode() ==
          static_cast<Mode>(booster::robot::RobotMode::kWalking));
      });
    } catch (...) {
      // A failed constructor has no destructor: disarm callbacks before the
      // caller starts unwinding its Node/Core, including partial subscription.
      stop();
      throw;
    }
  }

  ~BoosterBackend() override { stop(); }

  int move(double vx, double vy, double yaw_rate) override {
    std::lock_guard<std::mutex> guard(mutex_);
    if (stopped_) {
      return -1;
    }
    return loco_.MoveCommand(static_cast<float>(vx), static_cast<float>(vy),
                             static_cast<float>(yaw_rate));
  }

  void stop() override {
    std::lock_guard<std::mutex> guard(mutex_);
    if (stopped_) {
      return;
    }
    stopped_ = true;
    callbacks_->close();
    if (mode_sub_) {
      mode_sub_->CloseChannel();
    }
    if (odom_sub_) {
      odom_sub_->CloseChannel();
    }
  }

private:
  std::mutex mutex_;
  bool stopped_ = false;
  std::shared_ptr<TelemetryGate> callbacks_;
  booster::robot::b1::B1LocoClient loco_;
  std::unique_ptr<booster::robot::ChannelSubscriber<nav_msgs::msg::Odometry>> odom_sub_;
  std::unique_ptr<booster::robot::ChannelSubscriber<
    booster_interface::msg::RobotStatesMsg>> mode_sub_;
};

}  // namespace

std::shared_ptr<HumanoidBackend> createBoosterSdkBackend(
  const BackendConfig & config, OdometryCallback on_odometry,
  MotionModeCallback on_mode) {
  return std::make_shared<BoosterBackend>(config, std::move(on_odometry),
                                          std::move(on_mode));
}

int emergencyZero(std::int32_t domain, const std::string & network_interface) {
  try {
    booster::robot::ChannelFactory::Instance()->Init(domain, network_interface);
    booster::robot::b1::B1LocoClient loco;
    loco.Init();
    if (!loco.WaitForService(2000, false)) {
      return -1;
    }
    return loco.MoveCommand(0.0f, 0.0f, 0.0f);
  } catch (...) {
    return -1;
  }
}

}  // namespace chaos::humanoid

#else

namespace chaos::humanoid {

std::shared_ptr<HumanoidBackend> createBoosterSdkBackend(
  const BackendConfig &, OdometryCallback, MotionModeCallback) {
  throw std::runtime_error(
    "chaos_runtime was built without Booster SDK support "
    "(CHAOS_HUMANOID_WITH_BOOSTER_SDK=OFF); a real humanoid driver cannot start");
}

int emergencyZero(std::int32_t, const std::string &) {
  return -1;
}

}  // namespace chaos::humanoid

#endif

namespace chaos::humanoid {

BackendFactory boosterSdkBackendFactory() {
  return [](const BackendConfig & config, OdometryCallback on_odometry,
            MotionModeCallback on_mode) {
    return createBoosterSdkBackend(config, std::move(on_odometry),
                                   std::move(on_mode));
  };
}

void registerBoosterSdkBackend() {
  registerBackendFactory("booster_t2", boosterSdkBackendFactory());
}

}  // namespace chaos::humanoid
