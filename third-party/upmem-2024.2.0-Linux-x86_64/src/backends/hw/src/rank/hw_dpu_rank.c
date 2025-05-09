/* Copyright 2020 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <dpu_chip_config.h>
#include <string.h>
#include <dpu_profile.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/user.h>
#include <dpu_description.h>
#include <dpu_types.h>
#include <dpu_mask.h>
#include <dpu_log_utils.h>
#include <dpu_vpd.h>
#include <dpu_internals.h>
#include <dpu_target_macros.h>

#include "dpu_attributes.h"
// TODO: will conflict with driver header
#include "dpu_rank.h"
#include "dpu_mask.h"

/* Header shared with driver */
#include "dpu_region_address_translation.h"
#include "dpu_region_constants.h"
#include "hw_dpu_sysfs.h"
#include "dpu_rank_ioctl.h"
#include "dpu_fpga_ila.h"

#include "dpu_module_compatibility.h"

#include "static_verbose.h"
#include <assert.h>
#include <string.h>

const char *
get_rank_path(dpu_description_t description);

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

extern struct dpu_region_address_translation power9_translate;
extern struct dpu_region_address_translation xeon_sp_translate;
extern struct dpu_region_address_translation fpga_aws_translate;

struct dpu_region_address_translation *backend_translate[] = {
#ifdef __x86_64__
    &xeon_sp_translate,
#else
    0,
#endif
    0, /* fpga_kc705 has no user backend */
    &fpga_aws_translate,
    0, /*fpga_ku250u2 has no user backend */
#ifdef __powerpc64__
    &power9_translate,
#else
    0,
#endif
    0, /* devicetree user backend not yet implemented */
};

static dpu_rank_status_e
hw_allocate(struct dpu_rank_t *rank, dpu_description_t description);
static dpu_rank_status_e
hw_free(struct dpu_rank_t *rank);
static dpu_rank_status_e
hw_commit_commands(struct dpu_rank_t *rank, dpu_rank_buffer_t buffer);
static dpu_rank_status_e
hw_update_commands(struct dpu_rank_t *rank, dpu_rank_buffer_t buffer);
static dpu_rank_status_e
hw_copy_to_rank(struct dpu_rank_t *rank, struct dpu_transfer_matrix *transfer_matrix);
static dpu_rank_status_e
hw_copy_from_rank(struct dpu_rank_t *rank, struct dpu_transfer_matrix *transfer_matrix);
static dpu_rank_status_e
hw_fill_description_from_profile(dpu_properties_t properties, dpu_description_t description);
static dpu_rank_status_e
hw_custom_operation(struct dpu_rank_t *rank,
                    dpu_slice_id_t slice_id,
                    dpu_member_id_t member_id,
                    dpu_custom_command_t command,
                    dpu_custom_command_args_t args);
static dpu_rank_status_e
hw_get_nr_dpu_ranks(uint32_t *nr_ranks);

__API_SYMBOL__ struct dpu_rank_handler hw_dpu_rank_handler = {
    .allocate = hw_allocate,
    .free = hw_free,
    .commit_commands = hw_commit_commands,
    .update_commands = hw_update_commands,
    .copy_to_rank = hw_copy_to_rank,
    .copy_from_rank = hw_copy_from_rank,
    .fill_description_from_profile = hw_fill_description_from_profile,
    .custom_operation = hw_custom_operation,
    .get_nr_dpu_ranks = hw_get_nr_dpu_ranks,
};

typedef struct _hw_dpu_rank_context_t
{
    /* Hybrid mode: Address of control interfaces when memory mapped
     * Perf mode:   Base region address, mappings deal with offset to target control interfaces
     * Safe mode:   Buffer handed to the driver
     */
    uint64_t *control_interfaces;
} *hw_dpu_rank_context_t;

__API_SYMBOL__ uint64_t*
get_rank_ptr(struct dpu_rank_t *rank)
{
    hw_dpu_rank_context_t rank_ctx = (hw_dpu_rank_context_t)(rank->_internals);
    uint64_t *ptr = rank_ctx->control_interfaces;
    return  ptr;
}

typedef struct _fpga_allocation_parameters_t
{
    bool activate_ila;
    bool activate_filtering_ila;
    bool activate_mram_bypass;
    bool activate_mram_refresh_emulation;
    unsigned int mram_refresh_emulation_period;
    char *report_path;
    bool cycle_accurate;
} fpga_allocation_parameters_t;

typedef struct _hw_dpu_rank_allocation_parameters_t
{
    struct dpu_rank_fs rank_fs;
    struct dpu_region_address_translation translate;
    uint64_t region_size;
    uint8_t mode, dpu_chip_id, backend_id;
    uint8_t channel_id;
    uint8_t *ptr_region;
    bool bypass_module_compatibility;
    /* Backends specific */
    fpga_allocation_parameters_t fpga;
} *hw_dpu_rank_allocation_parameters_t;

static inline hw_dpu_rank_context_t
_this(struct dpu_rank_t *rank)
{
    return (hw_dpu_rank_context_t)(rank->_internals);
}

static inline hw_dpu_rank_allocation_parameters_t
_this_params(dpu_description_t description)
{
    return (hw_dpu_rank_allocation_parameters_t)(description->_internals.data);
}

