PROJECT_ROOT="/home/wupuqing/workspace/PIMANN"


rm -rf "$PROJECT_ROOT/SPACE1B20M4096_DIR"

if [ ! -d "$PROJECT_ROOT/SPACE1B20M4096_DIR/DPU_DIR" ]; then
    mkdir -p "$PROJECT_ROOT/SPACE1B20M4096_DIR/DPU_DIR"
fi

if [ ! -d "$PROJECT_ROOT/SPACE1B20M4096_DIR/GPU_DIR" ]; then
    mkdir -p "$PROJECT_ROOT/SPACE1B20M4096_DIR/GPU_DIR"
fi

if [ ! -d "$PROJECT_ROOT/SPACE1B20M4096_DIR/CPU_DIR" ]; then
    mkdir -p "$PROJECT_ROOT/SPACE1B20M4096_DIR/CPU_DIR"
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
    "$PROJECT_ROOT/common/dataset.h"

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

# ======================= #EXP8 DPU + CPU ================================

cp "$PROJECT_ROOT/space1B-20M-4096C.json" "$PROJECT_ROOT/config.json"

macro0_values=("#define TEST_DPU" "#define TEST_CPU")

for m0 in "${macro0_values[@]}"; do

    sed -i \
        -e "s|#define TEST_.*|$m0|" \
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
        run_single_command "./main $np"
        
        echo "The command ./main $np completed successfully."
        
    done

done



# ======================= #EXP8 GPU ================================

REMOTE_HOST="10.77.110.155"
REMOTE_USER="wpq"  # replace with your remote username
REMOTE_SCRIPT="/home/wpq/workspace/faiss/faiss/gpu/test/runexp8.sh"  # replace with your remote script path
REMOTE_OUTPUT_DIR="/home/wpq/workspace/faiss/faiss/gpu/test/GPU_DIR"  # replace with your remote output directory path
LOCAL_OUTPUT_DIR="$PROJECT_ROOT/SPACE1B20M4096_DIR/GPU_DIR"


ssh "$REMOTE_USER@$REMOTE_HOST" "bash $REMOTE_SCRIPT" #  run remote script

if [ $? -ne 0 ]; then
    echo "run remote script failed"
    exit 1
fi



scp -r "$REMOTE_USER@$REMOTE_HOST:$REMOTE_OUTPUT_DIR/*" "$LOCAL_OUTPUT_DIR"


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

output_file="$PROJECT_ROOT/AE/exp8.txt"

>"$output_file"

nprobe=(11)

cpu_price=1500
dpu_price=5473
gpu_price=9685

for np in "${nprobe[@]}"; do


    dpu_file="$PROJECT_ROOT/SPACE1B20M4096_DIR/DPU_DIR/dpu-time-nprobe${np}.txt"
    cpu_file="$PROJECT_ROOT/SPACE1B20M4096_DIR/CPU_DIR/cpu-time-nprobe${np}.txt"
    gpu_file="$PROJECT_ROOT/SPACE1B20M4096_DIR/GPU_DIR/gpu-time-nprobe${np}.txt"


    if [ -f "$dpu_file" ]; then
        dpu_qps=$(extract_value "$dpu_file" "qps")
        qps_per_dollar=$(awk -v qps="$dpu_qps" -v price="$dpu_price" 'BEGIN {printf "%.4f\n", qps/price}')
        echo "dpu_qps: $dpu_qps, qps_per_dollar: $qps_per_dollar" >> "$output_file"
    fi

    if [ -f "$cpu_file" ]; then
        cpu_qps=$(extract_value "$cpu_file" "qps")
        qps_per_dollar=$(awk -v qps="$cpu_qps" -v price="$cpu_price" 'BEGIN {printf "%.4f\n", qps/price}')
        echo "cpu_qps: $cpu_qps, qps_per_dollar: $qps_per_dollar" >> "$output_file"
    fi 

    if [ -f "$gpu_file" ]; then
        gpu_qps=$(extract_value "$gpu_file" "qps")
        qps_per_dollar=$(awk  -v qps="$gpu_qps" -v price="$gpu_price" 'BEGIN {printf "%.4f\n", qps/price}')
        echo "gpu_qps: $gpu_qps, qps_per_dollar: $qps_per_dollar" >> "$output_file"
    fi

done

echo "result saved to $output_file"

        
