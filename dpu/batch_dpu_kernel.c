#include <assert.h>
#include <barrier.h>
#include <ctype.h>
#include <defs.h>
#include <mram.h>
#include <perfcounter.h>
#include <profiling.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wramfifo.h>
#include "../common/common.h"
#include "cyclecounter.h"
#include "defs.h"
#include "mutex.h"

#include <alloc.h>

/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * C++ support for heaps. The set of functions is tailored for efficient
 * similarity search.
 *
 * There is no specific object for a heap, and the functions that operate on a
 * single heap are
d, because heaps are often small. More complex
 * functions are implemented in Heaps.cpp
 *
 * All heap functions rely on a C template class that define the type of the
 * keys and values and their ordering (increasing with CMax and decreasing with
 * Cmin). The C types are defined in ordered_key_value.h
 */

#include <limits.h>
#include <string.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

// #define HeapForIP
// HeapForL2 = CMax<float, int64_t>
// HeapForIP = CMin<float, int64_t>;

/*******************************************************************
 * Basic heap ops: push and pop
 *******************************************************************/

/** Pops the top element from the heap defined by bh_val[0..k-1] and
 * bh_ids[0..k-1].  on output the element at k-1 is undefined.
 */

/** Pushes the element (val, ids) into the heap bh_val[0..k-2] and
 * bh_ids[0..k-2].  on output the element at k-1 is defined.
 */

#ifdef HeapForIP

#define MAX_VALUE -1e9

static bool cmp(DIST_TYPE a, DIST_TYPE b) {
    return a < b;
}

static bool cmp2(DIST_TYPE a1, DIST_TYPE b1, ID_TYPE a2, ID_TYPE b2) {
    return (a1 < b1) || ((a1 == b1) && (a2 < b2));
}

#else

#define MAX_VALUE 1e9

static bool cmp(DIST_TYPE a, DIST_TYPE b) {
    return a > b;
}

static bool cmp2(DIST_TYPE a1, DIST_TYPE b1, ID_TYPE a2, ID_TYPE b2) {
    return (a1 > b1) || ((a1 == b1) && (a2 > b2));
}
#endif

void heap_pop1(int64_t k, DIST_TYPE* bh_val, ID_TYPE* bh_ids) {
    bh_val--; /* Use 1-based indexing for easier node->child translation */
    bh_ids--;
    DIST_TYPE val = bh_val[k];
    ID_TYPE id = bh_ids[k];
    int64_t i = 1, i1, i2;
    while (1) {
        i1 = i << 1;
        i2 = i1 + 1;
        if (i1 > k)
            break;
        if ((i2 == k + 1) ||
            cmp2(bh_val[i1], bh_val[i2], bh_ids[i1], bh_ids[i2])) {
            if (cmp2(val, bh_val[i1], id, bh_ids[i1])) {
                break;
            }
            bh_val[i] = bh_val[i1];
            bh_ids[i] = bh_ids[i1];
            i = i1;
        } else {
            if (cmp2(val, bh_val[i2], id, bh_ids[i2])) {
                break;
            }
            bh_val[i] = bh_val[i2];
            bh_ids[i] = bh_ids[i2];
            i = i2;
        }
    }
    bh_val[i] = bh_val[k];
    bh_ids[i] = bh_ids[k];
}

void heap_push1(
        int64_t k,
        DIST_TYPE* bh_val,
        ID_TYPE* bh_ids,
        DIST_TYPE val,
        ID_TYPE id) {
    bh_val--; /* Use 1-based indexing for easier node->child translation */
    bh_ids--;
    int64_t i = k, i_father;
    while (i > 1) {
        i_father = i >> 1;
        if (!cmp2(val, bh_val[i_father], id, bh_ids[i_father])) {
            /* the heap structure is ok */
            break;
        }
        bh_val[i] = bh_val[i_father];
        bh_ids[i] = bh_ids[i_father];
        i = i_father;
    }
    bh_val[i] = val;
    bh_ids[i] = id;
}

/**
 * Replaces the top element from the heap defined by bh_val[0..k-1] and
 * bh_ids[0..k-1], and for identical bh_val[] values also sorts by bh_ids[]
 * values.
 */