static inline bool
fill_description_with_default_values_for(dpu_chip_id_e chip_id, dpu_description_t description)
{
    switch (chip_id)
    {
    default:
        return false;
    case vD_asic1:
    case vD_asic4:
    case vD_asic8:
    case vD_fpga1:
    case vD_fpga4:
    case vD_fpga8:
    case vD_asic1_v1_4:
    case vD_asic4_v1_4:
    case vD_asic8_v1_4:
    case vD_fpga1_v1_4:
    case vD_fpga4_v1_4:
    case vD_fpga8_v1_4:
    case vD:
        break;
    }

    dpu_description_t default_description;

    if ((default_description = default_description_for_chip(chip_id)) == NULL)
    {
        return false;
    }

    memcpy(description, default_description, sizeof(*description));

    return true;
}

static inline bool
is_chip_emulated(dpu_chip_id_e chip_id)
{
    switch (chip_id)
    {
    default:
        return false;
    case vD_fpga1:
    case vD_fpga4:
    case vD_fpga8:
    case vD_fpga1_v1_4:
    case vD_fpga4_v1_4:
    case vD_fpga8_v1_4:
        return true;
    }
}

static bool
fill_address_translation_backend(hw_dpu_rank_allocation_parameters_t params, dpu_description_t description)
{
    params->backend_id = dpu_sysfs_get_backend_id(&params->rank_fs);
    if (params->backend_id >= DPU_BACKEND_NUMBER)
        return false;

    if (!backend_translate[params->backend_id])
    {
        LOG_FN(WARNING, "No perf mode is available for the backend %d", params->backend_id);
        return false;
    }
    struct dpu_transfer_thread_configuration xfer_thread_conf = params->translate.xfer_thread_conf;
    memcpy(&params->translate, backend_translate[params->backend_id], sizeof(struct dpu_region_address_translation));
    params->translate.xfer_thread_conf = xfer_thread_conf;
    params->translate.desc = &description->hw;
    params->translate.one_read = false;

    return true;
}

__attribute__((used)) static int
hw_set_debug_mode(struct dpu_rank_t *rank, uint8_t mode)
{
    hw_dpu_rank_allocation_parameters_t params = _this_params(rank->description);
    int ret;

    ret = ioctl(params->rank_fs.fd_rank, DPU_RANK_IOCTL_DEBUG_MODE, mode);
    if (ret)
        LOG_RANK(WARNING, rank, "Failed to change debug mode (%s)", strerror(errno));

    return ret;
}

static void
add_faulty_memory_address(struct dpu_memory_repair_t *repair_info, uint16_t address, uint64_t faulty_bits)
{
    uint32_t previous_nr_of_corrupted_addr = repair_info->nr_of_corrupted_addresses++;
    uint32_t index = previous_nr_of_corrupted_addr;
    uint32_t current_max_index = (previous_nr_of_corrupted_addr > NB_MAX_REPAIR_ADDR) ? NB_MAX_REPAIR_ADDR : previous_nr_of_corrupted_addr;

    for (uint32_t each_known_corrupted_addr_idx = 0; each_known_corrupted_addr_idx < current_max_index;
         ++each_known_corrupted_addr_idx)
    {
        if (repair_info->corrupted_addresses[each_known_corrupted_addr_idx].address == address)
        {
            index = each_known_corrupted_addr_idx;
            repair_info->nr_of_corrupted_addresses--;
            break;
        }
    }
    if (index < NB_MAX_REPAIR_ADDR)
    {
        repair_info->corrupted_addresses[index].address = address;
        repair_info->corrupted_addresses[index].faulty_bits |= faulty_bits;
    }
}

static dpu_error_t
fill_sram_repairs_and_update_enabled_dpus(struct dpu_rank_t *rank)
{
    hw_dpu_rank_allocation_parameters_t params = _this_params(rank->description);
    const char *rank_path = get_rank_path(rank->description);
    char vpd_path[512];
    uint8_t rank_index;
    struct dpu_vpd vpd;

    rank_index = dpu_sysfs_get_rank_index(&params->rank_fs);
    if (rank_index == DPU_INVALID_RANK_INDEX)
    {
        LOG_RANK(DEBUG, rank, "unable to get rank index");
        return DPU_ERR_SYSTEM;
    }

    if (dpu_vpd_get_vpd_path(rank_path, (char *)vpd_path) != DPU_VPD_OK)
    {
        LOG_RANK(DEBUG, rank, "unable to get vpd path");
        return DPU_ERR_SYSTEM;
    }

    if (dpu_vpd_init(vpd_path, &vpd) != DPU_VPD_OK)
    {
        LOG_RANK(DEBUG, rank, "unable to open sysfs VPD file");
        return DPU_ERR_VPD_INVALID_FILE;
    }

    if (vpd.vpd_header.repair_count == VPD_UNDEFINED_REPAIR_COUNT)
        return DPU_ERR_VPD_NO_REPAIR;

    uint8_t nr_cis = rank->description->hw.topology.nr_of_control_interfaces;
    uint8_t nr_dpus = rank->description->hw.topology.nr_of_dpus_per_control_interface;

    uint64_t disabled_mask = 0;

    bool repair_requested = (rank->description->configuration.do_iram_repair) || (rank->description->configuration.do_wram_repair);

    if (repair_requested)
    {
        int i;
        uint16_t repair_cnt = 0;

        for (i = 0; i < vpd.vpd_header.repair_count; ++i)
        {
            repair_cnt++;

            struct dpu_vpd_repair_entry *entry = &vpd.repair_entries[i];

            if (entry->rank != rank_index)
                continue;

            struct dpu_t *dpu = DPU_GET_UNSAFE(rank, entry->ci, entry->dpu);

            struct dpu_memory_repair_t *repair_info;

            if (entry->iram_wram == DPU_VPD_REPAIR_IRAM)
            {
                repair_info = &dpu->repair.iram_repair;
            }
            else
            {
                repair_info = &dpu->repair.wram_repair[entry->bank];
            }

            add_faulty_memory_address(repair_info, entry->address, entry->bits);

            /* Temporary HACK: disable DPUs which have SRAM defects */
            disabled_mask |= (1UL << ((entry->ci * nr_dpus) + entry->dpu));
        }

        if (repair_cnt != vpd.vpd_header.repair_count)
        {
            LOG_RANK(WARNING, rank, "malformed VPD file");
            return DPU_ERR_VPD_INVALID_FILE;
        }
    }

    disabled_mask |= vpd.vpd_header.ranks[rank_index].dpu_disabled;

    for (uint8_t each_ci = 0; each_ci < nr_cis; ++each_ci)
    {
        dpu_selected_mask_t disabled_mask_for_ci = (dpu_selected_mask_t)((disabled_mask >> (nr_dpus * each_ci)) & 0xFFl);

        if (disabled_mask_for_ci)
            LOG_CI(VERBOSE, rank, each_ci, "Disabled mask: %x\n", disabled_mask_for_ci);

        rank->runtime.control_interface.slice_info[each_ci].enabled_dpus &= dpu_mask_difference(dpu_mask_all(nr_dpus), disabled_mask_for_ci);
        rank->runtime.control_interface.slice_info[each_ci].all_dpus_are_enabled = dpu_mask_intersection(rank->runtime.control_interface.slice_info[each_ci].enabled_dpus, dpu_mask_all(nr_dpus)) == dpu_mask_all(nr_dpus);
    }

    return DPU_OK;
}

