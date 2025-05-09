/* Copyright 2020 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

#include <dpu_transfer_matrix.h>
#include <dpu_api_verbose.h>
#include <dpu.h>
#include <dpu_error.h>
#include <dpu_types.h>
#include <dpu_attributes.h>
#include <dpu_log_utils.h>
#include <dpu_polling.h>
#include <dpu_management.h>
#include <dpu_rank_handler.h>
#include <dpu_thread_job.h>
#include <dpu_properties_loader.h>
#include <dpu_profile.h>
#include <dpu_config.h>
#include <dpu_rank.h>
#include <dpu_internals.h>
#include <dpu_mask.h>
#include <assert.h>
#include <dpu_transfer_matrix.h>
#include <dpu_types.h>

__API_SYMBOL__  uint32_t
get_nr_of_dpus_in_rank(struct dpu_rank_t *rank)
{
    dpu_description_t description = dpu_get_description(rank);

    uint8_t nr_cis = description->hw.topology.nr_of_control_interfaces;
    uint8_t nr_dpus_per_ci = description->hw.topology.nr_of_dpus_per_control_interface;

    uint32_t count = 0;

    for (uint8_t each_ci = 0; each_ci < nr_cis; ++each_ci) {
        if (rank->runtime.control_interface.slice_info[each_ci].all_dpus_are_enabled) {
            count += nr_dpus_per_ci;
        } else {
            count += dpu_mask_count(rank->runtime.control_interface.slice_info[each_ci].enabled_dpus);
        }
    }

    return count;
}

static dpu_error_t
sg_buffer_pool_create(struct dpu_set_t set, uint32_t dpu_max_nr_blocks)
{
    if (set.list.ranks[0]->api.sg_xfer_enabled == false)
        return DPU_ERR_INTERNAL;

    uint8_t **sg_buffer_pool_blocks_addr = set.list.ranks[0]->api.sg_buffer_pool[0].blocks_addr;
    if (sg_buffer_pool_blocks_addr != NULL)
        return DPU_ERR_INTERNAL;

    uint32_t *sg_buffer_pool_blocks_length = set.list.ranks[0]->api.sg_buffer_pool[0].blocks_length;
    if (sg_buffer_pool_blocks_length != NULL)
        return DPU_ERR_INTERNAL;

    sg_buffer_pool_blocks_addr = malloc(set.list.nr_ranks * MAX_NR_DPUS_PER_RANK * (1 + dpu_max_nr_blocks) * sizeof(uint8_t *));
    sg_buffer_pool_blocks_length = malloc(set.list.nr_ranks * MAX_NR_DPUS_PER_RANK * (1 + dpu_max_nr_blocks) * sizeof(uint32_t));

    for (uint32_t each_rank = 0; each_rank < set.list.nr_ranks; ++each_rank) {
        struct dpu_rank_t *rank = set.list.ranks[each_rank];
        struct sg_xfer_buffer *sg_buffer_pool = rank->api.sg_buffer_pool;

        for (uint32_t dpu_index = 0; dpu_index < MAX_NR_DPUS_PER_RANK; dpu_index++) {

            uint32_t dpu_buffer_pos = (each_rank * MAX_NR_DPUS_PER_RANK + dpu_index) * (1 + dpu_max_nr_blocks);
            sg_buffer_pool[dpu_index] = (struct sg_xfer_buffer) { .max_nr_blocks = dpu_max_nr_blocks,
                .blocks_addr = &sg_buffer_pool_blocks_addr[dpu_buffer_pos],
                .blocks_length = &sg_buffer_pool_blocks_length[dpu_buffer_pos],
                .nr_blocks = 0 };
        }
    }

    return DPU_OK;
}

static dpu_error_t
sg_buffer_pool_free(struct dpu_set_t set)
{
    if (set.list.ranks[0]->api.sg_xfer_enabled == false)
        return DPU_OK;

    {
        uint8_t **sg_buffer_pool_blocks_addr = set.list.ranks[0]->api.sg_buffer_pool[0].blocks_addr;

        if (sg_buffer_pool_blocks_addr == NULL)
            return DPU_ERR_INTERNAL;

        free(sg_buffer_pool_blocks_addr);
    }

    {
        uint32_t *sg_buffer_pool_blocks_length = set.list.ranks[0]->api.sg_buffer_pool[0].blocks_length;

        if (sg_buffer_pool_blocks_length == NULL)
            return DPU_ERR_INTERNAL;

        free(sg_buffer_pool_blocks_length);
    }

    set.list.ranks[0]->api.sg_xfer_enabled = false;

    return DPU_OK;
}

__API_SYMBOL__ dpu_error_t
dpu_get_nr_ranks(struct dpu_set_t dpu_set, uint32_t *nr_ranks)
{
    LOG_FN(DEBUG, "");

    switch (dpu_set.kind) {
        case DPU_SET_RANKS:
            *nr_ranks = dpu_set.list.nr_ranks;
            break;
        case DPU_SET_DPU:
            *nr_ranks = 1;
            break;
        default:
            return DPU_ERR_INTERNAL;
    }

    return DPU_OK;
}

__API_SYMBOL__ dpu_error_t
dpu_get_nr_dpus(struct dpu_set_t dpu_set, uint32_t *nr_dpus)
{
    LOG_FN(DEBUG, "");

    switch (dpu_set.kind) {
        case DPU_SET_RANKS:
            *nr_dpus = 0;

            for (uint32_t each_rank = 0; each_rank < dpu_set.list.nr_ranks; ++each_rank) {
                *nr_dpus += get_nr_of_dpus_in_rank(dpu_set.list.ranks[each_rank]);
            }

            break;
        case DPU_SET_DPU:
            *nr_dpus = 1;
            break;
        default:
            return DPU_ERR_INTERNAL;
    }

    return DPU_OK;
}

static pthread_mutex_t set_allocator_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct dpu_set_t *set_allocator_sets = NULL;
static uint32_t set_allocator_size = 0;
static uint32_t set_allocator_capacity = 0;

static void __attribute__((destructor, used)) set_allocator_destructor() { free(set_allocator_sets); }

static dpu_error_t
set_allocator_register(struct dpu_set_t *set)
{
    dpu_error_t status = DPU_OK;

    pthread_mutex_lock(&set_allocator_mutex);

    if (set_allocator_size == set_allocator_capacity) {
        set_allocator_capacity = 2 * set_allocator_capacity + 2;
        struct dpu_set_t *new_sets;
        if ((new_sets = realloc(set_allocator_sets, set_allocator_capacity * sizeof(*set_allocator_sets))) == NULL) {
            status = DPU_ERR_SYSTEM;
            goto unlock_mutex;
        }
        set_allocator_sets = new_sets;
    }

    memcpy(set_allocator_sets + set_allocator_size, set, sizeof(*set));

    set_allocator_size++;
unlock_mutex:
    pthread_mutex_unlock(&set_allocator_mutex);
    return status;
}

static struct dpu_set_t *
set_allocator_find(struct dpu_set_t *set)
{
    for (uint32_t each_set = 0; each_set < set_allocator_size; ++each_set) {
        struct dpu_set_t *allocated_set = set_allocator_sets + each_set;
        if (memcmp(set, allocated_set, sizeof(*set)) == 0) {
            return allocated_set;
        }
    }

    return NULL;
}

static dpu_error_t
set_allocator_unregister(struct dpu_set_t *set)
{
    dpu_error_t status = DPU_OK;
    struct dpu_set_t *allocated_set;

    pthread_mutex_lock(&set_allocator_mutex);

    if ((allocated_set = set_allocator_find(set)) == NULL) {
        status = DPU_ERR_INVALID_DPU_SET;
        goto unlock_mutex;
    }

    // todo: this is probably not the more efficient method
    uint32_t idx = allocated_set - set_allocator_sets;
    if (idx != (set_allocator_size - 1)) {
        memmove(
            &set_allocator_sets[idx], &set_allocator_sets[idx + 1], (set_allocator_size - 1 - idx) * sizeof(*set_allocator_sets));
    }
    set_allocator_size--;
unlock_mutex:
    pthread_mutex_unlock(&set_allocator_mutex);
    return status;
}

dpu_error_t
init_scatter_gather_transfer_buffer(const char *profile, struct dpu_set_t *dpu_set)
{
    uint32_t nr_dpus;

    dpu_error_t status = DPU_OK;

    dpu_get_nr_dpus(*dpu_set, &nr_dpus);

    // alloc scatter gather buffer pool
    bool dpu_sg_xfer_enabled = false;
    uint32_t dpu_sg_xfer_max_block_per_dpu = 0;
    dpu_properties_t properties = dpu_properties_load_from_profile(profile);
    if (!fetch_boolean_property(properties, DPU_PROFILE_PROPERTY_SG_XFER_ENABLE, &dpu_sg_xfer_enabled, false)) {
        status = DPU_ERR_INVALID_PROFILE;
        goto end;
    }

    if (!fetch_integer_property(properties, DPU_PROFILE_PROPERTY_SG_XFER_MAX_BLOCKS_PER_DPU, &dpu_sg_xfer_max_block_per_dpu, 0)) {
        status = DPU_ERR_INVALID_PROFILE;
        goto end;
    }

    if (!dpu_sg_xfer_enabled && dpu_sg_xfer_max_block_per_dpu > 0) {
        status = DPU_ERR_INVALID_PROFILE;
        goto end;
    }

    // set rank sg xfer enable
    for (uint32_t each_rank = 0; each_rank < dpu_set->list.nr_ranks; ++each_rank)
        dpu_set->list.ranks[each_rank]->api.sg_xfer_enabled = dpu_sg_xfer_enabled;

    if (dpu_sg_xfer_enabled) {
        if ((status
                = sg_buffer_pool_create(*dpu_set, (dpu_sg_xfer_max_block_per_dpu > 0) ? dpu_sg_xfer_max_block_per_dpu : nr_dpus))
            != DPU_OK) {
            goto end;
        }
    }

    // fill dpu_rank_offset structure
    nr_dpus = 0;
    for (uint32_t each_rank = 0; each_rank < dpu_set->list.nr_ranks; ++each_rank) {
        struct dpu_rank_t *rank = dpu_set->list.ranks[each_rank];
        dpu_set->list.ranks[each_rank]->dpu_offset = nr_dpus;
        nr_dpus += get_nr_of_dpus_in_rank(rank);
    }

end:
    dpu_properties_delete(properties);

    return status;
}

static dpu_error_t
init_dpu_set(struct dpu_rank_t **ranks, uint32_t nr_ranks, struct dpu_set_t *dpu_set, const char *profile)
{
    dpu_error_t status;

    // Making sure that the whole structure is initialized
    // (in particular, we are using memcmp in set_allocator_find)
    memset(dpu_set, 0, sizeof(*dpu_set));
    dpu_set->kind = DPU_SET_RANKS;
    dpu_set->list.nr_ranks = nr_ranks;
    dpu_set->list.ranks = ranks;

    if ((status = init_scatter_gather_transfer_buffer(profile, dpu_set)) != DPU_OK) {
        goto end;
    }

    if ((status = dpu_thread_job_create(ranks, nr_ranks)) != DPU_OK) {
        goto end;
    }

    if ((status = set_allocator_register(dpu_set)) != DPU_OK) {
        goto end;
    }

    if ((status = polling_thread_create()) != DPU_OK) {
        goto end;
    }

end:
    return status;
}

static dpu_error_t
disable_one_dpu(struct dpu_rank_t **ranks, uint32_t rank_count)
{
    dpu_member_id_t target_line = UCHAR_MAX;
    dpu_slice_id_t target_ci = UCHAR_MAX;
    uint32_t target_rank = UINT_MAX;
    uint32_t max_disabled_dpus_on_line = 0;
    uint32_t max_enabled_dpus_on_rank = 0;

    // iterating through ranks and lines
    for (uint32_t rank = 0; rank < rank_count; ++rank) {
        uint32_t enabled_dpus_on_rank = get_nr_of_dpus_in_rank(ranks[rank]);

        if (enabled_dpus_on_rank == 1) {
            continue;
        }
        for (dpu_member_id_t line = 0; line < ranks[rank]->description->hw.topology.nr_of_dpus_per_control_interface; ++line) {
            uint32_t count_disabled_on_line = 0;
            dpu_slice_id_t ci_enabled = 0;
            bool one_enabled_on_line = false;
            for (dpu_slice_id_t ci = 0; ci < ranks[rank]->description->hw.topology.nr_of_control_interfaces; ++ci) {
                struct dpu_t *dpu = DPU_GET_UNSAFE(ranks[rank], ci, line);
                if (dpu->enabled) {
                    ci_enabled = ci;
                    one_enabled_on_line = true;
                } else {
                    count_disabled_on_line++;
                }
            }
            /* we select rank and line if :
                line has at least one active dpu
                line has most disabled dpus of all line/ranks
                rank has most enabled dpus of all line/ranks
            */
            if (one_enabled_on_line
                && (count_disabled_on_line > max_disabled_dpus_on_line
                    || (count_disabled_on_line == max_disabled_dpus_on_line
                        && enabled_dpus_on_rank >= max_enabled_dpus_on_rank))) {
                target_line = line;
                target_rank = rank;
                target_ci = ci_enabled;
                max_disabled_dpus_on_line = count_disabled_on_line;
                max_enabled_dpus_on_rank = enabled_dpus_on_rank;
            }
        }
    }

    assert(target_line != UCHAR_MAX);
    assert(target_ci != UCHAR_MAX);
    assert(target_rank != UINT_MAX);

    struct dpu_t *dpu = DPU_GET_UNSAFE(ranks[target_rank], target_ci, target_line);

    return dpu_disable_one_dpu(dpu);
}

