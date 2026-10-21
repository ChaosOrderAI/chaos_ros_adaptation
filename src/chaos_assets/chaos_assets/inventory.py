"""Compatibility shim over the native inventory engine.

The implementation lives in ``chaos_assets._chaos_assets_core``; this module
only keeps the historical import path alive.
"""

from ._chaos_assets_core import ConfigurationError, Inventory, load_inventory

__all__ = ['ConfigurationError', 'Inventory', 'load_inventory']