/* Function used in dpu-diag */
__API_SYMBOL__ bool
is_kernel_module_compatible(void)
{
    /* 1/ Get module version */
    unsigned int major, minor;
    int ret = dpu_sysfs_get_kernel_module_version(&major, &minor);
    if (ret)
    {
        LOG_FN(WARNING, "Failed to get dpu kernel module version");
        return false;
    }

    /* 2/ Check compatibility */
    /* Do not use DPU_MODULE_MIN_MINOR directly in the comparison as the compiler
     * complains if DPU_MODULE_MIN_MINOR = 0 (comparison is always false)
     */
    unsigned int min_minor = DPU_MODULE_MIN_MINOR;
    if ((major != DPU_MODULE_EXPECTED_MAJOR) || (minor < min_minor))
    {
        LOG_FN(WARNING,
               "SDK is not compatible with dpu kernel module (expected at least '%u.%u', got '%u.%u')",
               DPU_MODULE_EXPECTED_MAJOR,
               DPU_MODULE_MIN_MINOR,
               major,
               minor);
        return false;
    }

    return true;
}

/* In perf script that measures memory bandwidth, we need for per-rank
 * statistics to get the equivalence rank pointer <=> rank path: use
 * this function for perf to probe and get the rank path from the rank
 * pointer.
 */
__PERF_PROFILING_SYMBOL__ __API_SYMBOL__ void
log_rank_path(struct dpu_rank_t *rank, char *path)
{
    LOG_RANK(DEBUG, rank, "rank path is: %s", path);
}

/* Function used in dpu-diag */
__API_SYMBOL__ const char *
get_rank_path(dpu_description_t description)
{
    hw_dpu_rank_allocation_parameters_t params = _this_params(description);
    return params->rank_fs.rank_path;
}

static dpu_rank_status_e
get_byte_order(struct dpu_rank_t *rank, hw_dpu_rank_allocation_parameters_t params, uint8_t nr_cis)
{
#define BYTE_ORDER_STR_LEN strlen("0xFFFFFFFFFFFFFFFF ")
    const char *sysfs_byte_order = dpu_sysfs_get_byte_order(&params->rank_fs);
    char byte_order[BYTE_ORDER_STR_LEN * nr_cis + 1];
    uint8_t each_ci;

    strncpy(byte_order, sysfs_byte_order, BYTE_ORDER_STR_LEN * nr_cis);
    byte_order[BYTE_ORDER_STR_LEN * nr_cis] = 0;

    for (each_ci = 0; each_ci < nr_cis; ++each_ci)
    {
        char *next_ptr = strtok(!each_ci ? byte_order : NULL, " ");
        if (!next_ptr)
            return DPU_RANK_SYSTEM_ERROR;

        sscanf(next_ptr, "%lx", &rank->runtime.control_interface.slice_info[each_ci].byte_order);
    }

    return DPU_RANK_SUCCESS;
}

