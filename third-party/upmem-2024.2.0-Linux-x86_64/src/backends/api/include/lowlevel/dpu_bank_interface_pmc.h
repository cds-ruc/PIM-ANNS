/* Copyright 2024 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef DPU_BANK_INTERFACE_PMC
#define DPU_BANK_INTERFACE_PMC

#include <stdint.h>

#include <dpu_error.h>

struct dpu_set_t;

/**
 * @file dpu_bank_interface_pmc.h
 * @brief C API for managing bank interface performance monitoring counter.
 */

/**
 * @brief Valid event identifier for PMC.
 */
typedef enum {
    BANK_INTERFACE_PMC_NONE = 0x0,

    /** Count LDMA instruction made by the DPU. */
    BANK_INTERFACE_PMC_LDMA_INSTRUCTION = 0x1,

    /** Count SDMA instruction made by the DPU. */
    BANK_INTERFACE_PMC_SDMA_INSTRUCTION = 0x2,

    /** Count number of 64-bit value read by the DPU. */
    BANK_INTERFACE_PMC_READ_64BIT_INSTRUCTION = 0x4,

    /** Count number of 64-bit value written by the DPU. */
    BANK_INTERFACE_PMC_WRITE_64BIT_INSTRUCTION = 0x8,

    /** Count number of activate command made by the host. */
    BANK_INTERFACE_PMC_HOST_ACTIVATE_COMMAND = 0x10,

    /** Count number of refresh command made by the host. */
    BANK_INTERFACE_PMC_HOST_REFRESH_COMMAND = 0x20,

    /** Count RowHammer refresh protection made by the bank interface. */
    BANK_INTERFACE_PMC_ROW_HAMMER_REFRESH_COMMAND = 0x40,

    /** Count number of clock cycles of the bank interface. */
    BANK_INTERFACE_PMC_CYCLES = 0x80
} bank_interface_pmc_event_t;

/**
 * @brief Specify counting mode.
 * In 32bit mode, it is possible to count two counter with 32-bits precision.
 * In 64bit mode, one counter is available with 64-bits precision.
 */
typedef enum {
    BANK_INTERFACE_PMC_32BIT_MODE = 0x0,
    BANK_INTERFACE_PMC_64BIT_MODE = 0x1,
} bank_interface_pmc_mode_t;

/**
 * @brief A structure to specify configuration for PMC counting.
 */
typedef struct {
    /** The operating mode */
    bank_interface_pmc_mode_t mode;
    /** The configuration for counter 1. */
    bank_interface_pmc_event_t counter_1;
    /** The configuration for counter 2. */
    bank_interface_pmc_event_t counter_2;
} bank_interface_pmc_config_t;

/**
 * @brief A structure to store PMC values.
 */
typedef struct {
    /** The result is either two 32-bits values, or one 64-bits value. */
    union {
        /** Result as two 32-bits counter. */
        struct {
            /** First counter. */
            uint32_t counter_1;
            /** Second counter. */
            uint32_t counter_2;
        } two_32bits;
        /** Result as one 64-bits counter. */
        struct {
            /** The counter. */
            uint64_t counter_1;
        } one_64bits;
    };
} bank_interface_pmc_result_t;

/**
 * @brief Enable PMC module on the given DPU.
 * @param set the DPU
 * @param configuration the configuration
 * @return Whether the operation was successful.
 */
dpu_error_t
dpu_bank_interface_pmc_enable(struct dpu_set_t set, bank_interface_pmc_config_t configuration);

/**
 * @brief Disable PMC on the given DPU.
 * @param set the DPU
 * @return Whether the operation was successful.
 */
dpu_error_t
dpu_bank_interface_pmc_disable(struct dpu_set_t set);

/**
 * @brief Stop the counters on the given DPU.
 * @param set the DPU
 * @return Whether the operation was successful.
 */
dpu_error_t
dpu_bank_interface_pmc_stop_counters(struct dpu_set_t set);

/**
 * @brief Read counters on the given DPU.
 * @param set the DPU
 * @param result the result
 * @return Whether the operation was successful.
 */
dpu_error_t
dpu_bank_interface_pmc_read_counters(struct dpu_set_t set, bank_interface_pmc_result_t *result);

#endif /* DPU_BANK_INTERFACE_PMC */
