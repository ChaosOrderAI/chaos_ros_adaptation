"""Bounded, read-only source timestamp audits for ROS messages and rosbag2.

The difference between bag reception time and a message timestamp includes
clock domain differences, driver buffering, transport and scheduling. It is
not a measurement of device clock offset or proof of physical synchronisation.
Original messages and timestamps are never changed, paired or interpolated.
"""

import argparse
from contextlib import ExitStack
from dataclasses import asdict, dataclass, field
import json
import math
from pathlib import Path
import sys


_MISSING = object()
_NS = 1_000_000_000


def _member(value, key, default=_MISSING):
    if isinstance(value, dict):
        return value.get(key, default)
    return getattr(value, key, default)


def _stamp_ns(stamp) -> tuple[int | None, str]:
    sec, nanosec = _member(stamp, 'sec'), _member(stamp, 'nanosec')
    if (type(sec) is not int or type(nanosec) is not int
            or not 0 <= sec < 2**31 or not 0 <= nanosec < _NS):
        return None, 'invalid'
    value = sec * _NS + nanosec
    if value == 0:
        return None, 'zero'
    return value, 'valid'


def source_stamp(message, type_name: str = '') -> tuple[int | None, str, str | None]:
    """Return (nanoseconds, category, frame); never pick the first TF stamp."""
    if type_name == 'tf2_msgs/msg/TFMessage' or type(message).__name__ == 'TFMessage':
        return None, 'multi_stamp_excluded', None
    header = _member(message, 'header')
    if header is not _MISSING:
        frame = _member(header, 'frame_id', None)
        frame = frame if isinstance(frame, str) else None
        stamp = _member(header, 'stamp')
        if stamp is _MISSING:
            return None, 'invalid', frame
    else:
        frame = _member(message, 'frame_id', None)
        frame = frame if isinstance(frame, str) else None
        stamp = _member(message, 'stamp')
        if stamp is _MISSING:
            return None, 'missing', None
    nanoseconds, category = _stamp_ns(stamp)
    return nanoseconds, category, frame


@dataclass
class _Topic:
    type_name: str = ''
    messages: int = 0
    valid_source_stamps: int = 0
    missing_source_stamps: int = 0
    invalid_source_stamps: int = 0
    zero_source_stamps: int = 0
    excluded_multi_stamp_messages: int = 0
    decode_errors: int = 0
    invalid_receipt_stamps: int = 0
    source_reversals: int = 0
    source_duplicates: int = 0
    receipt_reversals: int = 0
    receipt_duplicates: int = 0
    epoch_changes: int = 0
    type_changes: int = 0
    first_source_ns: int | None = None
    last_source_ns: int | None = None
    first_receipt_ns: int | None = None
    last_receipt_ns: int | None = None
    difference_samples: int = 0
    difference_min_ns: int | None = None
    difference_max_ns: int | None = None
    difference_abs_exceeded: int = 0
    source_ahead_of_receipt: int = 0
    frames: dict[str, int] = field(default_factory=dict)
    frames_not_retained: int = 0
    _difference_sum: int = 0
    _last_source_in_epoch: int | None = None
    _last_receipt_in_epoch: int | None = None
    _epoch: int | None = None


