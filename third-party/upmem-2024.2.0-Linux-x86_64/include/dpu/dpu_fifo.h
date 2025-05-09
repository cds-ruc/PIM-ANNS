/* Copyright 2022 UPMEM. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef DPU_FIFO_H
#define DPU_FIFO_H

#include <stdbool.h>
#include <stdint.h>

#include <dpu_error.h>
#include <dpu_transfer_matrix.h>
#include <dpu_types.h>

struct dpu_rank_t;
struct dpu_t;

/**
 * @struct dpu_fifo_rank_t
 * structure to manage a FIFO in WRAM of DPUs of a rank
 **/
struct dpu_fifo_rank_t {

    /** pointer size of the fifo in number of bits (default 7, i.e., 128 elements) */
    uint8_t dpu_fifo_ptr_size;
    /** address of the DPU FIFO (in 32bit words) */
    uint32_t dpu_fifo_address;
    /** size of one element of the DPU FIFO (in bytes) */
    uint32_t dpu_fifo_data_size;

    /** array to store the FIFO pointers (read then write) */
    uint64_t dpu_fifo_pointers[MAX_NR_DPUS_PER_RANK * 2];

    /** transfer matrix for the FIFO pointers.
     ** Contains the addresses of dpu_fifo_pointers positions */
    struct dpu_transfer_matrix fifo_pointers_matrix;

    /** transfer matrix used to transfer data to or from the WRAM FIFO
     ** this contains the addresses of the buffer that the user
     ** needs to send to the FIFO (input FIFO case) or the buffer to store
     ** the data obtained from the FIFO (output FIFO case) */
    struct dpu_transfer_matrix transfer_matrix;

    /** Max number of retries after which pushing to a full input fifo is
     ** abandonned and an error in returned */
    uint32_t max_retries;

    /** Time in us between two retries when pushing to an input fifo that is full */
    uint32_t time_for_retry;
};

/**
 * @brief Direction of the FIFO : can be input or output
 **/
enum dpu_fifo_direction_t { DPU_INPUT_FIFO, DPU_OUTPUT_FIFO };

/**
 * @struct dpu_fifo_link_t
 * structure used in the host code to
 * link to DPUs FIFOs
 **/
struct dpu_fifo_link_t {

    /** fifo direction (input or output) */
    enum dpu_fifo_direction_t direction;

    /** fifo symbol in DPU code */
    struct dpu_symbol_t fifo_symbol;

    /** array of dpu_fifo_rank_t for each rank */
    struct dpu_fifo_rank_t *rank_fifos;
};

/**
 * create a link to the DPU input FIFO
 * Used to push data to the input FIFO
 * @param dpu_set the dpu set for which to create FIFO link
 * @param[out] link the link to be created
 * @param fifo_name the name of the input fifo symbol on the DPU side
 * @return Whether or not the operation was successful (symbol must exist on DPU
 * and be an input FIFO type)
 **/
dpu_error_t
dpu_link_input_fifo(struct dpu_set_t dpu_set, struct dpu_fifo_link_t *link, char *fifo_name);

/**
 * create a link to the DPU output FIFO
 * Used to get data from the output FIFO
 * @param dpu_set the dpu set for which to create FIFO link
 * @param[out] link the link to be created
 * @param fifo_name the name of the output fifo symbol on the DPU side
 * @return Whether or not the operation was successful (symbol must exist on DPU
 * and be an output FIFO type)
 **/
dpu_error_t
dpu_link_output_fifo(struct dpu_set_t dpu_set, struct dpu_fifo_link_t *link, char *fifo_name);

/**
 * free DPU FIFO link created using dpu_link_input_fifo or dpu_link_output_fifo
 * @return Whether or not the operation was successful
 **/
dpu_error_t
dpu_fifo_link_free(struct dpu_fifo_link_t *fifo_link);

/**
 * @return the number of elements in the FIFO (as seen from the host)
 **/
uint16_t
get_fifo_size(struct dpu_fifo_link_t *fifo_link, struct dpu_set_t dpu);

uint8_t
get_fifo_elem_index(struct dpu_fifo_link_t *fifo_link, struct dpu_set_t dpu, uint8_t i);

uint8_t *
get_fifo_elem(struct dpu_fifo_link_t *fifo_link, struct dpu_set_t dpu, uint8_t *fifo_data, uint8_t i);

/**
 * @return the maximum size of the DPU fifos in this rank
 **/
uint16_t
get_fifo_max_size(struct dpu_rank_t *rank, struct dpu_fifo_rank_t *fifo);

/**
 * @return the number of elements in the FIFO (as seen from the host)
 **/
struct dpu_fifo_rank_t *
get_rank_fifo(struct dpu_fifo_link_t *fifo_link, struct dpu_rank_t *rank);

/**
 * Set the max number of retries when pushing to an input fifo
 * @param max_retries the maximum number of retries
 */
void
dpu_fifo_set_push_max_retries(struct dpu_fifo_link_t *fifo_link, struct dpu_set_t dpu_set, uint32_t max_retries);

/**
 * Set the time between retries when pushing to an input fifo
 * @param time_us the time to wait between retries in microseconds
 */
void
dpu_fifo_set_time_for_push_retries(struct dpu_fifo_link_t *fifo_link, struct dpu_set_t dpu_set, uint32_t time_us);

/**
 * Get read pointer of the DPU FIFO
 **/
uint8_t
get_fifo_rd_ptr(struct dpu_fifo_rank_t *fifo, struct dpu_t *dpu);

/**
 * Get write pointer of the DPU FIFO
 **/
uint8_t
get_fifo_wr_ptr(struct dpu_fifo_rank_t *fifo, struct dpu_t *dpu);

/**
 * Get read pointer of the DPU FIFO (absolute value)
 **/
uint64_t
get_fifo_abs_rd_ptr(struct dpu_fifo_rank_t *fifo, struct dpu_t *dpu);

/**
 * Get write pointer of the DPU FIFO (absolute value)
 **/
uint64_t
get_fifo_abs_wr_ptr(struct dpu_fifo_rank_t *fifo, struct dpu_t *dpu);

/**
 * Set read pointer of the DPU FIFO
 **/
void
set_fifo_rd_ptr(struct dpu_fifo_rank_t *fifo, struct dpu_t *dpu, uint8_t val);

/**
 * Set write pointer of the DPU FIFO
 **/
void
set_fifo_wr_ptr(struct dpu_fifo_rank_t *fifo, struct dpu_t *dpu, uint8_t val);

/**
 * Swap the read and write pointers
 **/
void
swap_fifo_rd_wr_ptr(struct dpu_fifo_rank_t *fifo);

/**
 * Increment the fifo write pointers
 **/
void
incr_fifo_wr_ptr(struct dpu_fifo_rank_t *fifo);

/**
 * return true if the FIFO of the given DPU is full
 * This happens when the masked read pointer is equal the masked write pointer
 * and the read pointer is different from the write pointer (when they are equal it
 * means the fifo is empty)
 **/
bool
is_fifo_full(struct dpu_fifo_rank_t *fifo, struct dpu_t *dpu);

/**
 * return true if the FIFO of the given DPU is empty
 * This happens when the read pointer is equal to the write pointer
 **/
bool
is_fifo_empty(struct dpu_fifo_rank_t *fifo, struct dpu_t *dpu);


bool
is_fifo_empty_by_link(struct dpu_fifo_link_t *fifo_link, struct dpu_set_t dpu);
#endif
