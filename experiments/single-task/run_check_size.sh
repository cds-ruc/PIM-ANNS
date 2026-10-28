#!/bin/bash
set -euo pipefail

# Project root directory
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)" || exit 1
COMMON_H="$ROOT_DIR/checksum/common/common.h"
DPU_SRC_DIR="$ROOT_DIR/checksum/dpu"
DPU_C_FILE="$DPU_SRC_DIR/dpu.c"
DPU_SRC_FILE="$DPU_SRC_DIR/dpu-pimann.txt"
INPUT_DIR="$ROOT_DIR/input"
OUTPUT_DIR="$ROOT_DIR/SEN-CHECK_SIZE"

# M values
M_VALUES=(20 40 60 80 100)

# CHECK_SIZE values
CHECK_SIZE_VALUES=(1 2 4 5 10 20)

# Default parameter values
DEFAULT_DSIZE=8
DEFAULT_MAX_K=10
DEFAULT_PRE_COMPUTE=10000
DEFAULT_LUT_SIZE=20

# Map MAX_K to K_TASKLET
declare -A K_TASKLET_MAP=(
    [100]=15
    [80]=12
    [60]=10
    [40]=7
    [20]=4
    [10]=3
)



# The pruning kernel checks only after a complete CHECK_SIZE group.
if (( DEFAULT_LUT_SIZE <= 0 )); then
    echo "LUT_SIZE must be positive" >&2
    exit 1
fi
for M in "${M_VALUES[@]}"; do
    if (( M <= 0 || M % DEFAULT_LUT_SIZE != 0 )); then
        echo "M=$M must be a positive multiple of LUT_SIZE=$DEFAULT_LUT_SIZE" >&2
        exit 1
    fi
    for input in data id lut; do
        if [[ ! -r "$INPUT_DIR/${input}_M$M.bin" ]]; then
            echo "Missing or unreadable input: $INPUT_DIR/${input}_M$M.bin" >&2
            exit 1
        fi
    done
done
for CHECK_SIZE in "${CHECK_SIZE_VALUES[@]}"; do
    if (( CHECK_SIZE <= 0 || DEFAULT_LUT_SIZE % CHECK_SIZE != 0 )); then
        echo "CHECK_SIZE=$CHECK_SIZE must be a positive divisor of LUT_SIZE=$DEFAULT_LUT_SIZE" >&2
        exit 1
    fi
done
if [[ ! -r "$DPU_SRC_FILE" ]]; then
    echo "Missing pruning kernel: $DPU_SRC_FILE" >&2
    exit 1
fi

# Preserve the caller's header and kernel, including uncommitted edits.
BACKUP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/pimann-check-size.XXXXXX")"
if ! cp "$COMMON_H" "$BACKUP_DIR/common.h" || ! cp "$DPU_C_FILE" "$BACKUP_DIR/dpu.c"; then
    rm -rf -- "$BACKUP_DIR"
    exit 1
fi
restore_sources() {
    local status=$?
    trap - EXIT INT TERM
    local restore_failed=0
    cp "$BACKUP_DIR/common.h" "$COMMON_H" || restore_failed=1
    cp "$BACKUP_DIR/dpu.c" "$DPU_C_FILE" || restore_failed=1
    if (( restore_failed )); then
        echo "Could not restore sources; backups remain in $BACKUP_DIR" >&2
        exit 1
    fi
    rm -rf -- "$BACKUP_DIR" || exit 1
    exit "$status"
}
trap restore_sources EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

cp "$DPU_SRC_FILE" "$DPU_C_FILE"
mkdir -p "$OUTPUT_DIR"

# Iterate over M values
for M in "${M_VALUES[@]}"; do
    echo "=== Processing M=$M ==="

    # Create a directory for each M
    M_DIR="$OUTPUT_DIR/M=$M"
    mkdir -p "$M_DIR"

    # Iterate over CHECK_SIZE values
    for CHECK_SIZE in "${CHECK_SIZE_VALUES[@]}"; do
        echo "Processing CHECK_SIZE=$CHECK_SIZE"

        # Create an output file for each CHECK_SIZE
        OUTPUT_FILE="$M_DIR/CHECK_SIZE=$CHECK_SIZE.txt"



        # Update common.h parameters with sed
        sed -i "s/#define MY_PQ_M [0-9]\+/#define MY_PQ_M $M/" "$COMMON_H"
        sed -i "s/#define CHECK_SIZE [0-9]\+/#define CHECK_SIZE $CHECK_SIZE/" "$COMMON_H"
        sed -i "s/#define DSIZE [0-9]\+/#define DSIZE $DEFAULT_DSIZE/" "$COMMON_H"
        sed -i "s/#define MAX_K [0-9]\+/#define MAX_K $DEFAULT_MAX_K/" "$COMMON_H"
        sed -i "s/#define PRE_COMPUTE [0-9]\+/#define PRE_COMPUTE $DEFAULT_PRE_COMPUTE/" "$COMMON_H"
        sed -i "s/#define LUT_SIZE [0-9]\+/#define LUT_SIZE $DEFAULT_LUT_SIZE/" "$COMMON_H"

        # Set the corresponding K_TASKLET
        K_TASKLET=${K_TASKLET_MAP[$DEFAULT_MAX_K]}
        sed -i "s/#define K_TASKLET [0-9]\+/#define K_TASKLET $K_TASKLET/" "$COMMON_H"

        # Update input paths for this M
        sed -i "s|#define DATA_FILE \".*\"|#define DATA_FILE \"$INPUT_DIR/data_M$M.bin\"|" "$COMMON_H"
        sed -i "s|#define ID_FILE \".*\"|#define ID_FILE \"$INPUT_DIR/id_M$M.bin\"|" "$COMMON_H"
        sed -i "s|#define LUT_FILE \".*\"|#define LUT_FILE \"$INPUT_DIR/lut_M$M.bin\"|" "$COMMON_H"

        # Recompute SEG_NUM with the fixed LUT_SIZE
        SEG_NUM=$((M / DEFAULT_LUT_SIZE))
        sed -i "s/^#define SEG_NUM .*/#define SEG_NUM $SEG_NUM/" "$COMMON_H"

        # Build and run the experiment
        echo "Running make clean && make test for M=$M, CHECK_SIZE=$CHECK_SIZE..."
        cd "$ROOT_DIR"
        make clean > /dev/null 2>&1
        make test > "$OUTPUT_FILE" 2>&1

        echo "Results saved to $OUTPUT_FILE"
    done
done



echo "=== Third experiment (CHECK_SIZE) completed ==="
