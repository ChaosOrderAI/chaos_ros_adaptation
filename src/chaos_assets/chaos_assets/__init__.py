"""Asset configuration is usable without importing ROS or a vendor SDK."""

from .inventory import ConfigurationError, Inventory, load_inventory

__all__ = ['ConfigurationError', 'Inventory', 'load_inventory']
