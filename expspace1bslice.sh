PROJECT_ROOT="/home/wupuqing/workspace/PIM-ANNS"




if [ ! -d "$PROJECT_ROOT/SPACE1B20M4096_DIR/DPU_DIR" ]; then
    mkdir -p "$PROJECT_ROOT/SPACE1B20M4096_DIR/DPU_DIR"
fi


# ===================== 1: RUN EXP ====================================
# =====================================================================


# clear

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
    \
    -e "s|#define DYNAMIC_BALANCE.*|#define DYNAMIC_BALANCE 0|" \
    $path_common


# ======================= #EXP dynamic ================================



macro0_values=("space1B-20M-4096C.json")
macro1_values=("#define TEST_DPU" )

slice_values=(50000 100000 150000 200000 250000)



run_single_command() {
    local cmd="$1"
    local exit_code=0
    
    while true; do
        timeout 30m $cmd
        exit_code=$?
        
        if [ $exit_code -eq 0 ]; then
            return 0
        fi
    done
}

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

for m0 in "${macro0_values[@]}"; do
    for m1 in "${macro1_values[@]}"; do
    
        sed -i -e "s/#define TEST_.*/${m1}/" \
               -e "s/#define SLOT_L.*/#define SLOT_L 100000/" \
               -e "s/#define COPY_RATE.*/#define COPY_RATE 10/" \
               -e "s/#define MAX_COROUTINE.*/#define MAX_COROUTINE 4/" \
               $path_common

        if [[ ${m0} == "space1B-20M-4096C.json" ]]; then
            cp ../${m0} ../config.json

            sed -i -e "s/#define MY_PQ_M.*/#define MY_PQ_M 20/" \
                   -e "s/#define DIM.*/#define DIM 100/" \
                   -e "s/#define QUERY_TYPE.*/#define QUERY_TYPE 1/" \
                   $path_common
            

            for slice in "${slice_values[@]}"; do
                # 1. 修改宏定义
                sed -i \
                    -e "s|#define SLOT_L.*|#define SLOT_L ${slice}|" \
                    $path_common

                # Step 1: 修改 config.json 中的 RESULT_DIR
                jq ".RESULT_DIR = \"SLOT_L_${slice}_SPACE1B20M4096_DIR\"" ../config.json > ../tmp.json \
                    && mv ../tmp.json ../config.json

                # Step 2: 提取新目录路径并创建目录
                new_dir=$(grep -oP '"RESULT_DIR": "\K[^"]+' ../config.json)
                if [ ! -d "../$new_dir" ]; then
                    mkdir -p "../$new_dir"
                    mkdir -p "../$new_dir/DPU_DIR"
                    echo "Created directory: ../$new_dir"
                else
                    echo "Directory already exists: ../$new_dir"
                    mkdir -p "../$new_dir/DPU_DIR"
                fi
            
                make -j
                make main -j

                nprobe=(4 5 8 10 21 71)
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
            done
        fi
    done
done



