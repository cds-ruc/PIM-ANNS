/* Copyright 2020 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <dpuruntime.h>
#include <dpu_characteristics.h>

unsigned char __attribute__((used)) __sys_wq_table[DPU_NR_THREADS] = { [0 ...(DPU_NR_THREADS - 1)] = __EMPTY_WAIT_QUEUE };
