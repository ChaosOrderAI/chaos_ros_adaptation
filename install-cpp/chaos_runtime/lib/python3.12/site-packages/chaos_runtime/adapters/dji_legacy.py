"""Read existing gateway status responses without claiming controllable PSDK access."""
import json
import time

from chaos_runtime.adapters.base import Adapter, Snapshot, validate_absolute_topic


class DjiLegacyAdapter(Adapter):
    option_names = frozenset({'status_topic'})
    max_status_characters = 64 * 1024

    @classmethod
    def validate_options(cls, asset):
        super().validate_options(asset)
        topic = asset['driver'].get('options', {}).get('status_topic', '/m3td/system/status')
        validate_absolute_topic(topic, 'DJI status_topic')

    def __init__(self, node, asset, limits):
        from std_msgs.msg import String
        super().__init__(node, asset, limits)
        topic = asset['driver'].get('options', {}).get('status_topic', '/m3td/system/status')
        self.last_received = 0.0
        self.flight_fresh = False
        self._subscription = node.create_subscription(
            String, topic, self._on_status, 10, callback_group=node.io_group)

    def _on_status(self, message):
        if not isinstance(message.data, str) or len(message.data) > self.max_status_characters:
            return
        try:
            status = json.loads(message.data)
        except (ValueError, TypeError, RecursionError):
            return
        if not isinstance(status, dict) or status.get('ok') is not True:
            return
        with self.node.adapter_lock:
            self.last_received = time.monotonic()
            self.flight_fresh = status.get('flight_telemetry_fresh') is True

    def snapshot(self):
        fresh = bool(self.last_received and
                     time.monotonic() - self.last_received < self.limits.state_timeout_s)
        return Snapshot(
            connected=fresh and self.flight_fresh,
            detail=('Legacy DJI status received; read-only, pose conversion not implemented'
                    if fresh else 'Waiting for legacy DJI system/status responses; upstream publishes on status requests'),
        )
