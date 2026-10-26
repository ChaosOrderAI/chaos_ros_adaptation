"""Discovery settings shared by launch and installed-workspace smoke clients."""

import os


DISCOVERY_VARIABLES = (
    'ROS_LOCALHOST_ONLY', 'ROS_AUTOMATIC_DISCOVERY_RANGE', 'ROS_STATIC_PEERS',
)


def discovery_range(value):
    value = value.upper()
    if value not in ('LOCALHOST', 'SUBNET'):
        raise ValueError('discovery_range must be LOCALHOST or SUBNET')
    return value


def discovery_environment(value, *, ros_distro=None):
    """Return the selected policy in variables understood by the sourced ROS.

    Humble needs ROS_LOCALHOST_ONLY. Keep the newer Jazzy policy where it is
    available; the legacy variable is also the conservative compatibility
    default when ROS_DISTRO is missing or names another distribution.
    """
    value = discovery_range(value)
    distro = os.environ.get('ROS_DISTRO', 'humble') if ros_distro is None else ros_distro
    if distro.lower() == 'jazzy':
        return {'ROS_AUTOMATIC_DISCOVERY_RANGE': value}
    return {'ROS_LOCALHOST_ONLY': '1' if value == 'LOCALHOST' else '0'}


def configure_discovery(value):
    """Apply a policy before initializing ROS, removing inherited overrides.

    Static peers can reach other hosts even with Jazzy LOCALHOST discovery.
    These startup paths expose LOCALHOST/SUBNET, not a static-peer policy.
    """
    settings = discovery_environment(value)
    for name in DISCOVERY_VARIABLES:
        os.environ.pop(name, None)
    os.environ.update(settings)
