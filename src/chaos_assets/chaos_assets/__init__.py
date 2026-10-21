"""Asset configuration is usable without importing ROS or a vendor SDK.

The package is a thin binding over the native C++ configuration engine in
``chaos_assets._chaos_assets_core``: every schema check, merge, path
resolution, description validation and digest is performed by the compiled
library. The modules below only re-export that engine under the original
import paths so that the launch glue and the test suite keep working.
"""

from ._chaos_assets_core import ConfigurationError, Inventory, load_inventory

__all__ = ['ConfigurationError', 'Inventory', 'load_inventory']
