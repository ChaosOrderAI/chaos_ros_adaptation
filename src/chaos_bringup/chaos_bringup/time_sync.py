"""Sync board client and camera trigger pairing.

The derivation is native (``chaos_bringup._chaos_bringup_core``): the planner
attaches the host board configuration to every selected real plan item and
marks exactly one item per host as the board owner. This module only turns the
process specifications into ROS actions.
"""

from ._chaos_bringup_core import time_sync_processes as _time_sync_processes


def time_sync_processes(item, environment):
    """Return (label, process) pairs for the board client and trigger pairing."""
    from .nodes import node_action

    return [(spec['label'], node_action(spec, environment))
            for spec in _time_sync_processes(item)]
