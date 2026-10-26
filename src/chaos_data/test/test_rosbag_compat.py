"""Regressions using the public API shape of upstream ROS 2 Humble."""

import sys
import threading
from types import SimpleNamespace as NS
import weakref

import pytest

from chaos_data import native_recorder
from chaos_data.audit import audit_bag
from chaos_data.rosbag_compat import close_bag, request_read_order, topic_metadata


class HumbleRecordOptions:
    # Deliberately reject Jazzy-only assignments, like the Humble pybind class.
    __slots__ = ('all', 'topics', 'include_hidden_topics', 'include_unpublished_topics',
                 'start_paused', 'use_sim_time', 'topic_qos_profile_overrides')


def test_humble_record_options_do_not_access_newer_fields(monkeypatch, tmp_path):
    monkeypatch.setitem(sys.modules, 'rosbag2_py', NS(
        RecordOptions=HumbleRecordOptions, StorageOptions=lambda **values: NS(**values)))
    monkeypatch.setitem(sys.modules, 'rclpy.qos', NS(
        QoSProfile=lambda **values: NS(**values), ReliabilityPolicy=NS(RELIABLE='reliable'),
        DurabilityPolicy=NS(TRANSIENT_LOCAL='transient')))
    config = {'storage_id': 'sqlite3', 'max_bagfile_size_mb': 1,
              'max_bagfile_duration_s': 10, 'max_cache_size_mb': 1,
              'topics': ['/camera/image', '/tf_static']}
    storage, recording = native_recorder.options(config, tmp_path / 'bag')
    assert storage.storage_id == 'sqlite3'
    assert recording.all is False and recording.topics == config['topics']
    assert not recording.include_unpublished_topics and not recording.use_sim_time
    assert not recording.start_paused
    assert set(recording.topic_qos_profile_overrides) == {'/tf_static'}


class BlockingHumbleRecorder:
    def __init__(self, log_level):
        assert log_level == 'info'
        self.entered = threading.Event()
        self.cancelled = threading.Event()
        self.completed = False

    def record(self, storage, recording, node_name):
        assert (storage, recording, node_name) == ('storage', 'recording', 'recorder')
        self.entered.set()
        self.cancelled.wait()
        self.completed = True

    def cancel(self):
        self.cancelled.set()


def test_humble_blocking_record_waits_for_readiness_and_cancel_finishes_flush(monkeypatch):
    recorder = native_recorder._HumbleRecorder(
        BlockingHumbleRecorder, 'storage', 'recording', 'recorder')
    checked = []

    def wait_ready(name, check_running):
        assert name == 'recorder'
        assert recorder._native.entered.wait(1)
        check_running()
        assert not recorder._native.completed
        checked.append(True)

    monkeypatch.setattr(native_recorder, '_wait_for_humble_ready', wait_ready)
    recorder.start()
    assert checked == [True] and recorder._thread.is_alive()
    recorder.stop()
    assert recorder._native.completed and not recorder._thread.is_alive()


def test_humble_start_error_is_not_misreported_as_ready(monkeypatch):
    class BrokenRecorder(BlockingHumbleRecorder):
        def record(self, *_):
            raise ValueError('storage unavailable')

    recorder = native_recorder._HumbleRecorder(
        BrokenRecorder, 'storage', 'recording', 'recorder')

    def wait_ready(_name, check_running):
        recorder._thread.join(timeout=1)
        check_running()

    monkeypatch.setattr(native_recorder, '_wait_for_humble_ready', wait_ready)
    with pytest.raises(RuntimeError, match='storage unavailable'):
        recorder.start()
    assert not recorder._thread.is_alive()


def test_humble_recorder_unexpected_return_is_a_failure(monkeypatch):
    class ExitingRecorder(BlockingHumbleRecorder):
        def record(self, *_):
            return

    recorder = native_recorder._HumbleRecorder(
        ExitingRecorder, 'storage', 'recording', 'recorder')

    def wait_ready(_name, check_running):
        recorder._thread.join(timeout=1)
        check_running()

    monkeypatch.setattr(native_recorder, '_wait_for_humble_ready', wait_ready)
    with pytest.raises(RuntimeError, match='exited unexpectedly'):
        recorder.start()


def test_humble_reader_does_not_require_unexposed_methods():
    reader = object()  # no close(), set_read_order(), or read-order enum
    assert not request_read_order(reader, NS(), 'File')
    close_bag(reader)


