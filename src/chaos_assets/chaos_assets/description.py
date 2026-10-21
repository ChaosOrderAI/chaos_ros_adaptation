"""Compatibility shim over the native robot description resolver.

The implementation lives in ``chaos_assets._chaos_assets_core``; this module
only keeps the historical import path alive.
"""

from ._chaos_assets_core import ConfigurationError
from ._chaos_assets_core import description as _native

resolve_description = _native.resolve_description
validate_description_graph = _native.validate_description_graph

__all__ = ['ConfigurationError', 'resolve_description', 'validate_description_graph']
