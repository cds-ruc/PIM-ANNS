PROJECT_ROOT="/home/wupuqing/workspace/PIMANN"

rm -rf "$PROJECT_ROOT/SIFT1B32M4096_DIR"
rm -rf "$PROJECT_ROOT/SPACE1B20M4096_DIR"

if [ ! -d "$PROJECT_ROOT/SIFT1B32M4096_DIR/DPU_DIR" ]; then
    mkdir -p "$PROJECT_ROOT/SIFT1B32M4096_DIR/DPU_DIR"
fi

if [ ! -d "$PROJECT_ROOT/SIFT1B32M4096_DIR/BATCH_DPU_DIR" ]; then
    mkdir -p "$PROJECT_ROOT/SIFT1B32M4096_DIR/BATCH_DPU_DIR"
fi

if [ ! -d "$PROJECT_ROOT/SIFT1B32M4096_DIR/CPU_DIR" ]; then
    mkdir -p "$PROJECT_ROOT/SIFT1B32M4096_DIR/CPU_DIR"
fi

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


# ======================= #EXP1 + #EXP2 + #EXP3 ================================

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

macro0_values=("sift1B-32M-4096C.json" "space1B-20M-4096C.json")
macro1_values=("#define TEST_DPU" "#define TEST_CPU" "#define TEST_BATCH_DPU")


for m0 in "${macro0_values[@]}"; do
    for m1 in "${macro1_values[@]}"; do
    
        sed -i -e "s/#define TEST_.*/${m1}/" \
               -e "s/#define SLOT_L.*/#define SLOT_L 100000/" \
               -e "s/#define COPY_RATE.*/#define COPY_RATE 10/" \
               -e "s/#define MAX_COROUTINE.*/#define MAX_COROUTINE 4/" \
               $path_common

        if [[ ${m0} == "sift1B-32M-4096C.json" ]]; then
            cp ../${m0} ../config.json

            sed -i -e "s/#define MY_PQ_M.*/#define MY_PQ_M 32/" \
                   -e "s/#define DIM.*/#define DIM 128/" \
                   -e "s/#define QUERY_TYPE.*/#define QUERY_TYPE 0/" \
                   $path_common 

            make -j
            make main -j

            echo "Running with ${m0}"

            nprobe=(6 7 9 11 15 24)
            
            for np in "${nprobe[@]}"; do
                date +"%Y-%m-%d %H:%M:%S"
                run_single_command "./main $np"
                
                echo "The command ./main $np completed successfully."
                
            done
            
        elif [[ ${m0} == "space1B-20M-4096C.json" ]]; then
            cp ../${m0} ../config.json

            sed -i -e "s/#define MY_PQ_M.*/#define MY_PQ_M 20/" \
                   -e "s/#define DIM.*/#define DIM 100/" \
                   -e "s/#define QUERY_TYPE.*/#define QUERY_TYPE 1/" \
                   $path_common
            
            make -j
            make main -j

            nprobe=(4 5 8 11 21 71)
            echo "Running with ${m0}"
            
            for np in "${nprobe[@]}"; do
                # when nprobe is 71, we use the best settings for MAX_COROUTINE
                if [[ $np -eq 71 ]]; then
                    sed -i -e "s/#define MAX_COROUTINE.*/#define MAX_COROUTINE 2/" $path_common
                    make -j
                    make main -j
                fi

                
                date +"%Y-%m-%d %H:%M:%S"
                run_single_command "./main $np"
                
                echo "The command ./main $np completed successfully."
                

                if [[ $np -eq 71 ]]; then
                    sed -i -e "s/#define MAX_COROUTINE.*/#define MAX_COROUTINE 4/" $path_common
                fi

            done
        fi
    done
done



# ======================= 2 PROCESS DATA ==============================
# =====================================================================




extract_value() {
    local file_path=$1
    local key=$2

    grep -o "${key}=[0-9.]*" "$file_path" | tail -n 1 | awk -F= '{printf "%.2f\n", $2}'
}



output_file="$PROJECT_ROOT/AE/exp3-fig11.txt"

> "$output_file"

dpu_file="$PROJECT_ROOT/SPACE1B20M4096_DIR/DPU_DIR/dpu-active-num-nprobe11.txt"
batch_dpu_file="$PROJECT_ROOT/SPACE1B20M4096_DIR/BATCH_DPU_DIR/batch-dpu-active-num-nprobe11.txt"



dpu_length=$(wc -l < "$dpu_file")
batch_dpu_length=$(wc -l < "$batch_dpu_file")

min_length=$((dpu_length < batch_dpu_length ? dpu_length : batch_dpu_length))


paste -d ',' "$dpu_file" "$batch_dpu_file" | head -n "$min_length" | while IFS=',' read -r dpu_num batch_dpu_num; do
    echo "DPU=$dpu_num, Batch_DPU=$batch_dpu_num" >> "$output_file"
done


echo "result saved to $output_file"




output_file="$PROJECT_ROOT/AE/exp3-fig12.txt"

> "$output_file"

nprobe=(6 7 9 11 15 24)
for np in "${nprobe[@]}"
do

    dpu_file="$PROJECT_ROOT/SIFT1B32M4096_DIR/DPU_DIR/dpu-time-nprobe${np}.txt"
    batch_dpu_file="$PROJECT_ROOT/SIFT1B32M4096_DIR/BATCH_DPU_DIR/batch-dpu-time-nprobe${np}.txt"
    cpu_file="$PROJECT_ROOT/SIFT1B32M4096_DIR/CPU_DIR/cpu-time-nprobe${np}.txt"

    if [ -f "$dpu_file" ]; then
        dpu_rate=$(extract_value "$dpu_file" "avg_active_rate")
        echo "SIFT, DPU, nprobe=$np, avg_active_rate=$dpu_rate" >> "$output_file"
    fi

    if [ -f "$batch_dpu_file" ]; then
        batch_dpu_rate=$(extract_value "$batch_dpu_file" "avg_active_rate")
        echo "SIFT, Batch DPU, nprobe=$np, avg_active_rate=$batch_dpu_rate" >> "$output_file"
    fi

   
done


nprobe=(4 5 8 11 21 71)
for np in "${nprobe[@]}"
do

    dpu_file="$PROJECT_ROOT/SPACE1B20M4096_DIR/DPU_DIR/dpu-time-nprobe${np}.txt"
    batch_dpu_file="$PROJECT_ROOT/SPACE1B20M4096_DIR/BATCH_DPU_DIR/batch-dpu-time-nprobe${np}.txt"

    if [ -f "$dpu_file" ]; then
        dpu_rate=$(extract_value "$dpu_file" "avg_active_rate")
        echo "SPACE, DPU, nprobe=$np, avg_active_rate=$dpu_rate" >> "$output_file"
    fi

    if [ -f "$batch_dpu_file" ]; then
        batch_dpu_rate=$(extract_value "$batch_dpu_file" "avg_active_rate")
        echo "SPACE, Batch DPU, nprobe=$np, avg_active_rate=$batch_dpu_rate" >> "$output_file"
    fi
done

echo "result saved to $output_file"