/* Copyright 2022 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef DPUSYSCORE_VMUTEX_H
#define DPUSYSCORE_VMUTEX_H

/**
 * @file vmutex.h
 * @brief Mutual exclusions extension (virtual mutexes).
 *
 * A mutex ensures mutual exclusion between threads: only one thread can have the mutex at a time, blocking all the
 * other threads trying to take the mutex.
 * There is a number of 56 mutexes available in the DPU with hardware support.
 * Virtual mutexes are an abstraction over hardware mutexes, to allow using more than 56 mutexes.
 * A virtual mutex is one state bit in WRAM and several virtual
 * mutexes are protected using a common physical mutex. These are beneficial in cases where many mutexes are required to
 * avoid false conflicts in mutual exclusion, since the physical mutex is held only during the update to the state bit of the
 * virtual mutex, but not during the critical section itself. Therefore, if a false conflict arises to acquire a physical mutex,
 * the penalty is 3 instructions (the number of instructions in the critical section of vmutex_lock and vmutex_unlock ).
 */

#include <stdint.h>
#include <mutex.h>
#include <assert.h>

/**
 * @struct vmutex
 * @brief structure holding virtual mutexes
 */
struct vmutex {
    uint8_t *vm; /**< virtual mutexes */
    uint8_t *hw_mutexes; /**< hardware mutexes */
    uint8_t hw_mutexes_mask; /**< mask for hardware mutexes */
};

/**
 * @brief initialize the specified number of virtual mutexes using the specified number of hardware mutexes
 * The number of virtual mutexes should be a multiple of 8
 * The number of hardware mutexes should be a power of 2
 */
#define VMUTEX_INIT(NAME, NB_VMUTEXES, NB_MUTEXES)                                                                               \
    static_assert(NB_MUTEXES > 0, "Number of hw mutexes should be at least 1.");                                                 \
    static_assert((__builtin_popcount(NB_MUTEXES)) == 1, "Number of hardware mutexes should be a power of 2");                   \
    static_assert((NB_VMUTEXES & 7) == 0, "Number of virtual mutexes should be a multiple of 8");                                \
    uint8_t __atomic_bit hw_mutexes_##NAME[NB_MUTEXES] = { 0 };                                                                  \
    uint8_t vmutex_##NAME[NB_VMUTEXES >> 3] = { 0 };                                                                             \
    struct vmutex NAME = { .vm = vmutex_##NAME, .hw_mutexes = hw_mutexes_##NAME, .hw_mutexes_mask = NB_MUTEXES - 1 };

/**
 * @brief Takes the lock on the given virtual mutex.
 * @param vm the virtual mutex structure
 * @param id the id of the virtual mutex to lock
 */
static inline void
vmutex_lock(struct vmutex *vm, uint16_t id)
{

    /* clang-format off */
  __asm__ volatile(
      "lsr_add r8, %[vm_], %[id_], 3\n"// r8 = vm->vm + (id >> 3) => byte address
      "and r9, %[id_], 7\n"
      "lsl r9, one, r9\n" // r9 = 1 << (id & 7) => the bit mask
      "lsr r10, %[id_], 3\n"
      "and r10, r10, %[nb_]\n" 
      "add r10, r10, %[hm_]\n" //r10 = vm->hm + ((id >> 3) & hw_mask) => the hw mutex id
      "0:\n"
      "acquire r10, 0, nz, 0b\n"
      "lbu r11, r8, 0\n" // r11 =  byte of vm
      "or r12, r11, r9\n" // r12 = byte after update
      "sb r8, 0, r12\n" // store byte after update
      "release r10, 0, nz, .+1\n" 
      "1:\n"
      "jeq r11, r12, 0b\n"  // if virtual mutex not acquired, loop back
       :: [vm_] "r" (vm->vm), [id_] "r" (id), [hm_] "r" (vm->hw_mutexes), [nb_] "r" (vm->hw_mutexes_mask)
       : "r8", "r9", "r10", "r11", "r12");
    /* clang-format on */
}

/**
 * @brief Releases the lock on the given virtual mutex.
 * @param vm the virtual mutex structure
 * @param id the id of the virtual mutex to unlock
 */
static inline void
vmutex_unlock(struct vmutex *vm, uint16_t id)
{

    /* clang-format off */
  __asm__ volatile(
      "lsr_add r8, %[vm_], %[id_], 3\n"// r8 = vm + (id >> 3) => byte address
      "and r9, %[id_], 7\n"
      "lsl r9, one, r9\n" 
      "xor r9, lneg, r9\n"  // r9 = 0xFF ^ (1 << (id & 7)) => the bit mask))
      "lsr r10, %[id_], 3\n"
      "and r10, r10, %[nb_]\n" 
      "add r10, r10, %[hm_]\n" //r10 = vm->hm + ((id >> 3) & hw_mask) => the hw mutex id
      "0:\n"
      "acquire r10, 0, nz, 0b\n"
      "lbu r11, r8, 0\n" // r11 =  byte of vm
      "and r12, r11, r9\n" // r12 = byte after update
      "sb r8, 0, r12\n" // store byte after update
      "release r10, 0, nz, .+1\n" 
       :: [vm_] "r" (vm->vm), [id_] "r" (id), [hm_] "r" (vm->hw_mutexes), [nb_] "r" (vm->hw_mutexes_mask)
       : "r8", "r9", "r10", "r11", "r12");
    /* clang-format on */
}

#endif
