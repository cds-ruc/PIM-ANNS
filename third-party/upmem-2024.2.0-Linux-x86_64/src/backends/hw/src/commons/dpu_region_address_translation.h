/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright 2020 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 */

#ifndef DPU_REGION_ADDRESS_TRANSLATION_INCLUDE_H
#define DPU_REGION_ADDRESS_TRANSLATION_INCLUDE_H

#include <stdint.h>
#include <stdbool.h>

#include <dpu_chip_id.h>
#include <dpu_types.h>
#include <dpu_hw_description.h>

#define DPU_XFER_THREAD_CONF_DEFAULT (UINT_MAX)
struct dpu_transfer_thread_configuration {
    uint32_t nb_thread_per_pool;
    uint32_t threshold_1_thread;
    uint32_t threshold_2_threads;
    uint32_t threshold_4_threads;
};

enum {
#ifdef __x86_64__
    DPU_BACKEND_XEON_SP = 0,
#endif
    DPU_BACKEND_FPGA_KC705,
    DPU_BACKEND_FPGA_AWS,
    DPU_BACKEND_FPGA_KU250U2,
#ifdef __powerpc64__
    DPU_BACKEND_POWER9,
#endif
    DPU_BACKEND_DEVICETREE,

    DPU_BACKEND_NUMBER
};

#define CAP_SAFE (1 << 0)
#define CAP_PERF (1 << 1)
#define CAP_HYBRID_CONTROL_INTERFACE (1 << 2)
#define CAP_HYBRID_MRAM (1 << 3)
#define CAP_HYBRID (CAP_HYBRID_MRAM | CAP_HYBRID_CONTROL_INTERFACE)

extern struct dpu_region_address_translation *backend_translate[];

#ifndef struct_dpu_transfer_matrix_t
#define struct_dpu_transfer_matrix_t
#define MAX_NR_DPUS_PER_RANK 64
struct dpu_transfer_matrix {
    union {
        void *ptr[MAX_NR_DPUS_PER_RANK];
        struct sg_xfer_buffer *sg_ptr[MAX_NR_DPUS_PER_RANK];
    };
    uint32_t offset;
    uint32_t size;
    // TBC : compilation issue if using enum defined in dpu_types.h
    uint8_t type;
};
#endif

/* Backend description of the CPU/BIOS configuration address translation:
 * interleave: Describe the machine configuration, retrieved from ACPI table
 *		and dpu_chip_id_info: ACPI table gives info about physical
 *		topology (number of channels, number of dimms...etc)
 *		and the dpu_chip_id whose configuration is hardcoded
 *		into dpu_chip_id_info.h (number of dpus, size of MRAM...etc).
 * init_rank: Init data structures/threads for a single rank
 * destroy_rank: Destroys data structures/threads for a single rank
 * write_to_cis: Writes blocks of 64 bytes that targets all CIs. The
 *		 backend MUST:
 *			- interleave
 *			- byte order
 *			- nopify and send MSB
 *		 bit ordering must be done by upper software layer since only
 *		 a few commands require it, which is unknown at this level.
 * read_from_cis: Reads blocks of 64 bytes from all CIs, same comment as
 *		  write_block_to_ci.
 * write_to_rank: Writes to MRAMs using the matrix of descriptions of
 *		  transfers for each dpu.
 * read_from_rank: Reads from MRAMs using the matrix of descriptions of
 *		   transfers for each dpu.
 */
struct dpu_region_address_translation {
    /* Physical topology */
    struct dpu_hw_description_t *desc;

    /* Id exposed through sysfs for userspace. */
    uint8_t backend_id;

    /* PERF, SAFE, HYBRID & MRAM, HYBRID & CTL IF, ... */
    uint64_t capabilities;

    /* In hybrid mode, userspace needs to know the size it needs to mmap */
    uint64_t hybrid_mmap_size;

    /* Thread configuration to perform MRAM transfer */
    struct dpu_transfer_thread_configuration xfer_thread_conf;

    bool one_read;

    /* Pointer to private data for each backend implementation */
    void *private;

    /* Returns -errno on error, 0 otherwise. */
    int (*init_rank)(struct dpu_region_address_translation *tr, uint8_t channel_id);
    void (*destroy_rank)(struct dpu_region_address_translation *tr, uint8_t channel_id);

    /* block_data points to differents objects depending on 'where' the
     * backend is implemented:
     * - in userspace, it points to a virtually contiguous buffer
     * - in kernelspace, it points to an array of pages of size PAGE_SIZE.
     */

    /* Returns the number of bytes written */
    void (*write_to_rank)(struct dpu_region_address_translation *tr,
        void *base_region_addr,
        uint8_t channel_id,
        struct dpu_transfer_matrix *transfer_matrix);
    /* Returns the number of bytes read */
    void (*read_from_rank)(struct dpu_region_address_translation *tr,
        void *base_region_addr,
        uint8_t channel_id,
        struct dpu_transfer_matrix *transfer_matrix);

    /* block_data points to an array of nb_ci uint64_t */

    /* Returns the number of bytes written */
    void (*write_to_cis)(struct dpu_region_address_translation *tr,
        void *base_region_addr,
        uint8_t channel_id,
        void *block_data,
        uint32_t block_size);
    /* Returns the number of bytes read */
    void (*read_from_cis)(struct dpu_region_address_translation *tr,
        void *base_region_addr,
        uint8_t channel_id,
        void *block_data,
        uint32_t block_size);
#ifdef __KERNEL__
    int (*mmap_hybrid)(struct dpu_region_address_translation *tr, struct file *filp, struct vm_area_struct *vma);
#endif
};


#endif /* DPU_REGION_ADDRESS_TRANSLATION_INCLUDE_H */
