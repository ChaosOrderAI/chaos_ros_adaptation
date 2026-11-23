#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace chaos_skill
{

/// The base capability families of the platform.
///
/// The skill layer is the only layer allowed to invoke them: the onboard
/// agent and the external communication adapter are peers above it and reach
/// the platform only through the skills. Each family names one group of base
/// functions of the platform.
enum class Capability
{
  /// 运动：直接改变资产运动的技能（起飞、降落、停止）。
  Motion,
  /// 导航：把资产移到目标位姿的技能。
  Navigation,
  /// 传感器：读取资产自身状态与外部传感流的技能。
  Sensor,
  /// 通信：向外部通信通道发出通告的技能。
  Communication,
};

/// Canonical name of a capability family, as written in the vocabulary JSON.
const char * to_string(Capability capability);

/// Parse a capability family name; std::nullopt when the name is unknown.
std::optional<Capability> capability_from_string(const std::string & name);

/// One reading of an auxiliary sensor stream.
struct AuxReading
{
  /// Stream name the vocabulary refers to, e.g. "cloud" or "imu".
  std::string stream;
  /// Frame the sample was expressed in; empty when the message carries none.
  std::string frame_id;
  /// Time stamp of the newest sample, in ROS nanoseconds. It is the stamp of
  /// the message, not the moment the skill layer received it.
  std::int64_t stamp_ns = 0;
  /// Whether a sample was ever received on this stream.
  bool valid = false;
};

/// Where the request that triggered a skill came from.
struct SkillOrigin
{
  /// Correlation id of the request; empty when the caller does not correlate.
  std::string request_id;
  /// Identifier of the source that produced the request, e.g.
  /// "agent:vehicle_001" or "mqtt:vehicle_001".
  std::string source;
  /// Whether the hub precondition of the vocabulary applies to the request.
  ///
  /// A planning decision is bound by it; a direct operator command is not,
  /// because the hub is the state machine of the planning layer and must not
  /// veto a direct command.
  bool enforce_hub = true;
  /// Control token (lease) of the caller.
  ///
  /// When it is set, the skill runs with that token instead of acquiring a
  /// lease of its own, and it never releases it: the lease belongs to the
  /// caller. The runtime still admits one lease holder at a time.
  std::string control_token;
  /// Absolute Unix UTC deadline for starting the underlying goal, in
  /// nanoseconds. Zero means the caller set no transport deadline, and the
  /// SDK's own start TTL applies. A deadline the caller did set is never
  /// extended.
  std::int64_t deadline_ns = 0;
};

/// The base capabilities the executor may invoke besides the SDK robot.
///
/// The executor reaches the platform only through this interface and through
/// chaos_sdk::Robot. It never talks to a broker, a transport or a device
/// driver directly, so everything below the skill layer stays replaceable.
/// The communication channel behind announce() is the generic egress of the
/// platform: the inter-machine transport subscribes to it and forwards the
/// announcement, and swapping that transport does not touch this interface.
class CapabilityPorts
{
public:
  virtual ~CapabilityPorts() = default;

  /// Publish one announcement on the external communication channel.
  ///
  /// Returns false and fills `code` and `message` when the announcement could
  /// not be published.
  virtual bool announce(
    const std::string & topic,
    const std::string & payload_json,
    const SkillOrigin & origin,
    std::string & code,
    std::string & message) = 0;

  /// Newest reading of an auxiliary sensor stream.
  virtual AuxReading read_aux(const std::string & stream) const = 0;

  /// Names of the auxiliary sensor streams this deployment exposes.
  virtual std::vector<std::string> aux_streams() const = 0;
};

}  // namespace chaos_skill
