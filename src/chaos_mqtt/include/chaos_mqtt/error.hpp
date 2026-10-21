#pragma once

#include <stdexcept>
#include <string>

namespace chaos_mqtt
{

// Validation failure with a stable machine-readable wire code, mirroring the
// former Python CommandError. Codes travel in MQTT response envelopes.
class CommandError : public std::runtime_error
{
public:
  CommandError(std::string code, std::string message)
  : std::runtime_error(message), code_(std::move(code))
  {}

  const std::string & code() const {return code_;}

private:
  std::string code_;
};

}  // namespace chaos_mqtt
