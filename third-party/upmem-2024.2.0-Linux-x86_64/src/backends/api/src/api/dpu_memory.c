/* Copyright 2020 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <dpu_transfer_matrix.h>
#include <dpu.h>
#include <dpu_api_verbose.h>
#include <dpu_types.h>
#include <dpu_error.h>
#include <dpu_log_utils.h>
#include <dpu_rank.h>
#include <dpu_management.h>
#include <dpu_program.h>
#include <dpu_memory.h>
#include <dpu_internals.h>
#include <dpu_attributes.h>
#include <dpu_thread_job.h>
#include <pthread.h>

#define IRAM_MASK (0x80000000u)
#define MRAM_MASK (0x08000000u)

#define IRAM_ALIGN (3u)
#define WRAM_ALIGN (2u)

#define ALIGN_MASK(align) (~((1u << (align)) - 1u))
#define IRAM_ALIGN_MASK ALIGN_MASK(IRAM_ALIGN)
#define WRAM_ALIGN_MASK ALIGN_MASK(WRAM_ALIGN)

#define MEMORY_SWITCH(address, iram_statement, mram_statement, wram_statement)                                                   \
    do {                                                                                                                         \
        if (((address)&IRAM_MASK) == IRAM_MASK) {                                                                                \
            iram_statement;                                                                                                      \
        } else if (((address)&MRAM_MASK) == MRAM_MASK) {                                                                         \
            mram_statement;                                                                                                      \
        } else {                                                                                                                 \
            wram_statement;                                                                                                      \
        }                                                                                                                        \
    } while (0)

#define UPDATE_MRAM_COPY_PARAMETERS(address, length)                                                                             \
    do {                                                                                                                         \
        (address) &= ~MRAM_MASK;                                                                                                 \
    } while (0)

#define UPDATE_WRAM_COPY_PARAMETERS(address, length)                                                                             \
    do {                                                                                                                         \
        (address) >>= WRAM_ALIGN;                                                                                                \
        (length) >>= WRAM_ALIGN;                                                                                                 \
    } while (0)

#define UPDATE_IRAM_COPY_PARAMETERS(address, length)                                                                             \
    do {                                                                                                                         \
        (address) = ((address) & ~IRAM_MASK) >> IRAM_ALIGN;                                                                      \
        (length) >>= IRAM_ALIGN;                                                                                                 \
    } while (0)

#define MEMORY_SWITCH_AND_UPDATE_COPY_PARAMETERS(address, length, iram_statement, mram_statement, wram_statement)                \
    do {                                                                                                                         \
        MEMORY_SWITCH((address), iram_statement; UPDATE_IRAM_COPY_PARAMETERS((address), (length)), mram_statement;               \
                      UPDATE_MRAM_COPY_PARAMETERS((address), (length));                                                          \
                      , wram_statement;                                                                                          \
                      UPDATE_WRAM_COPY_PARAMETERS((address), (length)));                                                         \
    } while (0)

#define CHECK_SYMBOL(symbol, symbol_offset, length)                                                                              \
    do {                                                                                                                         \
        dpu_mem_max_addr_t _address = (symbol).address + (symbol_offset);                                                        \
        if (((symbol_offset) + (length)) > (symbol).size) {                                                                      \
            LOG_FN(WARNING, "invalid symbol access (offset:%u + length:%u > size:%u)", symbol_offset, length, (symbol).size);    \
            return DPU_ERR_INVALID_SYMBOL_ACCESS;                                                                                \
        }                                                                                                                        \
        MEMORY_SWITCH(                                                                                                           \
            _address,                                                                                                            \
            if ((_address & ~IRAM_ALIGN_MASK) != 0) {                                                                            \
                LOG_FN(WARNING, "invalid iram access (offset:0x%x, address:0x%x)", symbol_offset, (symbol).address);             \
                return DPU_ERR_INVALID_IRAM_ACCESS;                                                                              \
            } if (((length) & ~IRAM_ALIGN_MASK) != 0) {                                                                          \
                LOG_FN(WARNING, "invalid iram access (length:0x%x)", length);                                                    \
                return DPU_ERR_INVALID_IRAM_ACCESS;                                                                              \
            },                                                                                                                   \
            /* All alignment are allowed */,                                                                                     \
            if ((_address & ~WRAM_ALIGN_MASK) != 0) {                                                                            \
                LOG_FN(WARNING, "invalid wram access (offset:0x%x, address:0x%x)", symbol_offset, (symbol).address);             \
                return DPU_ERR_INVALID_WRAM_ACCESS;                                                                              \
            } if (((length) & ~WRAM_ALIGN_MASK) != 0) {                                                                          \
                LOG_FN(WARNING, "invalid wram access (length:0x%x)", length);                                                    \
                return DPU_ERR_INVALID_WRAM_ACCESS;                                                                              \
            });                                                                                                                  \
    } while (0)

#define DPU_BROADCAST_SET_JOB_TYPE(job_type, address, length)                                                                    \
    do {                                                                                                                         \
        MEMORY_SWITCH_AND_UPDATE_COPY_PARAMETERS(address,                                                                        \
            length,                                                                                                              \
            (job_type) = DPU_THREAD_JOB_COPY_IRAM_TO_RANK,                                                                       \
            (job_type) = DPU_THREAD_JOB_COPY_MRAM_TO_RANK,                                                                       \
            (job_type) = DPU_THREAD_JOB_COPY_WRAM_TO_RANK);                                                                      \
    } while (0)

