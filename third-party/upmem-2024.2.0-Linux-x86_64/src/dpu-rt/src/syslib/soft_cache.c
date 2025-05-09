/* Copyright 2020 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <attributes.h>
#include <dpu_characteristics.h>

unsigned long __sw_cache_buffer[DPU_NR_THREADS] __dma_aligned __used;