class AuditTracker:
    """Streaming audit, O(max_topics * max_frames_per_topic) report memory.

    Caller supplied epochs must refer to recording-clock epochs. A bag reader
    cannot reconstruct wall-clock steps from reception timestamps alone.
    """

    def __init__(
        self, *, max_topics: int = 256, max_frames_per_topic: int = 8,
        max_difference_s: float = 0.1,
    ):
        if type(max_topics) is not int or not 1 <= max_topics <= 4096:
            raise ValueError('max_topics must be an integer in [1, 4096]')
        if type(max_frames_per_topic) is not int or not 1 <= max_frames_per_topic <= 256:
            raise ValueError('max_frames_per_topic must be an integer in [1, 256]')
        if (isinstance(max_difference_s, bool) or not math.isfinite(max_difference_s)
                or max_difference_s <= 0):
            raise ValueError('max_difference_s must be finite and positive')
        self.max_topics = max_topics
        self.max_frames_per_topic = max_frames_per_topic
        self.max_difference_ns = round(max_difference_s * _NS)
        self._topics: dict[str, _Topic] = {}
        self.messages = 0
        self.topic_limit_dropped_messages = 0

    def _get(self, topic: str, type_name: str, epoch: int) -> _Topic | None:
        if not isinstance(topic, str) or not topic or len(topic) > 1024:
            raise ValueError('topic must be nonempty and at most 1024 characters')
        if type(epoch) is not int or epoch < 0:
            raise ValueError('epoch must be a nonnegative integer')
        if not isinstance(type_name, str) or len(type_name) > 1024:
            raise ValueError('invalid type_name')
        self.messages += 1
        if topic not in self._topics:
            if len(self._topics) >= self.max_topics:
                self.topic_limit_dropped_messages += 1
                return None
            self._topics[topic] = _Topic(type_name=type_name)
        stats = self._topics[topic]
        stats.messages += 1
        if stats.type_name != type_name:
            stats.type_changes += 1
        if stats._epoch != epoch:
            if stats._epoch is not None:
                stats.epoch_changes += 1
            stats._epoch = epoch
            stats._last_source_in_epoch = None
            stats._last_receipt_in_epoch = None
        return stats

    @staticmethod
    def _receipt(stats: _Topic, receipt_ns: int) -> bool:
        if type(receipt_ns) is not int or not 0 <= receipt_ns < 2**63:
            stats.invalid_receipt_stamps += 1
            return False
        if stats._last_receipt_in_epoch is not None:
            stats.receipt_reversals += receipt_ns < stats._last_receipt_in_epoch
            stats.receipt_duplicates += receipt_ns == stats._last_receipt_in_epoch
        stats._last_receipt_in_epoch = receipt_ns
        if stats.first_receipt_ns is None:
            stats.first_receipt_ns = receipt_ns
        stats.last_receipt_ns = receipt_ns
        return True

    def observe(
        self, topic: str, message, receipt_ns: int, *, type_name: str = '', epoch: int = 0,
    ) -> None:
        stats = self._get(topic, type_name, epoch)
        if stats is None:
            return
        receipt_valid = self._receipt(stats, receipt_ns)
        stamp, category, frame = source_stamp(message, type_name)
        if frame is not None:
            # A corrupted/custom frame string must not make the report grow.
            if len(frame) <= 256 and (frame in stats.frames or len(stats.frames) < self.max_frames_per_topic):
                stats.frames[frame] = stats.frames.get(frame, 0) + 1
            else:
                stats.frames_not_retained += 1
        if category == 'missing':
            stats.missing_source_stamps += 1
        elif category == 'invalid':
            stats.invalid_source_stamps += 1
        elif category == 'zero':
            stats.zero_source_stamps += 1
        elif category == 'multi_stamp_excluded':
            stats.excluded_multi_stamp_messages += 1
        else:
            stats.valid_source_stamps += 1
            if stats._last_source_in_epoch is not None:
                stats.source_reversals += stamp < stats._last_source_in_epoch
                stats.source_duplicates += stamp == stats._last_source_in_epoch
            stats._last_source_in_epoch = stamp
            if stats.first_source_ns is None:
                stats.first_source_ns = stamp
            stats.last_source_ns = stamp
            if receipt_valid:
                delta = receipt_ns - stamp
                stats.difference_samples += 1
                stats._difference_sum += delta
                stats.difference_min_ns = delta if stats.difference_min_ns is None else min(stats.difference_min_ns, delta)
                stats.difference_max_ns = delta if stats.difference_max_ns is None else max(stats.difference_max_ns, delta)
                stats.difference_abs_exceeded += abs(delta) > self.max_difference_ns
                stats.source_ahead_of_receipt += delta < 0

    def observe_decode_error(
        self, topic: str, receipt_ns: int, *, type_name: str = '', epoch: int = 0,
    ) -> None:
        stats = self._get(topic, type_name, epoch)
        if stats is not None:
            stats.decode_errors += 1
            self._receipt(stats, receipt_ns)

    def snapshot(self) -> dict:
        topics = {}
        for name, stats in sorted(self._topics.items()):
            values = {key: value for key, value in asdict(stats).items() if not key.startswith('_')}
            values['difference_mean_ns'] = (
                stats._difference_sum / stats.difference_samples if stats.difference_samples else None
            )
            topics[name] = values
        return {
            'schema_version': 1,
            'timestamp_difference': 'receipt_minus_source; not device clock offset',
            'source_clock_domain': 'unverified; source and recorder clocks may differ',
            'epoch_policy': 'caller supplied; no correction or cross-epoch pairing',
            'max_difference_ns': self.max_difference_ns,
            'messages': self.messages,
            'topic_limit_dropped_messages': self.topic_limit_dropped_messages,
            'topics': topics,
        }


