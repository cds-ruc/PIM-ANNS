#include <mram_unaligned.h>
#include <mram.h>
#include <defs.h>

#define VL_HASH_MRAM_ADDR(addr) (((uintptr_t)(addr) >> 3) & ((1 << __MRAM_UNALIGNED_ACCESS_LOG_NB_VLOCK) - 1))
#define ALIGN_8_LOW(x) (((uintptr_t)(x) >> 3) << 3)
#define ALIGN_8_HIGH(x) ((((uintptr_t)(x) + 7) >> 3) << 3)
#define MRAM_ADDR(addr) ((__mram_ptr void *)(addr))

VMUTEX_INIT(__mram_unaligned_access_virtual_locks, 1 << __MRAM_UNALIGNED_ACCESS_LOG_NB_VLOCK, 1);

__attribute((used)) __dma_aligned uint8_t __mram_unaligned_access_buffer[DPU_NR_THREADS << 3];

void *
mram_read_unaligned(const __mram_ptr void *from, void *buffer, unsigned int nb_of_bytes)
{

    uint32_t addr_8align = ALIGN_8_LOW(from);
    uint8_t diff = (uintptr_t)from - addr_8align;
    uint32_t size_8align = ALIGN_8_HIGH(nb_of_bytes + diff);

    mram_read((__mram_ptr void *)addr_8align, buffer, size_8align);

    return (uint8_t *)buffer + diff;
}

void
mram_write_unaligned(const void *from, __mram_ptr void *dest, unsigned nb_of_bytes)
{

    int is_addr_aligned = (((uintptr_t)dest & 7) == 0);
    int is_sz_aligned = ((nb_of_bytes & 7) == 0);
    if (is_addr_aligned && is_sz_aligned) {
        mram_write(from, dest, nb_of_bytes);
        return;
    }

    // if the WRAM and MRAM addresses have different alignment,
    // raise an error
    if (((uintptr_t)from & 7) != ((uintptr_t)dest & 7)) {
        __asm__("fault " __STR(__FAULT_MRAM_UNALIGNED__));
    }

    // if the destination address is not a multiple of 8
    // there will be a prolog
    // This prolog needs to be in critical section associated with
    // the address
    const uint8_t *wram_addr = from;
    if (!is_addr_aligned) {
        uint8_t start = (uintptr_t)dest & 7;
        uintptr_t hash = VL_HASH_MRAM_ADDR(dest);
        uintptr_t dest_low = ALIGN_8_LOW(dest);
        vmutex_lock(&__mram_unaligned_access_virtual_locks, hash);
        mram_read(MRAM_ADDR(dest_low), &__mram_unaligned_access_buffer[me() * 8], 8);
        for (uint8_t i = start; (i < 8) && nb_of_bytes; ++i) {
            __mram_unaligned_access_buffer[me() * 8 + i] = *wram_addr++;
            nb_of_bytes--;
        }
        mram_write(&__mram_unaligned_access_buffer[me() * 8], MRAM_ADDR(dest_low), 8);
        vmutex_unlock(&__mram_unaligned_access_virtual_locks, hash);
        if (!nb_of_bytes)
            return;
    }

    // mram transfer of aligned part
    uint8_t nb_bytes_low = ALIGN_8_LOW(nb_of_bytes);
    uintptr_t dest_high = ALIGN_8_HIGH(dest);
    if (nb_bytes_low) {
        mram_write(wram_addr, MRAM_ADDR(dest_high), nb_bytes_low);
        wram_addr += nb_bytes_low;
    }

    if (nb_of_bytes > nb_bytes_low) {
        // epilog
        // this needs to be handled in critical section
        uintptr_t addr = dest_high + nb_bytes_low;
        uintptr_t hash = VL_HASH_MRAM_ADDR(addr);
        vmutex_lock(&__mram_unaligned_access_virtual_locks, hash);
        mram_read(MRAM_ADDR(addr), &__mram_unaligned_access_buffer[me() * 8], 8);
        uint8_t diff = nb_of_bytes - nb_bytes_low;
        for (uint8_t i = 0; i < diff; ++i) {
            __mram_unaligned_access_buffer[me() * 8 + i] = *wram_addr++;
        }
        mram_write(&__mram_unaligned_access_buffer[me() * 8], MRAM_ADDR(addr), 8);
        vmutex_unlock(&__mram_unaligned_access_virtual_locks, hash);
    }
}
