
#include "defs.h"
#include "mutex.h"
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

#include <alloc.h>

#include <limits.h>
#include <string.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <defs.h>
#include <mram.h>
#include <perfcounter.h>
#include <stdint.h>
#include <stdio.h>

#include "../common/common.h"

#include <stdint.h>

#define MAX_VALUE 1e9

#define DATA_TYPE uint8_t
#define ID_TYPE int64_t
#define DIST_TYPE int32_t

static bool cmp(DIST_TYPE a, DIST_TYPE b) { return a > b; }

static bool cmp2(DIST_TYPE a1, DIST_TYPE b1, ID_TYPE a2, ID_TYPE b2) { return (a1 > b1) || ((a1 == b1) && (a2 > b2)); }

void heap_pop1(int64_t k, DIST_TYPE *bh_val, ID_TYPE *bh_ids)
{
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
        if ((i2 == k + 1) || cmp2(bh_val[i1], bh_val[i2], bh_ids[i1], bh_ids[i2])) {
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

void heap_push1(int64_t k, DIST_TYPE *bh_val, ID_TYPE *bh_ids, DIST_TYPE val, ID_TYPE id)
{
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

void heap_replace_top1(int64_t k, DIST_TYPE *bh_val, ID_TYPE *bh_ids, DIST_TYPE val, ID_TYPE id)
{
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
        if ((i2 == k + 1) || cmp2(bh_val[i1], bh_val[i2], bh_ids[i1], bh_ids[i2])) {
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

void heap_heapify1(int64_t k, DIST_TYPE *bh_val, ID_TYPE *bh_ids, const DIST_TYPE *x, const ID_TYPE *ids, int64_t k0)
{
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

void heap_addn1(int64_t k, DIST_TYPE *bh_val, ID_TYPE *bh_ids, const DIST_TYPE *x, const ID_TYPE *ids, int64_t n)
{
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

int64_t heap_reorder1(int64_t k, DIST_TYPE *bh_val, ID_TYPE *bh_ids)
{
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

/*---------------barrir------------*/
BARRIER_INIT(barrier, NR_TASKLETS);

__mram DATA_TYPE data[SEG_NUM * SLOT_L * LUT_SIZE];
__mram ID_TYPE data_id[SLOT_L];

__mram DIST_TYPE part_dis[SLOT_L]; // 存储分段距离

__mram DIST_TYPE LUT[MY_PQ_M * MY_PQ_CLUSTER];

__host DIST_TYPE lutw[LUT_SIZE * MY_PQ_CLUSTER];

__host DATA_TYPE data_wram[NR_TASKLETS * DSIZE * LUT_SIZE];
__host ID_TYPE id_wram[NR_TASKLETS * DSIZE];
__host DIST_TYPE part_dis_wram[NR_TASKLETS * DSIZE];

__host DIST_TYPE local_dis_all[NR_TASKLETS * K_DPU];
__host ID_TYPE local_id_all[NR_TASKLETS * K_DPU];

__host DIST_TYPE result_v[K_DPU];
__host ID_TYPE result_id[K_DPU];

__host DIST_TYPE best_value = 0;

__host DIST_TYPE best_value_loop = 0;

//先计算一部分，得到best_value，

int32_t count_exit = 0;

bool use_best_value_loop = false;

void task_perf()
{

    int t_id = me(); // 当前线程ID (0 ≤ t_id < NR_TASKLETS)

    DIST_TYPE *local_dis = &local_dis_all[t_id * K_DPU];
    ID_TYPE *local_id = &local_id_all[t_id * K_DPU];

    heap_heapify1(K_DPU, local_dis, local_id, NULL, NULL, 0);
    if (t_id == 0) {
        heap_heapify1(K_DPU, result_v, result_id, NULL, NULL, 0);
    }
    barrier_wait(&barrier);

    /* ========== 阶段1：分段计算部分距离 ========== */

    // ====================get a best value =============================================

    int totallen = PRE_COMPUTE;

    int chunk_size = totallen / NR_TASKLETS;
    chunk_size = (chunk_size + 7) & ~7; // 对齐到8的倍数

    int start = t_id * chunk_size;
    if (t_id == NR_TASKLETS - 1) {
        chunk_size = totallen - start; // 最后一个线程处理剩余数据
    }

    printf("first loop, start=%d, chunk_size=%d\n", start, chunk_size);

    barrier_wait(&barrier);

    for (int seg = 0; seg < SEG_NUM; seg++) {
        int seg_len = MY_PQ_M / SEG_NUM; // 当前段的维度数
        int lut_start = seg * seg_len; // 当前段在LUT中的起始位置

        if (t_id == 0) {
            for (int l = 0; l < seg_len; l++) {
                mram_read((__mram_ptr void *)&LUT[(lut_start + l) * MY_PQ_CLUSTER], &lutw[l * MY_PQ_CLUSTER],
                    MY_PQ_CLUSTER * sizeof(DIST_TYPE));
            }
        }
        barrier_wait(&barrier);

        for (int i = start; i < start + chunk_size; i += DSIZE) {

            int use_len = DSIZE;

            if ((i + DSIZE) >= (start + chunk_size)) {
                use_len = (start + chunk_size) - i;
            }

            mram_read((__mram_ptr void *)&data[seg * (SLOT_L * LUT_SIZE) + i * LUT_SIZE], &data_wram[t_id * DSIZE * LUT_SIZE],
                DSIZE * LUT_SIZE * sizeof(DATA_TYPE));

            mram_read((__mram_ptr void *)&part_dis[i], &part_dis_wram[t_id * DSIZE], DSIZE * sizeof(DIST_TYPE));

            if (seg == SEG_NUM - 1) {
                mram_read((__mram_ptr void *)&data_id[i], &id_wram[t_id * DSIZE], DSIZE * sizeof(ID_TYPE));
            }
            for (int j = 0; j < use_len; j++) {
                DIST_TYPE seg_sum = part_dis_wram[t_id * DSIZE + j];

                for (int l = 0; l < seg_len; l++) {
                    uint8_t pqcode = data_wram[t_id * DSIZE * LUT_SIZE + j * LUT_SIZE + l];

                    seg_sum += lutw[l * MY_PQ_CLUSTER + pqcode];
                }

                part_dis_wram[t_id * DSIZE + j] = seg_sum;

                if (seg == SEG_NUM - 1) {
                    if (seg_sum < local_dis[0]) {
                        heap_replace_top1(K_DPU, local_dis, local_id, seg_sum, id_wram[t_id * DSIZE + j]);
                    }
                }
            }
            if (seg != SEG_NUM - 1) {

                mram_write(&part_dis_wram[t_id * DSIZE], (__mram_ptr void *)&part_dis[i], DSIZE * sizeof(DIST_TYPE));
            }
        }
    }

    /* ========== 结果合并与输出 ========== */
    barrier_wait(&barrier);
    if (t_id == 0) {
        // 合并所有线程的局部堆
        for (int i = 0; i < NR_TASKLETS; i++) {
            for (int j = 0; j < K_DPU; j++) {
                if (local_dis_all[i * K_DPU + j] < result_v[0]) {
                    heap_replace_top1(
                        K_DPU, result_v, result_id, local_dis_all[i * K_DPU + j], local_id_all[i * K_DPU + j]);
                }
            }
        }

        best_value = result_v[0];
        for (int j = 1; j < K_DPU; j++) {
            if (result_v[j] < best_value) {
                best_value = result_v[j];
            }
            printf("result_v[%d]=%d, result_id[%d]=%lld\n", j, result_v[j], j, result_id[j]);
        }

        printf("best_value=%d\n", best_value);
    }

    if (t_id == 0) {
        heap_heapify1(K_DPU, result_v, result_id, NULL, NULL, 0);
    }

    barrier_wait(&barrier);

    // ====================use a best value =============================================

    totallen = SLOT_L - PRE_COMPUTE;
    chunk_size = totallen / NR_TASKLETS;
    chunk_size = (chunk_size + 7) & ~7; // 对齐到8的倍数

    start = t_id * chunk_size + PRE_COMPUTE;
    if (t_id == NR_TASKLETS - 1) {
        chunk_size = SLOT_L - start; // 最后一个线程处理剩余数据
    }

    printf("start=%d, chunk_size=%d\n", start, chunk_size);

    barrier_wait(&barrier);

    for (int seg = 0; seg < SEG_NUM; seg++) {
        int seg_len = MY_PQ_M / SEG_NUM; // 当前段的维度数
        int lut_start = seg * seg_len; // 当前段在LUT中的起始位置

        if (t_id == 0) {
            for (int l = 0; l < seg_len; l++) {
                mram_read((__mram_ptr void *)&LUT[(lut_start + l) * MY_PQ_CLUSTER], &lutw[l * MY_PQ_CLUSTER],
                    MY_PQ_CLUSTER * sizeof(DIST_TYPE));
            }
            if (use_best_value_loop) {
                best_value_loop = (best_value / SEG_NUM) * (seg + 1) - 1000;
            } else {
                best_value_loop = best_value;
            }
            if (seg == SEG_NUM - 1) {
                best_value_loop = best_value;
            }
            printf("best_value_loop=%d\n", best_value_loop);
        }
        barrier_wait(&barrier);

        for (int i = start; i < start + chunk_size; i += DSIZE) {

            int use_len = DSIZE;

            if ((i + DSIZE) >= (start + chunk_size)) {
                use_len = (start + chunk_size) - i;
            }

            mram_read((__mram_ptr void *)&data[seg * (SLOT_L * LUT_SIZE) + i * LUT_SIZE], &data_wram[t_id * DSIZE * LUT_SIZE],
                DSIZE * LUT_SIZE * sizeof(DATA_TYPE));

            mram_read((__mram_ptr void *)&part_dis[i], &part_dis_wram[t_id * DSIZE], DSIZE * sizeof(DIST_TYPE));

            if (seg == SEG_NUM - 1) {
                mram_read((__mram_ptr void *)&data_id[i], &id_wram[t_id * DSIZE], DSIZE * sizeof(ID_TYPE));
            }
            for (int j = 0; j < use_len; j++) {
                DIST_TYPE seg_sum = part_dis_wram[t_id * DSIZE + j];

                for (int l = 0; l < seg_len; l += CHECK_SIZE) {
                    for (int check_id = 0; check_id < CHECK_SIZE; check_id++) {

                        uint8_t pqcode = data_wram[t_id * DSIZE * LUT_SIZE + j * LUT_SIZE + l + check_id];

                        seg_sum += lutw[(l + check_id) * MY_PQ_CLUSTER + pqcode];
                    }
                }

                part_dis_wram[t_id * DSIZE + j] = seg_sum;

                if (seg == SEG_NUM - 1) {
                    if (seg_sum < local_dis[0]) {
                        heap_replace_top1(K_DPU, local_dis, local_id, seg_sum, id_wram[t_id * DSIZE + j]);
                    }
                }
            }
            if (seg != SEG_NUM - 1) {
                mram_write(&part_dis_wram[t_id * DSIZE], (__mram_ptr void *)&part_dis[i], DSIZE * sizeof(DIST_TYPE));
            }
        }
    }

    barrier_wait(&barrier);

    barrier_wait(&barrier);

    /* ========== 结果合并与输出 ========== */
    barrier_wait(&barrier);
    if (t_id == 0) {
        // 合并所有线程的局部堆
        for (int i = 0; i < NR_TASKLETS; i++) {
            for (int j = 0; j < K_DPU; j++) {
                if (local_dis_all[i * K_DPU + j] < result_v[0]) {
                    heap_replace_top1(
                        K_DPU, result_v, result_id, local_dis_all[i * K_DPU + j], local_id_all[i * K_DPU + j]);
                }
            }
        }
    }
    barrier_wait(&barrier);
}

__host uint64_t cycles;

int main()
{
    printf("tasklet %u: stack = %u\n", me(), check_stack());
    if (me() == 0) {

        uint32_t bytes_compute = 4 * 256 * MY_PQ_M / SEG_NUM + NR_TASKLETS * (MY_PQ_M / SEG_NUM + 8 + 4) * DSIZE
            + NR_TASKLETS * 10 * (8 + 4) + 10 * (8 + 4);

        uint32_t bytes_used = sizeof(lutw) + sizeof(data_wram) + sizeof(id_wram) + sizeof(part_dis_wram) + sizeof(local_dis_all)
            + sizeof(local_id_all) + sizeof(result_v) + sizeof(result_id);
        printf("Bytes used: %u bytes\n", bytes_used);
        printf("Bytes for compute: %u bytes\n", bytes_compute);
    }

    uint32_t tasklet_id = me();

    if (tasklet_id == 0) {
        printf("hello dpu!\n");
        perfcounter_config(COUNT_CYCLES, true);
    }
    task_perf();

    if (tasklet_id == 0) {
        for (int j = 0; j < K_DPU; j++) {
            printf("result_v[%d]=%d, result_id[%d]=%lld\n", j, result_v[j], j, result_id[j]);
        }
        printf("best_value=%d, count_exit=%d\n", best_value, count_exit);
    }

    cycles = (uint64_t)perfcounter_get();

    return 0;
}