PROJECT_ROOT="/home/wupuqing/workspace/PIM-ANNS"




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

cp /home/wupuqing/workspace/PIM-ANNS/dpu/dpu_kernel_bigM.c /home/wupuqing/workspace/PIM-ANNS/dpu/dpu_kernel.c

for m0 in "${macro0_values[@]}"; do
    
    cd build

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



