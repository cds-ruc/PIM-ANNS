#ifndef __COMMON_H__
#define __COMMON_H__

#define XSTR(x) STR(x)
#define STR(x) #x


#define SLOT_L 100000

#define MY_PQ_CLUSTER 256





//修改M参数
#define MY_PQ_M 100

#define DATA_FILE "/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/data_M100.bin"
#define ID_FILE "/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/id_M100.bin"
#define LUT_FILE "/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/lut_M100.bin"



//修改K参数
#define MAX_K 10
#define K_DPU MAX_K
#define K_TASKLET 3


//修改PRE_COMPUTE参数
#define PRE_COMPUTE 300

// 修改CHECK_SIZE参数
#define CHECK_SIZE 4



//修改DSIZE参数
#define DSIZE 8

// 修改LUT_SIZE参数
#define LUT_SIZE 20
#define SEG_NUM (MY_PQ_M / LUT_SIZE)

#endif /* __COMMON_H__ */
