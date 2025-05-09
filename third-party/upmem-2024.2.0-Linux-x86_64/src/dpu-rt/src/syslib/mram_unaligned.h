/* Copyright 2022 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef DPUSYSCORE_MRAM_UNALIGNED_H
#define DPUSYSCORE_MRAM_UNALIGNED_H

#include <stdint.h>
#include <vmutex.h>
#include <mram.h>

/**
 * @file mram_unaligned.h
 * @brief APIs for unaligned MRAM access (address/size) and tasklet-safe write/update of less than 8-byte values.
 */

/*
 * Use one HW mutex and 2K virtual mutexes for unaligned MRAM accesses
 */
#ifndef __MRAM_UNALIGNED_ACCESS_LOG_NB_VLOCK
/**
 * @brief Number of virtual locks used to protect unaligned MRAM accesses
 */
#define __MRAM_UNALIGNED_ACCESS_LOG_NB_VLOCK 11
#endif

/**
 * @brief virtual mutexes structure to protect unaligned MRAM accesses
 */
extern struct vmutex __mram_unaligned_access_virtual_locks;

/**
 * @brief Used to store MRAM 8 byte word before write.
 * One buffer per tasklet of 8 bytes.
 */
extern __dma_aligned uint8_t __mram_unaligned_access_buffer[DPU_NR_THREADS << 3];

/**
 * @brief Stores the specified number of bytes from MRAM to WRAM.
 * The number of bytes must be at most 2048.
 * The MRAM address needs not be a multiple of 8.
 * The number of bytes needs not be a multiple of 8.
 * Compared to the mram_read function, mram_read_unaligned handles the cases of misalignment, but with an additional cost.
 * If misaligned reads can be avoided, the mram_read function should be preferred.
 *
 * @param from source address in MRAM
 * @param buffer address of the buffer in WRAM
 * @param nb_of_bytes number of bytes to transfer
 * @warning Pre-condition: the buffer needs to be able to hold (nb_of_bytes + 16) bytes
 *
 * @return pointer to the first loaded element in WRAM
 */
void *
mram_read_unaligned(const __mram_ptr void *from, void *buffer, unsigned int nb_of_bytes);

/**
 * @brief Stores the specified number of bytes from WRAM to MRAM.
 * The number of bytes must be at most 2048.
 * The MRAM address needs not be a multiple of 8.
 * The number of bytes needs not be a multiple of 8.
 * The WRAM address must have the same alignment than the MRAM address
 * Compared to the mram_write function, mram_write_unaligned handles the cases of misalignment, but with an additional cost.
 * If misaligned writes can be avoided, the mram_write function should be prefered.
 *
 * @param from source address in WRAM
 * @param dest destination address in MRAM
 * @param nb_of_bytes number of bytes to transfer
 */
void
mram_write_unaligned(const void *from, __mram_ptr void *dest, unsigned nb_of_bytes);

/**
 * @brief update an integer in MRAM atomically (i.e., multi-tasklet safe)
 * @hideinitializer
 * @param dest the integer address in MRAM
 * @param update_func the pointer to the update function
 * @param args a void* pointer, context passed to the update function
 */
#define mram_update_int_atomic(dest, update_func, args)                                                                          \
                                                                                                                                 \
    do {                                                                                                                         \
        uintptr_t __mram_update_int_atomic_hash                                                                                  \
            = (((uintptr_t)(dest) >> 3) & ((1 << __MRAM_UNALIGNED_ACCESS_LOG_NB_VLOCK) - 1));                                    \
        uintptr_t __mram_update_int_atomic_dest_low = (((uintptr_t)(dest) >> 3) << 3);                                           \
        vmutex_lock(&__mram_unaligned_access_virtual_locks, __mram_update_int_atomic_hash);                                      \
        mram_read(((__mram_ptr void *)(__mram_update_int_atomic_dest_low)), &__mram_unaligned_access_buffer[me() << 3], 8);      \
        (*update_func)((int *)(&__mram_unaligned_access_buffer[me() << 3]                                                        \
                           + ((__mram_update_int_atomic_dest_low != (uintptr_t)(dest)) << 2)),                                   \
            args);                                                                                                               \
        mram_write(&__mram_unaligned_access_buffer[me() << 3], ((__mram_ptr void *)(__mram_update_int_atomic_dest_low)), 8);     \
        vmutex_unlock(&__mram_unaligned_access_virtual_locks, __mram_update_int_atomic_hash);                                    \
    } while (0)

