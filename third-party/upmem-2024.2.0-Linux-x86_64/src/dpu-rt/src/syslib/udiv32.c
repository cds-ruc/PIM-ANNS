/* Copyright 2024 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <dpuruntime.h>

uint64_t __attribute__((used)) __udiv32(uint32_t dividend, uint32_t divider)
{
    uint64_t dest;

    uint32_t temp0;
    uint32_t temp1;

    /* clang-format off */
    __asm__ volatile("  clz %[temp0], %[divider], max, 1f\n" // %[temp0] = by how many the divider can be shifted on 32-bit
                     "  clz %[temp1], %[dividend]\n" // %[temp1] = number of useless bits of the dividend
                     "  sub %[temp0], %[temp1], %[temp0], gtu, 2f\n" // %[temp0] = the maximal shift to be done
                     "  move %[temp1], %[divider]\n"
                     "  move.u %[dest], %[dividend]\n"
                     "  jump %[temp0], 3f\n" // As we will jump backward relatively to label 3 forward
                     "  div_step %[dest], %[temp1], %[dest], 31\n"
                     "  div_step %[dest], %[temp1], %[dest], 30\n"
                     "  div_step %[dest], %[temp1], %[dest], 29\n"
                     "  div_step %[dest], %[temp1], %[dest], 28\n"
                     "  div_step %[dest], %[temp1], %[dest], 27\n"
                     "  div_step %[dest], %[temp1], %[dest], 26\n"
                     "  div_step %[dest], %[temp1], %[dest], 25\n"
                     "  div_step %[dest], %[temp1], %[dest], 24\n"
                     "  div_step %[dest], %[temp1], %[dest], 23\n"
                     "  div_step %[dest], %[temp1], %[dest], 22\n"
                     "  div_step %[dest], %[temp1], %[dest], 21\n"
                     "  div_step %[dest], %[temp1], %[dest], 20\n"
                     "  div_step %[dest], %[temp1], %[dest], 19\n"
                     "  div_step %[dest], %[temp1], %[dest], 18\n"
                     "  div_step %[dest], %[temp1], %[dest], 17\n"
                     "  div_step %[dest], %[temp1], %[dest], 16\n"
                     "  div_step %[dest], %[temp1], %[dest], 15\n"
                     "  div_step %[dest], %[temp1], %[dest], 14\n"
                     "  div_step %[dest], %[temp1], %[dest], 13\n"
                     "  div_step %[dest], %[temp1], %[dest], 12\n"
                     "  div_step %[dest], %[temp1], %[dest], 11\n"
                     "  div_step %[dest], %[temp1], %[dest], 10\n"
                     "  div_step %[dest], %[temp1], %[dest], 9\n"
                     "  div_step %[dest], %[temp1], %[dest], 8\n"
                     "  div_step %[dest], %[temp1], %[dest], 7\n"
                     "  div_step %[dest], %[temp1], %[dest], 6\n"
                     "  div_step %[dest], %[temp1], %[dest], 5\n"
                     "  div_step %[dest], %[temp1], %[dest], 4\n"
                     "  div_step %[dest], %[temp1], %[dest], 3\n"
                     "  div_step %[dest], %[temp1], %[dest], 2\n"
                     "  div_step %[dest], %[temp1], %[dest], 1\n"
                     "3:\n"
                     "  div_step %[dest], %[temp1], %[dest], 0\n"
                     "4:\n"
                     "  jump 5f\n"
                     "2:\n"
                     "  move.u %[dest], %[dividend], true, 4b\n"
                     "1:\n"
                     "  fault "__STR(__FAULT_DIVISION_BY_ZERO__)"\n"
                     "5:\n"
                     : [dest] "=r"(dest), [temp0] "=&r" (temp0), [temp1] "=&r" (temp1)
                     : [dividend] "r"(dividend), [divider] "r"(divider));
    /* clang-format on */

    return dest;
}
