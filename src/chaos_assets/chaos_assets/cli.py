"""Read-only config validation / deterministic resolved inventory output."""

import argparse
import json
from pathlib import Path
import sys

from .inventory import ConfigurationError, load_inventory


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inventory')
    parser.add_argument('--host')
    parser.add_argument('--mode', choices=('mock', 'real'))
    parser.add_argument('--output', help='Write resolved JSON instead of stdout')
    args = parser.parse_args(argv)
    try:
        inventory = load_inventory(args.inventory, mode=args.mode)
        result = inventory.as_dict()
        if args.host:
            result['assets'] = inventory.select(args.host)
        content = json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False) + '\n'
        if args.output:
            Path(args.output).write_text(content, encoding='utf-8')
        else:
            print(content, end='')
    except (ConfigurationError, OSError) as error:
        print(f'configuration error: {error}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