#define DPU_COPY_MATRIX_SET_JOB_TYPE(job_type, xfer, address, length)                                                            \
    do {                                                                                                                         \
        switch ((xfer)) {                                                                                                        \
            case DPU_XFER_TO_DPU:                                                                                                \
                MEMORY_SWITCH_AND_UPDATE_COPY_PARAMETERS(address,                                                                \
                    length,                                                                                                      \
                    (job_type) = DPU_THREAD_JOB_COPY_IRAM_TO_MATRIX,                                                             \
                    (job_type) = DPU_THREAD_JOB_COPY_MRAM_TO_MATRIX,                                                             \
                    (job_type) = DPU_THREAD_JOB_COPY_WRAM_TO_MATRIX);                                                            \
                break;                                                                                                           \
            case DPU_XFER_FROM_DPU:                                                                                              \
                MEMORY_SWITCH_AND_UPDATE_COPY_PARAMETERS(address,                                                                \
                    length,                                                                                                      \
                    (job_type) = DPU_THREAD_JOB_COPY_IRAM_FROM_MATRIX,                                                           \
                    (job_type) = DPU_THREAD_JOB_COPY_MRAM_FROM_MATRIX,                                                           \
                    (job_type) = DPU_THREAD_JOB_COPY_WRAM_FROM_MATRIX);                                                          \
                break;                                                                                                           \
            default:                                                                                                             \
                return DPU_ERR_INVALID_MEMORY_TRANSFER;                                                                          \
        }                                                                                                                        \
    } while (0)

#define SYNCHRONOUS_FLAGS(flags) ((DPU_XFER_ASYNC & flags) == 0)

static dpu_error_t
dpu_copy_symbol_dpu(struct dpu_t *dpu,
    struct dpu_symbol_t symbol,
    uint32_t symbol_offset,
    void *buffer,
    size_t length,
    dpu_xfer_t xfer,
    dpu_xfer_flags_t flags)
{
    if (!dpu->enabled) {
        return DPU_ERR_DPU_DISABLED;
    }
    CHECK_SYMBOL(symbol, symbol_offset, length);

    dpu_mem_max_addr_t address = symbol.address + symbol_offset;
    struct dpu_rank_t *rank = dpu_get_rank(dpu);
    dpu_error_t status = DPU_OK;

    enum dpu_thread_job_type job_type;
    DPU_COPY_MATRIX_SET_JOB_TYPE(job_type, xfer, address, length);

    uint32_t nr_jobs_per_rank;
    struct dpu_thread_job_sync sync;
    DPU_THREAD_JOB_GET_JOBS(&rank, 1, nr_jobs_per_rank, jobs, &sync, SYNCHRONOUS_FLAGS(flags), status);

    struct dpu_rank_t *rrank __attribute__((unused));
    struct dpu_thread_job *job;
    DPU_THREAD_JOB_SET_JOBS(&rank, rrank, 1, jobs, job, &sync, SYNCHRONOUS_FLAGS(flags), {
        job->type = job_type;
        dpu_transfer_matrix_clear_all(rank, &job->matrix);
        dpu_transfer_matrix_add_dpu(dpu, &job->matrix, buffer);
        job->matrix.offset = address;
        job->matrix.size = length;
    });

    status = dpu_thread_job_do_jobs(&rank, 1, nr_jobs_per_rank, jobs, SYNCHRONOUS_FLAGS(flags), &sync);

    return status;
}

static dpu_error_t
dpu_broadcast_to_symbol_for_ranks(struct dpu_rank_t **ranks,
    uint32_t nr_ranks,
    struct dpu_symbol_t symbol,
    uint32_t symbol_offset,
    const void *src,
    size_t length,
    dpu_xfer_flags_t flags)
{
    CHECK_SYMBOL(symbol, symbol_offset, length);

    dpu_error_t status = DPU_OK;
    dpu_mem_max_addr_t address = symbol.address + symbol_offset;

    enum dpu_thread_job_type job_type;
    DPU_BROADCAST_SET_JOB_TYPE(job_type, address, length);

    uint32_t nr_jobs_per_rank;
    struct dpu_thread_job_sync sync;
    DPU_THREAD_JOB_GET_JOBS(ranks, nr_ranks, nr_jobs_per_rank, jobs, &sync, SYNCHRONOUS_FLAGS(flags), status);

    struct dpu_rank_t *rank __attribute__((unused));
    struct dpu_thread_job *job;
    DPU_THREAD_JOB_SET_JOBS(ranks, rank, nr_ranks, jobs, job, &sync, SYNCHRONOUS_FLAGS(flags), {
        job->type = job_type;
        job->address = address;
        job->length = length;
        job->buffer = src;
    });

    status = dpu_thread_job_do_jobs(ranks, nr_ranks, nr_jobs_per_rank, jobs, SYNCHRONOUS_FLAGS(flags), &sync);

    return status;
}

static const char *
dpu_transfer_to_string(dpu_xfer_t transfer)
{
    switch (transfer) {
        case DPU_XFER_TO_DPU:
            return "HOST_TO_DPU";
        case DPU_XFER_FROM_DPU:
            return "DPU_TO_HOST";
        default:
            return "UNKNOWN";
    }
}