// disables current_nr_of_dpus - nr_dpus dpus. Priorizes ranks with most DPU on it, and in each rank, tries to group disabled dpus
// to minimize number of disabled lines
static dpu_error_t
disable_unused_dpus(uint32_t current_nr_of_dpus, uint32_t nr_dpus, struct dpu_rank_t **current_ranks, uint32_t rank_count)
{
    assert(current_ranks != NULL);
    assert(rank_count > 0);
    assert(current_nr_of_dpus >= nr_dpus);

    uint32_t dpus_to_disable = current_nr_of_dpus - nr_dpus;

    for (uint32_t nr_disabled_dpus = 0; nr_disabled_dpus < dpus_to_disable; ++nr_disabled_dpus) {
        dpu_error_t status = DPU_OK;
        if ((status = disable_one_dpu(current_ranks, rank_count)) != DPU_OK) {
            return status;
        }
    }
    return DPU_OK;
}

__API_SYMBOL__ dpu_error_t
dpu_alloc(uint32_t nr_dpus, const char *profile, struct dpu_set_t *dpu_set)
{
    LOG_FN(DEBUG, "%d, \"%s\"", nr_dpus, profile);

    bool dispatch_on_all_ranks;

    if (nr_dpus == 0) {
        LOG_FN(WARNING, "cannot allocate 0 DPUs");
        return DPU_ERR_ALLOCATION;
    }

    {
        dpu_properties_t properties = dpu_properties_load_from_profile(profile);
        if (properties == DPU_PROPERTIES_INVALID) {
            return DPU_ERR_INVALID_PROFILE;
        }

        if (!fetch_boolean_property(properties, DPU_PROFILE_PROPERTY_DISPATCH_ON_ALL_RANKS, &dispatch_on_all_ranks, false)) {
            dpu_properties_delete(properties);
            return DPU_ERR_INVALID_PROFILE;
        }

        dpu_properties_delete(properties);
    }

    // dispatch on all ranks means we try to allocate as much ranks as possible to dispatch our DPUs
    dispatch_on_all_ranks = dispatch_on_all_ranks && (nr_dpus != DPU_ALLOCATE_ALL);

    uint32_t capacity = 0;
    uint32_t current_nr_of_dpus = 0;
    uint32_t current_nr_of_ranks = 0;
    struct dpu_rank_t **current_ranks = NULL;
    dpu_error_t status = DPU_OK;

    do {
        // allocating space for new rank
        if (current_nr_of_ranks == capacity) {
            capacity = 2 * capacity + 2;

            struct dpu_rank_t **current_ranks_tmp;
            if ((current_ranks_tmp = realloc(current_ranks, capacity * sizeof(*current_ranks))) == NULL) {
                status = DPU_ERR_SYSTEM;
                goto error_free_ranks;
            }
            current_ranks = current_ranks_tmp;
        }

        struct dpu_rank_t **next_rank = current_ranks + current_nr_of_ranks;
        // We try to allocate a new rank
        status = dpu_get_rank_of_type(profile, next_rank);

        // case : it failed but we simply allocate all
        if (status == DPU_ERR_ALLOCATION && current_nr_of_ranks != 0 && (nr_dpus == DPU_ALLOCATE_ALL || dispatch_on_all_ranks)) {
            // in case not enough dpus
            if (dispatch_on_all_ranks && current_nr_of_dpus < nr_dpus) {
                goto error_free_ranks;
            }
            // case : it failed but that's not normal
        } else if (status != DPU_OK) {
            goto error_free_ranks;
            // case : otherwise it passed
        } else {
            current_nr_of_ranks++;
            if (!(*next_rank)->description->configuration.disable_reset_on_alloc) {
                if ((status = dpu_reset_rank(*next_rank)) != DPU_OK) {
                    goto error_free_ranks;
                }
            }
            current_nr_of_dpus += get_nr_of_dpus_in_rank(*next_rank);
        }
        // we either reached sufficient dpus or failed to allocate
    } while ((current_nr_of_ranks < nr_dpus) && (dispatch_on_all_ranks || current_nr_of_dpus < nr_dpus)
        && (status != DPU_ERR_ALLOCATION));

    if (nr_dpus == DPU_ALLOCATE_ALL) {
        nr_dpus = current_nr_of_dpus;
    }

    if ((status = disable_unused_dpus(current_nr_of_dpus, nr_dpus, current_ranks, current_nr_of_ranks)) != DPU_OK) {
        goto error_free_ranks;
    }

    if ((status = init_dpu_set(current_ranks, current_nr_of_ranks, dpu_set, profile)) != DPU_OK) {
        goto error_free_ranks;
    }

    return DPU_OK;

error_free_ranks:
    for (unsigned int each_allocated_rank = 0; each_allocated_rank < current_nr_of_ranks; ++each_allocated_rank) {
        dpu_free_rank(current_ranks[each_allocated_rank]);
    }
    if (current_ranks != NULL) {
        free(current_ranks);
    }
    return status;
}

