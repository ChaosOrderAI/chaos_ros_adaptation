"""Thin binding over the native C++ adapter-config generator.

``build_adapter_config`` is compiled C++ (chaos_mqtt::build_adapter_config);
this module adds no business logic of its own.
"""

from chaos_mqtt._chaos_mqtt_core import build_adapter_config

__all__ = ["build_adapter_config"]
