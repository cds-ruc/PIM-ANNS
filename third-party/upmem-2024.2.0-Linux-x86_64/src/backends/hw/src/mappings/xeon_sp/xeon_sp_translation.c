/* Copyright 2020 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE
#include <stdint.h>

#include "dpu_region_address_translation.h"

#include <errno.h>
#include <limits.h>
#include <numa.h>
#include <numaif.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <x86intrin.h>
#include <immintrin.h>
#include <sys/sysinfo.h>
#include "static_verbose.h"
#include "dpu_attributes.h"



static inline void clflushopt(void *addr)
{

    static int isClflushoptEnabled =
        true;
    static int isClwbEnabled = true;
    static int isClflushEnabled = true;
    if (isClflushoptEnabled)
    {
        asm volatile(".byte 0x66; clflush %0" : "+m"(*(volatile char *)(addr)));
    }
    else if (isClwbEnabled)
    {
        asm volatile(".byte 0x66; xsaveopt %0" : "+m"(*(volatile char *)(addr)));
    }
    else if (isClflushEnabled)
    {
        asm volatile("clflush %0" : "+m"(*(volatile char *)addr));
    }
    else
    {
        // assert(0);
        *(int*)(0) =0;
        return;
    }
}

static inline void clflushopt_range(void *des, size_t size)
{
    char *addr = (char *)(des);
    size = size + ((uint64_t)(addr) & (64 - 1));
    for (size_t i = 0; i < size; i += 64)
    {
        clflushopt(addr + i);
    }
    asm volatile("sfence" ::: "memory");
}

static struct verbose_control *this_vc;
static struct verbose_control *
__vc()
{
    if (this_vc == NULL)
    {
        this_vc = get_verbose_control_for("hw");
    }
    return this_vc;
}

#define NB_REAL_CIS (8)
#define NB_ELEM_MATRIX 8

void byte_interleave(uint64_t *input, uint64_t *output)
{
    unsigned int i, j;

    for (i = 0; i < NB_ELEM_MATRIX; ++i)
        for (j = 0; j < sizeof(uint64_t); ++j)
            ((uint8_t *)&output[i])[j] = ((uint8_t *)&input[j])[i];
}

/* SSE4.1 and AVX2 implementations come from:
 * https://stackoverflow.com/questions/42162270/a-better-8x8-bytes-matrix-transpose-with-sse
 */
void byte_interleave_sse4_1(uint64_t *input, uint64_t *output)
{
    char *A = (char *)input;
    char *B = (char *)output;

    __m128i pshufbcnst_0 = _mm_set_epi8(15, 7, 11, 3, 13, 5, 9, 1, 14, 6, 10, 2, 12, 4, 8, 0);

    __m128i pshufbcnst_1 = _mm_set_epi8(13, 5, 9, 1, 15, 7, 11, 3, 12, 4, 8, 0, 14, 6, 10, 2);

    __m128i pshufbcnst_2 = _mm_set_epi8(11, 3, 15, 7, 9, 1, 13, 5, 10, 2, 14, 6, 8, 0, 12, 4);

    __m128i pshufbcnst_3 = _mm_set_epi8(9, 1, 13, 5, 11, 3, 15, 7, 8, 0, 12, 4, 10, 2, 14, 6);
    __m128 B0, B1, B2, B3, T0, T1, T2, T3;

    B0 = _mm_loadu_ps((float *)&A[0]);
    B1 = _mm_loadu_ps((float *)&A[16]);
    B2 = _mm_loadu_ps((float *)&A[32]);
    B3 = _mm_loadu_ps((float *)&A[48]);

    B1 = _mm_shuffle_ps(B1, B1, 0b10110001);
    B3 = _mm_shuffle_ps(B3, B3, 0b10110001);
    T0 = _mm_blend_ps(B0, B1, 0b1010);
    T1 = _mm_blend_ps(B2, B3, 0b1010);
    T2 = _mm_blend_ps(B0, B1, 0b0101);
    T3 = _mm_blend_ps(B2, B3, 0b0101);

    B0 = _mm_castsi128_ps(_mm_shuffle_epi8(_mm_castps_si128(T0), pshufbcnst_0));
    B1 = _mm_castsi128_ps(_mm_shuffle_epi8(_mm_castps_si128(T1), pshufbcnst_1));
    B2 = _mm_castsi128_ps(_mm_shuffle_epi8(_mm_castps_si128(T2), pshufbcnst_2));
    B3 = _mm_castsi128_ps(_mm_shuffle_epi8(_mm_castps_si128(T3), pshufbcnst_3));

    T0 = _mm_blend_ps(B0, B1, 0b1010);
    T1 = _mm_blend_ps(B0, B1, 0b0101);
    T2 = _mm_blend_ps(B2, B3, 0b1010);
    T3 = _mm_blend_ps(B2, B3, 0b0101);
    T1 = _mm_shuffle_ps(T1, T1, 0b10110001);
    T3 = _mm_shuffle_ps(T3, T3, 0b10110001);

    _mm_storeu_ps((float *)&B[0], T0);
    _mm_storeu_ps((float *)&B[16], T1);
    _mm_storeu_ps((float *)&B[32], T2);
    _mm_storeu_ps((float *)&B[48], T3);
}

void byte_interleave_avx2(uint64_t *input, uint64_t *output)
{
    __m256i tm = _mm256_set_epi8(15,
                                 11,
                                 7,
                                 3,
                                 14,
                                 10,
                                 6,
                                 2,
                                 13,
                                 9,
                                 5,
                                 1,
                                 12,
                                 8,
                                 4,
                                 0,

                                 15,
                                 11,
                                 7,
                                 3,
                                 14,
                                 10,
                                 6,
                                 2,
                                 13,
                                 9,
                                 5,
                                 1,
                                 12,
                                 8,
                                 4,
                                 0);
    char *src1 = (char *)input, *dst1 = (char *)output;

    __m256i vindex = _mm256_setr_epi32(0, 8, 16, 24, 32, 40, 48, 56);
    __m256i perm = _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7);

    __m256i load0 = _mm256_i32gather_epi32((int *)src1, vindex, 1);
    __m256i load1 = _mm256_i32gather_epi32((int *)(src1 + 4), vindex, 1);

    __m256i transpose0 = _mm256_shuffle_epi8(load0, tm);
    __m256i transpose1 = _mm256_shuffle_epi8(load1, tm);

    __m256i final0 = _mm256_permutevar8x32_epi32(transpose0, perm);
    __m256i final1 = _mm256_permutevar8x32_epi32(transpose1, perm);

    _mm256_storeu_si256((__m256i *)&dst1[0], final0);
    _mm256_storeu_si256((__m256i *)&dst1[32], final1);
}

__API_SYMBOL__ void byte_interleave_avx512(uint64_t *input, uint64_t *output, bool use_stream)
{
    __m512i load = _mm512_loadu_si512(input);

    // LEVEL 0
    __m512i vindex = _mm512_setr_epi32(0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15);
    __m512i gathered = _mm512_permutexvar_epi32(vindex, load);

    // LEVEL 1
    __m512i mask = _mm512_set_epi64(0x0f0b07030e0a0602ULL,
                                    0x0d0905010c080400ULL,

                                    0x0f0b07030e0a0602ULL,
                                    0x0d0905010c080400ULL,

                                    0x0f0b07030e0a0602ULL,
                                    0x0d0905010c080400ULL,

                                    0x0f0b07030e0a0602ULL,
                                    0x0d0905010c080400ULL);

    __m512i transpose = _mm512_shuffle_epi8(gathered, mask);

    // LEVEL 2
    __m512i perm = _mm512_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7, 8, 12, 9, 13, 10, 14, 11, 15);
    __m512i final = _mm512_permutexvar_epi32(perm, transpose);

    if (use_stream)
    {
        _mm512_stream_si512((void *)output, final);
        return;
    }

    _mm512_storeu_si512((void *)output, final);
}

