PROJECT_ROOT="/home/wupuqing/workspace/PIMANN"


rm -rf "$PROJECT_ROOT/SPACE1B20M4096_DIR"

if [ ! -d "$PROJECT_ROOT/SPACE1B20M4096_DIR/DPU_DIR" ]; then
    mkdir -p "$PROJECT_ROOT/SPACE1B20M4096_DIR/DPU_DIR"
fi

if [ ! -d "$PROJECT_ROOT/SPACE1B20M4096_DIR/GPU_DIR" ]; then
    mkdir -p "$PROJECT_ROOT/SPACE1B20M4096_DIR/GPU_DIR"
fi

# ===================== 1: RUN EXP ====================================
# =====================================================================

clear

cd "$PROJECT_ROOT/build"

path_common="$PROJECT_ROOT/common/dataset.h"

# init config
sed -i \
    -e "s|#define SLOT_L.*|#define SLOT_L 100000|" \
    -e "s|#define MAX_COROUTINE.*|#define MAX_COROUTINE 4|" \
    -e "s|#define COPY_RATE.*|#define COPY_RATE 10|" \
    -e "s|#define ENABLE_REPLICA.*|#define ENABLE_REPLICA 1|" \
    -e "s|#define CHANGE_MAX_COROUTINE 0.*|#define CHANGE_MAX_COROUTINE 0|" \
    -e "s|#define CHANGE_COPY_RATE 0.*|#define CHANGE_COPY_RATE 0|" \
    -e "s|#define CHANGE_ENABLE_REPLICA 0.*|#define CHANGE_ENABLE_REPLICA 0|" \
    -e "s|#define ENABLE_DPU_LOAD.*|#define ENABLE_DPU_LOAD 0|" \
    $path_common

run_single_command() {
    local cmd="$1"
    local max_test=10
    local i=0
    local exit_code=0
    
    while [ $i -lt $max_test ]; do
        timeout 30m $cmd
        exit_code=$?
        if [ $exit_code -eq 0 ]; then
            return 0
        fi
        i=$((i + 1))  
    done

    return $exit_code
}


# ======================= #EXP7 DPU ================================

cp "$PROJECT_ROOT/space1B-20M-4096C.json" "$PROJECT_ROOT/config.json"

sed -i \
    -e "s|#define TEST_.*|#define TEST_DPU|" \
    -e "s|#define SLOT_L.*|#define SLOT_L 100000|" \
    -e "s|#define MAX_COROUTINE.*|#define MAX_COROUTINE 4|" \
    -e "s|#define COPY_RATE.*|#define COPY_RATE 10|" \
    -e "s|#define MY_PQ_M.*|#define MY_PQ_M 20|" \
    -e "s|#define DIM.*|#define DIM 100|" \
    -e "s|#define QUERY_TYPE.*|#define QUERY_TYPE 1|" \
    $path_common

cd "$PROJECT_ROOT/build"
make -j
make main -j

nprobe=(4 5 8 11 21 71)
for np in "${nprobe[@]}"; do
    date +"%Y-%m-%d %H:%M:%S"
    run_single_command "./main $np"
    
    echo "The command ./main $np completed successfully."
    
done

# ======================= #EXP7 GPU ================================

REMOTE_HOST="10.77.110.155"
REMOTE_USER="wpq"                                                    
REMOTE_SCRIPT="/home/wpq/workspace/faiss/faiss/gpu/test/run.sh"      
REMOTE_OUTPUT_DIR="/home/wpq/workspace/faiss/faiss/gpu/test/GPU_DIR" 
LOCAL_OUTPUT_DIR="$PROJECT_ROOT/SPACE1B20M4096_DIR/GPU_DIR"


ssh "$REMOTE_USER@$REMOTE_HOST" "bash $REMOTE_SCRIPT" #  run remote script


if [ $? -ne 0 ]; then
    echo "remote script execution failed" 
    exit 1
fi

scp -r "$REMOTE_USER@$REMOTE_HOST:$REMOTE_OUTPUT_DIR/*" "$LOCAL_OUTPUT_DIR" # copy remote output to local


if [ $? -ne 0 ]; then
    echo "SCP failed"
    exit 1
fi

echo "SCP success"





# ======================= 2 PROCESS DATA ==============================
# =====================================================================

extract_value() {
    local file_path=$1
    local key=$2

    # extract numeric value and keep two decimal places, only keep the last match
    grep -o "${key}=[0-9.]*" "$file_path" | tail -n 1 | awk -F= '{printf "%.2f\n", $2}'
}

dpu_power=462

gpu_power=300

output_file="$PROJECT_ROOT/AE/exp7.txt"

>"$output_file"

nprobe=(4 5 8 11 21 71)

for np in "${nprobe[@]}"; do

    
    dpu_file="$PROJECT_ROOT/SPACE1B20M4096_DIR/DPU_DIR/dpu-time-nprobe${np}.txt"
    gpu_file="$PROJECT_ROOT/SPACE1B20M4096_DIR/GPU_DIR/gpu-time-nprobe${np}.txt"
    
    dpu_qps=$(extract_value "$dpu_file" "qps")
    gpu_qps=$(extract_value "$gpu_file" "qps")

    echo "nprobe: $np, DPU QPS: $dpu_qps, GPU QPS: $gpu_qps" >>"$output_file"

    dpu_qps_per_watt=$(awk -v qps="$dpu_qps" -v power="$dpu_power" 'BEGIN {printf "%.4f\n", qps/power}')
    gpu_qps_per_watt=$(awk -v qps="$gpu_qps" -v power="$gpu_power" 'BEGIN {printf "%.4f\n", qps/power}')
    echo "nprobe: $np, DPU QPS per Watt: $dpu_qps_per_watt, GPU QPS per Watt: $gpu_qps_per_watt" >>"$output_file"

done

echo "result saved to $output_file"
