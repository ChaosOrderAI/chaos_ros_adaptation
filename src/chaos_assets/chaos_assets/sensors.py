"""Compatibility shim over the native sensor resolver.

The implementation lives in ``chaos_assets._chaos_assets_core``; this module
only keeps the historical import path alive.
"""

from ._chaos_assets_core import ConfigurationError
from ._chaos_assets_core import sensors as _native

resolve_sensors = _native.resolve_sensors
validate_sensor_graph = _native.validate_sensor_graph
resolve_topic = _native.resolve_topic

__all__ = [
    'ConfigurationError', 'resolve_sensors', 'validate_sensor_graph', 'resolve_topic',
]
