/* Copyright 2022 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef DPUSYSCORE_WRAMFIFO_H
#define DPUSYSCORE_WRAMFIFO_H

/**
 * @file wramfifo.h
 * @brief WRAM input/output fifo for host/DPU communication.
 *
 * WRAM fifos can be used to communicate data to/from the DPUs
 * while they are executing.
 * This allows to parallelize computation and communication.
 * The host sends inputs in the DPU input fifo, and retrieves
 * results from the output fifo.
 * The DPU takes the next input in the fifo, computes a result,
 * and push it to the output fifo.
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include <barrier.h>
#include <mutex.h>

/// @cond INTERNAL
#define WRAM_FIFO_SIZE(FIFO_PTR_SIZE) (1 << FIFO_PTR_SIZE)
#define MASK_WRAM_FIFO_PTR(FIFO_PTR_SIZE, x) (x & ((1U << FIFO_PTR_SIZE) - 1))
#define __get_fifo_data_name(fifo_name) __CONCAT(__CONCAT(__, fifo_name), _data)
#define __get_fifo_tmp_data_name(fifo_name) __CONCAT(__CONCAT(__, fifo_name), _tmp_data)
/// @endcond

/**
 * @struct dpu_input_fifo_t
 * @brief An input fifo to get data from the host to the DPU
 */
struct dpu_input_fifo_t {
    uint64_t read_ptr; /*!< read pointer, owned by the DPU, read-only for the host */
    volatile uint64_t write_ptr; /*!< write pointer, owned by the host, read-only for the DPU */
    uint8_t *data; /*!< data array to store fifo elements */
    uint8_t *tmp_data; /*!< temporary array used by specific DPU APIs */
    const uint32_t ptr_size; /*!< fifo pointer size in bits. Number of elements is 2 ^ ptr_size */
    const uint32_t data_size; /*!< fifo element size in bytes */
};

/**
 * @brief initialize a dpu_input_fifo_t structure
 * @hideinitializer
 * @param name the name of the fifo
 * @param INPUT_FIFO_PTR_SIZE the size of the fifo pointer in bits
 * @param INPUT_FIFO_DATA_SIZE the size of a fifo element in bytes
 */
#define INPUT_FIFO_INIT(name, INPUT_FIFO_PTR_SIZE, INPUT_FIFO_DATA_SIZE)                                                         \
    _Static_assert(INPUT_FIFO_PTR_SIZE <= 10, "wram fifo error: invalid input pointer size defined, should be lower than 10");   \
    _Static_assert(INPUT_FIFO_DATA_SIZE && (INPUT_FIFO_DATA_SIZE & 7) == 0,                                                      \
        "wram fifo error: invalid input data size defined, should be strictly positive and a multiple of 8");                    \
    __host uint8_t __get_fifo_data_name(name)[(1 << INPUT_FIFO_PTR_SIZE) * INPUT_FIFO_DATA_SIZE];                                \
    __dma_aligned uint8_t __get_fifo_tmp_data_name(name)[NR_TASKLETS * INPUT_FIFO_DATA_SIZE];                                    \
    __host struct dpu_input_fifo_t name                                                                                          \
        = { 0, 0, __get_fifo_data_name(name), __get_fifo_tmp_data_name(name), INPUT_FIFO_PTR_SIZE, INPUT_FIFO_DATA_SIZE };

/**
 * @struct dpu_output_fifo_t
 * @brief An output fifo to send data from the DPU to the host
 */
struct dpu_output_fifo_t {
    volatile uint64_t read_ptr; /*!< read pointer, owned by the host, read-only for the DPU */
    uint64_t write_ptr; /*!< write pointer, owned by the DPU, read-only for the host */
    uint8_t *data; /*!< data array to store fifo elements */
    uint8_t *tmp_data; /*!< temporary array used by specific DPU APIs */
    const uint32_t ptr_size; /*!< fifo pointer size in bits. Number of elements is 2 ^ ptr_size */
    const uint32_t data_size; /*!< fifo element size in bytes */
    uint32_t nb_reserved; /*!< number of elements currently reserved by tasklets */
};