__API_SYMBOL__ dpu_error_t
dpu_alloc_ranks(uint32_t nr_ranks, const char *profile, struct dpu_set_t *dpu_set)
{
    LOG_FN(DEBUG, "%d, \"%s\"", nr_ranks, profile);

    dpu_error_t status = DPU_OK;
    struct dpu_rank_t **ranks;
    uint32_t each_rank;

    if (nr_ranks == DPU_ALLOCATE_ALL) {
        return dpu_alloc(DPU_ALLOCATE_ALL, profile, dpu_set);
    }

    if (nr_ranks == 0) {
        LOG_FN(WARNING, "cannot allocate 0 DPUs");
        status = DPU_ERR_ALLOCATION;
        goto end;
    }

    if ((ranks = calloc(nr_ranks, sizeof(*ranks))) == NULL) {
        status = DPU_ERR_SYSTEM;
        goto end;
    }

    for (each_rank = 0; each_rank < nr_ranks; ++each_rank) {
        if ((status = dpu_get_rank_of_type(profile, &ranks[each_rank])) != DPU_OK) {
            goto free_ranks;
        }

        if (!ranks[each_rank]->description->configuration.disable_reset_on_alloc) {
            if ((status = dpu_reset_rank(ranks[each_rank])) != DPU_OK) {
                goto free_ranks;
            }
        }
    }

    if ((status = init_dpu_set(ranks, nr_ranks, dpu_set, profile)) != DPU_OK) {
        goto free_ranks;
    }

    return DPU_OK;

free_ranks:
    for (unsigned int each_allocated_rank = 0; each_allocated_rank < each_rank; ++each_allocated_rank) {
        dpu_free_rank(ranks[each_allocated_rank]);
    }
    free(ranks);

end:
    return status;
}