static dpu_rank_status_e
hw_allocate(struct dpu_rank_t *rank, dpu_description_t description)
{
    dpu_rank_status_e status;
    hw_dpu_rank_allocation_parameters_t params = _this_params(description);
    hw_dpu_rank_context_t rank_context;
    int ret;
    uint8_t nr_cis;

    /* 1/ Make sure SDK is compatible with the kernel module */
    static bool compatibility_checked = false;
    if ((params->bypass_module_compatibility == false) && (compatibility_checked == false))
    {
        if (is_kernel_module_compatible() == false)
        {
            status = DPU_RANK_SYSTEM_ERROR;
            goto end;
        }
        compatibility_checked = true;
    }

    /* 2/ Find an available rank whose mode is compatible with the one asked
     * by the user.
     * TODO: Maybe user wants to have a specific dpu_chip_id passed as argument,
     * so we could enforce the allocation for this specific id.
     */
    ret = dpu_sysfs_get_available_rank(params->rank_fs.rank_path, &params->rank_fs);
    if (ret)
    {
        LOG_FN(INFO,
               "Failed to find available rank with mode %u%s",
               params->mode,
               ret == -EACCES ? ", you don't have permissions for existing devices" : "");
        status = DPU_RANK_SYSTEM_ERROR;
        goto end;
    }

    rank->rank_id = (rank->rank_id & ~DPU_TARGET_MASK) | dpu_sysfs_get_rank_id(&params->rank_fs);
    rank->numa_node = dpu_sysfs_get_numa_node(&params->rank_fs);
    params->channel_id = dpu_sysfs_get_channel_id(&params->rank_fs);

    /* 3/ dpu_rank_handler initialization */
    if ((rank_context = malloc(sizeof(*rank_context))) == NULL)
    {
        status = DPU_RANK_SYSTEM_ERROR;
        goto free_physical_rank;
    }

    rank->_internals = rank_context;

    params->dpu_chip_id = dpu_sysfs_get_dpu_chip_id(&params->rank_fs);

    // retrieve ci_mask from sysfs
    description->hw.topology.ci_mask = dpu_sysfs_get_ci_mask(&params->rank_fs);

    if (params->dpu_chip_id != description->hw.signature.chip_id)
    {
        LOG_RANK(
            WARNING, rank, "Unexpected chip id %u (description is %u)", params->dpu_chip_id, description->hw.signature.chip_id);
        status = DPU_RANK_SYSTEM_ERROR;
        goto free_rank_context;
    }

    rank->description = description;

    // TODO: When driver safe mode is fully implemented, this must be set at false in this case.
    rank->description->configuration.api_must_switch_mram_mux = true;
    rank->description->configuration.init_mram_mux = true;

    nr_cis = description->hw.topology.nr_of_control_interfaces;

    /* Get byte order values from the driver */
    status = get_byte_order(rank, params, nr_cis);
    if (status != DPU_RANK_SUCCESS)
        goto free_rank_context;

    if (params->mode == DPU_REGION_MODE_SAFE)
    {
        rank_context->control_interfaces = malloc(nr_cis * sizeof(uint64_t));
        if (!rank_context->control_interfaces)
        {
            LOG_RANK(WARNING, rank, "Failed to allocate memory for control interfaces %u", params->dpu_chip_id);
            status = DPU_RANK_SYSTEM_ERROR;
            goto free_rank_context;
        }
    }
    else if (params->mode == DPU_REGION_MODE_PERF || params->mode == DPU_REGION_MODE_HYBRID)
    {
        /* 4/ Initialize CPU backend for this rank */
        ret = fill_address_translation_backend(params, description);
        if (!ret)
        {
            LOG_RANK(WARNING, rank, "Failed to retrieve backend");
            status = DPU_RANK_SYSTEM_ERROR;
            goto free_rank_context;
        }

        if (params->translate.init_rank)
        {
            ret = params->translate.init_rank(&params->translate, params->channel_id);
            if (ret < 0)
            {
                LOG_RANK(WARNING, rank, "Failed to init rank: %s", strerror(errno));
                status = DPU_RANK_SYSTEM_ERROR;
                goto free_rank_context;
            }
        }

        if (params->mode == DPU_REGION_MODE_HYBRID && (params->translate.capabilities & CAP_HYBRID_CONTROL_INTERFACE) == 0)
        {
            rank_context->control_interfaces = malloc(nr_cis * sizeof(uint64_t));
            if (!rank_context->control_interfaces)
            {
                LOG_RANK(WARNING, rank, "Failed to allocate memory for control interfaces %u", params->dpu_chip_id);
                status = DPU_RANK_SYSTEM_ERROR;
                goto free_rank;
            }
        }
        else if (params->mode == DPU_REGION_MODE_HYBRID && (params->translate.capabilities & CAP_HYBRID_CONTROL_INTERFACE))
        {
            rank_context->control_interfaces = mmap(NULL, params->translate.hybrid_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, params->rank_fs.fd_rank, 0);
            if (rank_context->control_interfaces == MAP_FAILED)
            {
                LOG_RANK(WARNING, rank, "Failed to mmap control interfaces %u", params->dpu_chip_id);
                status = DPU_RANK_SYSTEM_ERROR;
                goto free_rank;
            }
        }

        if (params->mode == DPU_REGION_MODE_PERF)
        {
            /* 5/ Mmap the whole physical region */
            params->region_size = dpu_sysfs_get_region_size(&params->rank_fs);

            /* mmap does not guarantee (at all) that the address will be aligned on hugepage size (1GB) but the driver does. */
            params->ptr_region = mmap(0, params->region_size, PROT_READ | PROT_WRITE, MAP_SHARED, params->rank_fs.fd_dax, 0);

            if (params->ptr_region == MAP_FAILED)
            {
                LOG_RANK(WARNING, rank, "Failed to mmap dax region: %s", strerror(errno));
                status = DPU_RANK_SYSTEM_ERROR;
                goto free_ci;
            }

            rank_context->control_interfaces = (uint64_t *)params->ptr_region;
        }
    }

    /* 6/ Inform DPUs about their SRAM defects, and update CIs runtime configuration */
    /* Do not check SRAM defects in case of FPGA */
    if (is_chip_emulated(params->dpu_chip_id) == false)
    {
        if (!description->configuration.ignore_vpd)
        {
            dpu_error_t repair_status = fill_sram_repairs_and_update_enabled_dpus(rank);
            if (repair_status != DPU_OK)
            {
                if (repair_status == DPU_ERR_VPD_INVALID_FILE)
                {
                    LOG_RANK(WARNING,
                             rank,
                             "VPD can't be read, cannot allocate the rank.\n"
                             "VPD must be created using the following command:\n"
                             "dpu-diag --sram-gen-vpd --force");
                }
                else if (repair_status == DPU_ERR_VPD_NO_REPAIR)
                {
                    LOG_RANK(WARNING,
                             rank,
                             "VPD does not contain repairs, it must be created\n"
                             "using the following command:\n"
                             "dpu-diag --sram-gen-vpd");
                }
                status = DPU_RANK_SYSTEM_ERROR;
                goto free_ptr_region;
            }
        }
    }

    log_rank_path(rank, params->rank_fs.rank_path);

    return DPU_RANK_SUCCESS;

free_ptr_region:
    if (params->mode == DPU_REGION_MODE_PERF || params->mode == DPU_REGION_MODE_HYBRID)
        if (params->mode == DPU_REGION_MODE_PERF)
            munmap(params->ptr_region, params->region_size);
free_ci:
    if (params->mode == DPU_REGION_MODE_SAFE || (params->mode == DPU_REGION_MODE_HYBRID && (params->translate.capabilities & CAP_HYBRID_CONTROL_INTERFACE) == 0))
        free(rank_context->control_interfaces);
    else if (params->mode == DPU_REGION_MODE_HYBRID && (params->translate.capabilities & CAP_HYBRID_CONTROL_INTERFACE))
        munmap(rank_context->control_interfaces, params->translate.hybrid_mmap_size);
free_rank:
    if (params->mode == DPU_REGION_MODE_PERF || params->mode == DPU_REGION_MODE_HYBRID)
        if (params->translate.destroy_rank)
            params->translate.destroy_rank(&params->translate, params->channel_id);
free_rank_context:
    free(rank_context);
free_physical_rank:
    dpu_sysfs_free_rank(&params->rank_fs);
end:
    return status;
}

