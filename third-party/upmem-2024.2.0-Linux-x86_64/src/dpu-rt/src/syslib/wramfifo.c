#include <wramfifo.h>
#include <built_ins.h>
#include <string.h>
#include <defs.h>
#include <stdio.h>

// return true if the input fifo is empty
// this is the case when the read and write pointers are equal
bool
is_input_fifo_empty(struct dpu_input_fifo_t *fifo)
{

    return fifo->read_ptr == fifo->write_ptr;
}

uint8_t *
input_fifo_peek(struct dpu_input_fifo_t *fifo)
{

    return &(fifo->data[MASK_WRAM_FIFO_PTR(fifo->ptr_size, fifo->read_ptr) * fifo->data_size]);
}

void
input_fifo_pop(struct dpu_input_fifo_t *fifo)
{

    fifo->read_ptr++;
}

// return true if the output fifo is full
// this is the case when the read and write pointers are equal
// on the FIFO_PTR_SIZE first bits and different otherwise
bool
is_output_fifo_full(struct dpu_output_fifo_t *fifo)
{
    // Note: fifo read pointer can be written by the host asynchronously
    // Make sure the value is consistent in both comparisons by reading it once
    // in a local variable
    // use a memory barrier to make sure that only one read of fifo->read_ptr is done
    // otherwise this would break the synchronization
    uint64_t rd_ptr = fifo->read_ptr;
    uint64_t wr_ptr = fifo->write_ptr + fifo->nb_reserved;
    asm volatile("" ::: "memory");
    return (MASK_WRAM_FIFO_PTR(fifo->ptr_size, rd_ptr) == MASK_WRAM_FIFO_PTR(fifo->ptr_size, wr_ptr)) && (rd_ptr != wr_ptr);
}

void
output_fifo_push(struct dpu_output_fifo_t *fifo, const uint8_t *data)
{

    memcpy((void *)&(fifo->data[MASK_WRAM_FIFO_PTR(fifo->ptr_size, fifo->write_ptr) * fifo->data_size]),
        (void *)data,
        fifo->data_size);

    // Enforce that the memcpy is finished before updating the fifo write pointer
    // The host should only see valid data in the output fifo
    asm volatile("" ::: "memory");

    fifo->write_ptr++;
}

void
output_fifo_reserve(struct dpu_output_fifo_t *fifo)
{
    fifo->nb_reserved++;
}

void
output_fifo_cede(struct dpu_output_fifo_t *fifo)
{
    fifo->nb_reserved--;
}

void
process_inputs_all_tasklets(struct dpu_input_fifo_t *input_fifo,
    struct dpu_output_fifo_t *output_fifo,
    void (*process_input)(uint8_t *, void *),
    void (*reduce)(uint8_t *, uint8_t *, void *),
    void *ctx,
    barrier_t *barrier,
    volatile uint64_t *active)
{

    // loop until the host sets the loop variable to false
    while (true) {

        if (!is_input_fifo_empty(input_fifo)) {
            if (!is_output_fifo_full(output_fifo)) {

                // after this barrier all tasklets are ready for handling a new input
                barrier_wait(barrier);

                // some data is ready to be processed
                uint8_t *input_data = input_fifo_peek(input_fifo);

                // here we are ready to process the data
                (*process_input)(input_data, ctx);

                // wait for all tasklets to finish
                barrier_wait(barrier);

                // tasklet 0 fills the output fifo and update the fifo pointers
                if (me() == 0) {

                    // TODO this part of code is exclusive to tasklet 0
                    // Hence can maybe avoid the copy and write directly into
                    // the output fifo
                    (*reduce)(input_data, output_fifo->tmp_data, ctx);
                    output_fifo_push(output_fifo, output_fifo->tmp_data);
                    input_fifo_pop(input_fifo);
                }

                // wait for for tasklet 0 update
                barrier_wait(barrier);
            }
        } else if (!*active) {
            // when the input fifo is empty
            // stop if the active condition is false
            // i.e., stop waiting for new inputs
            break;
        }
    }
}

void
process_inputs_each_tasklet(struct dpu_input_fifo_t *input_fifo,
    struct dpu_output_fifo_t *output_fifo,
    void (*process_input)(uint8_t *, uint8_t *),
    mutex_id_t mutex,
    volatile uint64_t *active)
{

    // loop until the host sets the loop variable to false
    while (true) {

        if (!is_input_fifo_empty(input_fifo)) {

            if (!is_output_fifo_full(output_fifo)) {

                mutex_lock(mutex);

                if (!is_input_fifo_empty(input_fifo) && !is_output_fifo_full(output_fifo)) {

                    // some data is ready to be processed
                    // assign next data in the FIFO to this tasklet and
                    // release the mutex
                    // copy the input data in the local buffer
                    // TODO see if having the copy in critical section is a perf issue
                    memcpy(
                        &input_fifo->tmp_data[me() * input_fifo->data_size], input_fifo_peek(input_fifo), input_fifo->data_size);
                    // pop the input from the queue
                    input_fifo_pop(input_fifo);
                    output_fifo_reserve(output_fifo);
                    mutex_unlock(mutex);

                    // here we are ready to process the data
                    (*process_input)(&input_fifo->tmp_data[me() * input_fifo->data_size],
                        &output_fifo->tmp_data[me() * output_fifo->data_size]);

                    // push the output data in the output fifo
                    mutex_lock(mutex);
                    output_fifo_push(output_fifo, &output_fifo->tmp_data[me() * output_fifo->data_size]);
                    output_fifo_cede(output_fifo);
                    mutex_unlock(mutex);
                } else {
                    mutex_unlock(mutex);
                }
            }
        } else if (!*active) {
            // when the input fifo is empty
            // stop if the active condition is false
            // i.e., stop waiting for new inputs
            break;
        }
    }
}
