#!/usr/bin/env bash
PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/." && pwd)" || exit 1
cd "$PROJECT_ROOT" || exit 1




if [ ! -d "$PROJECT_ROOT/T2I100M100M4096_DIR/DPU_DIR" ]; then
    mkdir -p "$PROJECT_ROOT/T2I100M100M4096_DIR/DPU_DIR"
fi

if [ ! -d "$PROJECT_ROOT/T2I100M100M4096_DIR/CPU_DIR" ]; then
    mkdir -p "$PROJECT_ROOT/T2I100M100M4096_DIR/CPU_DIR"
fi




macro0_values=("t2i100M-100M-4096C.json")





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

cp "$PROJECT_ROOT/dpu/dpu_kernel_bigM.c" "$PROJECT_ROOT/dpu/dpu_kernel.c"

for m0 in "${macro0_values[@]}"; do
    
    cd "$PROJECT_ROOT/build" || exit 1

    cp ../${m0} ../config.json

    

    make -j
    make main -j

    nprobe=(1)
    echo "Running with ${m0}"
    
    for np in "${nprobe[@]}"; do
    
        
        date +"%Y-%m-%d %H:%M:%S"
        run_single_command "./main $np"
        
        echo "The command ./main $np completed successfully."
        


    done
          
        
done