static dpu_rank_status_e
hw_free(struct dpu_rank_t *rank)
{
    hw_dpu_rank_context_t rank_context = _this(rank);
    hw_dpu_rank_allocation_parameters_t params = _this_params(rank->description);

    if (params->mode == DPU_REGION_MODE_PERF)
    {
        munmap(params->ptr_region, params->region_size);
        if (params->translate.destroy_rank)
            params->translate.destroy_rank(&params->translate, params->channel_id);
    }
    else if (params->mode == DPU_REGION_MODE_HYBRID && (params->translate.capabilities & CAP_HYBRID_CONTROL_INTERFACE))
    {
        munmap(rank_context->control_interfaces, params->translate.hybrid_mmap_size);
    }
    else
        free(rank_context->control_interfaces);

    dpu_sysfs_free_rank(&params->rank_fs);

    free(rank_context);

    return DPU_RANK_SUCCESS;
}

static dpu_rank_status_e
hw_commit_commands(struct dpu_rank_t *rank, dpu_rank_buffer_t buffer)
{
    hw_dpu_rank_context_t rank_context = _this(rank);
    hw_dpu_rank_allocation_parameters_t params = _this_params(rank->description);
    dpu_rank_buffer_t ptr_buffer = buffer;
    int ret;

    switch (params->mode)
    {
    case DPU_REGION_MODE_PERF:
        params->translate.write_to_cis(&params->translate,
                                       rank_context->control_interfaces,
                                       params->channel_id,
                                       ptr_buffer,
                                       rank->description->hw.topology.nr_of_control_interfaces * sizeof(uint64_t));
        break;
    case DPU_REGION_MODE_HYBRID:
        if (params->translate.capabilities & CAP_HYBRID_CONTROL_INTERFACE)
        {
            params->translate.write_to_cis(&params->translate,
                                           rank_context->control_interfaces,
                                           params->channel_id,
                                           ptr_buffer,
                                           rank->description->hw.topology.nr_of_control_interfaces * sizeof(uint64_t));
            break;
        }
        /* fall through */
    case DPU_REGION_MODE_SAFE:
        ret = ioctl(params->rank_fs.fd_rank, DPU_RANK_IOCTL_COMMIT_COMMANDS, ptr_buffer);
        if (ret)
        {
            LOG_RANK(WARNING, rank, "%s", strerror(errno));
            return DPU_RANK_SYSTEM_ERROR;
        }
        break;
    default:
        return DPU_RANK_SYSTEM_ERROR;
    }

    return DPU_RANK_SUCCESS;
}