static bool
check_dpu_program(struct dpu_t *dpu, struct dpu_program_t **the_program)
{

    if (!dpu_is_enabled(dpu)) {
        return true;
    }

    struct dpu_program_t *dpu_program = dpu_get_program(dpu);

    if (*the_program == NULL) {
        *the_program = dpu_program;
    }

    if (*the_program != dpu_program) {
        return false;
    }
    return true;
}

dpu_error_t
dpu_get_common_program(struct dpu_set_t *dpu_set, struct dpu_program_t **program)
{
    struct dpu_program_t *the_program = NULL;

    switch (dpu_set->kind) {
        case DPU_SET_RANKS:
            for (uint32_t each_rank = 0; each_rank < dpu_set->list.nr_ranks; ++each_rank) {
                struct dpu_rank_t *rank = dpu_set->list.ranks[each_rank];
                uint8_t nr_cis = rank->description->hw.topology.nr_of_control_interfaces;
                uint8_t nr_dpus_per_ci = rank->description->hw.topology.nr_of_dpus_per_control_interface;

                for (int each_ci = 0; each_ci < nr_cis; ++each_ci) {
                    for (int each_dpu = 0; each_dpu < nr_dpus_per_ci; ++each_dpu) {
                        struct dpu_t *dpu = DPU_GET_UNSAFE(rank, each_ci, each_dpu);

                        if (!check_dpu_program(dpu, &the_program))
                            return DPU_ERR_DIFFERENT_DPU_PROGRAMS;
                    }
                }
            }
            break;
        case DPU_SET_DPU:
            the_program = dpu_get_program(dpu_set->dpu);
            break;
        default:
            return DPU_ERR_INTERNAL;
    }
    if (the_program == NULL) {
        return DPU_ERR_NO_PROGRAM_LOADED;
    }

    *program = the_program;
    return DPU_OK;
}


__API_SYMBOL__ dpu_error_t
dpu_get_symbol(struct dpu_program_t *program, const char *symbol_name, struct dpu_symbol_t *symbol)
{
    LOG_FN(DEBUG, "\"%s\"", symbol_name);

    dpu_error_t status = DPU_OK;

    uint32_t nr_symbols = program->symbols->nr_symbols;

    for (uint32_t each_symbol = 0; each_symbol < nr_symbols; ++each_symbol) {
        dpu_elf_symbol_t *elf_symbol = program->symbols->map + each_symbol;
        if (strcmp(symbol_name, elf_symbol->name) == 0) {
            symbol->address = elf_symbol->value;
            symbol->size = elf_symbol->size;
            goto end;
        }
    }

    status = DPU_ERR_UNKNOWN_SYMBOL;

end:
    return status;
}

__API_SYMBOL__ dpu_error_t
dpu_broadcast_to(struct dpu_set_t dpu_set,
    const char *symbol_name,
    uint32_t symbol_offset,
    const void *src,
    size_t length,
    dpu_xfer_flags_t flags)
{
    LOG_FN(DEBUG, "%s, %d, %zd, 0x%x", symbol_name, symbol_offset, length, flags);
    dpu_error_t status;
    struct dpu_program_t *program;
    struct dpu_symbol_t symbol;

    if ((status = dpu_get_common_program(&dpu_set, &program)) != DPU_OK) {
        return status;
    }

    if ((status = dpu_get_symbol(program, symbol_name, &symbol)) != DPU_OK) {
        return status;
    }


    return dpu_broadcast_to_symbol(dpu_set, symbol, symbol_offset, src, length, flags);
}

__API_SYMBOL__ uint32_t
fifo_get_symbol_offset(struct dpu_set_t dpu_set,
                       const char *symbol_name, uint32_t symbol_offset)
{
    struct dpu_program_t *program;
    struct dpu_symbol_t symbol;

    dpu_get_common_program(&dpu_set, &program);

    dpu_get_symbol(program, symbol_name, &symbol);

    return symbol.address + symbol_offset;
}

__API_SYMBOL__ dpu_error_t
dpu_broadcast_to_symbol(struct dpu_set_t dpu_set,
    struct dpu_symbol_t symbol,
    uint32_t symbol_offset,
    const void *src,
    size_t length,
    dpu_xfer_flags_t flags)
{
    LOG_FN(DEBUG, "0x%08x, %d, %d, %zd, 0x%x", symbol.address, symbol.size, symbol_offset, length, flags);
    dpu_error_t status = DPU_OK;

    switch (dpu_set.kind) {
        case DPU_SET_RANKS:
            status = dpu_broadcast_to_symbol_for_ranks(
                dpu_set.list.ranks, dpu_set.list.nr_ranks, symbol, symbol_offset, src, length, flags);
            break;
        case DPU_SET_DPU:
            status = dpu_copy_symbol_dpu(dpu_set.dpu, symbol, symbol_offset, (void *)src, length, DPU_XFER_TO_DPU, flags);
            break;
        default:
            return DPU_ERR_INTERNAL;
    }

    return status;
}

