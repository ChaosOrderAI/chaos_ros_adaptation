# 技能层向外部通信通道发出的一条通告。
#
# 本消息是"通信"基础能力的通用出口，不绑定任何机间传输方式：
# 当前由 MQTT 适配器订阅本话题并转发到 broker；更换机间通信机制时只替换
# 适配器，技能层与基础功能层不变。

# 与来源意图的关联标识；来源未提供关联时为空。
string request_id
# 产生该通告的来源标识。
string source
# 通告的逻辑主题，例如 "status"、"task_result"。
string topic
# 通告载荷，JSON 编码。
string payload_json
# 发布时刻（ROS 时间）。截止时间用 Unix UTC，观察时刻用 ROS 时间：
# 前者是线路契约，后者是机器上的观察。
builtin_interfaces/Time stamp