static dpu_rank_status_e
hw_update_commands(struct dpu_rank_t *rank, dpu_rank_buffer_t buffer)
{
    hw_dpu_rank_context_t rank_context = _this(rank);
    hw_dpu_rank_allocation_parameters_t params = _this_params(rank->description);
    dpu_rank_buffer_t ptr_buffer = buffer;
    int ret;

    switch (params->mode)
    {
    case DPU_REGION_MODE_PERF:
        params->translate.read_from_cis(&params->translate,
                                        rank_context->control_interfaces,
                                        params->channel_id,
                                        ptr_buffer,
                                        rank->description->hw.topology.nr_of_control_interfaces * sizeof(uint64_t));
        break;
    case DPU_REGION_MODE_HYBRID:
        if (params->translate.capabilities & CAP_HYBRID_CONTROL_INTERFACE)
        {
            params->translate.read_from_cis(&params->translate,
                                            rank_context->control_interfaces,
                                            params->channel_id,
                                            ptr_buffer,
                                            rank->description->hw.topology.nr_of_control_interfaces * sizeof(uint64_t));
            break;
        }
        /* fall through */
    case DPU_REGION_MODE_SAFE:
        ret = ioctl(params->rank_fs.fd_rank, DPU_RANK_IOCTL_UPDATE_COMMANDS, ptr_buffer);
        if (ret)
        {
            LOG_RANK(WARNING, rank, "%s", strerror(errno));
            return DPU_RANK_SYSTEM_ERROR;
        }

        break;
    default:
        return DPU_RANK_SYSTEM_ERROR;
    }

    return DPU_RANK_SUCCESS;
}

static inline bool
is_transfer_matrix_supported_by_region_mode(struct dpu_transfer_matrix *transfer_matrix, uint8_t region_mode)
{
    if (transfer_matrix->type == DPU_SG_XFER_MATRIX)
    {
        switch (region_mode)
        {
        case DPU_REGION_MODE_PERF:
            break;
        case DPU_REGION_MODE_HYBRID:
        case DPU_REGION_MODE_SAFE:
            return false;
        default:
            assert(false && "invalid region mode");
        }
    }
    return true;
}

static dpu_rank_status_e
hw_copy_to_rank(struct dpu_rank_t *rank, struct dpu_transfer_matrix *transfer_matrix)
{
    hw_dpu_rank_allocation_parameters_t params = _this_params(rank->description);
    struct dpu_transfer_matrix *ptr_transfer_matrix = transfer_matrix;
    int ret;

    /* check if transfer matrix type is supported for this region mode */
    if (!is_transfer_matrix_supported_by_region_mode(transfer_matrix, params->mode))
    {
        LOG_RANK(WARNING, rank, "Unsupported transfer_matrix type %u for regionMode=%u", transfer_matrix->type, params->mode);
        return DPU_RANK_SYSTEM_ERROR;
    }

    switch (params->mode)
    {
    case DPU_REGION_MODE_PERF:
        params->translate.write_to_rank(&params->translate, params->ptr_region, params->channel_id, ptr_transfer_matrix);

        break;
    case DPU_REGION_MODE_HYBRID:
        if ((params->translate.capabilities & CAP_HYBRID_CONTROL_INTERFACE) == 0)
        {
            params->translate.write_to_rank(&params->translate, params->ptr_region, params->channel_id, ptr_transfer_matrix);

            break;
        }
        /* fall through */
    case DPU_REGION_MODE_SAFE:
        ret = ioctl(params->rank_fs.fd_rank, DPU_RANK_IOCTL_WRITE_TO_RANK, ptr_transfer_matrix);
        if (ret)
        {
            LOG_RANK(WARNING, rank, "%s", strerror(errno));
            return DPU_RANK_SYSTEM_ERROR;
        }

        break;
    default:
        return DPU_RANK_SYSTEM_ERROR;
    }

    return DPU_RANK_SUCCESS;
}

static dpu_rank_status_e
hw_copy_from_rank(struct dpu_rank_t *rank, struct dpu_transfer_matrix *transfer_matrix)
{
    hw_dpu_rank_allocation_parameters_t params = _this_params(rank->description);
    struct dpu_transfer_matrix *ptr_transfer_matrix = transfer_matrix;
    int ret;

    /* check if transfer matrix type is supported for this region mode */
    if (!is_transfer_matrix_supported_by_region_mode(transfer_matrix, params->mode))
    {
        LOG_RANK(WARNING, rank, "Unsupported transfer_matrix type %u for regionMode=%u", transfer_matrix->type, params->mode);
        return DPU_RANK_SYSTEM_ERROR;
    }

    switch (params->mode)
    {
    case DPU_REGION_MODE_PERF:
        params->translate.read_from_rank(&params->translate, params->ptr_region, params->channel_id, ptr_transfer_matrix);

        break;
    case DPU_REGION_MODE_HYBRID:
        if ((params->translate.capabilities & CAP_HYBRID_CONTROL_INTERFACE) == 0)
        {
            params->translate.read_from_rank(&params->translate, params->ptr_region, params->channel_id, ptr_transfer_matrix);

            break;
        }
        /* fall through */
    case DPU_REGION_MODE_SAFE:
        ret = ioctl(params->rank_fs.fd_rank, DPU_RANK_IOCTL_READ_FROM_RANK, ptr_transfer_matrix);
        if (ret)
        {
            LOG_RANK(WARNING, rank, "%s", strerror(errno));
            return DPU_RANK_SYSTEM_ERROR;
        }

        break;
    default:
        return DPU_RANK_SYSTEM_ERROR;
    }

    return DPU_RANK_SUCCESS;
}

#define validate(p)                                 \
    do                                              \
    {                                               \
        if (!(p))                                   \
            return DPU_RANK_INVALID_PROPERTY_ERROR; \
    } while (0)

static void
free_hw_parameters(void *description)
{
    hw_dpu_rank_allocation_parameters_t params = description;

    if (params->fpga.report_path)
        free(params->fpga.report_path);

    free(params);
}

