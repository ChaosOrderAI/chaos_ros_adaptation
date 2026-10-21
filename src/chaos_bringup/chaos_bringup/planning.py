"""Prepare all selected assets before any subprocess can start.

The planner itself is native (``chaos_bringup._chaos_bringup_core``): the
selection of the assets, the collision rules, the MQTT identity and the
preflight checks are performed by the C++ implementation. This module only
forwards the request, wraps the resulting snapshot and injects the MQTT
adapter configuration producer of ``chaos_mqtt``; the adapter configuration
format is owned by that package.
"""

import copy

from chaos_assets import ConfigurationError  # noqa: F401  (shared error type)

from ._chaos_bringup_core import (  # noqa: F401
    collection_processes,
    collection_summary,
    component_asset,
    description_processes,
    description_summary,
    driver_processes,
    native_backend_available,
    plan_summary,
    preflight as _preflight,
    prepare_edge_plan as _prepare_edge_plan,
    prepare_plan as _prepare_plan,
    resolve_mqtt_executable as _resolve_mqtt_executable,
    sensor_processes,
    sensor_summary,
    snapshot_parameter_files,
    validate_asset_with_runtime,
)


class LaunchPlan:
    """An immutable startup snapshot; each access returns detached data.

    The content of the plan is produced by the native planner. This wrapper
    only keeps the snapshot immutable for the Python callers: every access
    returns a copy, so a caller can never modify a checked plan through a
    nested document. The launch never reloads an input file.
    """

    def __init__(self, items):
        self._items = [copy.deepcopy(item) for item in items]

    @classmethod
    def from_items(cls, items):
        """Freeze the given items into a plan."""
        return cls(items)

    def __len__(self):
        return len(self._items)

    def __getitem__(self, index):
        if isinstance(index, slice):
            return [copy.deepcopy(item) for item in self._items[index]]
        return copy.deepcopy(self._items[index])

    def __iter__(self):
        return iter(copy.deepcopy(self._items))

    def __repr__(self):
        return f'LaunchPlan({len(self._items)} items)'


def _mqtt_adapter_config(asset, transport_profile):
    """Build the MQTT adapter configuration of one asset.

    The generator is the native implementation of ``chaos_mqtt``; the
    planner only consumes its result to validate the MQTT identity.
    """
    from chaos_mqtt.config import build_adapter_config

    return build_adapter_config(asset, transport_profile)


def prepare_plan(inventory_path, host_id, mode=None, asset_ids=None, enable_mqtt=False,
                 check_files=True, mqtt_config=None):
    """Validate and freeze the exact inventory already selected by the caller.

    The MQTT adapter configuration producer is injected, so the planner stays
    independent of the transport implementation and a caller can supply its
    own producer.
    """
    return LaunchPlan(_prepare_plan(inventory_path, host_id, mode or '', list(asset_ids or []),
                                     enable_mqtt, check_files,
                                     mqtt_config or _mqtt_adapter_config))


def prepare_edge_plan(inventory_path, asset_id, enable_mqtt=True, check_files=True,
                      mqtt_config=None):
    """Onboard deployment is explicitly bound to one real asset on one host.

    The developer-facing system launch remains available for multi-asset/mock
    configurations. Edge startup never silently promotes a mock asset to real.
    """
    return LaunchPlan(_prepare_edge_plan(inventory_path, asset_id, enable_mqtt, check_files,
                                          mqtt_config or _mqtt_adapter_config))


def preflight(plan, registered_writers=(), backend_available=None, executable_available=None,
              validate_asset=None, check_files=True):
    """Check the whole selection before any managed process is started.

    The capabilities of the build are injected: the registered storage
    writers, the native driver backends, the available ROS executables and
    the validation of the runtime. The native planner never probes a vendor
    SDK itself.
    """
    _preflight(list(plan), list(registered_writers), backend_available, executable_available,
               validate_asset, check_files)


def resolve_mqtt_executable(executable='mqtt_transport', prefix_resolver=None):
    """Resolve the MQTT transport executable of the sourced installation.

    The former default names are resolved through the package index of
    ``chaos_mqtt``; an explicit path or another name stays a valid override.
    """
    return _resolve_mqtt_executable(executable, prefix_resolver)