def audit_bag(
    path: str | Path, *, storage_id: str = '', max_topics: int = 256,
    max_frames_per_topic: int = 8, max_difference_s: float = 0.1,
    max_messages: int | None = None, index: str | Path | None = None,
) -> dict:
    """Read an existing MCAP/SQLite bag without publishing or modifying it.

    Jazzy read_next() exposes the recorder's receive timestamp, not a source
    timestamp; source stamps are read from each original deserialized payload.
    """
    if storage_id not in ('', 'mcap', 'sqlite3'):
        raise ValueError('storage_id must be mcap, sqlite3, or empty for detection')
    if max_messages is not None and (type(max_messages) is not int or max_messages < 1):
        raise ValueError('max_messages must be a positive integer')
    bag_path = Path(path).expanduser().resolve(strict=True)
    index_path = Path(index).expanduser().resolve() if index is not None else None
    if index_path is not None:
        if index_path == bag_path or (bag_path.is_dir() and index_path.is_relative_to(bag_path)):
            raise ValueError('index must be outside the source bag')
    tracker = AuditTracker(
        max_topics=max_topics, max_frames_per_topic=max_frames_per_topic,
        max_difference_s=max_difference_s,
    )
    import rosbag2_py
    from rclpy.serialization import deserialize_message
    from rosidl_runtime_py.utilities import get_message

    reader = rosbag2_py.SequentialReader()
    reader.open(
        rosbag2_py.StorageOptions(uri=str(bag_path), storage_id=storage_id),
        rosbag2_py.ConverterOptions('', ''),
    )
    with ExitStack() as resources:
        resources.callback(reader.close)
        index_file = resources.enter_context(index_path.open('x', encoding='utf-8')) if index_path else None
        metadata = reader.get_all_topics_and_types()
        type_map = {item.name: item.type for item in metadata[:max_topics]}
        format_map = {item.name: item.serialization_format for item in metadata[:max_topics]}
        classes = {}
        for name, type_name in type_map.items():
            try:
                classes[name] = get_message(type_name) if format_map[name] == 'cdr' else None
            except (ImportError, AttributeError, ValueError):
                classes[name] = None
        # A storage plugin may sort by receive time, hiding original arrival
        # reversals. Request file order and report whether it was supported.
        try:
            file_order = reader.set_read_order(
                rosbag2_py.ReadOrder(rosbag2_py.ReadOrderSortBy.File, False)
            )
        except RuntimeError:
            file_order = False
        count = 0
        while reader.has_next() and (max_messages is None or count < max_messages):
            topic, payload, receipt_ns = reader.read_next()
            count += 1
            source_ns, stamp_kind, frame = None, 'topic_limit_excluded', None
            if topic not in type_map:
                tracker.messages += 1
                tracker.topic_limit_dropped_messages += 1
            else:
                message_class = classes[topic]
                message = None
                if message_class is not None:
                    try:
                        message = deserialize_message(payload, message_class)
                    except Exception:  # Per-message decoding errors stay visible.
                        pass
                if message is None:
                    tracker.observe_decode_error(topic, receipt_ns, type_name=type_map[topic])
                    stamp_kind = 'decode_error'
                else:
                    tracker.observe(topic, message, receipt_ns, type_name=type_map[topic])
                    source_ns, stamp_kind, frame = source_stamp(message, type_map[topic])
            if index_file is not None:
                index_file.write(json.dumps({
                    'sequence': count, 'topic': topic, 'type': type_map.get(topic),
                    'receive_ns': receipt_ns, 'source_ns': source_ns,
                    'stamp_kind': stamp_kind,
                    'frame': frame if frame is None or len(frame) <= 256 else None,
                    'clock_epoch': None,
                }, ensure_ascii=False, allow_nan=False) + '\n')
        report = tracker.snapshot()
        report.update({
            'bag_path': str(bag_path),
            'storage_id': reader.get_metadata().storage_identifier,
            'reader_order': 'file' if file_order else 'storage_default',
            'complete': not reader.has_next(),
            'clock_epoch_evidence': 'unavailable from bag receive timestamps alone; consult session manifest and time_sync.jsonl',
            'modified_messages': 0,
            'index_path': str(index_path) if index_path else None,
        })
        return report


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bag_path')
    parser.add_argument('--storage-id', choices=('mcap', 'sqlite3'), default='')
    parser.add_argument('--max-difference-s', type=float, default=0.1)
    parser.add_argument('--max-topics', type=int, default=256)
    parser.add_argument('--max-messages', type=int)
    parser.add_argument('--index', help='Create a new metadata-only JSONL index outside the input bag')
    parser.add_argument('--output', help='Create a new JSON report file (default: stdout)')
    args = parser.parse_args(argv)
    try:
        report = audit_bag(
            args.bag_path, storage_id=args.storage_id,
            max_difference_s=args.max_difference_s,
            max_topics=args.max_topics, max_messages=args.max_messages,
            index=args.index,
        )
        output = json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False)
        if args.output:
            output_path = Path(args.output).expanduser().resolve()
            bag_path = Path(args.bag_path).expanduser().resolve()
            if output_path == bag_path or (bag_path.is_dir() and output_path.is_relative_to(bag_path)):
                raise ValueError('output report must be outside the source bag')
            with output_path.open('x', encoding='utf-8') as report_file:
                report_file.write(output + '\n')
        else:
            print(output)
    except Exception as exc:
        print(f'Cannot audit bag: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
