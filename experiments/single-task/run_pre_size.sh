#!/bin/bash

# Project root directory
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)" || exit 1
COMMON_H="$ROOT_DIR/checksum/common/common.h"
INPUT_DIR="$ROOT_DIR/input"
OUTPUT_DIR="$ROOT_DIR/SEN-PRE_COMPUTE"

# Create the output directory
mkdir -p "$OUTPUT_DIR"

# M values
M_VALUES=(20 40 60 80 100)

# PRE_COMPUTE values
PRE_COMPUTE_VALUES=(100 200 300 400 500 600 700 800 900 1000 10000)

# Default parameter values
DEFAULT_DSIZE=8
DEFAULT_MAX_K=10
DEFAULT_CHECK_SIZE=4
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


# Iterate over M values
for M in "${M_VALUES[@]}"; do
    echo "=== Processing M=$M ==="
    
    # Create a directory for each M
    M_DIR="$OUTPUT_DIR/M=$M"
    mkdir -p "$M_DIR"
    
    # Iterate over PRE_COMPUTE values
    for PRE_COMPUTE in "${PRE_COMPUTE_VALUES[@]}"; do
        echo "Processing PRE_COMPUTE=$PRE_COMPUTE"
        
        # Create an output file for each PRE_COMPUTE
        OUTPUT_FILE="$M_DIR/PRE_COMPUTE=$PRE_COMPUTE.txt"
        
       
        
        # Update common.h parameters with sed
        sed -i "s/#define MY_PQ_M [0-9]\+/#define MY_PQ_M $M/" "$COMMON_H"
        sed -i "s/#define PRE_COMPUTE [0-9]\+/#define PRE_COMPUTE $PRE_COMPUTE/" "$COMMON_H"
        sed -i "s/#define DSIZE [0-9]\+/#define DSIZE $DEFAULT_DSIZE/" "$COMMON_H"
        sed -i "s/#define MAX_K [0-9]\+/#define MAX_K $DEFAULT_MAX_K/" "$COMMON_H"
        sed -i "s/#define CHECK_SIZE [0-9]\+/#define CHECK_SIZE $DEFAULT_CHECK_SIZE/" "$COMMON_H"
        sed -i "s/#define LUT_SIZE [0-9]\+/#define LUT_SIZE $DEFAULT_LUT_SIZE/" "$COMMON_H"
        
        # Set the corresponding K_TASKLET
        K_TASKLET=${K_TASKLET_MAP[$DEFAULT_MAX_K]}
        sed -i "s/#define K_TASKLET [0-9]\+/#define K_TASKLET $K_TASKLET/" "$COMMON_H"
        
        # Update input paths for this M
        sed -i "s|#define DATA_FILE \".*\"|#define DATA_FILE \"$INPUT_DIR/data_M$M.bin\"|" "$COMMON_H"
        sed -i "s|#define ID_FILE \".*\"|#define ID_FILE \"$INPUT_DIR/id_M$M.bin\"|" "$COMMON_H"
        sed -i "s|#define LUT_FILE \".*\"|#define LUT_FILE \"$INPUT_DIR/lut_M$M.bin\"|" "$COMMON_H"
        
        # Recompute SEG_NUM
        SEG_NUM=$((M / DEFAULT_LUT_SIZE))
        sed -i "s/#define SEG_NUM [0-9]\+/#define SEG_NUM $SEG_NUM/" "$COMMON_H"
        
        # Build and run the experiment
        echo "Running make clean && make test for M=$M, PRE_COMPUTE=$PRE_COMPUTE..."
        cd "$ROOT_DIR"
        make clean > /dev/null 2>&1
        make test > "$OUTPUT_FILE" 2>&1
        
        echo "Results saved to $OUTPUT_FILE"
    done
done



echo "=== Fourth experiment (PRE_COMPUTE) completed ==="