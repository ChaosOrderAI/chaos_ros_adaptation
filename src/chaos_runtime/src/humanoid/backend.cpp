#include "chaos_runtime/humanoid/backend.hpp"

#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace chaos::humanoid {
namespace {
std::mutex registry_mutex;
std::map<std::string, BackendFactory> & registry() {
  static std::map<std::string, BackendFactory> factories;
  return factories;
}
}  // namespace

void registerBackendFactory(const std::string & name, BackendFactory factory) {
  if (name.empty() || !factory) {
    throw std::invalid_argument("Humanoid backend registration requires a name and factory");
  }
  std::lock_guard<std::mutex> guard(registry_mutex);
  if (!registry().emplace(name, std::move(factory)).second) {
    throw std::invalid_argument("Humanoid backend already registered: " + name);
  }
}

BackendFactory backendFactory(const std::string & name) {
  std::lock_guard<std::mutex> guard(registry_mutex);
  const auto found = registry().find(name);
  if (found == registry().end()) {
    throw std::runtime_error("Unknown or unregistered humanoid backend: " + name);
  }
  return found->second;
}

std::shared_ptr<HumanoidBackend> createBackend(
  const std::string & name, const BackendConfig & config,
  OdometryCallback on_odometry, MotionModeCallback on_mode) {
  auto backend = backendFactory(name)(config, std::move(on_odometry),
                                     std::move(on_mode));
  if (!backend) {
    throw std::runtime_error("Humanoid backend factory returned no backend: " + name);
  }
  return backend;
}

}  // namespace chaos::humanoid
