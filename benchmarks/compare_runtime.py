#!/usr/bin/env python3
"""Run the two mock runtimes with matched settings and counterbalanced order."""
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', type=Path, required=True)
parser.add_argument('--native', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--repeats', type=int, default=2)
parser.add_argument('--domain', type=int, default=194)
args = parser.parse_args()
if args.repeats < 1:
    parser.error('repeats must be positive')
args.output.mkdir(parents=True, exist_ok=False)
report = {'environment': {'platform': platform.platform(), 'python': sys.version,
                         'ros_distro': os.environ.get('ROS_DISTRO')},
          'method': 'Matched mock fixture; 50 warmup commands, fixed 3s drain, alternating order; direct runtime process CPU/RSS. Inspect missing/rejected before comparing received-only latency.',
          'runs': []}
failed = False
for rate, samples in ((100, 500), (20, 200)):
    for repeat in range(args.repeats):
        order = ('python', 'cpp') if repeat % 2 == 0 else ('cpp', 'python')
        for name in order:
            executable = args.baseline if name == 'python' else args.native
            command = [sys.executable, str(Path(__file__).with_name('latency_roundtrip.py')),
                       str(rate), str(samples), '--executable', str(executable.resolve()),
                       '--domain', str(args.domain)]
            if name == 'cpp':
                command.append('--native')
            started = time.monotonic()
            record = {'implementation': name, 'rate_hz': rate, 'repeat': repeat + 1,
                      'load_start': os.getloadavg()}
            process = subprocess.run(command, capture_output=True, text=True, timeout=90)
            record.update(exit_code=process.returncode, elapsed_s=time.monotonic()-started,
                          load_end=os.getloadavg())
            stem = f'{name}-{rate}-{repeat+1}'
            (args.output / (stem + '.stderr')).write_text(process.stderr)
            try:
                record['result'] = json.loads(process.stdout)
            except json.JSONDecodeError:
                (args.output / (stem + '.stdout')).write_text(process.stdout)
                failed = True
            failed = failed or process.returncode != 0
            report['runs'].append(record)
            (args.output / 'result.json').write_text(json.dumps(report, indent=2)+'\n')
            result = record.get('result', {})
            print(stem, 'exit', process.returncode, 'received', result.get('samples_received'),
                  'missing', result.get('missing'), 'p95_us', result.get('p95'), flush=True)
raise SystemExit(1 if failed else 0)
