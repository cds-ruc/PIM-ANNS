#!/bin/bash

# 项目根目录
ROOT_DIR="/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo"
COMMON_H="$ROOT_DIR/checksum/common/common.h"
DPU_SRC_DIR="$ROOT_DIR/checksum/dpu"
DPU_C_FILE="$DPU_SRC_DIR/dpu.c"
INPUT_DIR="$ROOT_DIR/input"
OUTPUT_DIR="$ROOT_DIR/SEN-LUT_SIZE"

# 创建输出目录
mkdir -p "$OUTPUT_DIR"

# M参数列表
M_VALUES=(20 40 60 80 100)

# LUT_SIZE参数列表
LUT_SIZE_VALUES=(1 2 4 5 10 20)

# 默认参数值
DEFAULT_DSIZE=8
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
    
    # 遍历LUT_SIZE参数
    for LUT_SIZE in "${LUT_SIZE_VALUES[@]}"; do
        echo "Processing LUT_SIZE=$LUT_SIZE"
        
        # 为每个LUT_SIZE创建输出文件
        OUTPUT_FILE="$M_DIR/LUT_SIZE=$LUT_SIZE.txt"
        
        # 复制对应LUT_SIZE的dpu.c文件（从.txt文件复制）
        DPU_SRC_FILE="$DPU_SRC_DIR/dpu-LUT-SIZE=$LUT_SIZE.txt"
        if [ -f "$DPU_SRC_FILE" ]; then
            cp "$DPU_SRC_FILE" "$DPU_C_FILE"
            echo "Copied dpu.c from $DPU_SRC_FILE for LUT_SIZE=$LUT_SIZE"
        fi
        
        # 使用sed修改common.h参数
        sed -i "s/#define MY_PQ_M [0-9]\+/#define MY_PQ_M $M/" "$COMMON_H"
        sed -i "s/#define LUT_SIZE [0-9]\+/#define LUT_SIZE $LUT_SIZE/" "$COMMON_H"
        sed -i "s/#define DSIZE [0-9]\+/#define DSIZE $DEFAULT_DSIZE/" "$COMMON_H"
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
        
        # 重新计算SEG_NUM（因为LUT_SIZE改变了）
        SEG_NUM=$((M / LUT_SIZE))
        sed -i "s/#define SEG_NUM [0-9]\+/#define SEG_NUM $SEG_NUM/" "$COMMON_H"
        
        # 编译并运行测试
        echo "Running make clean && make test for M=$M, LUT_SIZE=$LUT_SIZE..."
        cd "$ROOT_DIR"
        make clean > /dev/null 2>&1
        make test > "$OUTPUT_FILE" 2>&1
        
        echo "Results saved to $OUTPUT_FILE"
    done
done


echo "=== Second experiment (LUT_SIZE) completed ==="