__API_SYMBOL__ dpu_error_t
dpu_copy_to(struct dpu_set_t dpu_set, const char *symbol_name, uint32_t symbol_offset, const void *src, size_t length)
{
    LOG_FN(DEBUG, "\"%s\", %d, %p, %zd)", symbol_name, symbol_offset, src, length);

    dpu_error_t status;
    struct dpu_program_t *program;
    struct dpu_symbol_t symbol;

    if ((status = dpu_get_common_program(&dpu_set, &program)) != DPU_OK) {
        return status;
    }

    if ((status = dpu_get_symbol(program, symbol_name, &symbol)) != DPU_OK) {
        return status;
    }

    return dpu_broadcast_to_symbol(dpu_set, symbol, symbol_offset, src, length, DPU_XFER_DEFAULT);
}

__API_SYMBOL__ dpu_error_t
dpu_copy_from(struct dpu_set_t dpu_set, const char *symbol_name, uint32_t symbol_offset, void *dst, size_t length)
{
    LOG_FN(DEBUG, "\"%s\", %d, %p, %zd)", symbol_name, symbol_offset, dst, length);

    if (dpu_set.kind != DPU_SET_DPU) {
        return dpu_set.kind == DPU_SET_RANKS ? DPU_ERR_INVALID_DPU_SET : DPU_ERR_INTERNAL;
    }

    struct dpu_t *dpu = dpu_set.dpu;
    dpu_error_t status = DPU_OK;

    struct dpu_symbol_t symbol;
    struct dpu_program_t *program;

    if ((program = dpu_get_program(dpu)) == NULL) {
        return DPU_ERR_NO_PROGRAM_LOADED;
    }

    if ((status = dpu_get_symbol(program, symbol_name, &symbol)) != DPU_OK) {
        return status;
    }

    return dpu_copy_symbol_dpu(dpu_set.dpu, symbol, symbol_offset, dst, length, DPU_XFER_FROM_DPU, DPU_XFER_DEFAULT);
}

__API_SYMBOL__ dpu_error_t
dpu_copy_to_symbol(struct dpu_set_t dpu_set, struct dpu_symbol_t symbol, uint32_t symbol_offset, const void *src, size_t length)
{
    LOG_FN(DEBUG, "0x%08x, %d, %d, %p, %zd)", symbol.address, symbol.size, symbol_offset, src, length);

    return dpu_broadcast_to_symbol(dpu_set, symbol, symbol_offset, src, length, DPU_XFER_DEFAULT);
}

__API_SYMBOL__ dpu_error_t
dpu_copy_from_symbol(struct dpu_set_t dpu_set, struct dpu_symbol_t symbol, uint32_t symbol_offset, void *dst, size_t length)
{
    LOG_FN(DEBUG, "0x%08x, %d, %d, %p, %zd)", symbol.address, symbol.size, symbol_offset, dst, length);

    if (dpu_set.kind != DPU_SET_DPU) {
        return dpu_set.kind == DPU_SET_RANKS ? DPU_ERR_INVALID_DPU_SET : DPU_ERR_INTERNAL;
    }

    return dpu_copy_symbol_dpu(dpu_set.dpu, symbol, symbol_offset, dst, length, DPU_XFER_FROM_DPU, DPU_XFER_DEFAULT);
}

struct dpu_transfer_matrix *
dpu_get_transfer_matrix(struct dpu_rank_t *rank)
{
    if (rank->api.callback_tid_set && rank->api.callback_tid == pthread_self()) {
        return &rank->api.callback_matrix;
    } else {
        return &rank->api.matrix;
    }
}

struct dpu_rank_t *
dpu_get_as_rank_t(struct dpu_set_t set)
{
    return set.list.ranks[0];
}

static inline void
reset_sg_buffer_pool(struct dpu_set_t rank, struct sg_xfer_buffer *sg_buffer_pool)
{
    struct dpu_set_t dpu;
    DPU_FOREACH (rank, dpu) {
        uint32_t dpu_transfer_matrix_index = get_transfer_matrix_index(dpu.dpu->rank, dpu.dpu->slice_id, dpu.dpu->dpu_id);
        // set the number of block to 0
        sg_buffer_pool[dpu_transfer_matrix_index].nr_blocks = 0;
    }
}

struct sg_xfer_callback_args {
    /** get_block information */
    get_block_t *get_block_info;
    /** direction of the transfer */
    dpu_xfer_t xfer;
    /** the DPU symbol address where the transfer starts */
    dpu_mem_max_addr_t symbol_addr;
    /** length the number of bytes to copy */
    size_t length;
    /** flags options of the transfer */
    dpu_sg_xfer_flags_t flags;
    /** reference counter used to free the callback arguments*/
    uint32_t freeing_refcpt;
};

static pthread_mutex_t freeing_refcpt_mutex = PTHREAD_MUTEX_INITIALIZER;

static inline void
free_cb_args(struct sg_xfer_callback_args *args)
{
    pthread_mutex_lock(&freeing_refcpt_mutex);
    args->freeing_refcpt--;
    if (!args->freeing_refcpt) {
        free(args->get_block_info->args);
        free(args->get_block_info);
        free(args);
    }
    pthread_mutex_unlock(&freeing_refcpt_mutex);
}