static dpu_rank_status_e
hw_fill_description_from_profile(dpu_properties_t properties, dpu_description_t description)
{
    hw_dpu_rank_allocation_parameters_t parameters;
    uint32_t clock_division, refresh_emulation_period, fck_frequency;
    int ret;
    char *report_path, *rank_path, *region_mode_input = NULL;
    bool activate_ila = false, activate_filtering_ila = false, activate_mram_bypass = false, cycle_accurate = false;
    bool mram_access_by_dpu_only;
    uint8_t chip_id, capabilities_mode;
    bool bypass_module_compatibility;

    parameters = malloc(sizeof(*parameters));
    if (!parameters)
    {
        return DPU_RANK_SYSTEM_ERROR;
    }

    ret = dpu_sysfs_get_hardware_chip_id(&chip_id);
    if (ret == -1)
    {
        free(parameters);
        return DPU_RANK_SYSTEM_ERROR;
    }

    validate(fill_description_with_default_values_for((dpu_chip_id_e)chip_id, description));

    ret = dpu_sysfs_get_hardware_description(description, &capabilities_mode);
    if (ret == -1)
    {
        free(parameters);
        return DPU_RANK_SYSTEM_ERROR;
    }

    validate(fetch_string_property(properties, DPU_PROFILE_PROPERTY_REGION_MODE, &region_mode_input, NULL));
    validate(fetch_string_property(properties, DPU_PROFILE_PROPERTY_RANK_PATH, &rank_path, NULL));
    validate(fetch_integer_property(
        properties, DPU_PROFILE_PROPERTY_CLOCK_DIVISION, &clock_division, description->hw.timings.clock_division));
    validate((clock_division & ~0xFF) == 0);
    validate(fetch_integer_property(
        properties, DPU_PROFILE_PROPERTY_FCK_FREQUENCY, &fck_frequency, description->hw.timings.fck_frequency_in_mhz));
    validate(fetch_boolean_property(
        properties, DPU_PROFILE_PROPERTY_TRY_REPAIR_IRAM, &(description->configuration.do_iram_repair), true));
    validate(fetch_boolean_property(
        properties, DPU_PROFILE_PROPERTY_TRY_REPAIR_WRAM, &(description->configuration.do_wram_repair), true));
    validate(
        fetch_boolean_property(properties, DPU_PROFILE_PROPERTY_IGNORE_VPD, &(description->configuration.ignore_vpd), false));

    /* FPGA specific */
    validate(fetch_string_property(properties, DPU_PROFILE_PROPERTY_REPORT_FILE_NAME, &report_path, "/tmp/fpga_ila_report.csv"));
    validate(fetch_boolean_property(properties, DPU_PROFILE_PROPERTY_ANALYZER_ENABLED, &activate_ila, activate_ila));
    validate(fetch_boolean_property(
        properties, DPU_PROFILE_PROPERTY_ANALYZER_FILTERING_ENABLED, &activate_filtering_ila, activate_filtering_ila));
    validate(fetch_boolean_property(
        properties, DPU_PROFILE_PROPERTY_MRAM_BYPASS_ENABLED, &activate_mram_bypass, activate_mram_bypass));
    validate(fetch_integer_property(properties, DPU_PROFILE_PROPERTY_MRAM_EMULATE_REFRESH, &refresh_emulation_period, 0));
    validate(fetch_boolean_property(properties, DPU_PROFILE_PROPERTY_CYCLE_ACCURATE, &cycle_accurate, cycle_accurate));

    /* XEON SP specific*/
    {
        struct dpu_transfer_thread_configuration xfer_thread_conf;
        validate(fetch_integer_property(properties,
                                        DPU_PROFILE_PROPERTY_NR_THREAD_PER_POOL,
                                        &xfer_thread_conf.nb_thread_per_pool,
                                        DPU_XFER_THREAD_CONF_DEFAULT));
        validate(fetch_integer_property(properties,
                                        DPU_PROFILE_PROPERTY_POOL_THRESHOLD_1_THREAD,
                                        &xfer_thread_conf.threshold_1_thread,
                                        DPU_XFER_THREAD_CONF_DEFAULT));
        validate(fetch_integer_property(properties,
                                        DPU_PROFILE_PROPERTY_POOL_THRESHOLD_2_THREADS,
                                        &xfer_thread_conf.threshold_2_threads,
                                        DPU_XFER_THREAD_CONF_DEFAULT));
        validate(fetch_integer_property(properties,
                                        DPU_PROFILE_PROPERTY_POOL_THRESHOLD_4_THREADS,
                                        &xfer_thread_conf.threshold_4_threads,
                                        DPU_XFER_THREAD_CONF_DEFAULT));
        parameters->translate.xfer_thread_conf = xfer_thread_conf;
    }

    validate(fetch_boolean_property(properties, DPU_PROFILE_PROPERTY_MRAM_ACCESS_BY_DPU_ONLY, &mram_access_by_dpu_only, false));

    validate(fetch_boolean_property(properties, DPU_PROFILE_PROPERTY_IGNORE_VERSION, &bypass_module_compatibility, false));

    memset(&parameters->rank_fs, 0, sizeof(struct dpu_rank_fs));
    if (rank_path)
    {
        strcpy(parameters->rank_fs.rank_path, rank_path);
        free(rank_path);
    }

    if (region_mode_input)
    {
        if (!strcmp(region_mode_input, "safe"))
            parameters->mode = (uint8_t)DPU_REGION_MODE_SAFE;
        else if (!strcmp(region_mode_input, "perf"))
            parameters->mode = (uint8_t)DPU_REGION_MODE_PERF;
        else if (!strcmp(region_mode_input, "hybrid"))
            parameters->mode = (uint8_t)DPU_REGION_MODE_HYBRID;
        else
        {
            LOG_FN(WARNING,
                   "Provided region mode (%s) is unknown, switching to default (%s)",
                   region_mode_input,
                   (capabilities_mode & CAP_PERF) ? "perf" : "safe");
            parameters->mode = (capabilities_mode & CAP_PERF) ? (uint8_t)DPU_REGION_MODE_PERF : (uint8_t)DPU_REGION_MODE_SAFE;
        }

        free(region_mode_input);
    }
    else
    {
        LOG_FN(DEBUG, "Region mode not specified, switching to default (%s)", (capabilities_mode & CAP_PERF) ? "perf" : "safe");
        parameters->mode = (capabilities_mode & CAP_PERF) ? (uint8_t)DPU_REGION_MODE_PERF : (uint8_t)DPU_REGION_MODE_SAFE;
    }

    description->configuration.mram_access_by_dpu_only = mram_access_by_dpu_only;

    /* FPGA specific */
    parameters->fpga.activate_ila = activate_ila;
    parameters->fpga.activate_filtering_ila = activate_filtering_ila;
    parameters->fpga.activate_mram_bypass = activate_mram_bypass;
    parameters->fpga.activate_mram_refresh_emulation = refresh_emulation_period != 0;
    parameters->fpga.mram_refresh_emulation_period = refresh_emulation_period;
    parameters->fpga.report_path = report_path;

    parameters->bypass_module_compatibility = bypass_module_compatibility;

    description->configuration.ila_control_refresh = activate_ila;

    description->configuration.enable_cycle_accurate_behavior = cycle_accurate;
    description->hw.timings.clock_division = clock_division;
    description->hw.timings.fck_frequency_in_mhz = fck_frequency;
    description->_internals.data = parameters;
    description->_internals.free = free_hw_parameters;

    return DPU_RANK_SUCCESS;
}