__API_SYMBOL__ dpu_error_t
dpu_free(struct dpu_set_t dpu_set)
{
    LOG_FN(DEBUG, "");

    dpu_error_t status, ret;

    if ((status = sg_buffer_pool_free(dpu_set)) != DPU_OK) {
        return status;
    }

    if ((status = set_allocator_unregister(&dpu_set)) != DPU_OK) {
        return status;
    }

    dpu_thread_job_free(dpu_set.list.ranks, dpu_set.list.nr_ranks);
    // Allocated set are always a DPU_SET_RANKS
    for (uint32_t each_rank = 0; each_rank < dpu_set.list.nr_ranks; ++each_rank) {
        if ((ret = dpu_free_rank(dpu_set.list.ranks[each_rank])) != DPU_OK) {
            status = ret;
        }
    }

    free(dpu_set.list.ranks);

    return status;
}

__API_SYMBOL__ struct dpu_set_rank_iterator_t
dpu_set_rank_iterator_from(struct dpu_set_t *set)
{
    struct dpu_set_t first;
    bool has_next;

    switch (set->kind) {
        case DPU_SET_RANKS:
            has_next = set->list.nr_ranks != 0;
            first = *set;
            first.list.nr_ranks = 1;
            break;
        case DPU_SET_DPU:
            has_next = true;
            first = *set;
            break;
        default:
            has_next = false;
            first = *set;
            break;
    }

    struct dpu_set_rank_iterator_t iterator = { .set = set, .count = 0, .next_idx = 1, .has_next = has_next, .next = first };

    return iterator;
}

