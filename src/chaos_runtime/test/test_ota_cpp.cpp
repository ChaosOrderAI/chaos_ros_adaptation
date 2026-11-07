#include <gtest/gtest.h>

#include <string>

#include "chaos_core/errors.hpp"
#include "chaos_runtime/ota.hpp"

namespace chaos::runtime {
namespace {

OtaRequest validRequest() {
  OtaRequest request;
  request.artifact_id = "pkg.alpha";
  request.version = "1.2.3";
  request.sha256 = std::string(64, 'b');
  request.size_bytes = 64;
  return request;
}

TEST(SimulatedOta, TwoPollsReportSimulatedAndDoNotApply) {
  SimulatedOtaExecutor executor;
  const OtaRequest request = validRequest();
  EXPECT_NO_THROW(executor.validate(request));
  executor.start(request);
  const OtaStatus verifying = executor.poll();
  EXPECT_FALSE(verifying.done);
  EXPECT_EQ(verifying.phase, "verifying");
  EXPECT_DOUBLE_EQ(verifying.progress, 0.5);
  const OtaStatus staged = executor.poll();
  EXPECT_TRUE(staged.done);
  EXPECT_TRUE(staged.success);
  EXPECT_FALSE(staged.applied);
  EXPECT_EQ(staged.phase, "staging");
  EXPECT_EQ(staged.code, "SIMULATED");
  EXPECT_NE(staged.message.find("did not read a package"), std::string::npos);
  const OtaStatus again = executor.poll();
  EXPECT_EQ(again.code, "SIMULATED");
  EXPECT_FALSE(again.applied);
}

TEST(SimulatedOta, CancelStopsBeforeSuccess) {
  SimulatedOtaExecutor executor;
  executor.start(validRequest());
  EXPECT_FALSE(executor.poll().done);
  executor.cancel();
  const OtaStatus canceled = executor.poll();
  EXPECT_TRUE(canceled.done);
  EXPECT_FALSE(canceled.success);
  EXPECT_FALSE(canceled.applied);
  EXPECT_EQ(canceled.code, "CANCELED");
}

TEST(SimulatedOta, RejectsABadDigestAndAnEmptyIdentity) {
  SimulatedOtaExecutor executor;
  OtaRequest request = validRequest();
  request.sha256 = std::string(64, 'A');
  EXPECT_THROW(executor.validate(request), chaos::Rejected);
  request = validRequest();
  request.artifact_id.clear();
  EXPECT_THROW(executor.validate(request), chaos::Rejected);
  request = validRequest();
  request.size_bytes = 0;
  EXPECT_THROW(executor.validate(request), chaos::Rejected);
  request = validRequest();
  request.size_bytes = 2147483649ULL;
  EXPECT_THROW(executor.validate(request), chaos::Rejected);
}

}  // namespace
}  // namespace chaos::runtime