__API_SYMBOL__ void just_one_chip_byte_interleave_avx512(uint64_t *input, __attribute__((unused)) uint64_t *output, int chip_id)
{
    int offset = chip_id * 8;
    for (int i = 0; i < 8; i++)
    {
        // memcpy((uint8_t *)output + i * 8 + chip_id, (uint8_t *)input + offset + i, 1);
        ((uint8_t *)output)[i * 8 + chip_id] = ((uint8_t *)input)[offset + i];
    }
}

#ifdef __AVX512VBMI__
void byte_interleave_avx512vbmi(uint64_t *src, uint64_t *dst, bool use_stream)
{
    const __m512i trans8x8shuf = _mm512_set_epi64(0x0f0b07030e0a0602ULL,
                                                  0x0d0905010c080400ULL,

                                                  0x0f0b07030e0a0602ULL,
                                                  0x0d0905010c080400ULL,

                                                  0x0f0b07030e0a0602ULL,
                                                  0x0d0905010c080400ULL,

                                                  0x0f0b07030e0a0602ULL,
                                                  0x0d0905010c080400ULL);

    __m512i vsrc = _mm512_loadu_si512(src);
    __m512i shuffled = _mm512_permutexvar_epi8(trans8x8shuf, vsrc);

    if (use_stream)
    {
        _mm512_stream_si512((void *)dst, shuffled);
        return;
    }

    _mm512_storeu_si512(dst, shuffled);
}
#endif

void write_block_sse4_1(uint8_t *ci_address, uint64_t *data)
{
    __m128i v0 = _mm_set_epi64x(data[0], data[1]);
    __m128i v1 = _mm_set_epi64x(data[2], data[3]);
    __m128i v2 = _mm_set_epi64x(data[4], data[5]);
    __m128i v3 = _mm_set_epi64x(data[6], data[7]);

    _mm_stream_si128((__m128i *)&ci_address[0], v0);
    _mm_stream_si128((__m128i *)&ci_address[16], v1);
    _mm_stream_si128((__m128i *)&ci_address[32], v2);
    _mm_stream_si128((__m128i *)&ci_address[48], v3);
}

