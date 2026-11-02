#pragma once

#include <mutex>
#include <utility>

#include "chaos_runtime/humanoid/backend.hpp"

namespace chaos::humanoid {

// SDK executors may retain queued callbacks after a subscription closes.
// Capture this gate by shared_ptr, never the backend or callback owner. close()
// waits for delivery in progress, then makes all retained callbacks inert.
// Callbacks must not re-enter this gate or close their own backend.
class TelemetryGate {
public:
  TelemetryGate(OdometryCallback odometry, MotionModeCallback mode)
      : odometry_(std::move(odometry)), mode_(std::move(mode)) {}

  void odometry(const Odometry & message) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (odometry_) odometry_(message);
  }

  void mode(bool enabled) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (mode_) mode_(enabled);
  }

  void close() {
    std::lock_guard<std::mutex> guard(mutex_);
    odometry_ = {};
    mode_ = {};
  }

private:
  std::mutex mutex_;
  OdometryCallback odometry_;
  MotionModeCallback mode_;
};

}  // namespace chaos::humanoid
