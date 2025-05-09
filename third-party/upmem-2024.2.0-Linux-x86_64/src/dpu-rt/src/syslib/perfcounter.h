/* Copyright 2020 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef DPUSYSCORE_PERFCOUNTER_H
#define DPUSYSCORE_PERFCOUNTER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @file perfcounter.h
 * @brief Utilities concerning the performance counter register.
 *
 */

/**
 * @typedef perfcounter_t
 * @brief A value which can be stored by the performance counter.
 */
typedef uint64_t perfcounter_t;

/**
 * @enum _perfcounter_config_t
 * @brief A configuration for the performance counter, defining what should be counted.
 */
/**
 * @typedef perfcounter_config_t
 * @brief A configuration for the performance counter, defining what should be counted.
 */
typedef enum _perfcounter_config_t {
    COUNT_SAME = 0, /**< keep the previous configuration */
    COUNT_CYCLES = 1, /**< switch to counting clock cycles */
    COUNT_INSTRUCTIONS = 2, /**< switch to counting executed instructions */
    COUNT_NOTHING = 3, /**< does not count anything */
    COUNT_DISABLE_BOTH = 6, /**< disable performance counters (v1B only) */
    COUNT_ENABLE_BOTH = 7, /**< enable performance counters (v1B only) */
} perfcounter_config_t;

/**
 * @brief A number used to convert the value returned by the perfcounter_get and perfcounter_config functions into seconds,
 *        when counting clock cycles.
 */
extern const volatile uint32_t CLOCKS_PER_SEC;

/**
 * @brief Fetch the value of the performance counter register.
 *
 * @return The current value of the performance counter register, or undefined if perfcounter_config has not been called before.
 */
perfcounter_t
perfcounter_get(void);

#ifdef DOXYGEN_ONLY
#undef DPU_PROFILING
#endif

#ifndef DPU_PROFILING
/**
 * @brief Configure the performance counter behavior.
 *
 * This function cannot be used when profiling an application.
 *
 * @param config        The new behavior for the performance counter register
 * @param reset_value   Whether the performance counter register should be set to 0
 *
 * @return The current value of the performance counter register, or undefined if perfcounter_config has not been called before.
 */
perfcounter_t
perfcounter_config(perfcounter_config_t config, bool reset_value);

#ifdef DOXYGEN_ONLY
#define __dpu_v1B__
#endif

#ifdef __dpu_v1B__
/**
 * @struct perfcounter_pair_t
 * @brief A structure holding both a cycle and an instruction counter.
 */
typedef struct {
    perfcounter_t cycles; /**< The cycle counter */
    perfcounter_t instr; /**< The instruction counter */
} perfcounter_pair_t;

/**
 * @brief Fetch values of the performance counter registers.
 *
 * This function cannot be used when profiling an application.
 * @warning This function is available only on v1B DPU version.
 *
 * @param should_reset   Whether the performance counter registers should be set to 0
 *
 * @return The current value of the performance counter registers, or undefined if perfcounter_config has not been called before.
 */
perfcounter_pair_t
perfcounter_get_both(bool should_reset);
#else
/// @cond INTERNAL
#define perfcounter_get_both(should_reset)                                                                                       \
    do {                                                                                                                         \
        _Static_assert(0, "[DPU_ERROR]: call to perfcounter_get_both is only available on v1B.");                                \
    } while (0)
/// @endcond

#endif /* __dpu_v1B__ */

#else
#define perfcounter_config(config, reset_value)                                                                                  \
    do {                                                                                                                         \
        _Static_assert(0, "[DPU_ERROR]: call to perfcounter_config is incompatible with -pg option.");                           \
    } while (0)
#endif /* !DPU_PROFILING */

#endif /* DPUSYSCORE_PERFCOUNTER_H */
