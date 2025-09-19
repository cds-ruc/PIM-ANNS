PROJECT_ROOT="/home/wupuqing/workspace/PIM-ANNS"




if [ ! -d "$PROJECT_ROOT/SPACE1M20M4096_DIR/DPU_DIR" ]; then
    mkdir -p "$PROJECT_ROOT/SPACE1M20M4096_DIR/DPU_DIR"
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



macro0_values=("space1M-20M-4096C.json")
macro1_values=("#define TEST_DPU" )

thread_values=(1)



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
    local max_test=1
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

        if [[ ${m0} == "space1M-20M-4096C.json" ]]; then
            cp ../${m0} ../config.json

            sed -i -e "s/#define MY_PQ_M.*/#define MY_PQ_M 20/" \
                   -e "s/#define DIM.*/#define DIM 100/" \
                   -e "s/#define QUERY_TYPE.*/#define QUERY_TYPE 1/" \
                   $path_common

            make -j
            make main -j

            nprobe=(1)
            echo "Running with ${m0}"
            
            for np in "${nprobe[@]}"; do
                

                
                date +"%Y-%m-%d %H:%M:%S"
                run_single_command "./main $np"
                
                echo "The command ./main $np completed successfully."
                


            done
    
        fi
    done
done



