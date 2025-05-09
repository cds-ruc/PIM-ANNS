PROJECT_ROOT="/home/wupuqing/workspace/PIMANN"


rm -rf "$PROJECT_ROOT/SPACE1M20M4096_DIR"

if [ ! -d "$PROJECT_ROOT/SPACE1M20M4096_DIR/DPU_DIR" ]; then
    mkdir -p "$PROJECT_ROOT/SPACE1M20M4096_DIR/DPU_DIR"
fi


# ===================== 1: RUN EXP ====================================
# =====================================================================

clear

cd "$PROJECT_ROOT/build"

path_common="$PROJECT_ROOT/common/dataset.h"

# ======================= #EXP8 DPU + CPU ================================

cp "$PROJECT_ROOT/space1M-20M-4096C.json" "$PROJECT_ROOT/config.json"


sed -i \
    -e "s|#define TEST_.*|#define TEST_DPU|" \
    -e "s|#define SLOT_L.*|#define SLOT_L 100000|" \
    -e "s|#define MAX_COROUTINE.*|#define MAX_COROUTINE 4|" \
    -e "s|#define COPY_RATE.*|#define COPY_RATE 10|" \
    -e "s|#define MY_PQ_M.*|#define MY_PQ_M 20|" \
    -e "s|#define DIM.*|#define DIM 100|" \
    -e "s|#define QUERY_TYPE.*|#define QUERY_TYPE 1|" \
    "$PROJECT_ROOT/common/dataset.h"

cd "$PROJECT_ROOT/build"
make -j
make main -j

nprobe=(11)
for np in "${nprobe[@]}"; do
    date +"%Y-%m-%d %H:%M:%S"
    timeout 30m ./main $np
    exit_code=$?
    if [ $exit_code -eq 124 ]; then
        echo "The command ./main $np timed out after 30 minutes. Skipping..."
    elif [ $exit_code -ne 0 ]; then
        echo "The command ./main $np failed with error code $exit_code."
    else
        echo "The command ./main $np completed successfully."
    fi
done






# ======================= 2 PROCESS DATA ==============================
# =====================================================================



        
