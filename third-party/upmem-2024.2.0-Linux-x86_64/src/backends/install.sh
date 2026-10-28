#!/usr/bin/env bash
set -e
BACKENDS_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SDK_DIR="$(cd -- "$BACKENDS_DIR/../.." && pwd)"

cmake -S "$BACKENDS_DIR" -B "$BACKENDS_DIR/build" -DCMAKE_INSTALL_PREFIX="$SDK_DIR"
make -C "$BACKENDS_DIR/build" -j
make -C "$BACKENDS_DIR/build" install
