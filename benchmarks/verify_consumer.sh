#!/usr/bin/env bash
# Builds the installed-downstream consumer against install-cpp only.
# Usage: benchmarks/verify_consumer.sh [install-prefix]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${1:-$ROOT/install-cpp}"
BUILD="$ROOT/build-cpp/consumer"
rm -rf "$BUILD"
cmake -S "$ROOT/benchmarks/consumer" -B "$BUILD" \
  -DCMAKE_PREFIX_PATH="$PREFIX" >/dev/null
cmake --build "$BUILD" >/dev/null
"$BUILD/consumer"
