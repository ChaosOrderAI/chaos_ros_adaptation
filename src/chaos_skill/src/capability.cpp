#include "chaos_skill/capability.hpp"

namespace chaos_skill
{

const char * to_string(Capability capability)
{
  switch (capability) {
    case Capability::Motion:
      return "motion";
    case Capability::Navigation:
      return "navigation";
    case Capability::Sensor:
      return "sensor";
    case Capability::Communication:
      return "communication";
  }
  return "unknown";
}

std::optional<Capability> capability_from_string(const std::string & name)
{
  if (name == "motion") {
    return Capability::Motion;
  }
  if (name == "navigation") {
    return Capability::Navigation;
  }
  if (name == "sensor") {
    return Capability::Sensor;
  }
  if (name == "communication") {
    return Capability::Communication;
  }
  return std::nullopt;
}

}  // namespace chaos_skill