__PERF_PROFILING_SYMBOL__ dpu_error_t
sg_xfer_rank_handler(struct dpu_set_t rank, __attribute((unused)) uint32_t rank_id, void *cb_args)
{
    struct dpu_set_t dpu;
    struct sg_block_info binfo;
    dpu_error_t status = DPU_OK;
    struct dpu_transfer_matrix transfer_matrix;

    // retreive arguments of the rank
    struct sg_xfer_callback_args *args = (struct sg_xfer_callback_args *)(cb_args);

    uint32_t dpu_rank_offset = dpu_get_as_rank_t(rank)->dpu_offset;
    struct sg_xfer_buffer *sg_buffer_pool = dpu_get_as_rank_t(rank)->api.sg_buffer_pool;

    dpu_xfer_t xfer = args->xfer;

    // clearing the transfer matrix before using it
    dpu_transfer_matrix_clear_all(dpu_get_as_rank_t(rank), &transfer_matrix);

    mram_addr_t mram_byte_offset = args->symbol_addr;
    UPDATE_MRAM_COPY_PARAMETERS(mram_byte_offset, length);

    transfer_matrix.type = DPU_SG_XFER_MATRIX;
    transfer_matrix.size = args->length;
    transfer_matrix.offset = mram_byte_offset;

    uint32_t block_index = 0;
    uint32_t rank_dpu_index;

    bool check_length = ((args->flags & DPU_SG_XFER_DISABLE_LENGTH_CHECK) == 0);

    if (!dpu_get_as_rank_t(rank)->api.sg_xfer_enabled) {
        status = DPU_ERR_SG_NOT_ACTIVATED;
        goto end;
    }

    // add each block of each DPU
    DPU_FOREACH (rank, dpu, rank_dpu_index) {

        uint32_t dpu_transfer_matrix_index = get_transfer_matrix_index(dpu.dpu->rank, dpu.dpu->slice_id, dpu.dpu->dpu_id);
        uint32_t dpu_index = rank_dpu_index + dpu_rank_offset;
        uint32_t dpu_sg_buffer_pool_max_nr_blocks = sg_buffer_pool[dpu_transfer_matrix_index].max_nr_blocks;
        uint32_t dpu_nr_bytes_to_transfer = 0;

        while (1) {

            // call the get_block function
            bool block_exists = args->get_block_info->f(&binfo, dpu_index, block_index, args->get_block_info->args);

            if (block_exists) {
                // the number of blocks exceed the capacity of the sg buffer pool
                if (block_index >= dpu_sg_buffer_pool_max_nr_blocks) {
                    status = DPU_ERR_SG_TOO_MANY_BLOCKS;
                    goto end;
                }
                // if DPU_SG_XFER_DISABLE_LENGTH_CHECK is provided, sending more bytes than "length" is authorized
                if (binfo.length && (check_length && dpu_nr_bytes_to_transfer >= args->length)) {
                    status = DPU_ERR_SG_LENGTH_MISMATCH;
                    goto end;
                }
            }
            // no more block to add for this DPU
            else {
                // the precedent block was the last one, so we can check the number of bytes to be send for this DPU
                // if DPU_SG_XFER_DISABLE_LENGTH_CHECK is not provided, the number of bytes must be equal to length
                if (check_length && dpu_nr_bytes_to_transfer != args->length) {
                    status = DPU_ERR_SG_LENGTH_MISMATCH;
                    goto end;
                }

                block_index = 0;
                break;
            }

            // add this block if not empty
            if (binfo.length) {
                dpu_nr_bytes_to_transfer += binfo.length;
                switch ((args->xfer)) {
                    case DPU_XFER_TO_DPU:
                    case DPU_XFER_FROM_DPU:

                        // if this is the first time we add a new block for this DPU
                        // initialize the current DPU sg buffer pool pointer
                        if (transfer_matrix.sg_ptr[dpu_transfer_matrix_index] == NULL)
                            transfer_matrix.sg_ptr[dpu_transfer_matrix_index] = &sg_buffer_pool[dpu_transfer_matrix_index];

                        // add the new block to the transfer matrix
                        if ((status = dpu_transfer_matrix_add_dpu_block(dpu.dpu, &transfer_matrix, binfo.addr, binfo.length))
                            != DPU_OK)
                            goto end;
                        break;
                    default:
                        status = DPU_ERR_INVALID_MEMORY_TRANSFER;
                        goto end;
                }
            }

            block_index++;
        }
    }

    // launch the transfer
    switch (xfer) {
        case DPU_XFER_TO_DPU:
            if ((status = dpu_copy_to_mrams(dpu_get_as_rank_t(rank), &transfer_matrix)) != DPU_OK)
                goto end;
            break;
        case DPU_XFER_FROM_DPU:
            if ((status = dpu_copy_from_mrams(dpu_get_as_rank_t(rank), &transfer_matrix)) != DPU_OK)
                goto end;
            break;
        default:
            status = DPU_ERR_INVALID_MEMORY_TRANSFER;
            goto end;
    }

end:
    // reset the scatter gather buffer pool
    reset_sg_buffer_pool(rank, sg_buffer_pool);

    // freeing the callback arguments
    free_cb_args(args);

    return status;
}

