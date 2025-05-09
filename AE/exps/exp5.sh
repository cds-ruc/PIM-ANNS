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




# ======================= #EXP5(a) ================================

cp "$PROJECT_ROOT/space1B-20M-4096C.json" "$PROJECT_ROOT/config.json"



sed -i \
    -e "s|#define TEST_.*|#define TEST_DPU|" \
    -e "s|#define SLOT_L.*|#define SLOT_L 100000|" \
    -e "s|#define MAX_COROUTINE.*|#define MAX_COROUTINE 4|" \
    -e "s|#define COPY_RATE.*|#define COPY_RATE 10|" \
    -e "s|#define MY_PQ_M.*|#define MY_PQ_M 20|" \
    -e "s|#define DIM.*|#define DIM 100|" \
    -e "s|#define QUERY_TYPE.*|#define QUERY_TYPE 1|" \
    -e "s|#define ENABLE_DPU_LOAD.*|#define ENABLE_DPU_LOAD 1|" \
    $path_common


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


sed -i \
    -e "s|#define TEST_.*|#define TEST_DPU|" \
    -e "s|#define SLOT_L.*|#define SLOT_L 100000|" \
    -e "s|#define MAX_COROUTINE.*|#define MAX_COROUTINE 4|" \
    -e "s|#define COPY_RATE.*|#define COPY_RATE 10|" \
    -e "s|#define MY_PQ_M.*|#define MY_PQ_M 20|" \
    -e "s|#define DIM.*|#define DIM 100|" \
    -e "s|#define QUERY_TYPE.*|#define QUERY_TYPE 1|" \
    -e "s|#define ENABLE_REPLICA.*|#define ENABLE_REPLICA 0|" \
    -e "s|#define CHANGE_ENABLE_REPLICA 0.*|#define CHANGE_ENABLE_REPLICA 1|" \
    -e "s|#define ENABLE_DPU_LOAD.*|#define ENABLE_DPU_LOAD 1|" \
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

sed -i \
    -e "s|#define ENABLE_REPLICA.*|#define ENABLE_REPLICA 1|" \
    -e "s|#define CHANGE_ENABLE_REPLICA 1.*|#define CHANGE_ENABLE_REPLICA 0|" \
    -e "s|#define ENABLE_DPU_LOAD.*|#define ENABLE_DPU_LOAD 0|" \
    "$PROJECT_ROOT/common/dataset.h"




# ======================= #EXP5(b) ================================

cp "$PROJECT_ROOT/space1B-20M-4096C.json" "$PROJECT_ROOT/config.json"


macro0_values=("#define COPY_RATE 1.5" "#define COPY_RATE 2" "#define COPY_RATE 2.5" "#define COPY_RATE 3" "#define COPY_RATE 3.5")



for m0 in "${macro0_values[@]}"; do
    sed -i \
        -e "s|#define TEST_.*|#define TEST_DPU|" \
        -e "s|#define SLOT_L.*|#define SLOT_L 100000|" \
        -e "s|#define MAX_COROUTINE.*|#define MAX_COROUTINE 4|" \
        -e "s|#define COPY_RATE.*|${m0}|" \
        -e "s|#define MY_PQ_M.*|#define MY_PQ_M 20|" \
        -e "s|#define DIM.*|#define DIM 100|" \
        -e "s|#define QUERY_TYPE.*|#define QUERY_TYPE 1|" \
        -e "s|#define CHANGE_COPY_RATE 0|#define CHANGE_COPY_RATE 1|" \
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
done


sed -i \
        -e "s|#define COPY_RATE.*|#define COPY_RATE 10|" \
        -e "s|#define CHANGE_COPY_RATE.*|#define CHANGE_COPY_RATE 0|" \
        "$PROJECT_ROOT/common/dataset.h"






# ======================= 2 PROCESS DATA ==============================
# =====================================================================




extract_value() {
    local file_path=$1
    local key=$2

    grep -o "${key}=[0-9.]*" "$file_path" | tail -n 1 | awk -F= '{printf "%.2f\n", $2}'
}

# ======================= #EXP5(a) DATA================================


normalize_and_sort() {
    local file_path=$1

    # extract values and sort in descending order
    if [ -f "$file_path" ]; then
        awk '{print $1}' "$file_path" | sort -nr | awk 'NR==1 {max=$1} {printf "%.4f\n", $1/max}' 
    else
        echo "File not found: $file_path"
    fi
}



nprobe=11
enable_replica_values=("0" "1")

output_file_w="$PROJECT_ROOT/AE/exp5a-w.txt"
output_file_wo="$PROJECT_ROOT/AE/exp5a-wo.txt"

> "$output_file_w"
> "$output_file_wo"



for enable_replica in "${enable_replica_values[@]}"; do

    if [ "$enable_replica" -eq 1 ]; then
        dpuload_path="$PROJECT_ROOT/SPACE1B20M4096_DIR/DPU_DIR/dpu-load-nprobe${nprobe}.txt"
        normalize_and_sort "$dpuload_path" >> "$output_file_w"
    else
        dpuload_path="$PROJECT_ROOT/SPACE1B20M4096_DIR/DPU_DIR/dpu-load-nprobe${nprobe}-ENABLE_REPLICA${enable_replica}.txt"
        normalize_and_sort "$dpuload_path" >> "$output_file_wo"
    fi

done





# ======================= #EXP5(b) DATA================================


nprobe=11
copy_rates=("1.5" "2" "2.5" "3" "3.5")


output_file="$PROJECT_ROOT/AE/exp5b.txt"

> "$output_file"

echo "COPY_RATE,QPS" > "$output_file"


for copy_rate in "${copy_rates[@]}"; do
    
    if [[ "$copy_rate" == *.* ]]; then
        result="${copy_rate:0:3}" 
    else
        result="${copy_rate:0:1}" 
    fi
    time_path_nprobe="$PROJECT_ROOT/SPACE1B20M4096_DIR/DPU_DIR/dpu-time-nprobe${nprobe}-COPY_RATE${result}.txt"



    if [ -f "$time_path_nprobe" ]; then
       
        qps=$(extract_value "$time_path_nprobe" "qps")
        echo "$copy_rate,$qps" >> "$output_file"
    else
        echo "$copy_rate,N/A" >> "$output_file"
    fi
done



echo "result saved to $output_file"
echo "result saved to $output_file_w"
echo "result saved to $output_file_wo"