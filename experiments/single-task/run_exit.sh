#!/usr/bin/env bash
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)" || exit 1
cd "$ROOT_DIR" || exit 1

cp dpu_early_exit.c checksum/dpu/dpu.c || exit 1
make clean && make test