__API_SYMBOL__ dpu_error_t
dpu_push_sg_xfer_symbol(struct dpu_set_t dpu_set,
    dpu_xfer_t xfer,
    struct dpu_symbol_t symbol,
    uint32_t symbol_offset,
    size_t length,
    get_block_t *get_block_info,
    dpu_sg_xfer_flags_t flags)
{
    LOG_FN(DEBUG,
        "%s, 0x%08x, %d, %d, %zd, 0x%x",
        dpu_transfer_to_string(xfer),
        symbol.address,
        symbol.size,
        symbol_offset,
        length,
        flags);

    dpu_error_t status;

    bool is_async = ((flags & DPU_SG_XFER_ASYNC) != 0);

    switch (dpu_set.kind) {
        case DPU_SET_RANKS: {
            break;
        }
        case DPU_SET_DPU: {
            fprintf(stderr, "scattered xfer to one DPU is not supported \n");
            return DPU_ERR_INTERNAL;
        }
        default:
            return DPU_ERR_INTERNAL;
    }

    dpu_mem_max_addr_t symbol_addr = (symbol).address + (symbol_offset);

    // check if the symbol is a MRAM symbol
    {
        if (!(((symbol_addr)&MRAM_MASK) == MRAM_MASK))
            return DPU_ERR_SG_NOT_MRAM_SYMBOL;
    }

    // alloc callback arguments and initialize ref counter
    // the arguments are freed at the end of the last executed callback
    struct sg_xfer_callback_args *sg_xfer_cb_args = malloc(sizeof(struct sg_xfer_callback_args));
    dpu_get_nr_ranks(dpu_set, &sg_xfer_cb_args->freeing_refcpt);
    sg_xfer_cb_args->get_block_info = malloc(sizeof(struct get_block_t));
    sg_xfer_cb_args->get_block_info->f = get_block_info->f;
    sg_xfer_cb_args->get_block_info->args = malloc(get_block_info->args_size);
    memcpy(sg_xfer_cb_args->get_block_info->args, get_block_info->args, get_block_info->args_size);
    sg_xfer_cb_args->get_block_info->args_size = get_block_info->args_size;
    sg_xfer_cb_args->xfer = xfer;
    sg_xfer_cb_args->symbol_addr = symbol_addr;
    sg_xfer_cb_args->length = length;
    sg_xfer_cb_args->flags = flags;

    if ((status
            = dpu_callback(dpu_set, sg_xfer_rank_handler, sg_xfer_cb_args, is_async ? DPU_CALLBACK_ASYNC : DPU_CALLBACK_DEFAULT))
        != DPU_OK)
        return status;

    return DPU_OK;
}

__API_SYMBOL__ dpu_error_t
dpu_push_sg_xfer(struct dpu_set_t dpu_set,
    dpu_xfer_t xfer,
    const char *symbol_name,
    uint32_t symbol_offset,
    size_t length,
    get_block_t *get_block_info,
    dpu_sg_xfer_flags_t flags)
{
    LOG_FN(DEBUG, "%s, %s, %d, %zd, 0x%x", dpu_transfer_to_string(xfer), symbol_name, symbol_offset, length, flags);

    dpu_error_t status;

    struct dpu_program_t *program;
    struct dpu_symbol_t symbol;

    if ((status = dpu_get_common_program(&dpu_set, &program)) != DPU_OK) {
        return status;
    }

    if ((status = dpu_get_symbol(program, symbol_name, &symbol)) != DPU_OK) {
        return status;
    }

    return dpu_push_sg_xfer_symbol(dpu_set, xfer, symbol, symbol_offset, length, get_block_info, flags);
}

__API_SYMBOL__ dpu_error_t
dpu_prepare_xfer(struct dpu_set_t dpu_set, void *buffer)
{
    LOG_FN(DEBUG, "%p", buffer);

    switch (dpu_set.kind) {
        case DPU_SET_RANKS:
            for (uint32_t each_rank = 0; each_rank < dpu_set.list.nr_ranks; ++each_rank) {
                struct dpu_rank_t *rank = dpu_set.list.ranks[each_rank];
                uint8_t nr_cis = rank->description->hw.topology.nr_of_control_interfaces;
                uint8_t nr_dpus_per_ci = rank->description->hw.topology.nr_of_dpus_per_control_interface;

                for (uint8_t each_ci = 0; each_ci < nr_cis; ++each_ci) {
                    for (uint8_t each_dpu = 0; each_dpu < nr_dpus_per_ci; ++each_dpu) {
                        struct dpu_t *dpu = DPU_GET_UNSAFE(rank, each_ci, each_dpu);

                        if (!dpu_is_enabled(dpu)) {
                            continue;
                        }

                        dpu_transfer_matrix_add_dpu(dpu, dpu_get_transfer_matrix(rank), buffer);
                    }
                }
            }

            break;
        case DPU_SET_DPU: {
            struct dpu_t *dpu = dpu_set.dpu;

            if (!dpu_is_enabled(dpu)) {
                return DPU_ERR_DPU_DISABLED;
            }

            dpu_transfer_matrix_add_dpu(dpu, dpu_get_transfer_matrix(dpu_get_rank(dpu)), buffer);

            break;
        }
        default:
            return DPU_ERR_INTERNAL;
    }

    return DPU_OK;
}

__API_SYMBOL__ dpu_error_t
dpu_push_xfer(struct dpu_set_t dpu_set,
    dpu_xfer_t xfer,
    const char *symbol_name,
    uint32_t symbol_offset,
    size_t length,
    dpu_xfer_flags_t flags)
{
    LOG_FN(DEBUG, "%s, %s, %d, %zd, 0x%x", dpu_transfer_to_string(xfer), symbol_name, symbol_offset, length, flags);

    dpu_error_t status;
    struct dpu_program_t *program;
    struct dpu_symbol_t symbol;

    if ((status = dpu_get_common_program(&dpu_set, &program)) != DPU_OK) {
        return status;
    }

    if ((status = dpu_get_symbol(program, symbol_name, &symbol)) != DPU_OK) {
        return status;
    }

    return dpu_push_xfer_symbol(dpu_set, xfer, symbol, symbol_offset, length, flags);
}

