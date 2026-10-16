"""Run real rclpy acceptance/result/expiration paths with a failing transport."""
import asyncio
import threading
from types import SimpleNamespace

import pytest

pytest.importorskip('rclpy')
pytest.importorskip('chaos_interfaces.action')

from action_msgs.msg import GoalInfo
from builtin_interfaces.msg import Time
from rclpy.action import ActionServer, GoalResponse
from rclpy.action.server import RCLError
from chaos_interfaces.action import Flight
from chaos_runtime.action_server import RecoveringActionServer


class FakeHandle:
    def __init__(self, server, info, request):
        self.server, self.goal_id, self.request = server, info.goal_id, request
        self.is_active, self.status = True, 1
        self.destroyed = False

    def execute(self, callback=None):
        self.status = 2
        self.server.notify_execute(self, callback)

    def abort(self):
        self.is_active, self.status = False, 6
        self.server.notify_goal_done()

    def destroy(self):
        self.destroyed = True


def test_failed_goal_response_finishes_results_and_normal_expiry_reclaims_every_goal(monkeypatch):
    monkeypatch.setattr('rclpy.action.server.ServerGoalHandle', FakeHandle)
    server = RecoveringActionServer.__new__(RecoveringActionServer)
    server._acceptance_records = {}
    server._goal_handles, server._result_futures = {}, {}
    server._futures = []
    server._lock = threading.Lock()
    server._action_type = Flight
    server._logger = SimpleNamespace(debug=lambda *a: None, warn=lambda *a: None,
                                     warning=lambda *a: None, error=lambda *a: None)
    pending, failed, accepted, done = [], [], [], []
    executor = SimpleNamespace(create_task=lambda fn, *args: pending.append((fn, args)))
    server._node = SimpleNamespace(executor=executor, get_clock=lambda: SimpleNamespace(
        now=lambda: SimpleNamespace(to_msg=lambda: Time())))
    server._goal_callback = lambda request: GoalResponse.ACCEPT
    server._runtime_accepted_callback = lambda handle: accepted.append(handle)
    server._handle_accepted_callback = server._deliver_accepted
    server._acceptance_failed_callback = lambda handle: failed.append(handle)

    def fail_response(*args):
        raise RCLError('injected response failure')

    server._handle = SimpleNamespace(
        goal_exists=lambda info: bytes(info.goal_id.uuid) in server._goal_handles,
        send_goal_response=fail_response,
        notify_goal_done=lambda: done.append(True),
        publish_status=lambda: None,
    )

    async def exercise():
        for index in range(1, 4):
            request = Flight.Impl.SendGoalService.Request()
            request.goal_id.uuid = [index] * 16
            await server.execute({'goal': (object(), request)})
        assert not accepted and len(failed) == 3
        assert not server._acceptance_records
        for fn, args in pending:
            await fn(*args)
        assert len(done) == 3
        assert all(not handle.is_active for handle in server._goal_handles.values())
        for future in server._result_futures.values():
            assert future.done()
            assert future.result().result.code == 'ACCEPTANCE_FAILED'
        handles = list(server._goal_handles.values())
        expired = [GoalInfo(goal_id=handle.goal_id) for handle in handles]
        await server.execute({'expired': expired})
        assert not server._goal_handles and not server._result_futures and not server._futures
        assert all(handle.destroyed for handle in handles)

    asyncio.run(exercise())


def test_duplicate_uuid_cannot_abort_the_original_accepted_goal(monkeypatch):
    server = RecoveringActionServer.__new__(RecoveringActionServer)
    server._acceptance_records = {}
    original = Flight.Impl.SendGoalService.Request()
    duplicate = Flight.Impl.SendGoalService.Request()
    original.goal_id.uuid = duplicate.goal_id.uuid = [7] * 16
    handle = SimpleNamespace(request=original.goal, is_active=True)
    server._goal_handles = {bytes(original.goal_id.uuid): handle}
    recovered = []
    server._acceptance_failed_callback = recovered.append

    async def reject_duplicate(self, data):
        pass

    monkeypatch.setattr(ActionServer, 'execute', reject_duplicate)
    asyncio.run(server.execute({'goal': (object(), duplicate)}))
    assert not recovered and handle.is_active
    assert not server._acceptance_records
