PROJECT_ROOT="/home/wupuqing/workspace/PIMANN"

rm -rf "$PROJECT_ROOT/SPACE1B20M4096_DIR"


if [ ! -d "$PROJECT_ROOT/SPACE1B20M4096_DIR/DPU_DIR" ]; then
    mkdir -p "$PROJECT_ROOT/SPACE1B20M4096_DIR/DPU_DIR"
fi

if [ ! -d "$PROJECT_ROOT/SPACE1B20M4096_DIR/BATCH_DPU_DIR" ]; then
    mkdir -p "$PROJECT_ROOT/SPACE1B20M4096_DIR/BATCH_DPU_DIR"
fi

if [ ! -d "$PROJECT_ROOT/SPACE1B20M4096_DIR/CPU_DIR" ]; then
    mkdir -p "$PROJECT_ROOT/SPACE1B20M4096_DIR/CPU_DIR"
fi


# ===================== 1: RUN EXP ====================================
# =====================================================================


clear

cd "$PROJECT_ROOT/build"

path_common="$PROJECT_ROOT/common/dataset.h"


# ======================= #EXP4 ================================



macro0_values=("#define MAX_COROUTINE 1" "#define MAX_COROUTINE 2" "#define MAX_COROUTINE 4" "#define MAX_COROUTINE 8" "#define MAX_COROUTINE 16")

for m0 in "${macro0_values[@]}"; do
    sed -i \
        -e "s|#define TEST_.*|#define TEST_DPU|" \
        -e "s|#define SLOT_L.*|#define SLOT_L 100000|" \
        -e "s|#define MAX_COROUTINE.*|${m0}|" \
        -e "s|#define COPY_RATE.*|#define COPY_RATE 10|" \
        -e "s|#define MY_PQ_M.*|#define MY_PQ_M 20|" \
        -e "s|#define DIM.*|#define DIM 100|" \
        -e "s|#define QUERY_TYPE.*|#define QUERY_TYPE 1|" \
        -e "s|#define CHANGE_MAX_COROUTINE 0|#define CHANGE_MAX_COROUTINE 1|" \
        "$PROJECT_ROOT/common/dataset.h"
    
    cd "$PROJECT_ROOT/build"
    make -j
    make main -j

    nprobe=(4 5 8 11)
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

            
done


sed -i \
         -e "s|#define CHANGE_MAX_COROUTINE 1|#define CHANGE_MAX_COROUTINE 0|" \
        "$PROJECT_ROOT/common/dataset.h"

# ======================= 2 PROCESS DATA ==============================
# =====================================================================




extract_value() {
    local file_path=$1
    local key=$2

    grep -o "${key}=[0-9.]*" "$file_path" | tail -n 1 | awk -F= '{printf "%.2f\n", $2}'
}

output_file="$PROJECT_ROOT/AE/exp4.txt"

> "$output_file"

nprobe=(4 5 8 11)
max_coroutines=(1 2 4 8 16)

for max_coroutine in "${max_coroutines[@]}"
do
    for np in "${nprobe[@]}"
    do
        dpu_file="$PROJECT_ROOT/SPACE1B20M4096_DIR/DPU_DIR/dpu-time-nprobe${np}.txt"

        
        if [[ "$max_coroutine" != "4" ]]; then
            dpu_file="$PROJECT_ROOT/SPACE1B20M4096_DIR/DPU_DIR/dpu-time-nprobe${np}-COROUTINE${max_coroutine}.txt"
        fi

        
        if [ -f "$dpu_file" ]; then
            dpu_qps=$(extract_value "$dpu_file" "qps")
            dpu_latency=$(extract_value "$dpu_file" "latency")
            echo "DPU, MAX_COROUTINE=$max_coroutine, nprobe=$np, qps=$dpu_qps, latency=$dpu_latency" >> "$output_file"
        fi
    done
done

echo "result saved to $output_file"