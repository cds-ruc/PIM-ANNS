/* Copyright 2020 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <seqread.h>
#include <mram.h>
#include <dpuconst.h>
#include <dpuruntime.h>
#include <atomic_bit.h>
#include <stddef.h>

#define PAGE_SIZE (SEQREAD_CACHE_SIZE)
#define PAGE_ALLOC_SIZE (2 * PAGE_SIZE)
#define PAGE_OFF_MASK (PAGE_SIZE - 1)
#define PAGE_IDX_MASK (~PAGE_OFF_MASK)

#define MRAM_READ_PAGE(from, to) mram_read((__mram_ptr void *)(from), (void *)(to), PAGE_ALLOC_SIZE)

extern void *
mem_alloc_nolock(size_t size);
ATOMIC_BIT_EXTERN(__heap_pointer);

seqreader_buffer_t
__SEQREAD_ALLOC(void)
{
    ATOMIC_BIT_ACQUIRE(__heap_pointer);

    unsigned int heap_pointer = __HEAP_POINTER;
    seqreader_buffer_t pointer = (seqreader_buffer_t)((heap_pointer + PAGE_OFF_MASK) & PAGE_IDX_MASK);
    size_t size = pointer + PAGE_ALLOC_SIZE - heap_pointer;
    /* We already compute the return pointer
     * mem_alloc_nolock is only used to really reserve the memory area in the heap */
    mem_alloc_nolock(size);

    ATOMIC_BIT_RELEASE(__heap_pointer);

    return pointer;
}

void *
__SEQREAD_INIT(seqreader_buffer_t cache, __mram_ptr void *mram_addr, seqreader_t *reader)
{
    reader->wram_cache = cache;
    reader->mram_addr = (uintptr_t)(1 << __DPU_MRAM_SIZE_LOG2);

    return __SEQREAD_SEEK(mram_addr, reader);
}

void *
__SEQREAD_GET(void *ptr, uint32_t inc, seqreader_t *reader)
{
#define FormatDMASize(size) (((size) >> 3) - 1)
    uint32_t mram_cache;
    uint32_t wram_cache;

    /* clang-format off */
    __asm__ volatile goto(
    "add %[p], %[p], %[i], nc" __STR(PAGE_SIZE_LOG2) ", %l[end]\n\t"

    "lw %[mram_cache], %[reader], 4\n\t"
    "add %[mram_cache], %[mram_cache], " __STR(PAGE_SIZE) "\n\t"
    "sw %[reader], 4, %[mram_cache]\n\t"

    "lw %[wram_cache], %[reader], 0\n\t"
    "ldma %[wram_cache], %[mram_cache], %[size]\n\t"

    "add %[p], %[p], -" __STR(PAGE_SIZE) "\n\t"
                          : /* [p] "+r"(ptr), */ [mram_cache]"=&r"(mram_cache), [wram_cache]"=&r"(wram_cache)
                          : [p] "r"(ptr), [reader]"r"(reader), [i]"r"(inc),
                            [size]"i"(FormatDMASize(PAGE_SIZE * 2))
                          : "memory"
                          : end);
      /* clang-format on */

// Note: the builtin cause correctness issue in the compiler
//       by correcting it, we get performance issue
//       so we revert to a situation without a specific builtin,
//       but with optimized inline assembly.
//       original work was in:
//         c46c81684897a20b3acb5b6307d349862e4a56fd
//         ab390d91d318d5b8633189c7c4e77b2f25889559
// Note: having the variable "p" as input read only is a trick to force
//       the usage of the r0 function parameter
//       we seem to have an issue in register allocation ...
// Note: we would like to allow to reuse r1, and r2 when done with them
//       but safety first so we early clobber mram_cache and wram_cache
end:
    return ptr;
}

void *
__SEQREAD_SEEK(__mram_ptr void *mram_addr, seqreader_t *reader)
{
    uintptr_t target_addr = (uintptr_t)mram_addr;
    uintptr_t current_addr = (uintptr_t)reader->mram_addr;
    uintptr_t wram_cache = (uintptr_t)reader->wram_cache;
    uintptr_t mram_offset = target_addr - current_addr;
    if ((mram_offset & PAGE_IDX_MASK) != 0) {
        uintptr_t target_addr_idx_page = target_addr & PAGE_IDX_MASK;
        MRAM_READ_PAGE(target_addr_idx_page, wram_cache);
        mram_offset = target_addr & PAGE_OFF_MASK;
        reader->mram_addr = target_addr_idx_page;
    }
    return (void *)(mram_offset + wram_cache);
}

__mram_ptr void *
__SEQREAD_TELL(void *ptr, seqreader_t *reader)
{
    return (__mram_ptr void *)((uintptr_t)reader->mram_addr + ((uintptr_t)ptr & PAGE_OFF_MASK));
}