def test_humble_audit_releases_reader_while_caller_retains_exception(tmp_path, monkeypatch):
    readers = []

    class Reader:
        def __init__(self):
            readers.append(weakref.ref(self))

        def open(self, *_):
            pass

    monkeypatch.setitem(sys.modules, 'rosbag2_py', NS(
        SequentialReader=Reader, StorageOptions=lambda **kw: kw, ConverterOptions=lambda *_: None))
    monkeypatch.setitem(sys.modules, 'rosidl_runtime_py.utilities', NS(get_message=lambda _: object))
    monkeypatch.setitem(sys.modules, 'rclpy.serialization', NS(deserialize_message=lambda *_: None))
    # The failure occurs after open, before reading, outside a reader method.
    with pytest.raises(FileNotFoundError) as captured:
        audit_bag(tmp_path, index=tmp_path.parent / ('missing-' + tmp_path.name) / 'index.jsonl')
    assert captured.value.__traceback__ is not None
    assert readers[0]() is None, 'retained traceback must not keep Humble storage open'


@pytest.mark.parametrize('use_sim_time', [False, True])
def test_humble_readiness_waits_for_parameter_reply_and_always_closes_probe(monkeypatch, use_sim_time):
    state = {'spins': 0, 'calls': 0, 'context_ok': False, 'destroyed': False, 'shutdown': False}

    class Context:
        def ok(self):
            return state['context_ok']

        def shutdown(self):
            state['context_ok'] = False

    class Client:
        def service_is_ready(self):
            return True

        def call_async(self, request):
            assert request.names == ['use_sim_time']
            state['calls'] += 1
            return NS(done=lambda: state['spins'] >= 3, result=lambda: NS(
                values=[NS(type=1, bool_value=use_sim_time)]))

    class Node:
        def create_client(self, _kind, name):
            assert name == '/test_recorder/get_parameters'
            return Client()

        def destroy_node(self):
            state['destroyed'] = True

    class Executor:
        def __init__(self, *, context):
            assert context.ok()

        def add_node(self, node):
            assert isinstance(node, Node)

        def spin_once(self, *, timeout_sec):
            state['spins'] += 1

        def shutdown(self):
            state['shutdown'] = True

    def init(**kwargs):
        assert kwargs['signal_handler_options'] == 'no_handlers'
        state['context_ok'] = True

    monkeypatch.setitem(sys.modules, 'rclpy', NS(init=init, create_node=lambda *_a, **_kw: Node()))
    monkeypatch.setitem(sys.modules, 'rclpy.context', NS(Context=Context))
    monkeypatch.setitem(sys.modules, 'rclpy.executors', NS(SingleThreadedExecutor=Executor))
    monkeypatch.setitem(sys.modules, 'rclpy.signals', NS(SignalHandlerOptions=NS(NO='no_handlers')))
    monkeypatch.setitem(sys.modules, 'rcl_interfaces.msg', NS(ParameterType=NS(PARAMETER_BOOL=1)))
    monkeypatch.setitem(sys.modules, 'rcl_interfaces.srv', NS(GetParameters=NS(Request=lambda **kw: NS(**kw))))
    if use_sim_time:
        with pytest.raises(RuntimeError, match='system clock'):
            native_recorder._wait_for_humble_ready('test_recorder', lambda: None)
    else:
        native_recorder._wait_for_humble_ready('test_recorder', lambda: None)
    assert state['spins'] == 3 and state['calls'] == 1
    assert state['shutdown'] and state['destroyed'] and not state['context_ok']


@pytest.mark.parametrize('accepts', [True, False])
def test_explicit_read_order_reports_storage_acceptance(accepts):
    calls = []
    reader = NS(set_read_order=lambda value: calls.append(value) or accepts)
    module = NS(ReadOrder=lambda kind, reverse: (kind, reverse),
                ReadOrderSortBy=NS(ReceivedTimestamp='receipt'))
    assert request_read_order(reader, module, 'ReceivedTimestamp') is accepts
    assert calls == [('receipt', False)]


def test_humble_topic_metadata_constructor_does_not_receive_id():
    class Metadata:
        def __init__(self, *, name, type, serialization_format):
            self.name, self.type, self.serialization_format = name, type, serialization_format

    value = topic_metadata(NS(TopicMetadata=Metadata), name='/data', type_name='std_msgs/msg/String',
                           topic_id=12)
    assert value.name == '/data' and value.serialization_format == 'cdr'
