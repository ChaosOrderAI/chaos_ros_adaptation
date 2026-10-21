"""The no-hardware interface of the runtime.

The runtime owns the validation of an asset and the detection of its native
backends. This module is the thin bridge to the native implementation: the
command line interfaces of the runtime are executed under a deadline and with
a bounded output, so a stuck or chatty runtime cannot block the startup.
"""

from ._chaos_bringup_core import (
    native_backend_available as _native_backend_available,
    validate_asset_with_runtime as _validate_asset_with_runtime,
)


def validate_asset(asset):
    """Validate one asset with the runtime.

    The asset document is written to the standard input of
    ``robot_runtime --validate-asset``, so the runtime applies its own rules
    for the plugin, the capabilities and the watchdog of the asset.

    Raises ConfigurationError when the runtime rejects the asset.
    """
    _validate_asset_with_runtime(asset)


def native_backend_available(plugin):
    """Whether the runtime provides the native backend of a driver plugin.

    The answer comes from ``go2_driver --build-capabilities``, which is
    executed before any ROS node or hardware channel is opened. A runtime
    that cannot be reached denies the backend.
    """
    return bool(_native_backend_available(plugin))