void write_block_avx512(uint64_t *ci_address, uint64_t *data)
{
    volatile __m512i zmm;

    zmm = _mm512_setr_epi64(data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
    _mm512_stream_si512((void *)ci_address, zmm);
}

void read_block_avx512(uint64_t *ci_address, uint64_t *output)
{
    volatile __m512i zmm;
    uint64_t *o = (uint64_t *)&zmm;

    zmm = _mm512_stream_load_si512((void *)ci_address);

    output[0] = o[0];
    output[1] = o[1];
    output[2] = o[2];
    output[3] = o[3];
    output[4] = o[4];
    output[5] = o[5];
    output[6] = o[6];
    output[7] = o[7];
}

void xeon_sp_write_to_cis(__attribute__((unused)) struct dpu_region_address_translation *tr,
                          void *base_region_addr,
                          __attribute__((unused)) uint8_t channel_id,
                          void *block_data,
                          __attribute__((unused)) uint32_t block_size)
{
    uint64_t *ci_address;

    ci_address = (uint64_t *)((uint8_t *)base_region_addr + 0x20000);

    byte_interleave_avx512(block_data, ci_address, true);

    tr->one_read = false;
}

void xeon_sp_read_from_cis(__attribute__((unused)) struct dpu_region_address_translation *tr,
                           void *base_region_addr,
                           __attribute__((unused)) uint8_t channel_id,
                           void *block_data,
                           __attribute__((unused)) uint32_t block_size)
{
#define NB_READS 3
    uint64_t input[NB_ELEM_MATRIX];
    uint64_t *ci_address;
    int i;
    uint8_t nb_reads = tr->one_read ? NB_READS - 1 : NB_READS;

    ci_address = (uint64_t *)((uint8_t *)base_region_addr + 0x20000 + 32 * 1024);

    for (i = 0; i < nb_reads; ++i)
    {
        /* FWIW: "data can be speculatively loaded into a cache line just
         * before, during, or after the execution of a CLFLUSH instruction that
         * references the cache line", Volume 2 of the Intel Architectures SW
         * Developer's Manual.
         */
        __builtin_ia32_clflushopt((uint8_t *)ci_address);
        __builtin_ia32_mfence();

        ((volatile uint64_t *)input)[0] = *(ci_address + 0);
        ((volatile uint64_t *)input)[1] = *(ci_address + 1);
        ((volatile uint64_t *)input)[2] = *(ci_address + 2);
        ((volatile uint64_t *)input)[3] = *(ci_address + 3);
        ((volatile uint64_t *)input)[4] = *(ci_address + 4);
        ((volatile uint64_t *)input)[5] = *(ci_address + 5);
        ((volatile uint64_t *)input)[6] = *(ci_address + 6);
        ((volatile uint64_t *)input)[7] = *(ci_address + 7);

        // printf("0x%" PRIx64 "\n", ((uint64_t *)block_data)[0]);
    }

    /* Do not use streaming instructions here because I observed that
     * dpu_planner is quite slowed down when it reads packet->data if
     * packet->data is not cached by this access./
     */
    byte_interleave_avx512(input, block_data, false);

    tr->one_read = true;
}

#define BANK_START(dpu_id) (0x40000 * ((dpu_id) % 4) + ((dpu_id >= 4) ? 0x40 : 0))
#define BANK_OFFSET_NEXT_DATA(i) (i * 16) // For each 64bit word, you must jump 16 * 64bit (2 cache lines)
#define BANK_CHUNK_SIZE 0x20000
#define BANK_NEXT_CHUNK_OFFSET 0x100000

static uint32_t
apply_address_translation_on_mram_offset(uint32_t byte_offset)
{
    /* We have observed that, within the 26 address bits of the MRAM address, we need to apply an address translation:
     *
     * virtual[13: 0] = physical[13: 0]
     * virtual[20:14] = physical[21:15]
     * virtual[   21] = physical[   14]
     * virtual[25:22] = physical[25:22]
     *
     * This function computes the "virtual" mram address based on the given "physical" mram address.
     */

    uint32_t mask_21_to_15 = ((1 << (21 - 15 + 1)) - 1) << 15;
    uint32_t mask_21_to_14 = ((1 << (21 - 14 + 1)) - 1) << 14;
    uint32_t bits_21_to_15 = (byte_offset & mask_21_to_15) >> 15;
    uint32_t bit_14 = (byte_offset >> 14) & 1;
    uint32_t unchanged_bits = byte_offset & ~mask_21_to_14;

    return unchanged_bits | (bits_21_to_15 << 14) | (bit_14 << 21);
}

enum thread_mram_xfer
{
    thread_mram_xfer_read,
    thread_mram_xfer_write,
};

struct thread_mram_args
{
    struct xeon_sp_private *xeon_sp_priv;
    uint8_t thread_id;
    pthread_t tid;
    bool stop_thread;
};

#define MAX_THREAD_PER_POOL (8)
struct xeon_sp_private
{
    struct dpu_region_address_translation *tr;
    void *base_region_addr;
    enum thread_mram_xfer direction;
    struct dpu_transfer_matrix *xfer_matrix;

    uint8_t nb_dpus_per_ci;
    uint8_t nb_threads_for_xfer;
    uint8_t nb_threads;

    struct thread_mram_args threads_args[MAX_THREAD_PER_POOL];

    pthread_barrier_t barrier_threads;
};

// we cannot have more than 31 pool as long as we use a 32-bit integer bitfield for the allocation
#define MAX_NB_POOL (31)
// no need for more pool than channel
// note that last pool is used for transfers whose channel ID is invalid
#define MAX_NB_CHANNEL MAX_NB_POOL
struct xeon_sp_ctx
{
    struct xeon_sp_private pool[MAX_NB_POOL];
    pthread_mutex_t channels_mutex[MAX_NB_CHANNEL];
    pthread_mutex_t mutex;
    uint32_t nb_region_per_channel[MAX_NB_CHANNEL];
};
static struct xeon_sp_ctx xeon_sp_ctx = {
    .channels_mutex = {PTHREAD_MUTEX_INITIALIZER},
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .nb_region_per_channel = {0},
};

#define FOREACH_DPU_MULTITHREAD(dpu_id, idx, start, stop) \
    for ((dpu_id) = (start), (idx) = (start) * NB_REAL_CIS; (dpu_id) < (stop); ++(dpu_id), (idx) += NB_REAL_CIS)

static bool numa_is_available = false;
void __attribute__((constructor)) __setup_numa_availability(void)
{
    if (numa_available() != -1)
        numa_is_available = true;
}

static inline int
channel_id_to_pool_id(int channel_id)
{
    int pool_id;

    if (channel_id == 0xFF) // Invalid channel ID
        pool_id = MAX_NB_CHANNEL - 1;
    else
        pool_id = channel_id;

    return pool_id;
}

/**
 * @brief Structure used as iterator over struct sg_xfer_buffer
 */
struct sg_xfer_buffer_iterator
{
    /** Array of pointers to block. */
    uint8_t **blocks_addr;
    /** Array of lengths of block. */
    uint32_t *blocks_length;
    /** Length of the current block. */
    uint32_t current_block_length;
    /** Pointer to the current block. */
    uint8_t *current_block_addr;
    /** Current index to read from the block. */
    uint32_t current_block_index;
    /** Reference to the current block pointer. */
    uint8_t **blocks_addr_ptr;
    /** Reference to the last block pointer. */
    uint8_t **blocks_addr_ptr_end;
    /** Reference to the current length. */
    uint32_t *blocks_length_ptr;
};

static uint8_t zeros[4096] = {0};

static inline void
init_sg_xfer_buffer_iterator(struct sg_xfer_buffer_iterator *sg_it, struct sg_xfer_buffer *sg_buff)
{
    sg_it->blocks_addr = sg_buff->blocks_addr;
    sg_it->blocks_length = sg_buff->blocks_length;
    sg_it->blocks_addr_ptr = &sg_it->blocks_addr[0];
    sg_it->blocks_addr_ptr_end = &sg_it->blocks_addr[sg_buff->nr_blocks];
    sg_it->blocks_length_ptr = &sg_it->blocks_length[0];
    sg_it->current_block_length = sg_it->blocks_length_ptr[0];
    sg_it->current_block_addr = sg_it->blocks_addr_ptr[0];
    sg_it->current_block_index = 0;

    // zero padding
    sg_it->blocks_length_ptr[sg_buff->nr_blocks] = 4096;
    sg_it->blocks_addr_ptr[sg_buff->nr_blocks] = zeros;
}

static inline void
go_to_next_block(struct sg_xfer_buffer_iterator *sg_it)
{

    sg_it->current_block_index = 0;
    if (!(sg_it->blocks_addr_ptr_end - sg_it->blocks_addr_ptr))
        return;

    sg_it->blocks_addr_ptr++;
    sg_it->current_block_addr = *sg_it->blocks_addr_ptr;

    sg_it->blocks_length_ptr++;
    sg_it->current_block_length = *sg_it->blocks_length_ptr;
}

static inline uint8_t *
get_next_byte(struct sg_xfer_buffer_iterator *sg_it)
{
    uint8_t *byte = &sg_it->current_block_addr[sg_it->current_block_index];
    sg_it->current_block_index++;
    if (sg_it->current_block_index == sg_it->current_block_length)
    {
        if (sg_it->blocks_addr_ptr != sg_it->blocks_addr_ptr_end)
        {
            go_to_next_block(sg_it);
        }
        else
        {
            sg_it->current_block_index = 0;
        }
    }

    return byte;
}

static inline void
get_next_eight_bytes(struct sg_xfer_buffer_iterator *sg_it, uint64_t *out_buffer)
{
    uint32_t length = sg_it->current_block_length;
    uint32_t current = sg_it->current_block_index;
    if (length > 8 + current)
    {
        memcpy(out_buffer, &sg_it->current_block_addr[sg_it->current_block_index], sizeof(uint64_t));
        sg_it->current_block_index += 8;
    }
    else if (length == 8 + current)
    {
        memcpy(out_buffer, &sg_it->current_block_addr[sg_it->current_block_index], sizeof(uint64_t));
        go_to_next_block(sg_it);
    }
    else
    {
        uint8_t *out_buffer_8 = (uint8_t *)out_buffer;
        for (int i = 0; i < 8; i++)
        {
            out_buffer_8[i] = *get_next_byte(sg_it);
        }
    }
}

static inline void
write_next_eight_bytes(struct sg_xfer_buffer_iterator *sg_it, uint64_t *in_buffer)
{
    // if the the last block is reached,
    // don't write to host memory
    if (sg_it->blocks_addr_ptr == sg_it->blocks_addr_ptr_end)
    {
        return;
    }
    uint32_t length = sg_it->current_block_length;
    uint32_t current = sg_it->current_block_index;
    if (length > 8 + current)
    {
        memcpy(&sg_it->current_block_addr[sg_it->current_block_index], in_buffer, sizeof(uint64_t));
        sg_it->current_block_index += 8;
    }
    else if (length == 8 + current)
    {
        memcpy(&sg_it->current_block_addr[sg_it->current_block_index], in_buffer, sizeof(uint64_t));
        go_to_next_block(sg_it);
    }
    else
    {
        uint8_t *in_buffer_8 = (uint8_t *)in_buffer;
        for (int i = 0; i < 8; i++)
        {
            *get_next_byte(sg_it) = in_buffer_8[i];
        }
    }
}

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
// sleep
#include <unistd.h>

static atomic_flag has_run = ATOMIC_FLAG_INIT;

static atomic_int run_times = 0;
// static atomic_int run_times2 = 0;

void read_rank_ptr(uint64_t *input)
{
    while (1)
    {
        /* code */

        uint64_t cache_line[NB_REAL_CIS], cache_line_interleave[NB_REAL_CIS];
        uint8_t dpu_id = 0;
        uint32_t size_transfer = 128;
        uint32_t offset = 0;
        uint64_t *output = (uint64_t *)malloc(size_transfer);

        uint32_t data_id_start = 0;

        uint8_t *ptr_dest = (uint8_t *)input + BANK_START(dpu_id);
        uint32_t i;

        __builtin_ia32_mfence();

        for (i = data_id_start; i < data_id_start + size_transfer / sizeof(uint64_t); ++i)
        {
            uint32_t mram_64_bit_word_offset = apply_address_translation_on_mram_offset(i * 8 + offset) / 8;
            uint64_t next_data = BANK_OFFSET_NEXT_DATA(mram_64_bit_word_offset * sizeof(uint64_t));
            uint64_t offset = (next_data % BANK_CHUNK_SIZE) + (next_data / BANK_CHUNK_SIZE) * BANK_NEXT_CHUNK_OFFSET;

            /* Invalidates possible prefetched cache line or old cache line */
            __builtin_ia32_clflushopt((uint8_t *)ptr_dest + offset);
        }

        __builtin_ia32_mfence();

        for (i = data_id_start; i < data_id_start + size_transfer / sizeof(uint64_t); ++i)
        {
            uint32_t mram_64_bit_word_offset = apply_address_translation_on_mram_offset(i * 8 + offset) / 8;
            uint64_t next_data = BANK_OFFSET_NEXT_DATA(mram_64_bit_word_offset * sizeof(uint64_t));
            uint64_t offset = (next_data % BANK_CHUNK_SIZE) + (next_data / BANK_CHUNK_SIZE) * BANK_NEXT_CHUNK_OFFSET;

            cache_line[0] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 0 * sizeof(uint64_t)));
            cache_line[1] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 1 * sizeof(uint64_t)));
            cache_line[2] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 2 * sizeof(uint64_t)));
            cache_line[3] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 3 * sizeof(uint64_t)));
            cache_line[4] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 4 * sizeof(uint64_t)));
            cache_line[5] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 5 * sizeof(uint64_t)));
            cache_line[6] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 6 * sizeof(uint64_t)));
            cache_line[7] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 7 * sizeof(uint64_t)));

            byte_interleave_avx512(cache_line, cache_line_interleave, false);

            output[i] = cache_line_interleave[0];
        }
        for (int i = 0; i < 16; i++)
        {
            printf("output[%d] = %ld\n", i, output[i]);
        }
        printf("sleep 1s, run_times = %d\n", run_times++);
        usleep(1000 * 1000);
    }
}