/**
 * @brief write an integer in MRAM atomically (i.e., multi-tasklet safe)
 * @hideinitializer
 * @param dest the integer address in MRAM
 * @param val the new integer value
 */
#define mram_write_int_atomic(dest, val)                                                                                         \
                                                                                                                                 \
    do {                                                                                                                         \
        uintptr_t __mram_write_int_atomic_hash = (((uintptr_t)(dest) >> 3) & ((1 << __MRAM_UNALIGNED_ACCESS_LOG_NB_VLOCK) - 1)); \
        uintptr_t __mram_write_int_atomic_dest_low = (((uintptr_t)(dest) >> 3) << 3);                                            \
        vmutex_lock(&__mram_unaligned_access_virtual_locks, __mram_write_int_atomic_hash);                                       \
        mram_read(((__mram_ptr void *)(__mram_write_int_atomic_dest_low)), &__mram_unaligned_access_buffer[me() << 3], 8);       \
        *((int *)(&__mram_unaligned_access_buffer[me() << 3] + ((__mram_write_int_atomic_dest_low != (uintptr_t)(dest)) << 2)))  \
            = val;                                                                                                               \
        mram_write(&__mram_unaligned_access_buffer[me() << 3], ((__mram_ptr void *)(__mram_write_int_atomic_dest_low)), 8);      \
        vmutex_unlock(&__mram_unaligned_access_virtual_locks, __mram_write_int_atomic_hash);                                     \
    } while (0)

/**
 * @brief update a byte in MRAM atomically (i.e., multi-tasklet safe)
 * @hideinitializer
 * @param dest the byte address in MRAM
 * @param update_func the pointer to the update function
 * @param args a void* pointer, context passed to the update function
 */
#define mram_update_byte_atomic(dest, update_func, args)                                                                         \
                                                                                                                                 \
    do {                                                                                                                         \
        uintptr_t __mram_update_byte_atomic_hash                                                                                 \
            = (((uintptr_t)(dest) >> 3) & ((1 << __MRAM_UNALIGNED_ACCESS_LOG_NB_VLOCK) - 1));                                    \
        uintptr_t __mram_update_byte_atomic_dest_low = (((uintptr_t)(dest) >> 3) << 3);                                          \
        vmutex_lock(&__mram_unaligned_access_virtual_locks, __mram_update_byte_atomic_hash);                                     \
        mram_read(((__mram_ptr void *)(__mram_update_byte_atomic_dest_low)), &__mram_unaligned_access_buffer[me() << 3], 8);     \
        (*update_func)(                                                                                                          \
            (uint8_t *)(&__mram_unaligned_access_buffer[me() << 3] + (uintptr_t)(dest)-__mram_update_byte_atomic_dest_low),      \
            args);                                                                                                               \
        mram_write(&__mram_unaligned_access_buffer[me() << 3], ((__mram_ptr void *)(__mram_update_byte_atomic_dest_low)), 8);    \
        vmutex_unlock(&__mram_unaligned_access_virtual_locks, __mram_update_byte_atomic_hash);                                   \
    } while (0)

/**
 * @brief write a byte in MRAM atomically (i.e., multi-tasklet safe)
 * @hideinitializer
 * @param dest the byte address in MRAM
 * @param val the new byte value
 */
#define mram_write_byte_atomic(dest, val)                                                                                        \
                                                                                                                                 \
    do {                                                                                                                         \
        uintptr_t __mram_write_byte_atomic_hash                                                                                  \
            = (((uintptr_t)(dest) >> 3) & ((1 << __MRAM_UNALIGNED_ACCESS_LOG_NB_VLOCK) - 1));                                    \
        uintptr_t __mram_write_byte_atomic_dest_low = (((uintptr_t)(dest) >> 3) << 3);                                           \
        vmutex_lock(&__mram_unaligned_access_virtual_locks, __mram_write_byte_atomic_hash);                                      \
        mram_read(((__mram_ptr void *)(__mram_write_byte_atomic_dest_low)), &__mram_unaligned_access_buffer[me() * 8], 8);       \
        *((uint8_t *)(&__mram_unaligned_access_buffer[me() << 3] + (uintptr_t)(dest)-__mram_write_byte_atomic_dest_low)) = val;  \
        mram_write(&__mram_unaligned_access_buffer[me() << 3], ((__mram_ptr void *)(__mram_write_byte_atomic_dest_low)), 8);     \
        vmutex_unlock(&__mram_unaligned_access_virtual_locks, __mram_write_byte_atomic_hash);                                    \
    } while (0)

#endif