__API_SYMBOL__ void
dpu_set_rank_iterator_next(struct dpu_set_rank_iterator_t *iterator)
{
    iterator->count++;

    if (!iterator->has_next) {
        return;
    }

    switch (iterator->set->kind) {
        case DPU_SET_RANKS:
            iterator->has_next = iterator->next_idx < iterator->set->list.nr_ranks;

            if (iterator->has_next) {
                iterator->next.list.ranks = iterator->set->list.ranks + iterator->next_idx;
                iterator->next_idx++;
            }
            break;
        case DPU_SET_DPU:
            iterator->has_next = false;
            break;
        default:
            iterator->has_next = false;
            break;
    }
}

static void
advance_to_next_dpu_in_rank_list(struct dpu_set_dpu_iterator_t *iterator)
{
    struct dpu_rank_t *rank = *iterator->rank_iterator.next.list.ranks;
    uint32_t dpu_idx = iterator->next_idx;

    uint8_t nr_cis = rank->description->hw.topology.nr_of_control_interfaces;
    uint8_t nr_dpus_per_ci = rank->description->hw.topology.nr_of_dpus_per_control_interface;
    uint32_t nr_dpus = nr_cis * nr_dpus_per_ci;

    do {
        for (; dpu_idx < nr_dpus; ++dpu_idx) {
            struct dpu_t *dpu = rank->dpus + dpu_idx;

            if (dpu->enabled) {
                iterator->has_next = true;
                iterator->next_idx = dpu_idx + 1;
                iterator->next.dpu = dpu;
                return;
            }
        }

        dpu_set_rank_iterator_next(&iterator->rank_iterator);
        rank = *iterator->rank_iterator.next.list.ranks;
        dpu_idx = 0;
    } while (iterator->rank_iterator.has_next);

    iterator->has_next = false;
}

