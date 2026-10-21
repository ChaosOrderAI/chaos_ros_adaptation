// Unitree SDK2 backend. Compiled and linked only with
// -DCHAOS_GO2_WITH_UNITREE_SDK=ON and the official SDK2 CMake package
// (find_package(unitree_sdk2 CONFIG REQUIRED)). Without that option this file
// builds an explicit failure stub: real-mode startup must refuse to run rather
// than pretend SDK support.
//
// API surface used, taken from the upstream headers of
// https://github.com/unitreerobotics/unitree_sdk2 (source pinned at
// d1abf001220d3e5f59e00a0c4aaa595e00089a4a):
//   - unitree::robot::ChannelFactory::Instance()->Init(domain, interface)
//   - unitree::robot::ChannelSubscriber<T>::InitChannel(callback, depth)
//     (the upstream method is InitChannel, not Init)
//   - SportModeState_::position()/velocity() and
//     IMUState_::quaternion()/gyroscope() return std::array<float, N>; they
//     are converted element by element into the double snapshot, never
//     reinterpret_cast-ed.
//   - unitree::robot::go2::SportClient::SetTimeout/Init/Move
//
// This backend compiles and links against the official SDK, and the driver
// was exercised end to end with a fake backend. It has never been run
// against a robot on this machine: the real write timing, the DDS discovery
// latency and the hardware reaction are untested. See
// docs/cpp-runtime-migration.md for the exact limitations.
#include "chaos_runtime/go2/sdk_backend.hpp"

#include <array>
#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#if defined(CHAOS_GO2_WITH_UNITREE_SDK)
#include <unitree/idl/go2/IMUState_.hpp>
#include <unitree/idl/go2/SportModeState_.hpp>
#include <unitree/robot/channel/channel_factory.hpp>
#include <unitree/robot/channel/channel_subscriber.hpp>
#include <unitree/robot/go2/sport/sport_client.hpp>

namespace chaos::go2 {

namespace {

// Converts a vendor float array into the double snapshot element by
// element. The vendor arrays are std::array<float, N>; no pointer is
// borrowed and no reinterpret_cast is used.
template <std::size_t N>
std::array<double, N> toDoubles(const std::array<float, N> & source) {
  std::array<double, N> result{};
  for (std::size_t index = 0; index < N; ++index) {
    result[index] = static_cast<double>(source[index]);
  }
  return result;
}

class UnitreeSdkBackend : public Go2SdkBackend {
public:
  UnitreeSdkBackend(int domain, const std::string & network_interface,
                    const std::string & sport_state_topic,
                    SportStateCallback on_state) {
    unitree::robot::ChannelFactory::Instance()->Init(domain,
                                                     network_interface);
    sport_client_.SetTimeout(0.05f);
    sport_client_.Init();
    subscriber_ = std::make_unique<
      unitree::robot::ChannelSubscriber<unitree_go::msg::dds_::SportModeState_>>(
      sport_state_topic);
    // The upstream subscription method is InitChannel(callback, depth).
    subscriber_->InitChannel(
      [callback = std::move(on_state)](const void * message) {
        const auto & state =
          *static_cast<const unitree_go::msg::dds_::SportModeState_ *>(message);
        SportStateSnapshot snapshot;
        const auto & position = state.position();
        const auto & velocity = state.velocity();
        const auto & quaternion = state.imu_state().quaternion();
        const auto & gyroscope = state.imu_state().gyroscope();
        snapshot.position = toDoubles(position);
        snapshot.velocity = toDoubles(velocity);
        snapshot.quaternion = toDoubles(quaternion);
        snapshot.gyroscope = toDoubles(gyroscope);
        snapshot.yaw_speed = static_cast<double>(state.yaw_speed());
        snapshot.error_code = static_cast<std::int32_t>(state.error_code());
        const auto & stamp = state.stamp();
        snapshot.has_stamp = true;
        snapshot.sec = static_cast<std::int64_t>(stamp.sec());
        snapshot.nanosec = static_cast<std::int64_t>(stamp.nanosec());
        callback(snapshot);
      },
      1);
  }

  int move(double vx, double vy, double wz) override {
    // The SDK client is not reentrant: the watchdog tick and the shutdown
    // zero write can overlap, because close() cancels the timer and then
    // writes the final zero itself. The writes are therefore serialised.
    std::lock_guard<std::mutex> guard(write_mutex_);
    return sport_client_.Move(static_cast<float>(vx),
                              static_cast<float>(vy),
                              static_cast<float>(wz));
  }

  void stop() override {
    // The vendor reader thread is released by the subscriber destructor;
    // the callback is captured by value, so no telemetry can reach a
    // destroyed owner after this call.
    std::lock_guard<std::mutex> guard(write_mutex_);
    subscriber_.reset();
  }

private:
  // Serialises the SDK writes against each other. It is never held while a
  // telemetry callback runs, so the vendor reader thread is never blocked by
  // a slow write.
  std::mutex write_mutex_;
  unitree::robot::go2::SportClient sport_client_;
  std::unique_ptr<
    unitree::robot::ChannelSubscriber<unitree_go::msg::dds_::SportModeState_>>
  subscriber_;
};

}  // namespace

std::unique_ptr<Go2SdkBackend> createUnitreeSdkBackend(
  int domain, const std::string & network_interface,
  const std::string & sport_state_topic, SportStateCallback on_state) {
  return std::make_unique<UnitreeSdkBackend>(domain, network_interface,
                                             sport_state_topic,
                                             std::move(on_state));
}

Go2BackendFactory unitreeSdkBackendFactory(
  int domain, const std::string & network_interface,
  const std::string & sport_state_topic) {
  return [domain, network_interface, sport_state_topic](
           SportStateCallback on_state) {
    return std::shared_ptr<Go2SdkBackend>(
      createUnitreeSdkBackend(domain, network_interface, sport_state_topic,
                              std::move(on_state)).release());
  };
}

}  // namespace chaos::go2

#else  // !CHAOS_GO2_WITH_UNITREE_SDK

namespace chaos::go2 {

std::unique_ptr<Go2SdkBackend> createUnitreeSdkBackend(
  int, const std::string &, const std::string &, SportStateCallback) {
  throw std::runtime_error(
    "chaos_runtime was built without Unitree SDK2 support "
    "(CHAOS_GO2_WITH_UNITREE_SDK=OFF); a real Go2 driver cannot start. "
    "Rebuild with the SDK prefix or use mock mode.");
}

Go2BackendFactory unitreeSdkBackendFactory(
  int, const std::string &, const std::string &) {
  return [](SportStateCallback) -> std::shared_ptr<Go2SdkBackend> {
    throw std::runtime_error(
      "chaos_runtime was built without Unitree SDK2 support "
      "(CHAOS_GO2_WITH_UNITREE_SDK=OFF); a real Go2 driver cannot start.");
  };
}

}  // namespace chaos::go2

#endif  // CHAOS_GO2_WITH_UNITREE_SDK