__API_SYMBOL__ void write_rank_ptr(uint64_t **args, int size_transfer, int dpu_id, int ci_id)
{
    printf("begin write_rank_ptr\n");
    uint64_t *input = args[0];
    uint64_t *output = args[1];

    uint8_t *ptr_dest = (uint8_t *)output + BANK_START(dpu_id);

    uint64_t cache_line[NB_REAL_CIS];
    uint32_t data_id_start = 0;
    printf("dpu_id = %d, ci_id = %d\n", dpu_id, ci_id);
    int offset = 1048576;
    for (uint32_t i = data_id_start; i < data_id_start + size_transfer / sizeof(uint64_t); ++i)
    {
        uint32_t mram_64_bit_word_offset = apply_address_translation_on_mram_offset(i * 8 + offset) / 8;
        uint64_t next_data = BANK_OFFSET_NEXT_DATA(mram_64_bit_word_offset * sizeof(uint64_t));
        uint64_t offset = (next_data % BANK_CHUNK_SIZE) + (next_data / BANK_CHUNK_SIZE) * BANK_NEXT_CHUNK_OFFSET;

        cache_line[ci_id] = *((uint64_t *)&input[dpu_id * 8 + ci_id] + i);
        printf("%d th cache_line[%d] = %ld\n", i, ci_id, cache_line[ci_id]);

        byte_interleave_avx512(cache_line, (uint64_t *)((uint8_t *)ptr_dest + offset), true);
    }
    __builtin_ia32_mfence();
}

__attribute__((unused)) void run_once_function(uint64_t *input)
{

    // use atomic_flag_test_and_set to check and set the flag
    if (!atomic_flag_test_and_set(&has_run))
    {
      
        printf("This function is running for the first time.\n");
    
        pthread_t tid;
        pthread_create(&tid, NULL, (void *)read_rank_ptr, input);
    }
    else
    {
       
        printf("This function has already run before.\n");
    }
}

__API_SYMBOL__ void
fifo_write_to_rank(void **ptr,
                   uint64_t *base_region_addr,
                   uint32_t offset,
                   uint32_t size,
                   uint8_t dpu_id_start,
                   uint8_t dpu_id_stop,
                   uint32_t data_id_start,
                   __attribute__((unused)) uint32_t data_id_stop)
{
    // printf("begin fifo_write_to_rank, dpu_id_start = %d, dpu_id_stop = %d, data_id_start = %d, data_id_stop = %d\n", dpu_id_start, dpu_id_stop, data_id_start, data_id_stop);

    uint64_t cache_line[NB_REAL_CIS];
    uint8_t idx, ci_id, dpu_id, nb_cis;
    uint32_t size_transfer = size;
    __attribute__((unused)) int chip_id = 0;

    nb_cis = 8;

    if (!size_transfer)
        return;

    data_id_start /= sizeof(uint64_t);

    /* Works only for transfers:
     * - of same size and same offset on the same line
     * - size and offset are aligned on 8B
     */

    
    FOREACH_DPU_MULTITHREAD(dpu_id, idx, dpu_id_start, dpu_id_stop)
    {
        uint32_t off_max = 0;
        uint32_t i;
        uint8_t *ptr_dest = (uint8_t *)base_region_addr + BANK_START(dpu_id);
        bool do_dpu_transfer = false;

        for (ci_id = 0; ci_id < nb_cis; ++ci_id)
        {
            if (ptr[idx + ci_id])
            {
                do_dpu_transfer = true;
                break;
            }
        }

        if (!do_dpu_transfer)
            continue;

        for (i = data_id_start; i < data_id_start + size_transfer / sizeof(uint64_t); ++i)
        {
            uint32_t mram_64_bit_word_offset = apply_address_translation_on_mram_offset(i * 8 + offset) / 8;
            uint64_t next_data = BANK_OFFSET_NEXT_DATA(mram_64_bit_word_offset * sizeof(uint64_t));
            uint64_t offset = (next_data % BANK_CHUNK_SIZE) + (next_data / BANK_CHUNK_SIZE) * BANK_NEXT_CHUNK_OFFSET;

            for (ci_id = 0; ci_id < nb_cis; ++ci_id)
            {
                if (ptr[idx + ci_id])
                {
                    cache_line[ci_id] = *((uint64_t *)ptr[idx + ci_id] + i);
                    chip_id = ci_id;
                }
            }

            byte_interleave_avx512(cache_line, (uint64_t *)((uint8_t *)ptr_dest + offset), true);
            off_max = offset < off_max ? off_max : offset;
            // just_one_chip_byte_interleave_avx512(cache_line, (uint64_t *)((uint8_t *)ptr_dest + offset), chip_id);
        }

        // clflushopt_range(ptr_dest, off_max * 8 + 64);
        __builtin_ia32_mfence();
     
    }

    // __builtin_ia32_mfence();

    

    /* Here we use non-temporal stores. These stores are "write-combining" and bypass the CPU cache,
     * so using them does not require a flush. The mfence instruction ensures the stores have reached
     * the system memory.
     * cf. https://www.usenix.org/system/files/login/articles/login_summer17_07_rudoff.pdf
     */
}
// 0x7fd300000000

