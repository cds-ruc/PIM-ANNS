/* Copyright 2020 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef BACKENDS_DPU_MACRO_UTILS_H
#define BACKENDS_DPU_MACRO_UTILS_H

/**
 * @file dpu_macro_utils.h
 * @brief Useful MACROs for the dpu headers
 */

#define _CONCAT_X(x, y) x##y
#define _CONCAT(x, y) _CONCAT_X(x, y)

#define _XSTR(x) #x
#define _STR(x) _XSTR(x)

#define _DPU_FOREACH_VARIANT(...) _DPU_FOREACH_VARIANT_(__VA_ARGS__, _DPU_FOREACH_VARIANT_SEQ())
#define _DPU_FOREACH_VARIANT_(a, ...) _DPU_FOREACH_VARIANT_N(__VA_ARGS__)
#define _DPU_FOREACH_VARIANT_N(_1, _2, N, ...) N
#define _DPU_FOREACH_VARIANT_SEQ() I, X, WRONG_NR_OF_ARGUMENTS

#endif /* BACKENDS_DPU_MACRO_UTILS_H */
