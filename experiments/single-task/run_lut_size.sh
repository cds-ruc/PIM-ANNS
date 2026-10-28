#!/bin/bash

# Project root directory
ROOT_DIR="/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo"
COMMON_H="$ROOT_DIR/checksum/common/common.h"
DPU_SRC_DIR="$ROOT_DIR/checksum/dpu"
DPU_C_FILE="$DPU_SRC_DIR/dpu.c"
INPUT_DIR="$ROOT_DIR/input"
OUTPUT_DIR="$ROOT_DIR/SEN-LUT_SIZE"

# Create the output directory
mkdir -p "$OUTPUT_DIR"

# M values
M_VALUES=(20 40 60 80 100)

# LUT_SIZE values
LUT_SIZE_VALUES=(1 2 4 5 10 20)

# Default parameter values
DEFAULT_DSIZE=8
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
    
    # Iterate over LUT_SIZE values
    for LUT_SIZE in "${LUT_SIZE_VALUES[@]}"; do
        echo "Processing LUT_SIZE=$LUT_SIZE"
        
        # Create an output file for each LUT_SIZE
        OUTPUT_FILE="$M_DIR/LUT_SIZE=$LUT_SIZE.txt"
        
        # Copy the DPU source template for this LUT_SIZE from its .txt file
        DPU_SRC_FILE="$DPU_SRC_DIR/dpu-LUT-SIZE=$LUT_SIZE.txt"
        if [ -f "$DPU_SRC_FILE" ]; then
            cp "$DPU_SRC_FILE" "$DPU_C_FILE"
            echo "Copied dpu.c from $DPU_SRC_FILE for LUT_SIZE=$LUT_SIZE"
        fi
        
        # Update common.h parameters with sed
        sed -i "s/#define MY_PQ_M [0-9]\+/#define MY_PQ_M $M/" "$COMMON_H"
        sed -i "s/#define LUT_SIZE [0-9]\+/#define LUT_SIZE $LUT_SIZE/" "$COMMON_H"
        sed -i "s/#define DSIZE [0-9]\+/#define DSIZE $DEFAULT_DSIZE/" "$COMMON_H"
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
        
        # Recompute SEG_NUM for the updated LUT_SIZE
        SEG_NUM=$((M / LUT_SIZE))
        sed -i "s/#define SEG_NUM [0-9]\+/#define SEG_NUM $SEG_NUM/" "$COMMON_H"
        
        # Build and run the experiment
        echo "Running make clean && make test for M=$M, LUT_SIZE=$LUT_SIZE..."
        cd "$ROOT_DIR"
        make clean > /dev/null 2>&1
        make test > "$OUTPUT_FILE" 2>&1
        
        echo "Results saved to $OUTPUT_FILE"
    done
done


echo "=== Second experiment (LUT_SIZE) completed ==="