static void
threads_write_to_rank(struct xeon_sp_private *xeon_sp_priv,
                      uint8_t dpu_id_start,
                      uint8_t dpu_id_stop,
                      uint32_t data_id_start,
                      uint32_t data_id_stop)
{

    struct dpu_transfer_matrix *xfer_matrix = xeon_sp_priv->xfer_matrix;
    uint64_t cache_line[NB_REAL_CIS];
    uint8_t idx, ci_id, dpu_id, nb_cis;
    uint32_t size_transfer = data_id_stop ? data_id_stop - data_id_start : xfer_matrix->size;
    uint32_t offset = xfer_matrix->offset;

    nb_cis = xeon_sp_priv->tr->desc->topology.nr_of_control_interfaces;

    if (!size_transfer)
        return;

    data_id_start /= sizeof(uint64_t);

    /* Works only for transfers:
     * - of same size and same offset on the same line
     * - size and offset are aligned on 8B
     */
    FOREACH_DPU_MULTITHREAD(dpu_id, idx, dpu_id_start, dpu_id_stop)
    {
        uint32_t i;
        uint8_t *ptr_dest = (uint8_t *)xeon_sp_priv->base_region_addr + BANK_START(dpu_id);
        bool do_dpu_transfer = false;

        for (ci_id = 0; ci_id < nb_cis; ++ci_id)
        {
            if (xfer_matrix->ptr[idx + ci_id])
            {
                do_dpu_transfer = true;
                break;
            }
        }

        if (!do_dpu_transfer)
            continue;

        if (xfer_matrix->type == DPU_SG_XFER_MATRIX)
        {
            struct sg_xfer_buffer_iterator sg_it[nb_cis];

            /* Initialize sg buffer iterators */
            for (ci_id = 0; ci_id < nb_cis; ++ci_id)
            {
                struct sg_xfer_buffer *sg_buff = xfer_matrix->sg_ptr[idx + ci_id];
                if (sg_buff)
                    init_sg_xfer_buffer_iterator(&sg_it[ci_id], sg_buff);
            }
            for (i = data_id_start; i < data_id_start + size_transfer / sizeof(uint64_t); ++i)
            {
                uint32_t mram_64_bit_word_offset = apply_address_translation_on_mram_offset(i * 8 + offset) / 8;
                uint64_t next_data = BANK_OFFSET_NEXT_DATA(mram_64_bit_word_offset * sizeof(uint64_t));
                uint64_t offset = (next_data % BANK_CHUNK_SIZE) + (next_data / BANK_CHUNK_SIZE) * BANK_NEXT_CHUNK_OFFSET;

                for (ci_id = 0; ci_id < nb_cis; ++ci_id)
                {
                    if (xfer_matrix->sg_ptr[idx + ci_id])
                        get_next_eight_bytes(&sg_it[ci_id], &cache_line[ci_id]);
                }

                byte_interleave_avx512(cache_line, (uint64_t *)((uint8_t *)ptr_dest + offset), true);
            }
        }
        else
        {
            for (i = data_id_start; i < data_id_start + size_transfer / sizeof(uint64_t); ++i)
            {
                uint32_t mram_64_bit_word_offset = apply_address_translation_on_mram_offset(i * 8 + offset) / 8;
                uint64_t next_data = BANK_OFFSET_NEXT_DATA(mram_64_bit_word_offset * sizeof(uint64_t));
                uint64_t offset = (next_data % BANK_CHUNK_SIZE) + (next_data / BANK_CHUNK_SIZE) * BANK_NEXT_CHUNK_OFFSET;

                for (ci_id = 0; ci_id < nb_cis; ++ci_id)
                {
                    if (xfer_matrix->ptr[idx + ci_id])
                        cache_line[ci_id] = *((uint64_t *)xfer_matrix->ptr[idx + ci_id] + i);
                }

                byte_interleave_avx512(cache_line, (uint64_t *)((uint8_t *)ptr_dest + offset), true);
            }
        }

        __builtin_ia32_mfence();

        /* Here we use non-temporal stores. These stores are "write-combining" and bypass the CPU cache,
         * so using them does not require a flush. The mfence instruction ensures the stores have reached
         * the system memory.
         * cf. https://www.usenix.org/system/files/login/articles/login_summer17_07_rudoff.pdf
         */
    }
    // run_once_function((uint64_t *)xeon_sp_priv->base_region_addr);

    // uint64_t **args = (uint64_t **)malloc(2 * sizeof(uint64_t *));
    // args[0] = (uint64_t *)malloc(128);
    // args[1] = (uint64_t *)xeon_sp_priv->base_region_addr;
    // for (int i = 0; i < 16; i++)
    // {
    //     args[0][i] = 999987;
    // }
}

__API_SYMBOL__ void
fifo_read_from_rank(void **ptr,
                    uint64_t *base_region_addr,
                    uint32_t offset,
                    uint32_t size,
                    uint8_t dpu_id_start,
                    uint8_t dpu_id_stop,
                    uint32_t data_id_start,
                    __attribute__((unused)) uint32_t data_id_stop)
{

    uint64_t cache_line[NB_REAL_CIS], cache_line_interleave[NB_REAL_CIS];
    uint8_t idx, ci_id, dpu_id, nb_cis;
    uint32_t size_transfer = data_id_stop ? data_id_stop - data_id_start : size;
    __attribute__((unused)) int chip_id = 0;

    nb_cis = 8;

    if (!size_transfer)
        return;

    data_id_start /= sizeof(uint64_t);

    /* Works only for transfers of same size and same offset on the
     * same line
     */
    FOREACH_DPU_MULTITHREAD(dpu_id, idx, dpu_id_start, dpu_id_stop)
    {
        uint8_t *ptr_dest = (uint8_t *)base_region_addr + BANK_START(dpu_id);
        uint32_t i;
        bool do_dpu_transfer = false;

        for (ci_id = 0; ci_id < nb_cis; ++ci_id)
        {
            if (ptr[idx + ci_id])
            {
                do_dpu_transfer = true;
                break;
            }
        }

        if (!do_dpu_transfer)
            continue;
        __builtin_ia32_mfence();

        for (i = data_id_start; i < data_id_start + size_transfer / sizeof(uint64_t); ++i)
        {
            uint32_t mram_64_bit_word_offset = apply_address_translation_on_mram_offset(i * 8 + offset) / 8;
            uint64_t next_data = BANK_OFFSET_NEXT_DATA(mram_64_bit_word_offset * sizeof(uint64_t));
            uint64_t offset = (next_data % BANK_CHUNK_SIZE) + (next_data / BANK_CHUNK_SIZE) * BANK_NEXT_CHUNK_OFFSET;

            /* Invalidates possible prefetched cache line or old cache line */
            __builtin_ia32_clflushopt((uint8_t *)ptr_dest + offset);
        }

        __builtin_ia32_mfence();

        for (i = data_id_start; i < data_id_start + size_transfer / sizeof(uint64_t); ++i)
        {
            uint32_t mram_64_bit_word_offset = apply_address_translation_on_mram_offset(i * 8 + offset) / 8;
            uint64_t next_data = BANK_OFFSET_NEXT_DATA(mram_64_bit_word_offset * sizeof(uint64_t));
            uint64_t offset = (next_data % BANK_CHUNK_SIZE) + (next_data / BANK_CHUNK_SIZE) * BANK_NEXT_CHUNK_OFFSET;

            cache_line[0] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 0 * sizeof(uint64_t)));
            cache_line[1] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 1 * sizeof(uint64_t)));
            cache_line[2] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 2 * sizeof(uint64_t)));
            cache_line[3] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 3 * sizeof(uint64_t)));
            cache_line[4] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 4 * sizeof(uint64_t)));
            cache_line[5] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 5 * sizeof(uint64_t)));
            cache_line[6] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 6 * sizeof(uint64_t)));
            cache_line[7] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 7 * sizeof(uint64_t)));

            for (ci_id = 0; ci_id < nb_cis; ++ci_id)
            {
                if (ptr[idx + ci_id])
                {
                    chip_id = ci_id;
                }
            }

            byte_interleave_avx512(cache_line, cache_line_interleave, false);
            // just_one_chip_byte_interleave_avx512(cache_line, cache_line_interleave, chip_id);

            for (ci_id = 0; ci_id < nb_cis; ++ci_id)
            {
                if (ptr[idx + ci_id])
                {
                    *((uint64_t *)ptr[idx + ci_id] + i) = cache_line_interleave[ci_id];
                }
            }
        }

        __builtin_ia32_mfence();

        for (i = data_id_start; i < data_id_start + size_transfer / sizeof(uint64_t); ++i)
        {
            uint32_t mram_64_bit_word_offset = apply_address_translation_on_mram_offset(i * 8 + offset) / 8;
            uint64_t next_data = BANK_OFFSET_NEXT_DATA(mram_64_bit_word_offset * sizeof(uint64_t));
            uint64_t offset = (next_data % BANK_CHUNK_SIZE) + (next_data / BANK_CHUNK_SIZE) * BANK_NEXT_CHUNK_OFFSET;

            __builtin_ia32_clflushopt((uint8_t *)ptr_dest + offset);
        }
        __builtin_ia32_mfence();
    }

    
}

