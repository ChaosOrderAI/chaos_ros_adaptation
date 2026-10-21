// Deterministic synthetic telemetry for integration tests, not a physics
// simulator. Port of chaos_runtime/adapters/mock.py.
#pragma once

#include <array>
#include <string>

#include "chaos_runtime/adapter.hpp"

namespace chaos::runtime {

class MockAdapter : public Adapter {
public:
  static AdapterInfo info() {
    return AdapterInfo{{"state", "motion.velocity", "motion.stop",
                        "navigation.pose", "flight.takeoff", "flight.land"},
                       {"kind"}};
  }

  // Mirrors MockAdapter.capabilities_for: ground kinds drop flight actions.
  static std::set<std::string> capabilitiesFor(const std::string & kind);

  // Throws AssetConfigError on unknown options or a kind/asset mismatch.
  static std::string validateOptions(const AssetConfig & asset);

  explicit MockAdapter(AssetConfig asset);

  AdapterSnapshot snapshot() override;
  void validateVelocity(const std::array<double, 6> & values) override;
  void setVelocity(const std::array<double, 6> & values) override;
  void stop() override;
  void startOperation(OperationKind kind,
                      const std::array<double, 4> & target) override;
  PollResult pollOperation() override;
  void update(double dt) override;

private:
  double distance() const;

  std::string kind_;
  control::Limits limits_;
  std::string map_frame_;
  std::string base_frame_;
  std::array<double, 3> position_{0.0, 0.0, 0.0};
  double yaw_ = 0.0;
  std::array<double, 6> velocity_{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  std::optional<std::array<double, 4>> target_;
  double initial_distance_ = 0.0;
  double progress_ = 0.0;
  bool finished_ = false;
};

}  // namespace chaos::runtime
