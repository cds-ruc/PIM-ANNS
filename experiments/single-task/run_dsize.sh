#!/bin/bash

# Project root directory
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)" || exit 1
COMMON_H="$ROOT_DIR/checksum/common/common.h"
INPUT_DIR="$ROOT_DIR/input"
OUTPUT_DIR="$ROOT_DIR/SEN-DSIZE"


rm -rf "$OUTPUT_DIR"
# Create the output directory
mkdir -p "$OUTPUT_DIR"

# M values
M_VALUES=(20 40 60 80 100)

# DSIZE values
DSIZE_VALUES=(2 4 8 16 32)

# Default parameter values
DEFAULT_LUT_SIZE=20
DEFAULT_MAX_K=10
DEFAULT_PRE_COMPUTE=10000
DEFAULT_CHECK_SIZE=4

# Map MAX_K to K_TASKLET
declare -A K_TASKLET_MAP=(
    [100]=15
    [80]=12
    [60]=10
    [40]=7
    [20]=4
    [10]=3
)


# Iterate over M values
for M in "${M_VALUES[@]}"; do
    echo "=== Processing M=$M ==="
    
    # Create a directory for each M
    M_DIR="$OUTPUT_DIR/M=$M"
    mkdir -p "$M_DIR"
    
    # Iterate over DSIZE values
    for DSIZE in "${DSIZE_VALUES[@]}"; do
        echo "Processing DSIZE=$DSIZE"
        
        
        
        # Update parameters with sed
        sed -i "s/#define MY_PQ_M [0-9]\+/#define MY_PQ_M $M/" "$COMMON_H"
        sed -i "s/#define DSIZE [0-9]\+/#define DSIZE $DSIZE/" "$COMMON_H"
        sed -i "s/#define LUT_SIZE [0-9]\+/#define LUT_SIZE $DEFAULT_LUT_SIZE/" "$COMMON_H"
        sed -i "s/#define MAX_K [0-9]\+/#define MAX_K $DEFAULT_MAX_K/" "$COMMON_H"
        sed -i "s/#define PRE_COMPUTE [0-9]\+/#define PRE_COMPUTE $DEFAULT_PRE_COMPUTE/" "$COMMON_H"
        sed -i "s/#define CHECK_SIZE [0-9]\+/#define CHECK_SIZE $DEFAULT_CHECK_SIZE/" "$COMMON_H"
        
        # Set the corresponding K_TASKLET
        K_TASKLET=${K_TASKLET_MAP[$DEFAULT_MAX_K]}
        sed -i "s/#define K_TASKLET [0-9]\+/#define K_TASKLET $K_TASKLET/" "$COMMON_H"
        
        # Update input paths for this M
        sed -i "s|#define DATA_FILE \".*\"|#define DATA_FILE \"$INPUT_DIR/data_M$M.bin\"|" "$COMMON_H"
        sed -i "s|#define ID_FILE \".*\"|#define ID_FILE \"$INPUT_DIR/id_M$M.bin\"|" "$COMMON_H"
        sed -i "s|#define LUT_FILE \".*\"|#define LUT_FILE \"$INPUT_DIR/lut_M$M.bin\"|" "$COMMON_H"
        
        # Build and run the experiment
        echo "Running make clean && make test for M=$M, DSIZE=$DSIZE..."
        cd "$ROOT_DIR"
        make clean > /dev/null 2>&1
        make test > "$M_DIR/DSIZE=$DSIZE.txt" 2>&1
        
        echo "Results saved to $M_DIR/DSIZE=$DSIZE.txt"
    done
done



echo "=== All experiments completed ==="