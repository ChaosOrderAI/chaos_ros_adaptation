#include "chaos_runtime/ota.hpp"

#include "chaos_core/errors.hpp"
#include "chaos_core/ota_wire.hpp"

namespace chaos::runtime {
namespace {

constexpr const char * kSimulatedMessage =
  "simulated update did not read a package, write files, or restart; "
  "installed version is unchanged";

using chaos::isOtaSha256;
using chaos::isOtaToken;
using chaos::kOtaMaxPackageBytes;

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
  if (!isOtaToken(request.artifact_id)) {
    throw chaos::Rejected(
      "INVALID_ARGUMENT",
      "artifact_id must be 1..128 letters, digits, '.', '_' or '-'");
  }
  if (!isOtaToken(request.version)) {
    throw chaos::Rejected(
      "INVALID_ARGUMENT",
      "version must be 1..128 letters, digits, '.', '_' or '-'");
  }
  if (!isOtaSha256(request.sha256)) {
    throw chaos::Rejected(
      "INVALID_ARGUMENT", "sha256 must be 64 lowercase hex characters");
  }
  if (request.size_bytes < 1 || request.size_bytes > kOtaMaxPackageBytes) {
    throw chaos::Rejected(
      "INVALID_ARGUMENT", "size_bytes must be in 1..2147483648");
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