__API_SYMBOL__ struct dpu_set_dpu_iterator_t
dpu_set_dpu_iterator_from(struct dpu_set_t *set)
{
    struct dpu_set_dpu_iterator_t iterator;
    iterator.rank_iterator = dpu_set_rank_iterator_from(set);
    iterator.count = 0;

    if (!iterator.rank_iterator.has_next) {
        iterator.has_next = false;
    } else {
        switch (set->kind) {
            case DPU_SET_RANKS:
                iterator.next.kind = DPU_SET_DPU;
                iterator.next_idx = 0;
                advance_to_next_dpu_in_rank_list(&iterator);
                break;
            case DPU_SET_DPU:
                iterator.has_next = true;
                iterator.next = *set;
                break;
            default:
                iterator.has_next = false;
                break;
        }
    }

    return iterator;
}

__API_SYMBOL__ void
dpu_set_dpu_iterator_next(struct dpu_set_dpu_iterator_t *iterator)
{
    iterator->count++;

    if (!iterator->has_next) {
        return;
    }

    switch (iterator->rank_iterator.set->kind) {
        case DPU_SET_RANKS: {
            advance_to_next_dpu_in_rank_list(iterator);
            break;
        }
        case DPU_SET_DPU:
            iterator->has_next = false;
            break;
        default:
            iterator->has_next = false;
            break;
    }
}

