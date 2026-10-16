"""Contain the Jazzy acceptance-response cleanup compatibility workaround.

Some Jazzy rclpy versions retain an accepted goal when send_goal_response fails,
without invoking handle_accepted_callback. The public Waitable.execute hook lets
us detect that completed handshake. The one internal dependency is the goal-ID
lookup: rclpy has no public API to retrieve a handle that it did not deliver.
Execution, typed result completion and expiry then follow normal rclpy APIs.
"""
from rclpy.action import ActionServer
from rclpy.executors import await_or_execute


class RecoveringActionServer(ActionServer):
    def __init__(self, *args, handle_accepted_callback, acceptance_failed_callback, **kwargs):
        self._acceptance_records = {}
        self._runtime_accepted_callback = handle_accepted_callback
        self._acceptance_failed_callback = acceptance_failed_callback
        super().__init__(*args, handle_accepted_callback=self._deliver_accepted, **kwargs)
        if not isinstance(getattr(self, '_goal_handles', None), dict):
            raise RuntimeError('Unsupported rclpy ActionServer goal storage; acceptance recovery unavailable')

    async def _deliver_accepted(self, handle):
        await await_or_execute(self._runtime_accepted_callback, handle)
        record = self._acceptance_records.get(id(handle.request))
        if record is not None:
            record['delivered'] = True

    def _abort_unacknowledged(self, handle):
        handle.abort()
        result = self.action_type.Result()
        result.success = False
        result.code = 'ACCEPTANCE_FAILED'
        result.message = 'Action acceptance response could not be delivered; operation was not started'
        return result

    async def execute(self, taken_data):
        request = taken_data.get('goal')
        if request is None:
            return await super().execute(taken_data)
        goal_request = request[1]
        record = {'delivered': False}
        key = id(goal_request.goal)
        self._acceptance_records[key] = record
        try:
            return await super().execute(taken_data)
        finally:
            self._acceptance_records.pop(key, None)
            if not record['delivered']:
                handle = self._goal_handles.get(bytes(goal_request.goal_id.uuid))
                # Identity excludes an already executing goal with a duplicate UUID.
                if (handle is not None and handle.request is goal_request.goal
                        and handle.is_active):
                    self._acceptance_failed_callback(handle)
                    # Let rclpy set the typed result Future and perform its normal
                    # result_timeout cleanup; never start the device operation.
                    handle.execute(self._abort_unacknowledged)