static dpu_rank_status_e
hw_custom_operation(struct dpu_rank_t *rank,
                    __attribute__((unused)) dpu_slice_id_t slice_id,
                    __attribute__((unused)) dpu_member_id_t member_id,
                    dpu_custom_command_t command,
                    __attribute__((unused)) dpu_custom_command_args_t args)
{
    dpu_rank_status_e status = DPU_RANK_SUCCESS;
    hw_dpu_rank_allocation_parameters_t params = _this_params(rank->description);

    // Important Note: fpga with ILA support implements only one DPU behind the control interface
    // => that's why we use the DPU_COMMAND_DPU* versions of the custom commands, the operations on ILA
    // would normally apply on the control interface.
    switch (command)
    {
    case DPU_COMMAND_ALL_SOFT_RESET:
    case DPU_COMMAND_DPU_SOFT_RESET:
        if (params->fpga.activate_ila)
        {
            if (!reset_ila(&params->rank_fs))
            {
                status = DPU_RANK_SYSTEM_ERROR;
                break;
            }

            if (params->fpga.activate_filtering_ila)
            {
                if (!activate_filter_ila(&params->rank_fs))
                {
                    status = DPU_RANK_SYSTEM_ERROR;
                    break;
                }
            }
            else
            {
                if (!deactivate_filter_ila(&params->rank_fs))
                {
                    status = DPU_RANK_SYSTEM_ERROR;
                    break;
                }
            }

            set_mram_bypass_to(&params->rank_fs, params->fpga.activate_mram_bypass);

            if (params->fpga.activate_mram_refresh_emulation)
            {
                if (!enable_refresh_emulation(&params->rank_fs, params->fpga.mram_refresh_emulation_period))
                {
                    status = DPU_RANK_SYSTEM_ERROR;
                    break;
                }
            }
            else
            {
                if (!disable_refresh_emulation(&params->rank_fs))
                {
                    status = DPU_RANK_SYSTEM_ERROR;
                    break;
                }
            }
        }

        break;
    case DPU_COMMAND_ALL_PREEXECUTION:
    case DPU_COMMAND_DPU_PREEXECUTION:
        if (params->fpga.activate_ila)
        {
            if (!activate_ila(&params->rank_fs))
            {
                status = DPU_RANK_SYSTEM_ERROR;
                break;
            }
        }

        break;
    case DPU_COMMAND_ALL_POSTEXECUTION:
    case DPU_COMMAND_DPU_POSTEXECUTION:
        if (params->fpga.activate_ila)
        {
            if (!deactivate_ila(&params->rank_fs))
            {
                status = DPU_RANK_SYSTEM_ERROR;
                break;
            }

            if (!dump_ila_report(&params->rank_fs, params->fpga.report_path))
            {
                status = DPU_RANK_SYSTEM_ERROR;
                break;
            }
        }

        break;
    case DPU_COMMAND_SET_SLICE_INFO:
        if (ioctl(params->rank_fs.fd_rank, DPU_RANK_IOCTL_SLICE_INFO, rank->runtime.control_interface.slice_info) != 0)
        {
            LOG_RANK(WARNING, rank, "Failed to provide driver with slice information (%s)", strerror(errno));
            status = DPU_RANK_SYSTEM_ERROR;
            break;
        }
    default:
        break;
    }

    return status;
}

static dpu_rank_status_e
hw_get_nr_dpu_ranks(uint32_t *nr_ranks)
{
    *nr_ranks = dpu_sysfs_get_nb_physical_ranks();
    return DPU_RANK_SUCCESS;
}
