#pragma once
#include "dataset.h"

#if defined(__GNUC__)
#define MRAM_PTR
#elif defined(__clang__)
#define MRAM_PTR __mram

#else
#error "compiler not supported"
#endif

typedef struct dpu_fifo_input_t
{
    int dpu_id;
    int pair_id;
    int rank_id;

    int slot_id;

    int c_id;
    int shard_id;
    int shard_l;

    // query related
    int k;

    int q_id;

    DIST_TYPE dis0;


    MRAM_PTR DIST_TYPE LUT[LUT_SIZE];
} __attribute__((aligned(8))) dpu_fifo_input_t;



typedef struct dpu_fifo_output_t
{
    DIST_TYPE result_v[MAX_K];
    ID_TYPE result_id[MAX_K];

    int k;
    int q_id;
    int dpu_id;
    uint64_t cycles;
    int64_t cycle_per_vec;

    uint64_t start;
    uint64_t end;

    uint64_t dis;
    uint64_t sort;

} __attribute__((aligned(8))) dpu_fifo_output_t;

typedef struct DPU_INFO_COMMON
{
    int dpu_id;
    int rank_id;
    int chip_id;
    int chip_dpu_id;
} DPU_INFO_COMMON;

#define ALIGN8B(x) (((x) + 7) & ~7)
#define IS_ALIGN8B(x) ((x) % 8 == 0)

/*-----------------wram fifo info ---------------------*/
#define INPUT_FIFO_PTR_SIZE 0
#define INPUT_FIFO_DATA_SIZE 16
#define INPUT_FIFO_NUM (1 << INPUT_FIFO_PTR_SIZE)

#define OUTPUT_FIFO_PTR_SIZE 0
#define OUTPUT_FIFO_DATA_SIZE (ALIGN8B(sizeof(dpu_fifo_output_t)) + 16)

#define OUTPUT_FIFO_NUM (1 << OUTPUT_FIFO_PTR_SIZE)

#define OFFSET_OUT_FIFO(offset) \
    (offset * OUTPUT_FIFO_NUM * OUTPUT_FIFO_DATA_SIZE)
#define OFFSET_IN_FIFO(offset) (offset * INPUT_FIFO_NUM * INPUT_FIFO_DATA_SIZE)

#define OFFSET_INPUT_FIFO 999
#define OFFSET_OUTPUT_FIFO 111

#define CLOCK_DPU_SELF 400000000

#define MS_CLOCK_DPU_SELF 400000