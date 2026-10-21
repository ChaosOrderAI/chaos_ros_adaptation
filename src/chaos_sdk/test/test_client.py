"""End to end test of the native SDK through its thin Python binding.

The mock asset runs as a separate native process, so the Python client talks
to a real ROS peer over DDS. The test exercises the whole user visible flow:
describe, acquire, navigate, result, cancel and close.
"""
import os
import subprocess
import time

import pytest

try:
    import chaos_sdk
    from chaos_sdk import Robot
except ImportError as error:  # pragma: no cover - the binding is mandatory
    pytest.fail(f'the native chaos_sdk package is not importable: {error}')

ASSET = 'sdk_python_test'
NAMESPACE = f'/robots/{ASSET}'


def _mock_asset_path():
    """Locate the mock asset.

    The path is configured by the build (CHAOS_SDK_MOCK_ASSET). The fallback
    derives it from the installed Python package.
    """
    override = os.environ.get('CHAOS_SDK_MOCK_ASSET')
    if override:
        return override
    package = os.path.dirname(os.path.abspath(chaos_sdk.__file__))
    # <prefix>/lib/python3.x/site-packages/chaos_sdk -> <prefix>/lib
    library = os.path.abspath(os.path.join(package, '..', '..', '..'))
    return os.path.join(library, 'chaos_sdk', 'chaos_sdk_mock_asset')


@pytest.fixture(scope='module')
def mock_asset():
    executable = _mock_asset_path()
    if not os.path.exists(executable):
        # A missing mock asset is a broken build, not an optional dependency:
        # skipping would hide the failure behind a green test run.
        pytest.fail(f'mock asset executable not found: {executable}')
    process = subprocess.Popen(
        [executable, ASSET, '--namespace', NAMESPACE],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2.0)
    yield process
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def test_native_client_discovers_acquires_navigates_and_closes(mock_asset):
    client = Robot(ASSET, response_timeout_s=10.0)
    try:
        assert client.wait_ready(20.0)
        description = client.describe().result()
        assert description.base_frame == f'{ASSET}/base_link'
        assert description.command_timeout_s == 0.5
        assert 'motion.velocity' in description.capabilities

        lease = client.acquire('python-smoke', 5.0).result()
        assert lease.success
        assert lease.token == 'mock-lease-token'
        assert client.lease_token == 'mock-lease-token'
        assert client.owner == 'python-smoke'

        target = {
            'frame_id': 'map',
            'position': {'x': 1.5, 'y': -2.5, 'z': 0.25},
            'orientation': {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0},
        }
        task = client.navigate(target).result()
        assert task.accepted
        assert task.goal_id
        result = task.result()
        assert result['ok'] is True
        assert result['code'] == 'OK'
        assert task.state == 'succeeded'
    finally:
        client.close()
    # A closed client rejects further calls with a stable error.
    with pytest.raises(RuntimeError):
        client.describe()


def test_a_flat_target_dict_reaches_the_server(mock_asset):
    """The flat dictionary layout must not be read out of range."""
    client = Robot(ASSET, response_timeout_s=10.0)
    try:
        client.acquire('python-smoke', 5.0).result()
        target = {
            'frame_id': 'map',
            'position': {'x': 1.5, 'y': -2.5, 'z': 0.25},
            'orientation': {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0},
        }
        task = client.navigate(target).result()
        assert task.accepted
        result = task.result()
        assert result['ok'] is True
        # The mock echoes the target it received.
        assert 'frame=map' in result['message']
        assert 'x=1.5' in result['message']
        assert 'y=-2.5' in result['message']
        assert 'z=0.25' in result['message']
        assert 'qw=1' in result['message']
    finally:
        client.close()


def test_a_ros_pose_stamped_reaches_the_server(mock_asset):
    """A real ROS message keeps its frame, position and orientation."""
    from geometry_msgs.msg import PoseStamped

    client = Robot(ASSET, response_timeout_s=10.0)
    try:
        client.acquire('python-smoke', 5.0).result()
        target = PoseStamped()
        target.header.frame_id = 'odom'
        target.pose.position.x = -0.75
        target.pose.position.y = 0.5
        target.pose.position.z = 0.125
        target.pose.orientation.w = 1.0
        task = client.navigate(target).result()
        assert task.accepted
        result = task.result()
        assert result['ok'] is True
        assert 'frame=odom' in result['message']
        assert 'x=-0.75' in result['message']
        assert 'y=0.5' in result['message']
        assert 'z=0.125' in result['message']
        assert 'qw=1' in result['message']
    finally:
        client.close()


def test_native_client_cancel_reports_the_server_answer(mock_asset):
    client = Robot(ASSET, response_timeout_s=10.0)
    try:
        client.acquire('python-smoke', 5.0).result()
        task = client.navigate({'frame_id': 'map'}).result()
        assert task.accepted
        outcome = task.cancel()
        assert 'accepted' in outcome
        assert 'return_code' in outcome
        assert outcome['return_code'] >= 0
    finally:
        client.close()


def test_closed_client_does_not_shut_down_other_clients(mock_asset):
    first = Robot(ASSET, response_timeout_s=10.0)
    second = Robot(ASSET, response_timeout_s=10.0)
    try:
        first.close()
        # The second client keeps its own context alive.
        assert second.acquire('python-smoke', 5.0).result().success
    finally:
        first.close()
        second.close()


def test_invalid_command_ttl_never_creates_a_client():
    with pytest.raises(ValueError):
        Robot(ASSET, command_ttl_s=0)
    with pytest.raises(ValueError):
        Robot(ASSET, command_ttl_s=31)


def test_velocity_requires_explicit_control(mock_asset):
    client = Robot(ASSET, response_timeout_s=1.0)
    try:
        with pytest.raises(ValueError, match='Acquire control'):
            client.velocity(x=1.0)
    finally:
        client.close()


def test_legacy_constructor_signature_is_accepted(mock_asset):
    client = Robot(None, ASSET, response_timeout_s=10.0)
    try:
        assert client.acquire('python-smoke', 5.0).result().success
    finally:
        client.close()
