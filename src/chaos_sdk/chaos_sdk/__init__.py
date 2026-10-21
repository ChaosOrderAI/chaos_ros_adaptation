"""Thin Python binding for the native C++ chaos_sdk client.

The Python package contains no business logic: leases, TTLs, capability
discovery, timeouts and the ROS client all live in the native library. The
binding only converts between Python and C++ values.
"""
from chaos_sdk._chaos_sdk import Future, Robot, Task

__all__ = ['Future', 'Robot', 'Task']