void heap_replace_top1(
        int64_t k,
        DIST_TYPE* bh_val,
        ID_TYPE* bh_ids,
        DIST_TYPE val,
        ID_TYPE id) {
    bh_val--; /* Use 1-based indexing for easier node->child translation */
    bh_ids--;
    int64_t i = 1, i1, i2;
    while (1) {
        i1 = i << 1;
        i2 = i1 + 1;
        if (i1 > k) {
            break;
        }

        // Note that cmp2() is a bool function answering
        // `(a1 > b1) || ((a1 == b1) && (a2 > b2))` for max
        // heap and same with the `<` sign for min heap.
        if ((i2 == k + 1) ||
            cmp2(bh_val[i1], bh_val[i2], bh_ids[i1], bh_ids[i2])) {
            if (cmp2(val, bh_val[i1], id, bh_ids[i1])) {
                break;
            }
            bh_val[i] = bh_val[i1];
            bh_ids[i] = bh_ids[i1];
            i = i1;
        } else {
            if (cmp2(val, bh_val[i2], id, bh_ids[i2])) {
                break;
            }
            bh_val[i] = bh_val[i2];
            bh_ids[i] = bh_ids[i2];
            i = i2;
        }
    }
    bh_val[i] = val;
    bh_ids[i] = id;
}

/*******************************************************************
 * Heap initialization
 *******************************************************************/

/* Initialization phase for the heap (with unconditionnal pushes).
 * Store k0 elements in a heap containing up to k values. Note that
 * (bh_val, bh_ids) can be the same as (x, ids) */

void heap_heapify1(
        int64_t k,
        DIST_TYPE* bh_val,
        ID_TYPE* bh_ids,
        const DIST_TYPE* x,
        const ID_TYPE* ids,
        int64_t k0) {
    if (k0 > 0)
        assert(x);

    if (ids) {
        for (int64_t i = 0; i < k0; i++)
            heap_push1(i + 1, bh_val, bh_ids, x[i], ids[i]);
    } else {
        for (int64_t i = 0; i < k0; i++)
            heap_push1(i + 1, bh_val, bh_ids, x[i], i);
    }

    for (int64_t i = k0; i < k; i++) {
        bh_val[i] = MAX_VALUE;
        bh_ids[i] = -1;
    }
}

/*******************************************************************
 * Add n elements to the heap
 *******************************************************************/

/* Add some elements to the heap  */

void heap_addn1(
        int64_t k,
        DIST_TYPE* bh_val,
        ID_TYPE* bh_ids,
        const DIST_TYPE* x,
        const ID_TYPE* ids,
        int64_t n) {
    int64_t i;
    if (ids)
        for (i = 0; i < n; i++) {
            if (cmp(bh_val[0], x[i])) {
                heap_replace_top1(k, bh_val, bh_ids, x[i], ids[i]);
            }
        }
    else
        for (i = 0; i < n; i++) {
            if (cmp(bh_val[0], x[i])) {
                heap_replace_top1(k, bh_val, bh_ids, x[i], i);
            }
        }
}

/*******************************************************************
 * Heap finalization (reorder elements)
 *******************************************************************/

/* This function maps a binary heap into a sorted structure.
   It returns the number  */

int64_t heap_reorder1(int64_t k, DIST_TYPE* bh_val, ID_TYPE* bh_ids) {
    int64_t i, ii;

    for (i = 0, ii = 0; i < k; i++) {
        /* top element should be put at the end of the list */
        DIST_TYPE val = bh_val[0];
        ID_TYPE id = bh_ids[0];

        /* boundary case: we will over-ride this value if not a true element */
        heap_pop1(k - i, bh_val, bh_ids);
        bh_val[k - ii - 1] = val;
        bh_ids[k - ii - 1] = id;
        if (id != -1)
            ii++;
    }
    /* Count the number of elements which are effectively returned */
    int64_t nel = ii;

    memmove(bh_val, bh_val + k - ii, ii * sizeof(*bh_val));
    memmove(bh_ids, bh_ids + k - ii, ii * sizeof(*bh_ids));

    for (; ii < k; ii++) {
        bh_val[ii] = MAX_VALUE;
        bh_ids[ii] = -1;
    }
    return nel;
}

/*---------------mutex----------------*/
MUTEX_INIT(heap_mtx);

/*---------------barrir------------*/
BARRIER_INIT(barrier, NR_TASKLETS);

