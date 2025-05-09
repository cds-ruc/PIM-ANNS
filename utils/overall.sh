#!/bin/bash

clear

cd /home/wupuqing/workspace/PIMANN/build


function run_space() {
    # nprobe=(4 5 8 11 21 71)
    nprobe=(2 )
    
    for np in "${nprobe[@]}"
    do
        date +"%Y-%m-%d %H:%M:%S"
        timeout 100m ./main $np
        # $? is the exit status of the last command executed
        # 124 used to indicate timeout
        if [ $? -eq 124 ]; then
            echo "The command ./main $np timed out after 10 minutes. Skipping..."
        elif [ $? -ne 0 ]; then
            echo "The command ./main $np failed with error code $?."
        else
            echo "The command ./main $np completed successfully."
        fi
    done
}

function run_sift(){
    nprobe=(6 7 9 11 15 24)
    
    for np in "${nprobe[@]}"
    do
        date +"%Y-%m-%d %H:%M:%S"
        timeout 30m ./main $np
        if [ $? -eq 124 ]; then
            echo "The command ./main $np timed out after 10 minutes. Skipping..."
        elif [ $? -ne 0 ]; then
            echo "The command ./main $np failed with error code $?."
        else
            echo "The command ./main $np completed successfully."
        fi
    done
}


path_common="/home/wupuqing/workspace/PIMANN/common/dataset.h"



# macro0_values=("sift1B-32M-4096C.json" "space1B-20M-4096C.json")
# macro1_values=("#define TEST_DPU" "#define TEST_CPU" "#define TEST_BATCH_DPU")
# macro2_values=("#define SLOT_L 250000" "#define SLOT_L 100000")
# macro3_values=("#define MAX_COROUTINE 8" "#define MAX_COROUTINE 4")
# macro4_values=("#define COPY_RATE 4" )


macro0_values=("sift1B-32M-4096C.json")
macro1_values=("#define TEST_DPU" )
macro2_values=("#define SLOT_L 100000")
macro3_values=("#define MAX_COROUTINE 4")
macro4_values=("#define COPY_RATE 3")


for m0 in "${macro0_values[@]}"; do
    for m1 in "${macro1_values[@]}"; do
        for m2 in "${macro2_values[@]}"; do
            for m3 in "${macro3_values[@]}"; do
                for m4 in "${macro4_values[@]}"; do
                    
                    sed -i -e "s/#define TEST_.*/${m1}/" \
                        -e "s/#define SLOT_L.*/${m2}/" \
                        -e "s/#define MAX_COROUTINE.*/${m3}/" \
                        -e "s/#define COPY_RATE.*/${m4}/" \
                        $path_common 
                    
                    make -j
                    make main -j

                   
                    if [[ $m0 == "sift1B-32M-4096C.json" ]]; then
                        cp ../sift1B-32M-4096C.json ../config.json

                        sed -i -e "s/#define MY_PQ_M.*/#define MY_PQ_M 32/" \
                            -e "s/#define DIM.*/#define DIM 128/" \
                            -e "s/#define QUERY_TYPE.*/#define QUERY_TYPE 0/" \
                            $path_common 
                        
                        # run_sift
                        echo "Running with sift1B-32M-4096C.json"
                
                    elif [[ $m0 == "space1B-20M-4096C.json" ]]; then
                        cp ../space1B-20M-4096C.json ../config.json

                        sed -i -e "s/#define MY_PQ_M.*/#define MY_PQ_M 20/" \
                            -e "s/#define DIM.*/#define DIM 100/" \
                            -e "s/#define QUERY_TYPE.*/#define QUERY_TYPE 1/" \
                            $path_common
                        
                        # run_space
                        echo "Running with space1B-20M-4096C.json"
                    fi
                done
            
            done
        done
    done
done




