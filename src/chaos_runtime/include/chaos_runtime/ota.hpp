// Replaceable software-update executor. ROS-free: the coordinator admits the
// goal, and this object only validates the request and reports progress.
//
// The default implementation never opens a file, a socket or a vendor SDK.
// A later executor may replace it without changing the action, MQTT or SDK
// fields. It must leave `applied` false until a byte commit has actually
// happened. There is no path that loads an executor from disk.
#pragma once

#include <cstdint>
#include <string>

namespace chaos::runtime {

struct OtaRequest {
  std::string artifact_id;
  std::string version;
  std::string sha256;
  std::uint64_t size_bytes = 0;
};

struct OtaStatus {
  bool done = false;
  bool success = false;
  bool applied = false;
  std::string phase;
  std::string code;
  std::string message;
  double progress = 0.0;
};

// Checks the request shape and throws chaos::Rejected on a violation.
// `artifact_id` and `version` are 1..128 tokens of letters, digits, '.', '_'
// and '-'. `sha256` is 64 lowercase hex digits. `size_bytes` is in
// 1..2147483648. The check does not read a package.
void validateOtaRequest(const OtaRequest & request);

class OtaExecutor {
public:
  virtual ~OtaExecutor() = default;
  virtual void validate(const OtaRequest & request) = 0;
  virtual void start(const OtaRequest & request) = 0;
  virtual OtaStatus poll() = 0;
  virtual void cancel() = 0;
};

// Two polls: `verifying` at 0.5, then `staging` at 1.0 with code SIMULATED
// and applied false. Cancel makes the next poll fail without applying.
class SimulatedOtaExecutor : public OtaExecutor {
public:
  void validate(const OtaRequest & request) override;
  void start(const OtaRequest & request) override;
  OtaStatus poll() override;
  void cancel() override;

private:
  bool started_ = false;
  bool canceled_ = false;
  bool finished_ = false;
  int polls_ = 0;
  OtaStatus terminal_;
};

}  // namespace chaos::runtime
