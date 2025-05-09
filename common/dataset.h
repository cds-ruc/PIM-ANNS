#pragma once

#define TEST_DPU

#define MY_PQ_M 20

#define DIM 100

//sift1B: 0
//space1B: 1
#define QUERY_TYPE 1

#define SLOT_L 100000

#define COPY_RATE 10

#define MAX_COROUTINE 4

// set to 1 to enable replica
#define ENABLE_REPLICA 1

#define CHANGE_MAX_COROUTINE 0
#define CHANGE_COPY_RATE 0
#define CHANGE_ENABLE_REPLICA 0

#define ENABLE_DPU_LOAD 0



// below is not necessary to change

#define NR_DPU 2558

#define FIG_BREAKDOWN


// SPACE 1B20M4096 MAX_SIZE = 22923525
// MAX_SHARD = MAX_CLUSTER_LEN / SLOT_L = 22923525 / 100000 = 229
#define MAX_SHARD 300

#define NO_QUERY -1
#define MAX_K 10


#define BACK_THREAD 1
#define DynamicBalance_THREAD 1
#define FRONT_THREAD 40

//set to 1 to enable detect dynamic balance
#define DYNAMIC_BALANCE 0


#define MAX_DPU 2560
#define MAX_PAIR 1280
#define MAX_LINE 320
#define MAX_PAIR_LINE 160
#define MAX_RANK 40

#define PERDPU_LOG_SIZE 128
#define MAX_NPROBE 1024


// when DIST_TYPE change, MAX_VALUE should be changed accordingly
#define DIST_TYPE int32_t
#define ID_TYPE int64_t


//MY_PQ_CLUSTER need to be changed according to DATA_TYPE
#define MY_PQ_CLUSTER 256
#define DATA_TYPE uint8_t

#define SAMPLE_INTERVAL_MS 10


#define LUT_SIZE (MY_PQ_M * MY_PQ_CLUSTER)

#define SLOT_DATA_SIZE (SLOT_L * MY_PQ_M * sizeof(DATA_TYPE))
#define SLOT_ID_SIZE (SLOT_L * sizeof(ID_TYPE))
#define SLOT_NUM ((MRAM_SIZE) / (SLOT_DATA_SIZE + SLOT_ID_SIZE))


// because batch dpu need more mram to store lut, so there is less mram for load balance
#if defined(TEST_BATCH_DPU)
#define MRAM_SIZE (40 * 1024 * 1024)
#define MAX_DPUBATCH 300
#else
#define MRAM_SIZE (55 * 1024 * 1024)
#define MAX_DPUBATCH 100
#endif
