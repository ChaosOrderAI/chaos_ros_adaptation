#include "chaos_runtime/ota.hpp"

#include "chaos_core/errors.hpp"
#include "chaos_core/ota_wire.hpp"

namespace chaos::runtime {
namespace {

constexpr const char * kSimulatedMessage =
  "simulated update did not read a package, write files, or restart; "
  "installed version is unchanged";

using chaos::ota_request_error;

OtaStatus canceledStatus() {
  OtaStatus status;
  status.done = true;
  status.success = false;
  status.applied = false;
  status.code = "CANCELED";
  status.message = "Update canceled before any bytes were written";
  status.progress = 0.0;
  return status;
}

}  // namespace

void validateOtaRequest(const OtaRequest & request) {
  // The rules are shared with the MQTT command parser, so a request that
  // reaches the runtime was already checked the same way.
  const std::string error = ota_request_error(
    request.artifact_id, request.version, request.sha256, request.size_bytes);
  if (!error.empty()) {
    throw chaos::Rejected("INVALID_ARGUMENT", error);
  }
}

void SimulatedOtaExecutor::validate(const OtaRequest & request) {
  validateOtaRequest(request);
}

void SimulatedOtaExecutor::start(const OtaRequest & request) {
  validate(request);
  started_ = true;
  canceled_ = false;
  finished_ = false;
  polls_ = 0;
  terminal_ = OtaStatus{};
}

OtaStatus SimulatedOtaExecutor::poll() {
  if (canceled_) {
    finished_ = true;
    terminal_ = canceledStatus();
    return terminal_;
  }
  if (finished_) {
    return terminal_;
  }
  if (!started_) {
    OtaStatus idle;
    idle.code = "NOT_READY";
    idle.message = "Update has not started";
    return idle;
  }
  OtaStatus status;
  if (polls_ == 0) {
    ++polls_;
    status.phase = "verifying";
    status.progress = 0.5;
    status.code = "VERIFYING";
    status.message = "Checking the update request";
    return status;
  }
  status.done = true;
  status.success = true;
  status.applied = false;
  status.phase = "staging";
  status.progress = 1.0;
  status.code = "SIMULATED";
  status.message = kSimulatedMessage;
  finished_ = true;
  terminal_ = status;
  return status;
}

void SimulatedOtaExecutor::cancel() {
  if (!finished_) {
    canceled_ = true;
  }
}

}  // namespace chaos::runtime