__API_SYMBOL__ dpu_error_t
dpu_callback(struct dpu_set_t dpu_set,
    dpu_error_t (*callback)(struct dpu_set_t, uint32_t, void *),
    void *args,
    dpu_callback_flags_t flags)
{
    LOG_FN(DEBUG, "%p, %p, 0x%x", callback, args, flags);

    if (dpu_set.kind != DPU_SET_RANKS && dpu_set.kind != DPU_SET_DPU) {
        return DPU_ERR_INTERNAL;
    }

    dpu_error_t status = DPU_OK;
    bool is_sync = (flags & DPU_CALLBACK_ASYNC) == 0;
    bool is_nonblocking = (flags & DPU_CALLBACK_NONBLOCKING) != 0;
    bool is_single_call = (flags & DPU_CALLBACK_SINGLE_CALL) != 0;

    if (is_sync && is_nonblocking) {
        return DPU_ERR_NONBLOCKING_SYNC_CALLBACK;
    }

    uint32_t nr_ranks;
    struct dpu_rank_t **ranks;
    struct dpu_rank_t *rank;
    bool use_initial_dpu_set;
    switch (dpu_set.kind) {
        case DPU_SET_RANKS:
            nr_ranks = dpu_set.list.nr_ranks;
            ranks = dpu_set.list.ranks;
            use_initial_dpu_set = false;
            break;
        case DPU_SET_DPU:
            nr_ranks = 1;
            rank = dpu_get_rank(dpu_set.dpu);
            ranks = &rank;
            use_initial_dpu_set = true;
            break;
        default:
            return DPU_ERR_INTERNAL;
    }

    if (is_single_call && is_sync) {
        nr_ranks = 1;
        use_initial_dpu_set = true;
    }

    struct dpu_thread_job *job;
    struct dpu_thread_job_sync sync_job;
    uint32_t nr_jobs_per_rank;
    enum dpu_thread_job_type job_type = DPU_THREAD_JOB_CALLBACK;
    if (flags & DPU_CALLBACK_PARALLEL)
        job_type = DPU_THREAD_JOB_CALLBACK_PARALLEL;

    DPU_THREAD_JOB_GET_JOBS(ranks, nr_ranks, nr_jobs_per_rank, jobs, &sync_job, is_sync, status);

    if (is_single_call && (nr_ranks != 1)) {
        uint32_t master_idx = 0;

        // When the callback is non-blocking, we need to be aware that one rank will lose a job slot.
        // This may create a scenario where a rank has no slot left for a while, slowing it down.
        // To combat that behavior, we choose the rank by looking at the number of available slots.
        // This is a "good enough" implementation: we are not taking any lock, which means that we may not
        // have a coherent maximum. The goal here is mainly not to always take the same rank.
        if (is_nonblocking) {
            uint32_t max_nr_available_slots = ranks[master_idx]->api.available_jobs_length;
            for (uint32_t each_rank = 1; each_rank < nr_ranks; ++each_rank) {
                uint32_t nr_available_slots = ranks[each_rank]->api.available_jobs_length;
                if (max_nr_available_slots < nr_available_slots) {
                    max_nr_available_slots = nr_available_slots;
                    master_idx = each_rank;
                }
            }
        }

        struct dpu_thread_job *master_job;
        pthread_mutex_lock(&ranks[master_idx]->api.jobs_mutex);
        status = dpu_thread_job_get_job_unlocked(ranks[master_idx], &master_job);
        pthread_mutex_unlock(&ranks[master_idx]->api.jobs_mutex);

        if (status != DPU_OK) {
            dpu_thread_job_unget_jobs(ranks, nr_ranks, nr_jobs_per_rank, jobs);
            return status;
        }

        master_job->type = job_type;
        master_job->callback.master_job = NULL;
        master_job->callback.slot_owner = ranks[master_idx];
        master_job->callback.function = callback;
        master_job->callback.args = args;
        master_job->callback.rank_idx = -1;
        master_job->callback.dpu_set = dpu_set;
        master_job->callback.is_nonblocking = is_nonblocking;
        master_job->callback.is_sync = false;
        master_job->callback.is_single_call = true;
        dpu_thread_job_init_sync_job(&master_job->callback.sync, nr_ranks);

        for (uint32_t each_rank = 0; each_rank < nr_ranks; ++each_rank) {
            job = jobs[each_rank];
            job->type = job_type;
            job->callback.master_job = master_job;
        }
    } else {
        DPU_THREAD_JOB_SET_JOBS_PARALLEL(ranks, rank, nr_ranks, jobs, job, &sync_job, is_sync, flags & DPU_CALLBACK_PARALLEL, {
            job->type = job_type;
            job->callback.master_job = NULL;
            job->callback.function = callback;
            job->callback.args = args;
            if (use_initial_dpu_set) {
                job->callback.rank_idx = -1;
                job->callback.dpu_set = dpu_set;
            } else {
                job->callback.rank_idx = _each_rank;
                job->callback.dpu_set = dpu_set_from_rank(&ranks[_each_rank]);
            }
            job->callback.is_nonblocking = is_nonblocking;
            job->callback.is_sync = is_sync;
            job->callback.is_single_call = false;
            dpu_thread_job_init_sync_job(&job->callback.sync, 1);
        });
    }

    status = dpu_thread_job_do_jobs(ranks, nr_ranks, nr_jobs_per_rank, jobs, is_sync, &sync_job);

    return status;
}