__API_SYMBOL__ dpu_error_t
dpu_push_xfer_symbol(struct dpu_set_t dpu_set,
    dpu_xfer_t xfer,
    struct dpu_symbol_t symbol,
    uint32_t symbol_offset,
    size_t length,
    dpu_xfer_flags_t flags)
{
    LOG_FN(DEBUG,
        "%s, 0x%08x, %d, %d, %zd, 0x%x",
        dpu_transfer_to_string(xfer),
        symbol.address,
        symbol.size,
        symbol_offset,
        length,
        flags);

    dpu_error_t status = DPU_OK;
    dpu_mem_max_addr_t address = symbol.address + symbol_offset;

    switch (dpu_set.kind) {
        case DPU_SET_RANKS: {
            CHECK_SYMBOL(symbol, symbol_offset, length);

            uint32_t nr_ranks = dpu_set.list.nr_ranks;
            struct dpu_rank_t **ranks = dpu_set.list.ranks;

            enum dpu_thread_job_type job_type;
            DPU_COPY_MATRIX_SET_JOB_TYPE(job_type, xfer, address, length);

            uint32_t nr_jobs_per_rank;
            bool is_parallel = false;
            if (flags & DPU_XFER_PARALLEL) {
                // this is a memory transfer to execute
                // in parallel of DPU execution
                // This is only possible if the transfer is in WRAM
                // return an error if not
                switch (job_type) {
                    case DPU_THREAD_JOB_COPY_WRAM_FROM_MATRIX:
                        job_type = DPU_THREAD_JOB_PARALLEL_COPY_WRAM_FROM_MATRIX;
                        is_parallel = true;
                        break;
                    case DPU_THREAD_JOB_COPY_WRAM_TO_MATRIX:
                        job_type = DPU_THREAD_JOB_PARALLEL_COPY_WRAM_TO_MATRIX;
                        is_parallel = true;
                        break;
                    default:
                        return DPU_ERR_INVALID_PARALLEL_MEMORY_TRANSFER;
                }
            }

            struct dpu_thread_job_sync sync;
            DPU_THREAD_JOB_GET_JOBS(ranks, nr_ranks, nr_jobs_per_rank, jobs, &sync, SYNCHRONOUS_FLAGS(flags), status);

            struct dpu_rank_t *rank;
            struct dpu_thread_job *job;
            DPU_THREAD_JOB_SET_JOBS_PARALLEL(ranks, rank, nr_ranks, jobs, job, &sync, SYNCHRONOUS_FLAGS(flags), is_parallel, {
                dpu_transfer_matrix_copy(&job->matrix, dpu_get_transfer_matrix(rank));
                job->type = job_type;
                job->matrix.offset = address;
                job->matrix.size = length;
            });

            status = dpu_thread_job_do_jobs(ranks, nr_ranks, nr_jobs_per_rank, jobs, SYNCHRONOUS_FLAGS(flags), &sync);

        } break;
        case DPU_SET_DPU: {
            struct dpu_t *dpu = dpu_set.dpu;
            struct dpu_rank_t *rank = dpu_get_rank(dpu);
            uint8_t dpu_transfer_matrix_index = get_transfer_matrix_index(rank, dpu_get_slice_id(dpu), dpu_get_member_id(dpu));
            void *buffer = dpu_get_transfer_matrix(rank)->ptr[dpu_transfer_matrix_index];

            status = dpu_copy_symbol_dpu(dpu, symbol, symbol_offset, buffer, length, xfer, flags);

            break;
        }
        default:
            return DPU_ERR_INTERNAL;
    }

    if ((flags & DPU_XFER_NO_RESET) == 0) {
        switch (dpu_set.kind) {
            case DPU_SET_RANKS:
                for (uint32_t each_rank = 0; each_rank < dpu_set.list.nr_ranks; ++each_rank) {
                    struct dpu_rank_t *rank = dpu_set.list.ranks[each_rank];
                    dpu_transfer_matrix_clear_all(rank, dpu_get_transfer_matrix(rank));
                }
                break;
            case DPU_SET_DPU: {
                struct dpu_t *dpu = dpu_set.dpu;
                dpu_transfer_matrix_clear_dpu(dpu, dpu_get_transfer_matrix(dpu_get_rank(dpu)));
                break;
            }
            default:
                return DPU_ERR_INTERNAL;
        }
    }

    return status;
}

