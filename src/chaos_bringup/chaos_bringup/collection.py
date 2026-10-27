"""Launch optional data collection without giving it authority over robot control.

The recording policy derivation, the summary, the output directory check and
the storage plugin check are performed by the native planner
(``chaos_bringup._chaos_bringup_core``). This module only turns a native
process specification into a ROS action.
"""

import launch.logging

from chaos_assets import ConfigurationError  # noqa: F401  (shared error type)

from ._chaos_bringup_core import (
    check_output_directory as _check_output_directory,
    collection_processes as _collection_processes,
    collection_summary as _collection_summary,
)


def collection_summary(asset):
    """The recording policy summary of an asset."""
    return _collection_summary(asset)


def registered_writers():
    """Inspect plugin registration; never construct a writer or a ROS node.

    The list of registered storage plugins is a property of the build of
    ``rosbag2_py``; the native planner receives it as an injected capability.
    """
    try:
        from rosbag2_py import get_registered_writers
        return set(get_registered_writers())
    except (ImportError, RuntimeError, OSError) as error:
        raise ConfigurationError(
            'data collection needs rosbag2_py and a working rosbag2 storage plugin') from error


def check_output_directory(path):
    """Check the nearest existing ancestor without creating any filesystem entry."""
    return _check_output_directory(path)


def collection_processes(asset, environment):
    """Return (label, process) pairs for the enabled recorder."""
    from .nodes import node_action

    # Allow a signal during an in-flight probe/startup as well as the native
    # stop/flush deadline (5 + 15 + 5 + 10 seconds, plus child cleanup).
    return [(spec['label'], node_action(spec, environment, sigterm_timeout='40.0',
                                         sigkill_timeout='5.0'))
            for spec in _collection_processes(asset)]


def on_collection_exit(label):
    """Report an auxiliary recorder failure while robot control continues running."""
    def callback(event, context):
        if context.is_shutdown:
            return []
        launch.logging.get_logger('launch.chaos_data').error(
            f'{label} exited with code {event.returncode}; data collection has stopped. '
            'Robot runtime continues; inspect the collection log and session manifest.')
        return []
    return callback
