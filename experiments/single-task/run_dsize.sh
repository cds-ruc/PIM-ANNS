#!/bin/bash

# 项目根目录
ROOT_DIR="/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo"
COMMON_H="$ROOT_DIR/checksum/common/common.h"
INPUT_DIR="$ROOT_DIR/input"
OUTPUT_DIR="$ROOT_DIR/SEN-DSIZE"


rm -rf "$OUTPUT_DIR"
# 创建输出目录
mkdir -p "$OUTPUT_DIR"

# M参数列表
M_VALUES=(20 40 60 80 100)

# DSIZE参数列表
DSIZE_VALUES=(2 4 8 16 32)

# 默认参数值
DEFAULT_LUT_SIZE=20
DEFAULT_MAX_K=10
DEFAULT_PRE_COMPUTE=10000
DEFAULT_CHECK_SIZE=4

# MAX_K与K_TASKLET的映射关系
declare -A K_TASKLET_MAP=(
    [100]=15
    [80]=12
    [60]=10
    [40]=7
    [20]=4
    [10]=3
)


# 遍历M参数
for M in "${M_VALUES[@]}"; do
    echo "=== Processing M=$M ==="
    
    # 为每个M创建子目录
    M_DIR="$OUTPUT_DIR/M=$M"
    mkdir -p "$M_DIR"
    
    # 遍历DSIZE参数
    for DSIZE in "${DSIZE_VALUES[@]}"; do
        echo "Processing DSIZE=$DSIZE"
        
        
        
        # 使用sed修改参数
        sed -i "s/#define MY_PQ_M [0-9]\+/#define MY_PQ_M $M/" "$COMMON_H"
        sed -i "s/#define DSIZE [0-9]\+/#define DSIZE $DSIZE/" "$COMMON_H"
        sed -i "s/#define LUT_SIZE [0-9]\+/#define LUT_SIZE $DEFAULT_LUT_SIZE/" "$COMMON_H"
        sed -i "s/#define MAX_K [0-9]\+/#define MAX_K $DEFAULT_MAX_K/" "$COMMON_H"
        sed -i "s/#define PRE_COMPUTE [0-9]\+/#define PRE_COMPUTE $DEFAULT_PRE_COMPUTE/" "$COMMON_H"
        sed -i "s/#define CHECK_SIZE [0-9]\+/#define CHECK_SIZE $DEFAULT_CHECK_SIZE/" "$COMMON_H"
        
        # 设置对应的K_TASKLET
        K_TASKLET=${K_TASKLET_MAP[$DEFAULT_MAX_K]}
        sed -i "s/#define K_TASKLET [0-9]\+/#define K_TASKLET $K_TASKLET/" "$COMMON_H"
        
        # 更新文件路径（根据M值）
        sed -i "s|#define DATA_FILE \".*\"|#define DATA_FILE \"$INPUT_DIR/data_M$M.bin\"|" "$COMMON_H"
        sed -i "s|#define ID_FILE \".*\"|#define ID_FILE \"$INPUT_DIR/id_M$M.bin\"|" "$COMMON_H"
        sed -i "s|#define LUT_FILE \".*\"|#define LUT_FILE \"$INPUT_DIR/lut_M$M.bin\"|" "$COMMON_H"
        
        # 编译并运行测试
        echo "Running make clean && make test for M=$M, DSIZE=$DSIZE..."
        cd "$ROOT_DIR"
        make clean > /dev/null 2>&1
        make test > "$M_DIR/DSIZE=$DSIZE.txt" 2>&1
        
        echo "Results saved to $M_DIR/DSIZE=$DSIZE.txt"
    done
done



echo "=== All experiments completed ==="