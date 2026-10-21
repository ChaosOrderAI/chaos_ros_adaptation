"""Unified MQTT transport and native C++ command gateway.

Business logic (protocol validation, correlation, adapter-config generation)
is implemented in C++ and imported from the compiled ``_chaos_mqtt_core``
extension; this package only exposes the thin Python facade used by launch
planning and smoke tests.
"""

from chaos_mqtt import config

__all__ = ["config"]
