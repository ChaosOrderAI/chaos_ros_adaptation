# 技能执行的生命周期事件，由技能层唯一发布。
#
# 顶层来源订阅本话题，按 request_id 与自己发出的意图配对。
# 事件不含传输层字段：来源自己决定如何把事件编码回它的通道。

# 与意图对应的关联标识；来源未提供关联时为空。
string request_id
# 产生该意图的来源标识，例如 "agent:vehicle_001"。
string source
# 正在执行或已执行的技能 token。
string token
# accepted：已受理；feedback：执行中；succeeded：终态成功；failed：终态失败。
string phase
# 仅终态有效；中间事件为 false。
bool success
# 机器可读错误码，例如 "skill.hub_mismatch"。成功终态为空。
string code
# 人类可读说明。成功终态为空。
string message
# 执行中或结束时资产所处的枢纽状态。
string hub_state
# 执行进度，[0,1]；无进度概念的技能恒为 0。
float32 progress
# 技能返回的结构化载荷，JSON 编码。为空表示该技能没有载荷。
# 例如 observe 技能返回传感器快照，announce 技能返回通告回执。
string payload_json
