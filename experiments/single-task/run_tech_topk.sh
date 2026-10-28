#!/bin/bash

# Project root directory
ROOT_DIR="/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo"
COMMON_H="$ROOT_DIR/checksum/common/common.h"
DPU_SRC_DIR="$ROOT_DIR/checksum/dpu"
DPU_C_FILE="$DPU_SRC_DIR/dpu.c"
INPUT_DIR="$ROOT_DIR/input"
OUTPUT_DIR="$ROOT_DIR/TECH-TOPK"

# Create the output directory
mkdir -p "$OUTPUT_DIR"

# MAX_K values
MAX_K_VALUES=(10 20 40 60 80 100)

# Methods: baseline and pimann
METHODS=("baseline" "pimann")

# Default parameter values
DEFAULT_M=20
DEFAULT_DSIZE=8
DEFAULT_CHECK_SIZE=10
DEFAULT_LUT_SIZE=20
DEFAULT_PRE_COMPUTE=10000

# Map MAX_K to K_TASKLET
declare -A K_TASKLET_MAP=(
    [100]=15
    [80]=12
    [60]=10
    [40]=7
    [20]=4
    [10]=3
)

# Back up the original common.h
cp "$COMMON_H" "$COMMON_H.backup"
# Back up the original dpu.c
cp "$DPU_C_FILE" "$DPU_C_FILE.backup"

# Iterate over MAX_K values
for MAX_K in "${MAX_K_VALUES[@]}"; do
    echo "=== Processing MAX_K=$MAX_K ==="
    
    # Create a directory for each MAX_K
    MAX_K_DIR="$OUTPUT_DIR/TOPK=$MAX_K"
    mkdir -p "$MAX_K_DIR"
    
    # Get the corresponding K_TASKLET
    K_TASKLET=${K_TASKLET_MAP[$MAX_K]}
    if [ -z "$K_TASKLET" ]; then
        echo "Warning: No K_TASKLET mapping found for MAX_K=$MAX_K, using default value 3"
        K_TASKLET=3
    fi
    
    # Iterate over both methods
    for METHOD in "${METHODS[@]}"; do
        echo "Processing method: $METHOD, K_TASKLET=$K_TASKLET"
        
        # Create an output file for each method
        OUTPUT_FILE="$MAX_K_DIR/$METHOD.txt"
        
        # Restore the original common.h
        cp "$COMMON_H.backup" "$COMMON_H"
        
        # Copy the DPU source for this method
        DPU_SRC_FILE="$DPU_SRC_DIR/dpu-$METHOD.txt"
        if [ -f "$DPU_SRC_FILE" ]; then
            cp "$DPU_SRC_FILE" "$DPU_C_FILE"
            echo "Copied dpu.c from $DPU_SRC_FILE for method $METHOD"
        else
            echo "Warning: Source file $DPU_SRC_FILE not found, using default dpu.c"
            cp "$DPU_C_FILE.backup" "$DPU_C_FILE"
        fi
        
        # Update common.h parameters with sed
        sed -i "s/#define MAX_K [0-9]\+/#define MAX_K $MAX_K/" "$COMMON_H"
        sed -i "s/#define K_TASKLET [0-9]\+/#define K_TASKLET $K_TASKLET/" "$COMMON_H"
        sed -i "s/#define MY_PQ_M [0-9]\+/#define MY_PQ_M $DEFAULT_M/" "$COMMON_H"
        sed -i "s/#define DSIZE [0-9]\+/#define DSIZE $DEFAULT_DSIZE/" "$COMMON_H"
        sed -i "s/#define CHECK_SIZE [0-9]\+/#define CHECK_SIZE $DEFAULT_CHECK_SIZE/" "$COMMON_H"
        sed -i "s/#define LUT_SIZE [0-9]\+/#define LUT_SIZE $DEFAULT_LUT_SIZE/" "$COMMON_H"
        sed -i "s/#define PRE_COMPUTE [0-9]\+/#define PRE_COMPUTE $DEFAULT_PRE_COMPUTE/" "$COMMON_H"
        
        # Update input paths for this M
        sed -i "s|#define DATA_FILE \".*\"|#define DATA_FILE \"$INPUT_DIR/data_M$DEFAULT_M.bin\"|" "$COMMON_H"
        sed -i "s|#define ID_FILE \".*\"|#define ID_FILE \"$INPUT_DIR/id_M$DEFAULT_M.bin\"|" "$COMMON_H"
        sed -i "s|#define LUT_FILE \".*\"|#define LUT_FILE \"$INPUT_DIR/lut_M$DEFAULT_M.bin\"|" "$COMMON_H"
        
        # Recompute SEG_NUM
        SEG_NUM=$((DEFAULT_M / DEFAULT_LUT_SIZE))
        sed -i "s/#define SEG_NUM [0-9]\+/#define SEG_NUM $SEG_NUM/" "$COMMON_H"
        
        # Build and run the experiment
        echo "Running make clean && make test for MAX_K=$MAX_K, method=$METHOD..."
        cd "$ROOT_DIR"
        make clean > /dev/null 2>&1
        make test > "$OUTPUT_FILE" 2>&1
        
        echo "Results saved to $OUTPUT_FILE"
    done
done

# Restore the original files
cp "$COMMON_H.backup" "$COMMON_H"
cp "$DPU_C_FILE.backup" "$DPU_C_FILE"
rm "$COMMON_H.backup" "$DPU_C_FILE.backup"

echo "=== Sixth experiment (TECH-TOPK comparison) completed ==="