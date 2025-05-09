/* Copyright 2020 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <attributes.h>
#include <dpufault.h>
#include <dpuruntime.h>
#include <macro_utils.h>
#include <perfcounter.h>

#define BIT_IMPRECISION 4

perfcounter_t
perfcounter_get(void)
{
    uint32_t reg_value;
    __asm__ volatile("time %[r]" : [r] "=r"(reg_value));
    return ((perfcounter_t)reg_value) << BIT_IMPRECISION;
}

#ifndef DPU_PROFILING
perfcounter_t
perfcounter_config(perfcounter_config_t config, bool reset_value)
{
#ifdef __dpu_v1A__
    switch (config) {
        default:
            break;
        case COUNT_DISABLE_BOTH:
        case COUNT_ENABLE_BOTH:
            __asm__("fault " __STR(__FAULT_PFM_COUNTER_INCOMPATIBILITY__));
            unreachable();
    }
#endif /* __dpu_v1A__ */

    uint32_t reg_value;
    uint32_t reg_config = (reset_value ? 1 : 0) | (config << 1);
    __asm__ volatile("time_cfg %[r], %[c]" : [r] "=r"(reg_value) : [c] "r"(reg_config));
    return ((perfcounter_t)reg_value) << BIT_IMPRECISION;
}

#ifdef __dpu_v1B__
perfcounter_pair_t
perfcounter_get_both(bool should_reset)
{
    perfcounter_pair_t res;

    uint32_t cycles;
    uint32_t instr;

    uint32_t disable_both = (COUNT_DISABLE_BOTH << 1);
    uint32_t enable_both = ((uint32_t)should_reset) | (COUNT_ENABLE_BOTH << 1);

    uint32_t select_cycles = 10;
    uint32_t select_instr = 12;

    /* clang-format off */
    __asm__ volatile("time_cfg zero, %[disable_both]\n\t"

                     "time_cfg zero, %[select_cycles]\n\t"
                     "time %[cycles]\n\t"

                     "time_cfg zero, %[select_instr]\n\t"
                     "time %[instr]\n\t"

                     : [instr] "=&r" (instr),
                       [cycles] "=&r" (cycles)
                     : [disable_both] "r" (disable_both),
                       [select_cycles] "r" (select_cycles),
                       [select_instr] "r" (select_instr)
		     );
    /* clang-format on */

    res.cycles = ((perfcounter_t)cycles) << BIT_IMPRECISION;
    res.instr = ((perfcounter_t)instr) << BIT_IMPRECISION;

    /* clang-format off */
    __asm__ volatile("time_cfg zero, %[enable_both]\n\t"
		     : : [enable_both] "r" (enable_both));
    /* clang-format on */

    return res;
}
#endif /* __dpu_v1B__ */

#endif /* !DPU_PROFILING */
