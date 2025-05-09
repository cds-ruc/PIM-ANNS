/* Copyright 2024 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <dpuruntime.h>

extern uint64_t
__udiv32(uint32_t dividend, uint32_t divider);

int64_t __attribute__((used)) __div32(int32_t dividend, int32_t divider)
{
    uint64_t res;
    uint32_t q;
    uint32_t rem;

    __asm__ goto("clo zero, %[dividend], z, %l[__div32_pos_dividend]\n\t"
                 "clo zero, %[divider], z, %l[__div32_neg_dividend_pos_divider]\n\t"
                 :
                 : [dividend] "r"(dividend), [divider] "r"(divider)
                 :
                 : __div32_pos_dividend, __div32_neg_dividend_pos_divider);

    /* The quotient's sign depends on the sign of the dividend and divider... After few tries it sounds */
    /* like the quickest way to select the operators is to branch according to the cases. */

    /* __div32_neg_dividend_neg_divider: */
    /* As a result, the quotient is positive and the remainder negative */
    dividend = 0 - dividend;
    divider = 0 - divider;
    res = __udiv32(dividend, divider);
    q = (uint32_t)(res >> 32);
    rem = (uint32_t)res;
    rem = 0 - rem;
    goto recombine;

__div32_neg_dividend_pos_divider:
    /* As a result, the quotient is negative and the remainder negative */
    dividend = 0 - dividend;
    res = __udiv32(dividend, divider);
    q = (uint32_t)(res >> 32);
    q = 0 - q;
    rem = (uint32_t)res;
    rem = 0 - rem;
    goto recombine;

__div32_pos_dividend:
    __asm__ goto("clo zero, %[divider], z, %l[__div32_pos_dividend_pos_divider]"
                 :
                 : [divider] "r"(divider)
                 :
                 : __div32_pos_dividend_pos_divider);
    /* As a result, the quotient is negative and the remainder positive */
    divider = 0 - divider;
    res = __udiv32(dividend, divider);
    q = (uint32_t)(res >> 32);
    q = 0 - q;
    rem = (uint32_t)res;
    goto recombine;

__div32_pos_dividend_pos_divider:
    /* The dividend and divider are both positive */
    res = __udiv32(dividend, divider);
    goto last_exit;
    /* q = (uint32_t) (res >> 32); */
    /* rem = (uint32_t) res; */
    /* goto recombine; */

recombine:
    res = q;
    res <<= 32;
    res |= rem;
last_exit:
    return res;
}

/* int64_t __attribute__((used)) __div32(int32_t a, int32_t b) */
/* { */
/*     uint64_t res; */
/*     uint32_t q; */
/*     uint32_t rem; */

/*     const int bits_in_word_m1 = (int)(sizeof(int) * CHAR_BIT) - 1; */
/*     int s_a = a >> bits_in_word_m1; /\* s_a = a < 0 ? -1 : 0 *\/ */
/*     int s_b = b >> bits_in_word_m1; /\* s_b = b < 0 ? -1 : 0 *\/ */
/*     a = (a ^ s_a) - s_a; /\* negate if s_a == -1 *\/ */
/*     b = (b ^ s_b) - s_b; /\* negate if s_b == -1 *\/ */
/*     int s_q = s_a ^ s_b; /\* sign of quotient *\/ */

/*     res = __udiv32(a, b); */
/*     q = (uint32_t) (res >> 32); */
/*     rem = (uint32_t) res; */

/*     q = (q ^ s_q) + (-s_q); */
/*     rem = (rem ^ s_a) + (-s_a); */

/*     res = q; */
/*     res <<= 32; */
/*     res |= rem; */

/*     return res; */
/* } */
