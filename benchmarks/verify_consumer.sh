#!/usr/bin/env bash
# Verify public headers and exported targets from a sourced install space.
# Usage: source <install>/local_setup.bash; benchmarks/verify_consumer.sh [prefix]
# The temporary build is owned by this invocation and removed on exit.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$(mktemp -d "${TMPDIR:-/tmp}/chaos-consumer.XXXXXXXX")"
trap 'rm -rf -- "$BUILD"' EXIT
CMAKE_OPTIONS=()
if [[ $# -gt 0 ]]; then
  CMAKE_OPTIONS+=("-DCMAKE_PREFIX_PATH=$1")
fi
cmake -S "$ROOT/benchmarks/consumer" -B "$BUILD" "${CMAKE_OPTIONS[@]}"
cmake --build "$BUILD" --parallel 2
"$BUILD/consumer"
