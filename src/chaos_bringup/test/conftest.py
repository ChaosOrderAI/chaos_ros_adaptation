"""Shared fixtures of the launch behaviour tests.

The preflight of the native planner asks the build for its capabilities
instead of probing it. A test therefore states what exists in the build it
simulates: the ROS executables, the storage plugins, the native driver
backends and the validation of the runtime.

The default is a build in which everything is available, so a test only has
to state the deviation it wants to observe.
"""

import pytest


@pytest.fixture
def capabilities():
    """The capabilities of a build in which everything is available."""
    return {
        'registered_writers': ['mcap', 'sqlite3'],
        'backend_available': lambda plugin: True,
        'executable_available': lambda package, executable: True,
        'validate_asset': lambda asset: None,
    }


@pytest.fixture
def checked_executables():
    """The executables that the preflight asked for."""
    asked = []

    def available(package, executable):
        asked.append((package, executable))
        return True

    available.asked = asked
    return available
