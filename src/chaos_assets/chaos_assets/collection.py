"""Compatibility shim over the native recording policy resolver.

The implementation lives in ``chaos_assets._chaos_assets_core``; this module
only keeps the historical import path alive.
"""

from ._chaos_assets_core import ConfigurationError
from ._chaos_assets_core import collection as _native

resolve_collection = _native.resolve_collection

__all__ = ['ConfigurationError', 'resolve_collection']
