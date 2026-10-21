#!/usr/bin/env python3
"""Expand one Xacro file with the third-party xacro tool.

This is a bridge, not a business component. It forwards a prepared request
to the ``xacro`` package and writes the expanded document to stdout. Every
policy decision - file size, deadline, output bound, entity rejection and
the resulting digest - belongs to the native C++ caller.

The request is a JSON document with the fields ``file``, ``source`` and
``arguments``; it is passed as a file path so that the bridge never needs
standard input and the parent process cannot block on a pipe.
"""

import json
import os
import re
import stat
import sys

MAX_INCLUDE_BYTES = 1024 * 1024
DECLARATION_PATTERN = re.compile(r"<!\s*(?:DOCTYPE|ENTITY)\b", re.IGNORECASE)


def read_include(filename, xacro_module):
    """Read one xacro include, rejecting anything but a small regular file.

    A missing file is reported the way the xacro package expects it: the
    file stack is restored and the error is wrapped in a ``XacroException``.
    The xacro package catches that exception for an optional include, so a
    missing optional include keeps the relative include stack of the parent
    and does not abort the expansion.
    """
    try:
        descriptor = os.open(filename, os.O_RDONLY | os.O_NONBLOCK)
    except OSError as error:
        if xacro_module.filestack:
            xacro_module.filestack.pop()
        detail = error.strerror or str(error)
        raise xacro_module.XacroException(
            detail + ": " + str(error.filename or filename), exc=error) from error
    try:
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            raise ValueError("Xacro include must be a regular file")
        with os.fdopen(descriptor, "rb", closefd=False) as stream:
            content = stream.read(MAX_INCLUDE_BYTES + 1)
    finally:
        os.close(descriptor)
    if len(content) > MAX_INCLUDE_BYTES:
        raise ValueError("Xacro include exceeds 1 MiB")
    return content.decode("utf-8")


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: xacro_expand.py REQUEST.json\n")
        return 2
    with open(argv[1], "r", encoding="utf-8") as stream:
        request = json.load(stream)
    try:
        import xacro
    except ImportError:
        sys.stderr.write(
            "Xacro requires the xacro package; install ros-jazzy-xacro and "
            "source the ROS environment\n")
        return 2

    original_parse = xacro.parse

    def checked_parse(inp, filename=None):
        if inp is None:
            inp = read_include(filename, xacro)
        if isinstance(inp, str) and DECLARATION_PATTERN.search(inp):
            raise ValueError("DOCTYPE and ENTITY declarations are not allowed")
        return original_parse(inp, filename)

    xacro.parse = checked_parse
    xacro.init_stacks(request["file"])
    document = xacro.parse(request["source"], request["file"])
    xacro.process_doc(document, mappings=request["arguments"])
    sys.stdout.write(document.toxml())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
