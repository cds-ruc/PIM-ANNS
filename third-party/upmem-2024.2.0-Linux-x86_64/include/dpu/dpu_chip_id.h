/* Copyright 2020 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 */

#ifndef __DPU_CHIP_H
#define __DPU_CHIP_H

/**
 * @file dpu_chip_id.h
 * @brief Definition of the different DPU chip IDs.
 */

/**
 * @brief Attribute to mark a function as maybe unused.
 * Should be used in header-implemented functions.
 * @hideinitializer
 */
#define __MAYBE_UNUSED__ __attribute__((unused))

/**
 * @brief The different valid DPU chip IDs.
 */
typedef enum _dpu_chip_id_e {
    vD = 0,
    vD_cas = 1,
    vD_fun = 2,
    vD_asic1 = 3,
    vD_asic8 = 4,
    vD_fpga1 = 5,
    vD_fpga8 = 6,
    vD_asic4 = 7,
    vD_fpga4 = 8,
    vD_fun_v1_4 = 66,
    vD_asic1_v1_4 = 67,
    vD_asic8_v1_4 = 68,
    vD_fpga1_v1_4 = 69,
    vD_fpga8_v1_4 = 70,
    vD_asic4_v1_4 = 71,
    vD_fpga4_v1_4 = 72,
} dpu_chip_id_e;

/**
 * @brief The next DPU chip index to be used.
 */
#define NEXT_DPU_CHIP_IDX 16

/**
 * @brief Get the DPU chip index for a given DPU chip ID.
 * @param chip_id The DPU chip ID to convert
 * @return The DPU chip index associated to the DPU chip ID.
 */
__MAYBE_UNUSED__ static inline unsigned int
chip_id_to_idx(dpu_chip_id_e chip_id)
{
    switch (chip_id) {
        case vD:
            return 0;
        case vD_cas:
            return 1;
        case vD_fun:
            return 2;
        case vD_asic1:
            return 3;
        case vD_asic8:
            return 4;
        case vD_fpga1:
            return 5;
        case vD_fpga8:
            return 6;
        case vD_asic4:
            return 7;
        case vD_fpga4:
            return 8;
        case vD_fun_v1_4:
            return 9;
        case vD_asic1_v1_4:
            return 10;
        case vD_asic8_v1_4:
            return 11;
        case vD_fpga1_v1_4:
            return 12;
        case vD_fpga8_v1_4:
            return 13;
        case vD_asic4_v1_4:
            return 14;
        case vD_fpga4_v1_4:
            return 15;
        default:
            return NEXT_DPU_CHIP_IDX;
    }
}

#endif /* __DPU_CHIP_H */