/*--------------------fifo info-----------------------*/
INPUT_FIFO_INIT(input_fifo, INPUT_FIFO_PTR_SIZE, INPUT_FIFO_DATA_SIZE);
OUTPUT_FIFO_INIT(output_fifo, OUTPUT_FIFO_PTR_SIZE, OUTPUT_FIFO_DATA_SIZE);

__host volatile uint64_t loop = 1;

/*---------------dpu info-----------------*/
__host DPU_INFO_COMMON dpu_info_common;

/*------------------dataset----------------------*/

__mram volatile DATA_TYPE data[SLOT_NUM * SLOT_L * MY_PQ_M + 1000];
__mram volatile ID_TYPE data_id[SLOT_NUM * SLOT_L + 1000];

#define INDEX_DATA(i, j, k) ((i) * (SLOT_L) * (MY_PQ_M) + (j) * (MY_PQ_M) + (k))
#define INDEX_ID(i, j) ((i) * (SLOT_L) + (j))

__host int64_t num_query_this_batch;

/*------------------query-------------------*/
__mram dpu_fifo_input_t query[MAX_DPUBATCH];

/*-------------------result-----------------*/
__mram dpu_fifo_output_t result[MAX_DPUBATCH];
__host dpu_fifo_output_t result_wram;

/*-----------cache------------------*/
#define DSIZE 10
#define IDSIZE DSIZE
__host DIST_TYPE lutw[LUT_SIZE];

__host DATA_TYPE data_wram[NR_TASKLETS * DSIZE * MY_PQ_M];

__host ID_TYPE id_wram[NR_TASKLETS * IDSIZE];

#define INDEX_DATA_W(i, j, k) \
    ((i) * (DSIZE) * (MY_PQ_M) + (j) * (MY_PQ_M) + (k))

#define INDEX_ID_W(i, j) ((i) * (IDSIZE) + (j))

__host DIST_TYPE local_dis_all[NR_TASKLETS * MAX_K];
__host ID_TYPE local_id_all[NR_TASKLETS * MAX_K];

__mram uint64_t justaddtime;
void delay(int ms) {
    for (int ii = 0; ii < ms * 4; ii++) {
        for (int i = 1; i < 2; i++) {
            for (int j = 1; j < 2; j++) {
                int k = 3;
                for (int l = 0; l < MY_PQ_M; l++) {
                    justaddtime += (i + j + k + l) % 100 + ii * j * k * l % 99;
                }
            }
        }
    }
}

