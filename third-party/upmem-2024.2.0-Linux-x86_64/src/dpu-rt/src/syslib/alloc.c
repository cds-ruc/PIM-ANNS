/* Copyright 2020 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stddef.h>
#include <dpuruntime.h>
#include <atomic_bit.h>
#include <attributes.h>

volatile unsigned int __sys_heap_pointer = (unsigned int)(&__sys_heap_pointer_reset);

ATOMIC_BIT_INIT(__heap_pointer);

/* noinline, because part of grind tracked functions
 * Also used by seqread.inc
 */
void *__noinline
mem_alloc_nolock(size_t size)
{
    unsigned int pointer = __HEAP_POINTER;

    if (size == 0)
        goto end;

    pointer = (pointer + 7) & ~7;

    unsigned int new_heap_pointer;

    /* force an error if overflow or crossing WRAM max size */
    /* clang-format off */
    __asm__ volatile("\tadd %[nhp], %[ptr], %[sz], c, 1f\n"
                     "\tjltu %[nhp], %[max_size], 2f\n"
                     "\t1:fault " __STR(__FAULT_ALLOC_HEAP_FULL__) "\n"
                     "\t2:\n"
                     : [nhp] "=&r"(new_heap_pointer)
                     : [ptr] "r"(pointer), [sz] "r"(size),
                       [max_size] "r"((unsigned int)(&__sys_heap_pointer_end))
                     );
    /* clang-format on */

    __HEAP_POINTER = new_heap_pointer;

end:
    return (void *)pointer;
}

void *
mem_alloc(size_t size)
{
    ATOMIC_BIT_ACQUIRE(__heap_pointer);
    void *pointer = mem_alloc_nolock(size);
    ATOMIC_BIT_RELEASE(__heap_pointer);
    return pointer;
}

void *
mem_reset()
{
    ATOMIC_BIT_ACQUIRE(__heap_pointer);

    void *initial = &__sys_heap_pointer_reset;

    __sys_heap_pointer = (unsigned int)initial;

    ATOMIC_BIT_RELEASE(__heap_pointer);

    return (void *)initial;
}
