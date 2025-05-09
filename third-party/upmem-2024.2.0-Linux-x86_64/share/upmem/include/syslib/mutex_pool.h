/* Copyright 2022 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef DPUSYSCORE_MUTEX_POOL_H
#define DPUSYSCORE_MUTEX_POOL_H

/**
 * @file mutex_pool.h
 * @brief Mutual exclusions extension (hardware mutex pool).
 *
 * A mutex ensures mutual exclusion between threads: only one thread can have the mutex at a time, blocking all the
 * other threads trying to take the mutex.
 * This file defines pools of mutexes.
 * A pool of mutexes can be used to protect a large number of elements. When provided with a specific id to lock, the
 * mutex_pool_lock function locks the hardware mutex whose id is equal to the log(N) least significant bits of the id to lock, N
 * being the number of hardware mutexes in the pool.
 */

#include <stdint.h>
#include <mutex.h>
#include <assert.h>

/**
 * @struct mutex_pool
 * @brief structure holding a pool of hardware mutexes
 */
struct mutex_pool {
    uint8_t *hw_mutexes; /**< hardware mutexes */
    uint8_t hw_mutexes_mask; /**< mask for hardware mutexes */
};

/**
 * @brief initialize a pool using a given number of hardware mutexes
 * @hideinitializer
 * The number of mutexes should be a power of 2
 */
#define MUTEX_POOL_INIT(NAME, NB_MUTEXES)                                                                                        \
    static_assert(NB_MUTEXES > 0, "Number of hw mutexes should be at least 1.");                                                 \
    static_assert((__builtin_popcount(NB_MUTEXES)) == 1, "Number of hardware mutexes should be a power of 2");                   \
    uint8_t __atomic_bit hw_mutexes_##NAME[NB_MUTEXES] = { 0 };                                                                  \
    struct mutex_pool NAME = { .hw_mutexes = hw_mutexes_##NAME, .hw_mutexes_mask = NB_MUTEXES - 1 };

/**
 * @brief Takes the lock for the given element id.
 * @param mp the mutex pool structure
 * @param id the id of the element to lock
 */
static inline void
mutex_pool_lock(struct mutex_pool *mp, uint16_t id)
{
    mutex_lock(&mp->hw_mutexes[id & mp->hw_mutexes_mask]);
}

/**
 * @brief Releases the lock for the given element id.
 * @param mp the mutex pool structure
 * @param id the id of the element to unlock
 */
static inline void
mutex_pool_unlock(struct mutex_pool *mp, uint16_t id)
{
    mutex_unlock(&mp->hw_mutexes[id & mp->hw_mutexes_mask]);
}

#endif
