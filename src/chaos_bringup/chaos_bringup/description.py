"""Launch robot state publishers from the validated, frozen URDF snapshot.

The description derivation, the summary and the dependency check are
performed by the native planner (``chaos_bringup._chaos_bringup_core``). This
module only turns a native process specification into a ROS action.
"""

from ._chaos_bringup_core import description_processes as _description_processes


def description_processes(asset, environment):
    """Return supervised publishers; never read or expand the source file again."""
    from .nodes import node_action

    return [(spec['label'], node_action(spec, environment))
            for spec in _description_processes(asset)]