static void
threads_read_from_rank(struct xeon_sp_private *xeon_sp_priv,
                       uint8_t dpu_id_start,
                       uint8_t dpu_id_stop,
                       uint32_t data_id_start,
                       uint32_t data_id_stop)
{

    struct dpu_transfer_matrix *xfer_matrix = xeon_sp_priv->xfer_matrix;
    uint64_t cache_line[NB_REAL_CIS], cache_line_interleave[NB_REAL_CIS];
    uint8_t idx, ci_id, dpu_id, nb_cis;
    uint32_t size_transfer = data_id_stop ? data_id_stop - data_id_start : xfer_matrix->size;
    uint32_t offset = xfer_matrix->offset;

    nb_cis = xeon_sp_priv->tr->desc->topology.nr_of_control_interfaces;

    if (!size_transfer)
        return;

    data_id_start /= sizeof(uint64_t);

    /* Works only for transfers of same size and same offset on the
     * same line
     */
    FOREACH_DPU_MULTITHREAD(dpu_id, idx, dpu_id_start, dpu_id_stop)
    {
        uint8_t *ptr_dest = (uint8_t *)xeon_sp_priv->base_region_addr + BANK_START(dpu_id);
        uint32_t i;
        bool do_dpu_transfer = false;

        for (ci_id = 0; ci_id < nb_cis; ++ci_id)
        {
            if (xfer_matrix->ptr[idx + ci_id])
            {
                do_dpu_transfer = true;
                break;
            }
        }

        if (!do_dpu_transfer)
            continue;
        __builtin_ia32_mfence();

        for (i = data_id_start; i < data_id_start + size_transfer / sizeof(uint64_t); ++i)
        {
            uint32_t mram_64_bit_word_offset = apply_address_translation_on_mram_offset(i * 8 + offset) / 8;
            uint64_t next_data = BANK_OFFSET_NEXT_DATA(mram_64_bit_word_offset * sizeof(uint64_t));
            uint64_t offset = (next_data % BANK_CHUNK_SIZE) + (next_data / BANK_CHUNK_SIZE) * BANK_NEXT_CHUNK_OFFSET;

            /* Invalidates possible prefetched cache line or old cache line */
            __builtin_ia32_clflushopt((uint8_t *)ptr_dest + offset);
        }

        __builtin_ia32_mfence();

        if (xfer_matrix->type == DPU_SG_XFER_MATRIX)
        {
            struct sg_xfer_buffer_iterator sg_it[nb_cis];

            /* Initialize sg buffer iterators */
            for (ci_id = 0; ci_id < nb_cis; ++ci_id)
            {
                struct sg_xfer_buffer *sg_buff = xfer_matrix->sg_ptr[idx + ci_id];
                if (sg_buff)
                    init_sg_xfer_buffer_iterator(&sg_it[ci_id], sg_buff);
            }

            for (i = data_id_start; i < data_id_start + size_transfer / sizeof(uint64_t); ++i)
            {
                uint32_t mram_64_bit_word_offset = apply_address_translation_on_mram_offset(i * 8 + offset) / 8;
                uint64_t next_data = BANK_OFFSET_NEXT_DATA(mram_64_bit_word_offset * sizeof(uint64_t));
                uint64_t offset = (next_data % BANK_CHUNK_SIZE) + (next_data / BANK_CHUNK_SIZE) * BANK_NEXT_CHUNK_OFFSET;

                cache_line[0] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 0 * sizeof(uint64_t)));
                cache_line[1] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 1 * sizeof(uint64_t)));
                cache_line[2] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 2 * sizeof(uint64_t)));
                cache_line[3] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 3 * sizeof(uint64_t)));
                cache_line[4] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 4 * sizeof(uint64_t)));
                cache_line[5] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 5 * sizeof(uint64_t)));
                cache_line[6] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 6 * sizeof(uint64_t)));
                cache_line[7] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 7 * sizeof(uint64_t)));

                byte_interleave_avx512(cache_line, cache_line_interleave, false);

                for (ci_id = 0; ci_id < nb_cis; ++ci_id)
                {
                    if (xfer_matrix->sg_ptr[idx + ci_id] != NULL)
                        write_next_eight_bytes(&sg_it[ci_id], &cache_line_interleave[ci_id]);
                }
            }
        }
        else
        {
            for (i = data_id_start; i < data_id_start + size_transfer / sizeof(uint64_t); ++i)
            {
                uint32_t mram_64_bit_word_offset = apply_address_translation_on_mram_offset(i * 8 + offset) / 8;
                uint64_t next_data = BANK_OFFSET_NEXT_DATA(mram_64_bit_word_offset * sizeof(uint64_t));
                uint64_t offset = (next_data % BANK_CHUNK_SIZE) + (next_data / BANK_CHUNK_SIZE) * BANK_NEXT_CHUNK_OFFSET;

                cache_line[0] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 0 * sizeof(uint64_t)));
                cache_line[1] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 1 * sizeof(uint64_t)));
                cache_line[2] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 2 * sizeof(uint64_t)));
                cache_line[3] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 3 * sizeof(uint64_t)));
                cache_line[4] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 4 * sizeof(uint64_t)));
                cache_line[5] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 5 * sizeof(uint64_t)));
                cache_line[6] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 6 * sizeof(uint64_t)));
                cache_line[7] = *((volatile uint64_t *)((uint8_t *)ptr_dest + offset + 7 * sizeof(uint64_t)));

                byte_interleave_avx512(cache_line, cache_line_interleave, false);

                for (ci_id = 0; ci_id < nb_cis; ++ci_id)
                {
                    if (xfer_matrix->ptr[idx + ci_id])
                    {
                        *((uint64_t *)xfer_matrix->ptr[idx + ci_id] + i) = cache_line_interleave[ci_id];
                    }
                }
            }
        }

        __builtin_ia32_mfence();

        for (i = data_id_start; i < data_id_start + size_transfer / sizeof(uint64_t); ++i)
        {
            uint32_t mram_64_bit_word_offset = apply_address_translation_on_mram_offset(i * 8 + offset) / 8;
            uint64_t next_data = BANK_OFFSET_NEXT_DATA(mram_64_bit_word_offset * sizeof(uint64_t));
            uint64_t offset = (next_data % BANK_CHUNK_SIZE) + (next_data / BANK_CHUNK_SIZE) * BANK_NEXT_CHUNK_OFFSET;

            __builtin_ia32_clflushopt((uint8_t *)ptr_dest + offset);
        }

        __builtin_ia32_mfence();
    }
}

static bool
is_dpu_line_in_xfer(struct xeon_sp_private *xeon_sp_priv, uint8_t line_id)
{

    // TODO check if this is correct
    struct dpu_transfer_matrix *m = xeon_sp_priv->xfer_matrix;
    for (uint8_t i = 0; i < xeon_sp_priv->nb_dpus_per_ci; ++i)
    {

        if (m->ptr[line_id * xeon_sp_priv->nb_dpus_per_ci + i])
            return true;
    }
    return false;
}

static uint8_t
get_nb_lines_in_xfer(struct xeon_sp_private *xeon_sp_priv)
{

    uint8_t res = 0;
    uint8_t nb_cis = xeon_sp_priv->tr->desc->topology.nr_of_control_interfaces;
    for (int i = 0; i < nb_cis; ++i)
    {
        if (is_dpu_line_in_xfer(xeon_sp_priv, i))
            ++res;
    }
    return res;
}

static void
thread_mram_get_dpu_and_data_ids(uint8_t thread_id,
                                 uint8_t nb_threads_per_line,
                                 struct xeon_sp_private *xeon_sp_priv,
                                 uint8_t *dpu_id_start,
                                 uint8_t *dpu_id_stop,
                                 uint32_t *data_id_start)
{

    uint8_t cnt = 0;
    uint8_t nb_cis = xeon_sp_priv->tr->desc->topology.nr_of_control_interfaces;
    uint8_t last_line = 0;
    for (uint8_t i = 0; i < nb_cis; ++i)
    {
        if (is_dpu_line_in_xfer(xeon_sp_priv, i))
            ++cnt;
        if (!cnt)
            continue;
        for (int j = 0; j < nb_threads_per_line; ++j)
        {
            if ((cnt - 1) * nb_threads_per_line + j == thread_id)
            {
                *data_id_start = j;
                *dpu_id_start = last_line;
                *dpu_id_stop = i + 1;
                return;
            }
        }
        if (is_dpu_line_in_xfer(xeon_sp_priv, i))
            last_line = i + 1;
    }
    *data_id_start = 0;
    *dpu_id_start = nb_cis;
    *dpu_id_stop = nb_cis;
}