/**
 * @brief initialize a dpu_output_fifo_t structure
 * @hideinitializer
 * @param name the name of the fifo
 * @param OUTPUT_FIFO_PTR_SIZE the size of the fifo pointer in bits
 * @param OUTPUT_FIFO_DATA_SIZE the size of a fifo element in bytes
 */
#define OUTPUT_FIFO_INIT(name, OUTPUT_FIFO_PTR_SIZE, OUTPUT_FIFO_DATA_SIZE)                                                      \
    _Static_assert(OUTPUT_FIFO_PTR_SIZE <= 10, "wram fifo error: invalid output pointer size defined, should be lower than 10"); \
    _Static_assert(OUTPUT_FIFO_DATA_SIZE && (OUTPUT_FIFO_DATA_SIZE & 7) == 0,                                                    \
        "wram fifo error: invalid output data size defined, should be strictly positive and a multiple of 8");                   \
    __host uint8_t __get_fifo_data_name(name)[(1 << OUTPUT_FIFO_PTR_SIZE) * OUTPUT_FIFO_DATA_SIZE];                              \
    __dma_aligned uint8_t __get_fifo_tmp_data_name(name)[NR_TASKLETS * OUTPUT_FIFO_DATA_SIZE];                                   \
    __host struct dpu_output_fifo_t name                                                                                         \
        = { 0, 0, __get_fifo_data_name(name), __get_fifo_tmp_data_name(name), OUTPUT_FIFO_PTR_SIZE, OUTPUT_FIFO_DATA_SIZE, 0 };

/**
 * @return true if the input fifo is empty
 * @param fifo the dpu input fifo
 */
bool
is_input_fifo_empty(struct dpu_input_fifo_t *fifo);

/**
 * @return the first element of the input fifo
 * @param fifo the dpu input fifo
 */
uint8_t *
input_fifo_peek(struct dpu_input_fifo_t *fifo);

/**
 * @brief pops the first element of the fifo
 * @param fifo the dpu input fifo
 */
void
input_fifo_pop(struct dpu_input_fifo_t *fifo);

/**
 * @return true if the output fifo is full
 * @param fifo the dpu output fifo
 */
bool
is_output_fifo_full(struct dpu_output_fifo_t *fifo);

/**
 * @brief pushes a new element in the fifo
 * @param fifo the dpu input fifo
 * @param data the element to be pushed
 */
void
output_fifo_push(struct dpu_output_fifo_t *fifo, const uint8_t *data);

/**
 * @brief handler to continously process inputs from an input fifo
 * and store results in an output fifo.
 * Each fifo input is handled by all tasklets in parallel.
 * @param input_fifo the DPU input fifo
 * @param output_fifo the DPU output fifo
 * @param process_input function to process an input fifo element, called by each tasklet in parallel
 * @param reduce function to generate the output associated with the input, called by tasklet 0 only
 * @param ctx a context passed to the process_input and reduce functions
 * @param barrier a barrier needed to synchronize tasklets
 * @param active when set to zero, the tasklets will stop executing as soon as the input fifo is empty
 */
void
process_inputs_all_tasklets(struct dpu_input_fifo_t *input_fifo,
    struct dpu_output_fifo_t *output_fifo,
    void (*process_input)(uint8_t *, void *),
    void (*reduce)(uint8_t *, uint8_t *, void *),
    void *ctx,
    barrier_t *barrier,
    volatile uint64_t *active);

/**
 * @brief handler to continously process inputs from an input fifo
 * and store results in an output fifo.
 * Each fifo input is handled by one tasklet.
 * @param input_fifo the DPU input fifo
 * @param output_fifo the DPU output fifo
 * @param process_input function to process an input fifo element, called by each tasklet on different inputs in parallel
 * @param mutex a mutex needed to protect access to the fifo by several tasklets
 * @param active when set to zero, the tasklets will stop executing as soon as the input fifo is empty
 */
void
process_inputs_each_tasklet(struct dpu_input_fifo_t *input_fifo,
    struct dpu_output_fifo_t *output_fifo,
    void (*process_input)(uint8_t *, uint8_t *),
    mutex_id_t mutex,
    volatile uint64_t *active);

#endif
