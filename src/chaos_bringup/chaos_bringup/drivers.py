"""Supervise hardware bridges that are part of this workspace.

The driver derivation is performed by the native planner
(``chaos_bringup._chaos_bringup_core``). This module only turns a native
process specification into a ROS action.
"""

from ._chaos_bringup_core import driver_processes as _driver_processes


def _backend_available(plugin):
    """Ask the runtime whether it provides the native backend of a plugin.

    The answer comes from the capability report of the runtime, which is
    executed before any ROS node or hardware channel is opened. A runtime
    that cannot be reached denies the backend, so a real asset fails the
    preflight instead of starting without its driver.
    """
    from .planning import native_backend_available

    return bool(native_backend_available(plugin))


def driver_processes(asset, environment):
    """Return (label, process) pairs for the managed hardware bridge."""
    from .nodes import node_action

    return [(spec['label'], node_action(spec, environment))
            for spec in _driver_processes(asset)]
