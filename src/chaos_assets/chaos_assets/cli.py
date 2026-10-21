"""Compatibility shim for the ``asset_config`` command line entry point.

The installed command is the native ``asset_config`` executable; this module
keeps ``python -m chaos_assets.cli`` and the historical ``main`` callable
working. Argument handling and output formatting stay here, every
validation is performed by the native engine.
"""

import argparse
import sys

from ._chaos_assets_core import ConfigurationError
from ._chaos_assets_core import resolve_inventory_json


def main(argv=None):
    parser = argparse.ArgumentParser(description='Validate a configuration and print it.')
    parser.add_argument('inventory')
    parser.add_argument('--host')
    parser.add_argument('--mode', choices=('mock', 'real'))
    parser.add_argument('--output', help='Write resolved JSON instead of stdout')
    args = parser.parse_args(argv)
    try:
        content = resolve_inventory_json(args.inventory, args.mode or '', args.host or '')
    except ConfigurationError as error:
        print(f'configuration error: {error}', file=sys.stderr)
        return 2
    if args.output:
        try:
            with open(args.output, 'w', encoding='utf-8') as stream:
                stream.write(content)
        except OSError as error:
            print(f'configuration error: {error}', file=sys.stderr)
            return 2
    else:
        print(content, end='')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