__API_SYMBOL__ dpu_error_t
dpu_fifo_prepare_xfer(struct dpu_set_t dpu_set, struct dpu_fifo_link_t *fifo_link, void *buffer)
{
    LOG_FN(DEBUG, "%p", buffer);
    switch (dpu_set.kind) {
        case DPU_SET_RANKS: {

            struct dpu_set_t dpu;
            DPU_FOREACH (dpu_set, dpu) {

                struct dpu_t *dpu_ = dpu_from_set(dpu);
                struct dpu_rank_t *rank = dpu_get_rank(dpu_);
                struct dpu_fifo_rank_t *fifo = get_rank_fifo(fifo_link, rank);
                struct dpu_transfer_matrix *matrix = &(fifo->transfer_matrix);

                if (!dpu_is_enabled(dpu_)) {
                    continue;
                }

                dpu_transfer_matrix_add_dpu(dpu_, matrix, buffer);
            }
            break;
        }
        case DPU_SET_DPU: {
            struct dpu_t *dpu = dpu_set.dpu;
            struct dpu_rank_t *rank = dpu_get_rank(dpu);

            if (!dpu_is_enabled(dpu)) {
                return DPU_ERR_DPU_DISABLED;
            }

            struct dpu_transfer_matrix *matrix = &(get_rank_fifo(fifo_link, rank)->transfer_matrix);
            dpu_transfer_matrix_add_dpu(dpu, matrix, buffer);

            break;
        }
        default:
            return DPU_ERR_INTERNAL;
    }

    return DPU_OK;
}

// TODO we could have the number of elements to be pushed to the DPU (i.e., be able to push several elements at once)
// TODO Here we could also have a condition that for push from DPU,
// we do it only if the max number of elements in the output FIFO exceeds a certain threshold.
// This would avoid transfering empty data
//
__API_SYMBOL__ dpu_error_t
dpu_fifo_push_xfer(struct dpu_set_t dpu_set, struct dpu_fifo_link_t *fifo_link, dpu_xfer_flags_t flags)
{

    dpu_error_t status = DPU_OK;

    switch (dpu_set.kind) {
        case DPU_SET_RANKS: {

            uint32_t nr_ranks = 0;
            struct dpu_rank_t **ranks = 0;

            enum dpu_thread_job_type job_type = DPU_THREAD_JOB_PARALLEL_FIFO_PUSH;
            if (fifo_link->direction == DPU_OUTPUT_FIFO)
                job_type = DPU_THREAD_JOB_PARALLEL_FIFO_FLUSH;
            struct dpu_thread_job_sync sync;
            uint32_t nr_jobs = 0;
            nr_ranks = dpu_set.list.nr_ranks;
            ranks = dpu_set.list.ranks;

            DPU_THREAD_JOB_GET_JOBS(ranks, nr_ranks, nr_jobs, jobs, &sync, SYNCHRONOUS_FLAGS(flags), status);

            struct dpu_rank_t *rank;
            struct dpu_thread_job *job;

            DPU_THREAD_JOB_SET_JOBS_PARALLEL(ranks, rank, nr_ranks, jobs, job, &sync, SYNCHRONOUS_FLAGS(flags), true, {
                job->fifo = get_rank_fifo(fifo_link, rank);
                job->fifo_dpu = NULL;
                dpu_transfer_matrix_copy(&job->fifo_transfer_matrix, &(get_rank_fifo(fifo_link, rank)->transfer_matrix));
                job->type = job_type;
            });

            status = dpu_thread_job_do_jobs(ranks, nr_ranks, nr_jobs, jobs, SYNCHRONOUS_FLAGS(flags), &sync);

        } break;
        case DPU_SET_DPU: {
            struct dpu_t *dpu = dpu_set.dpu;
            struct dpu_rank_t *rank = dpu_get_rank(dpu);

            enum dpu_thread_job_type job_type = DPU_THREAD_JOB_PARALLEL_FIFO_PUSH;
            
            struct dpu_thread_job_sync sync;
            uint32_t nr_jobs_per_rank = 0;
            DPU_THREAD_JOB_GET_JOBS(&rank, 1, nr_jobs_per_rank, jobs, &sync, SYNCHRONOUS_FLAGS(flags), status);

            struct dpu_rank_t *rrank __attribute__((unused));
            struct dpu_thread_job *job;
            DPU_THREAD_JOB_SET_JOBS_PARALLEL(&rank, rrank, 1, jobs, job, &sync, SYNCHRONOUS_FLAGS(flags), true, {
                job->fifo = get_rank_fifo(fifo_link, rank);
                job->fifo_dpu = dpu;
                dpu_transfer_matrix_copy(&job->fifo_transfer_matrix, &(get_rank_fifo(fifo_link, rank)->transfer_matrix));
                job->type = job_type;
            });

            status = dpu_thread_job_do_jobs(&rank, 1, nr_jobs_per_rank, jobs, SYNCHRONOUS_FLAGS(flags), &sync);

            break;
        }
        default:
            return DPU_ERR_INTERNAL;
    }

    if ((flags & DPU_XFER_NO_RESET) == 0) {
        switch (dpu_set.kind) {
            case DPU_SET_RANKS:
                for (uint32_t each_rank = 0; each_rank < dpu_set.list.nr_ranks; ++each_rank) {
                    struct dpu_rank_t *rank = dpu_set.list.ranks[each_rank];
                    dpu_transfer_matrix_clear_all(rank, &(get_rank_fifo(fifo_link, rank)->transfer_matrix));
                }
                break;
            case DPU_SET_DPU: {
                struct dpu_t *dpu = dpu_set.dpu;
                dpu_transfer_matrix_clear_dpu(dpu, &(get_rank_fifo(fifo_link, dpu_get_rank(dpu))->transfer_matrix));
                break;
            }
            default:
                return DPU_ERR_INTERNAL;
        }
    }

    return status;
}
