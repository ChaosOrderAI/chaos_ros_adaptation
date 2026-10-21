// Shared error type for Chaos C++ domain libraries. No ROS dependency.
#pragma once

#include <exception>
#include <string>
#include <utility>

namespace chaos {

// Typed rejection with a stable machine-readable code, mirroring
// chaos_runtime.control.Rejected. `code` values are part of the wire and
// log contract and must not be reformatted.
class Rejected : public std::exception {
public:
  Rejected(std::string code, std::string message)
      : code_(std::move(code)), message_(std::move(message)) {}

  const char * what() const noexcept override { return message_.c_str(); }
  const std::string & code() const { return code_; }
  const std::string & message() const { return message_; }

private:
  std::string code_;
  std::string message_;
};

}  // namespace chaos