void task_perf(int q_r_id) {
    // delay(500);
    perfcounter_cycles cycles;

    int t_id = me();
    int slot_id = query[q_r_id].slot_id;

    DIST_TYPE* local_dis = &local_dis_all[t_id * MAX_K];
    ID_TYPE* local_id = &local_id_all[t_id * MAX_K];

    barrier_wait(&barrier);

    heap_heapify1(query[q_r_id].k, local_dis, local_id, NULL, NULL, 0);

    if (t_id == 0) {
        heap_heapify1(
                query[q_r_id].k,
                result_wram.result_v,
                result_wram.result_id,
                NULL,
                NULL,
                0);
    }
    barrier_wait(&barrier);

    if (t_id == 0) {
        for (int i = 0; i < MY_PQ_M; i++) {
            mram_read(
                    (__mram_ptr void*)&query[q_r_id].LUT[i * MY_PQ_CLUSTER],
                    &lutw[i * MY_PQ_CLUSTER],
                    MY_PQ_CLUSTER * sizeof(DIST_TYPE));
        }
    }

    barrier_wait(&barrier);

    int totallen = query[q_r_id].shard_l;

    int chunk_size = totallen / NR_TASKLETS;
    chunk_size = (chunk_size + 7) & ~7;

    int start = t_id * chunk_size;
    if (t_id == NR_TASKLETS - 1) {
        chunk_size = totallen - start;
    }

    if (t_id == 0) {
        timer_start(&cycles);
    }

    barrier_wait(&barrier);

    for (int i = start; i < (start + chunk_size); i += DSIZE) {
        int use_len = DSIZE;

        if ((i + DSIZE) >= (start + chunk_size)) {
            use_len = (start + chunk_size) - i;
        }

        mram_read(
                (__mram_ptr void*)&data_id[INDEX_ID(slot_id, i)],
                &id_wram[INDEX_ID_W(t_id, 0)],
                DSIZE * sizeof(ID_TYPE));
        // mram_read(
        //         (__mram_ptr void*)&data[INDEX_DATA(slot_id, i, 0)],
        //         &data_wram[INDEX_DATA_W(t_id, 0, 0)],
        //         DSIZE * MY_PQ_M * sizeof(DATA_TYPE));

        uint64_t source_addr = (uint64_t)&data[INDEX_DATA(slot_id, i, 0)];
        if (source_addr % 8 != 0) {
            for (int j = 0; j < use_len; j++) {
                for (int l = 0; l < MY_PQ_M; l++) {
                    data_wram[INDEX_DATA_W(t_id, j, l)] =
                            data[INDEX_DATA(slot_id, i + j, l)];
                }
            }

        } else {
            mram_read(
                    (__mram_ptr void*)&data[INDEX_DATA(slot_id, i, 0)],
                    &data_wram[INDEX_DATA_W(t_id, 0, 0)],
                    DSIZE * MY_PQ_M * sizeof(DATA_TYPE));
        }

        for (int j = 0; j < use_len; j++) {
            DIST_TYPE sum = query[q_r_id].dis0;

            ID_TYPE id = id_wram[INDEX_ID_W(t_id, j)];
            // ID_TYPE id = data_id[INDEX_ID(slot_id, i + j)];

            for (int l = 0; l < MY_PQ_M; l++) {
                uint8_t pqcode = data_wram[INDEX_DATA_W(t_id, j, l)];

                // uint8_t pqcode = data[INDEX_DATA(slot_id, i + j, l)];

                sum += lutw[l * MY_PQ_CLUSTER + pqcode];
            }
            if (local_dis[0] > sum) {
                heap_replace_top1(
                        query[q_r_id].k, local_dis, local_id, sum, id);
            }

            // result_wram.dpu_id+=sum;
        }
    }

    if (me() == 0) {
        int64_t cycle_all = timer_stop(&cycles);
        int64_t cycle_per_vec = cycle_all / query[q_r_id].shard_l;
        result_wram.cycles = cycle_all;
        result_wram.cycle_per_vec = cycle_per_vec;
    }

    barrier_wait(&barrier);

    // if (me() == 0) {
    //     for (int j = 0; j < totallen; j++) {
    //         DIST_TYPE sum = query[q_r_id].dis0;
    //         ID_TYPE id = data_id[INDEX_ID(slot_id, j)];
    //         for (int l = 0; l < MY_PQ_M; l++) {
    //             uint8_t pqcode = data[INDEX_DATA(slot_id, j, l)];

    //             sum += query[q_r_id].LUT[l * MY_PQ_CLUSTER + pqcode];
    //         }
    //         if (local_dis[0] > sum) {
    //             heap_replace_top1(query[q_r_id].k, local_dis, local_id, sum,
    //             id);
    //         }
    //     }
    // }

    // barrier_wait(&barrier);

    /*-----------merge----------*/
    if (t_id == 0) {
        DIST_TYPE* simi_values = result_wram.result_v;
        ID_TYPE* simi_ids = result_wram.result_id;

        for (int i = 0; i < NR_TASKLETS; i++) {
            for (int j = 0; j < query[q_r_id].k; j++) {
                if (local_dis_all[i * MAX_K + j] < simi_values[0]) {
                    heap_replace_top1(
                            query[q_r_id].k,
                            simi_values,
                            simi_ids,
                            local_dis_all[i * MAX_K + j],
                            local_id_all[i * MAX_K + j]);
                }
            }
        }

        result_wram.k = query[q_r_id].k;
        result_wram.q_id = query[q_r_id].q_id;
        result_wram.dpu_id = query[q_r_id].dpu_id;
        mram_write(&result_wram, &result[q_r_id], sizeof(dpu_fifo_output_t));
    }

    barrier_wait(&barrier);
}

void kernel_ann() {
    for (int i = 0; i < num_query_this_batch; i++) {
        task_perf(i);

        barrier_wait(&barrier);
    }
}

int main() {
    if (me() == 0) {
        perfcounter_config(COUNT_CYCLES, true);
        result[0].start = (uint64_t)perfcounter_get();
    }

    barrier_wait(&barrier);

    kernel_ann();

    if (me() == 0) {
        result[0].end = (uint64_t)perfcounter_get();
    }

    return 0;
}