static void
thread_do_mram_xfer(struct xeon_sp_private *xeon_sp_priv, uint8_t thread_id)
{
    uint8_t nb_threads_for_xfer = xeon_sp_priv->nb_threads_for_xfer;
    if (!(nb_threads_for_xfer > thread_id))
    {
        return;
    }
    // get xfer_matrix, check the number of lines that are targetted, use
    // one thread per targetted line if possible, or 1 thread per 2 lines.
    uint8_t dpu_id_start;
    uint8_t dpu_id_stop;
    uint32_t data_id_start = 0;
    uint32_t data_id_stop = 0;
    uint8_t nb_lines_in_xfer = get_nb_lines_in_xfer(xeon_sp_priv);
    if (nb_lines_in_xfer <= nb_threads_for_xfer)
    {
        // can use one thread for each line

        uint8_t nb_threads_per_line = 1;
        if (nb_lines_in_xfer <= nb_threads_for_xfer / 2 && xeon_sp_priv->xfer_matrix->size % 16 == 0)
        {
            // can use 2 threads per line
            // parallelize on the data
            nb_threads_per_line = 2;
            data_id_start = 0;
            data_id_stop = 0;
        }
        uint32_t size = xeon_sp_priv->xfer_matrix->size / nb_threads_per_line;
        data_id_start = 0;
        thread_mram_get_dpu_and_data_ids(
            thread_id, nb_threads_per_line, xeon_sp_priv, &dpu_id_start, &dpu_id_stop, &data_id_start);
        data_id_start *= size;
        data_id_stop = data_id_start + size;
        if (xeon_sp_priv->xfer_matrix->size - data_id_stop < size)
            data_id_stop = xeon_sp_priv->xfer_matrix->size;
    }
    else
    {
        uint8_t nb_dpus_per_thread = xeon_sp_priv->nb_dpus_per_ci / nb_threads_for_xfer;
        dpu_id_start = thread_id * nb_dpus_per_thread;
        if (thread_id == nb_threads_for_xfer - 1)
        {
            dpu_id_stop = xeon_sp_priv->nb_dpus_per_ci;
        }
        else
        {
            dpu_id_stop = (thread_id + 1) * nb_dpus_per_thread;
        }
    }

    if (xeon_sp_priv->direction == thread_mram_xfer_read)
        threads_read_from_rank(xeon_sp_priv, dpu_id_start, dpu_id_stop, data_id_start, data_id_stop);
    else
    {
        threads_write_to_rank(xeon_sp_priv, dpu_id_start, dpu_id_stop, data_id_start, data_id_stop);
    }
}

static int
set_thread_numa_affinity(void *base_region_addr)
{
    int numa_node;
    int ret;

    ret = get_mempolicy(&numa_node, NULL, 0, base_region_addr, MPOL_F_NODE | MPOL_F_ADDR);
    if (ret < 0)
    {
        LOGD(__vc(), "%s: WARNING: Failed to evaluate NUMA node ID (%s)", __func__, strerror(errno));
        return ret;
    }

    LOGV(__vc(), "%s: VERBOSE: Setting thread affinity to NUMA node %d", __func__, numa_node);
    ret = numa_run_on_node(numa_node);
    if (ret < 0)
    {
        LOGD(__vc(), "%s: WARNING: Failed to set thread NUMA node affinity (%s)", __func__, strerror(errno));
        return ret;
    }

    return 0;
}

static void *
thread_mram(void *arg)
{
    struct xeon_sp_private *xeon_sp_priv = ((struct thread_mram_args *)arg)->xeon_sp_priv;
    uint8_t thread_id = ((struct thread_mram_args *)arg)->thread_id;
    bool *stop_thread = &((struct thread_mram_args *)arg)->stop_thread;
    bool numa_affinity_is_set = false;

    while (true)
    {
        // Wait for a job to perform
        pthread_barrier_wait(&xeon_sp_priv->barrier_threads);
        if (*stop_thread)
        {
            break;
        }

        // A thread can handle up to 4 ranks, but all are located in the same NUMA node
        if ((!numa_affinity_is_set) && (numa_is_available))
        {
            set_thread_numa_affinity(xeon_sp_priv->base_region_addr);
            numa_affinity_is_set = true;
        }

        thread_do_mram_xfer(xeon_sp_priv, thread_id);
        // Wait for every thread to complete their job
        pthread_barrier_wait(&xeon_sp_priv->barrier_threads);
    }
    return NULL;
}

static uint8_t
xeon_sp_acquire_pool(uint8_t channel_id)
{
    if (channel_id < MAX_NB_CHANNEL)
    {
        pthread_mutex_lock(&xeon_sp_ctx.channels_mutex[channel_id]);
    }
    else
    {
        LOGD(__vc(), "%s: WARNING: channel_id = %u", __func__, channel_id);
        pthread_mutex_lock(&xeon_sp_ctx.channels_mutex[MAX_NB_CHANNEL - 1]);
    }
    return channel_id_to_pool_id(channel_id);
}

static void
xeon_sp_release_pool(uint8_t channel_id)
{
    if (channel_id < MAX_NB_CHANNEL)
    {
        pthread_mutex_unlock(&xeon_sp_ctx.channels_mutex[channel_id]);
    }
    else
    {
        pthread_mutex_unlock(&xeon_sp_ctx.channels_mutex[MAX_NB_CHANNEL - 1]);
    }
}

static void
xeon_sp_init_and_do_xfer(struct xeon_sp_private *pool,
                         struct dpu_region_address_translation *tr,
                         void *base_region_addr,
                         struct dpu_transfer_matrix *xfer_matrix,
                         enum thread_mram_xfer direction)
{
    /* Init transfer */
    pool->tr = tr;
    pool->direction = direction;
    pool->xfer_matrix = xfer_matrix;
    pool->base_region_addr = base_region_addr;

    uint32_t size_transfer = xfer_matrix->size;

    struct dpu_transfer_thread_configuration *conf = &tr->xfer_thread_conf;
    if (size_transfer < conf->threshold_1_thread)
    {
        pool->nb_threads_for_xfer = 1;
    }
    else if (size_transfer < conf->threshold_2_threads && conf->nb_thread_per_pool >= 2)
    {
        pool->nb_threads_for_xfer = 2;
    }
    else if (size_transfer < conf->threshold_4_threads && conf->nb_thread_per_pool >= 4)
    {
        pool->nb_threads_for_xfer = 4;
    }
    else
    {
        pool->nb_threads_for_xfer = conf->nb_thread_per_pool;
    }
    /* Do transfer */
    pthread_barrier_wait(&pool->barrier_threads);
    /* Wait for every threads to complete their job */
    pthread_barrier_wait(&pool->barrier_threads);
}

void xeon_sp_write_to_rank(struct dpu_region_address_translation *tr,
                           void *base_region_addr,
                           uint8_t channel_id,
                           struct dpu_transfer_matrix *xfer_matrix)
{
    uint8_t pool_id = xeon_sp_acquire_pool(channel_id);
    xeon_sp_init_and_do_xfer(&xeon_sp_ctx.pool[pool_id], tr, base_region_addr, xfer_matrix, thread_mram_xfer_write);
    xeon_sp_release_pool(channel_id);
}

void xeon_sp_read_from_rank(struct dpu_region_address_translation *tr,
                            void *base_region_addr,
                            uint8_t channel_id,
                            struct dpu_transfer_matrix *xfer_matrix)
{
    uint8_t pool_id = xeon_sp_acquire_pool(channel_id);
    xeon_sp_init_and_do_xfer(&xeon_sp_ctx.pool[pool_id], tr, base_region_addr, xfer_matrix, thread_mram_xfer_read);
    xeon_sp_release_pool(channel_id);
}

