"""The no-hardware interface of the runtime.

The runtime owns the validation of an asset. This module is the thin bridge
to the native implementation: the command line interface is executed under a
deadline and with a bounded output, so a stuck or chatty runtime cannot block
the startup.
"""

from ._chaos_bringup_core import (
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