static const struct dpu_transfer_thread_configuration *
get_default_xfer_thread_configuration()
{
    static const struct dpu_transfer_thread_configuration *conf = NULL;
    static const struct dpu_transfer_thread_configuration default_xfer_thread_configuration = {
        .nb_thread_per_pool = 4,
        .threshold_1_thread = 1024,
        .threshold_2_threads = 2048,
        .threshold_4_threads = 32 * 1024,
    };
    if (conf != NULL)
    {
        goto end;
    }

    const char *cpuinfo_path = "/proc/cpuinfo";
    FILE *cpuinfo_file = fopen(cpuinfo_path, "r");
    if (cpuinfo_file == NULL)
    {
        LOGW(__vc(), "%s: Could not open '%s', using default configuration", __func__, cpuinfo_path);
        conf = &default_xfer_thread_configuration;
        goto end;
    }
    char model_name_line[FILENAME_MAX];
    const char *model_name_prefix = "model name\t: ";
    while (true)
    {
        if (fgets(model_name_line, FILENAME_MAX, cpuinfo_file) == NULL)
        {
            break;
        }
        if (strstr(model_name_line, model_name_prefix) != NULL)
        {
            break;
        }
    }
    char *model_name = &model_name_line[strlen(model_name_prefix)];
    const char *xeon_sliver_4110 = "Intel(R) Xeon(R) Silver 4110 CPU @ 2.10GHz";
    const char *xeon_gold_6130 = "Intel(R) Xeon(R) Gold 6130 CPU @ 2.10GHz";
    if (strncmp(model_name, xeon_sliver_4110, strlen(xeon_sliver_4110)) == 0)
    {
        LOGV(__vc(), "%s: Using '%s' default configuration", __func__, xeon_sliver_4110);
        conf = &default_xfer_thread_configuration;
    }
    else if (strncmp(model_name, xeon_gold_6130, strlen(xeon_gold_6130)) == 0)
    {
        LOGV(__vc(), "%s: Using '%s' default configuration", __func__, xeon_gold_6130);
        conf = &default_xfer_thread_configuration;
    }
    else
    {
        LOGI(__vc(), "%s: Could not find model name in '%s', using default configuration", __func__, cpuinfo_path);
        conf = &default_xfer_thread_configuration;
    }

    fclose(cpuinfo_file);
end:
    return conf;
}

static void
xeon_sp_set_configuration(struct dpu_transfer_thread_configuration *conf)
{
    if (conf->nb_thread_per_pool == DPU_XFER_THREAD_CONF_DEFAULT)
    {
        conf->nb_thread_per_pool = get_default_xfer_thread_configuration()->nb_thread_per_pool;
    }
    else if (conf->nb_thread_per_pool > MAX_THREAD_PER_POOL)
    {
        conf->nb_thread_per_pool = MAX_THREAD_PER_POOL;
    }
    if (conf->threshold_1_thread == DPU_XFER_THREAD_CONF_DEFAULT)
    {
        conf->threshold_1_thread = get_default_xfer_thread_configuration()->threshold_1_thread;
    }
    if (conf->threshold_2_threads == DPU_XFER_THREAD_CONF_DEFAULT)
    {
        conf->threshold_2_threads = get_default_xfer_thread_configuration()->threshold_2_threads;
    }
    if (conf->threshold_4_threads == DPU_XFER_THREAD_CONF_DEFAULT)
    {
        conf->threshold_4_threads = get_default_xfer_thread_configuration()->threshold_4_threads;
    }
    LOGD(__vc(),
         "%s: {%u, %u, %u, %u}",
         __func__,
         conf->nb_thread_per_pool,
         conf->threshold_1_thread,
         conf->threshold_2_threads,
         conf->threshold_4_threads);
}

// TODO retrieve environment variable that decides which byte_interleave function
// to use.
int xeon_sp_init_rank(struct dpu_region_address_translation *tr, uint8_t channel_id)
{
    int ret = 0;
    struct dpu_transfer_thread_configuration *conf = &tr->xfer_thread_conf;

    pthread_mutex_lock(&xeon_sp_ctx.mutex);

    xeon_sp_set_configuration(conf);

    // If we cannot get the channel_id allocate one pool anyway
    if (channel_id > MAX_NB_CHANNEL)
    {
        channel_id = MAX_NB_CHANNEL - 1;
    }

    if (xeon_sp_ctx.nb_region_per_channel[channel_id]++ > 0)
    {
        goto unlock_and_exit;
    }

    // One pool per channel
    uint32_t pool_id = channel_id_to_pool_id(channel_id);
    struct xeon_sp_private *pool = &xeon_sp_ctx.pool[pool_id];
    pool->nb_dpus_per_ci = tr->desc->topology.nr_of_dpus_per_control_interface;
    pool->nb_threads = conf->nb_thread_per_pool;
    ret = pthread_barrier_init(&pool->barrier_threads, NULL, conf->nb_thread_per_pool + 1);
    if (ret)
        goto free_unlock_and_exit;

    char thread_name[16];

    for (uint8_t each_thread = 0; each_thread < pool->nb_threads; each_thread++)
    {
        struct thread_mram_args *threads_args = &pool->threads_args[each_thread];
        threads_args->thread_id = each_thread;
        threads_args->xeon_sp_priv = pool;
        threads_args->stop_thread = false;
        ret = pthread_create(&threads_args->tid, NULL, thread_mram, (void *)threads_args);
        if (ret)
            goto free_unlock_and_exit;
        snprintf(thread_name, 16, "DPU_MRAM_%04x", ((pool_id & 0xff) << 8) | (each_thread & 0xff));
        pthread_setname_np(threads_args->tid, thread_name);
    }

    goto unlock_and_exit;

free_unlock_and_exit:
    xeon_sp_ctx.nb_region_per_channel[channel_id]--;

unlock_and_exit:
    pthread_mutex_unlock(&xeon_sp_ctx.mutex);
    return ret;
}

void xeon_sp_destroy_rank(struct dpu_region_address_translation *tr, uint8_t channel_id)
{
    struct dpu_transfer_thread_configuration *conf = &tr->xfer_thread_conf;

    if (channel_id > MAX_NB_CHANNEL)
    {
        channel_id = MAX_NB_CHANNEL - 1;
    }

    pthread_mutex_lock(&xeon_sp_ctx.mutex);

    xeon_sp_set_configuration(conf);

    if (--xeon_sp_ctx.nb_region_per_channel[channel_id] != 0)
    {
        goto unlock_and_exit;
    }

    uint32_t pool_id = channel_id_to_pool_id(channel_id);
    struct xeon_sp_private *pool = &xeon_sp_ctx.pool[pool_id];
    for (uint8_t each_thread = 0; each_thread < pool->nb_threads; each_thread++)
    {
        pool->threads_args[each_thread].stop_thread = true;
    }
    pthread_barrier_wait(&pool->barrier_threads);
    for (uint8_t each_thread = 0; each_thread < pool->nb_threads; each_thread++)
    {
        pthread_join(pool->threads_args[each_thread].tid, NULL);
    }

unlock_and_exit:
    pthread_mutex_unlock(&xeon_sp_ctx.mutex);
}

struct dpu_region_address_translation xeon_sp_translate = {
    .backend_id = DPU_BACKEND_XEON_SP,
    .capabilities = CAP_PERF | CAP_SAFE,
    .init_rank = xeon_sp_init_rank,
    .destroy_rank = xeon_sp_destroy_rank,
    .write_to_rank = xeon_sp_write_to_rank,
    .read_from_rank = xeon_sp_read_from_rank,
    .write_to_cis = xeon_sp_write_to_cis,
    .read_from_cis = xeon_sp_read_from_cis,
};
