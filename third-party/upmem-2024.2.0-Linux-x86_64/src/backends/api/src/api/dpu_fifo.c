#include <assert.h>
#include <stdint.h>
#include "dpu_attributes.h"
#include "dpu_fifo.h"
#include "dpu_management.h"
#include "dpu_mask.h"
#include "dpu_memory.h"
#include "dpu_program.h"
#include "dpu.h"
#include "dpu_rank.h"
#include "dpu_transfer_matrix.h"
#include "dpu_types.h"
#include "dpu_log_utils.h"
#include "dpu_api_verbose.h"

#define FIFO_SIZE(FIFO) (2 << FIFO->fifo_ptr_size)
#define MASK_FIFO_PTR(x, FIFO_PTR_SIZE) ((x) & ((1U << FIFO_PTR_SIZE) - 1))

#define DPU_ID(dpu) (dpu->slice_id * DPU_MAX_NR_DPUS_PER_CI + dpu->dpu_id)

#define WRAM_MAX (0x00010000u)

/**
 * Get read pointer of the DPU FIFO
 **/
uint8_t
get_fifo_rd_ptr(struct dpu_fifo_rank_t *fifo, struct dpu_t *dpu)
{

    // first element is read pointer and second write pointer
    uint64_t ptr = fifo->dpu_fifo_pointers[DPU_ID(dpu) * 2];
    return MASK_FIFO_PTR(ptr, fifo->dpu_fifo_ptr_size);
}

/**
 * Get read pointer of the DPU FIFO (absolute value)
 **/
uint64_t
get_fifo_abs_rd_ptr(struct dpu_fifo_rank_t *fifo, struct dpu_t *dpu)
{

    return fifo->dpu_fifo_pointers[DPU_ID(dpu) * 2];
}

/**
 * Get write pointer of the DPU FIFO
 **/
uint8_t
get_fifo_wr_ptr(struct dpu_fifo_rank_t *fifo, struct dpu_t *dpu)
{

    uint64_t ptr = fifo->dpu_fifo_pointers[DPU_ID(dpu) * 2 + 1];
    return MASK_FIFO_PTR(ptr, fifo->dpu_fifo_ptr_size);
}

/**
 * Get write pointer of the DPU FIFO (absolute value)
 **/
uint64_t
get_fifo_abs_wr_ptr(struct dpu_fifo_rank_t *fifo, struct dpu_t *dpu)
{

    return fifo->dpu_fifo_pointers[DPU_ID(dpu) * 2 + 1];
}

void
set_fifo_rd_ptr(struct dpu_fifo_rank_t *fifo, struct dpu_t *dpu, uint8_t val)
{

    fifo->dpu_fifo_pointers[DPU_ID(dpu) * 2] = val;
}

void
set_fifo_wr_ptr(struct dpu_fifo_rank_t *fifo, struct dpu_t *dpu, uint8_t val)
{

    fifo->dpu_fifo_pointers[DPU_ID(dpu) * 2 + 1] = val;
}

void
swap_fifo_rd_wr_ptr(struct dpu_fifo_rank_t *fifo)
{

    for (int dpu_id = 0; dpu_id < MAX_NR_DPUS_PER_RANK; ++dpu_id) {
        uint64_t rd_ptr = fifo->dpu_fifo_pointers[dpu_id * 2];
        fifo->dpu_fifo_pointers[dpu_id * 2] = fifo->dpu_fifo_pointers[dpu_id * 2 + 1];
        fifo->dpu_fifo_pointers[dpu_id * 2 + 1] = rd_ptr;
    }
}

void
incr_fifo_wr_ptr(struct dpu_fifo_rank_t *fifo)
{

    for (int dpu_id = 0; dpu_id < MAX_NR_DPUS_PER_RANK; ++dpu_id) {
        fifo->dpu_fifo_pointers[dpu_id * 2 + 1]++;
    }
}

/**
 * return true if the FIFO of the given DPU is full
 * This happens when the masked read pointer is equal the masked write pointer
 * and the read pointer is different from the write pointer (when they are equal it
 * means the fifo is empty)
 **/
bool
is_fifo_full(struct dpu_fifo_rank_t *fifo, struct dpu_t *dpu)
{

    uint64_t rd_ptr = fifo->dpu_fifo_pointers[DPU_ID(dpu) * 2];
    uint64_t wr_ptr = fifo->dpu_fifo_pointers[DPU_ID(dpu) * 2 + 1];

    if ((MASK_FIFO_PTR(rd_ptr, fifo->dpu_fifo_ptr_size) == MASK_FIFO_PTR(wr_ptr, fifo->dpu_fifo_ptr_size))
        && (rd_ptr != wr_ptr)) {
        return true;
    }
    return false;
}

/**
 * return true if the FIFO of the given DPU is empty
 * This happens when the read pointer is equal to the write pointer
 **/
__API_SYMBOL__
bool
is_fifo_empty(struct dpu_fifo_rank_t *fifo, struct dpu_t *dpu)
{

    uint64_t rd_ptr = fifo->dpu_fifo_pointers[DPU_ID(dpu) * 2 + 1];
    uint64_t wr_ptr = fifo->dpu_fifo_pointers[DPU_ID(dpu) * 2 + 2];

    return (rd_ptr == wr_ptr);
}

__API_SYMBOL__ struct dpu_fifo_rank_t *
get_rank_fifo(struct dpu_fifo_link_t *fifo_link, struct dpu_rank_t *rank)
{
    return &(fifo_link->rank_fifos[rank->api.rank_sw_id]);
}

__API_SYMBOL__
bool
is_fifo_empty_by_link(struct dpu_fifo_link_t *fifo_link, struct dpu_set_t dpu)
{
    struct dpu_t *dpu_ = dpu_from_set(dpu);
    struct dpu_rank_t *rank = dpu_get_rank(dpu_);
    struct dpu_fifo_rank_t *fifo = &(fifo_link->rank_fifos[rank->api.rank_sw_id]);
    uint64_t rd_ptr = fifo->dpu_fifo_pointers[DPU_ID(dpu_) * 2];
    uint64_t wr_ptr = fifo->dpu_fifo_pointers[DPU_ID(dpu_) * 2 + 1];
    printf("rd_ptr = %lu, wr_ptr = %lu\n", rd_ptr, wr_ptr);

    return (rd_ptr == wr_ptr);
}

__API_SYMBOL__
uint16_t
get_fifo_size(struct dpu_fifo_link_t *fifo_link, struct dpu_set_t dpu)
{

    struct dpu_t *dpu_ = dpu_from_set(dpu);
    struct dpu_rank_t *rank = dpu_get_rank(dpu_);
    struct dpu_fifo_rank_t *fifo = &(fifo_link->rank_fifos[rank->api.rank_sw_id]);
    return fifo->dpu_fifo_pointers[DPU_ID(dpu_) * 2 + 1] - fifo->dpu_fifo_pointers[DPU_ID(dpu_) * 2];
}

uint8_t
get_fifo_elem_index(struct dpu_fifo_link_t *fifo_link, struct dpu_set_t dpu, uint8_t i)
{

    struct dpu_t *dpu_ = dpu_from_set(dpu);
    struct dpu_rank_t *rank = dpu_get_rank(dpu_);
    struct dpu_fifo_rank_t *fifo = &(fifo_link->rank_fifos[rank->api.rank_sw_id]);
    uint64_t rd_ptr = fifo->dpu_fifo_pointers[DPU_ID(dpu_) * 2];
    // Note this should not go beyond the write pointer, we could check it
    return MASK_FIFO_PTR(rd_ptr + i, fifo->dpu_fifo_ptr_size);
}

__API_SYMBOL__
uint8_t *
get_fifo_elem(struct dpu_fifo_link_t *fifo_link, struct dpu_set_t dpu, uint8_t *fifo_data, uint8_t i)
{

    struct dpu_t *dpu_ = dpu_from_set(dpu);
    struct dpu_rank_t *rank = dpu_get_rank(dpu_);
    struct dpu_fifo_rank_t *fifo = &(fifo_link->rank_fifos[rank->api.rank_sw_id]);
    uint64_t rd_ptr = fifo->dpu_fifo_pointers[DPU_ID(dpu_) * 2];
    // Note this should not go beyond the write pointer, we could check it
    uint8_t id = MASK_FIFO_PTR(rd_ptr + i, fifo->dpu_fifo_ptr_size);
    return fifo_data + id * fifo->dpu_fifo_data_size;
}

__API_SYMBOL__
uint16_t
get_fifo_max_size(struct dpu_rank_t *rank, struct dpu_fifo_rank_t *fifo)
{

    uint16_t sz = 0;
    uint8_t nr_of_dpus_per_ci = rank->description->hw.topology.nr_of_dpus_per_control_interface;
    uint8_t nr_of_cis = rank->description->hw.topology.nr_of_control_interfaces;
    for (dpu_slice_id_t each_ci = 0; each_ci < nr_of_cis; ++each_ci) {
        for (dpu_member_id_t each_dpu = 0; each_dpu < nr_of_dpus_per_ci; ++each_dpu) {
            struct dpu_t *dpu = DPU_GET_UNSAFE(rank, each_ci, each_dpu);

            if (!dpu || !dpu->enabled)
                continue;

            uint16_t dpu_sz = fifo->dpu_fifo_pointers[DPU_ID(dpu) * 2 + 1] - fifo->dpu_fifo_pointers[DPU_ID(dpu) * 2];
            if (dpu_sz > sz)
                sz = dpu_sz;
        }
    }
    return sz;
}

__API_SYMBOL__
void
dpu_fifo_set_push_max_retries(struct dpu_fifo_link_t *fifo_link, struct dpu_set_t dpu_set, uint32_t max_retries)
{

    struct dpu_set_t rank;
    DPU_RANK_FOREACH (dpu_set, rank) {

        uint32_t each_rank = dpu_rank_from_set(rank)->api.rank_sw_id;
        fifo_link->rank_fifos[each_rank].max_retries = max_retries;
    }
}

__API_SYMBOL__
void
dpu_fifo_set_time_for_push_retries(struct dpu_fifo_link_t *fifo_link, struct dpu_set_t dpu_set, uint32_t time_us)
{

    struct dpu_set_t rank;
    DPU_RANK_FOREACH (dpu_set, rank) {

        uint32_t each_rank = dpu_rank_from_set(rank)->api.rank_sw_id;
        fifo_link->rank_fifos[each_rank].time_for_retry = time_us;
    }
}

// this function is specific to the name used in the DPU RT lib
// TODO can we find a way to define this in one place only ?
static char *
get_fifo_data_name(char *fifo_name)
{

    char *fifo_data_name = malloc(strlen(fifo_name) + 30);
    fifo_data_name[0] = '\0';
    strcat(fifo_data_name, "__");
    strcat(fifo_data_name, fifo_name);
    strcat(fifo_data_name, "_data");
    return fifo_data_name;
}

static void
get_fifo_parameters(struct dpu_set_t set, uint32_t *ptr_size, uint32_t *data_size, struct dpu_symbol_t *symbol)
{

    struct dpu_set_t dpu;
    DPU_FOREACH (set, dpu) {
        // use offset based on the DPU fifo structure in dpu-rt
        dpu_mem_max_addr_t address = (symbol->address + 24) >> 2;
        dpu_copy_from_wram_for_dpu(dpu_from_set(dpu), ptr_size, address, 1);
        dpu_copy_from_wram_for_dpu(dpu_from_set(dpu), data_size, address + 1, 1);
        break;
    }
}

#define CHECK_WRAM_SYMBOL(symbol)                                                                                                \
    if ((symbol.address) >= WRAM_MAX) {                                                                                          \
        LOG_FN(WARNING, "invalid wram fifo access (address:0x%x)", 0, (symbol).address);                                         \
        return DPU_ERR_INVALID_WRAM_ACCESS;                                                                                      \
    }

// this is the maximum allowed from the DPU macro initializing a fifo in wram
#define MAX_FIFO_PTR_SIZE 10

static dpu_error_t
init_fifo(struct dpu_set_t dpu_set, struct dpu_fifo_link_t *link, char *fifo_name, bool is_input)
{
    dpu_error_t status = DPU_OK;

    // find the symbol
    struct dpu_program_t *program;
    struct dpu_symbol_t symbol, symbol_data;

    if ((status = dpu_get_common_program(&dpu_set, &program)) != DPU_OK) {
        return status;
    }

    // get fifo data array and fifo symbols
    if ((status = dpu_get_symbol(program, fifo_name, &symbol)) != DPU_OK) {
        return status;
    }
    char *fifo_data_name = get_fifo_data_name(fifo_name);
    if ((status = dpu_get_symbol(program, fifo_data_name, &symbol_data)) != DPU_OK) {
        return status;
    }
    free(fifo_data_name);

    // TODO is there a way to check that the symbol is of fifo type ?
    CHECK_WRAM_SYMBOL(symbol);
    CHECK_WRAM_SYMBOL(symbol_data);

    link->fifo_symbol = symbol;

    uint32_t fifo_ptr_size;
    uint32_t fifo_data_size;
    get_fifo_parameters(dpu_set, &fifo_ptr_size, &fifo_data_size, &symbol);
    // check fifo parameters
    if (fifo_ptr_size > MAX_FIFO_PTR_SIZE || fifo_data_size & 7) {
        LOG_FN(WARNING, "invalid wram fifo access (address:0x%x)", 0, (symbol).address);
        return DPU_ERR_INVALID_WRAM_ACCESS;
    }

    uint32_t nr_ranks;
    dpu_get_nr_ranks(dpu_set, &nr_ranks);
    link->rank_fifos = calloc(nr_ranks, sizeof(struct dpu_fifo_rank_t));

    struct dpu_set_t rank;
    DPU_RANK_FOREACH (dpu_set, rank) {

        uint32_t each_rank = dpu_rank_from_set(rank)->api.rank_sw_id;
        assert(each_rank < nr_ranks);
        // dpu_fifo_address is the address in 32bit words
        link->rank_fifos[each_rank].dpu_fifo_address = symbol_data.address >> 2;
        link->rank_fifos[each_rank].dpu_fifo_data_size = fifo_data_size;
        link->rank_fifos[each_rank].dpu_fifo_ptr_size = fifo_ptr_size;
        // max retries is 1000 by default (note: this applies only to input fifo)
        link->rank_fifos[each_rank].max_retries = 1000;
        link->rank_fifos[each_rank].time_for_retry = 10;
        dpu_transfer_matrix_clear_all(dpu_rank_from_set(rank), &(link->rank_fifos[each_rank].transfer_matrix));
        dpu_transfer_matrix_clear_all(dpu_rank_from_set(rank), &(link->rank_fifos[each_rank].fifo_pointers_matrix));
        // here transfer matrix initialization is not important, we set it to the first element in the FIFO
        // but it is expected that it will be always set before the transfer in dpu_copy_to_wram_fifo
        link->rank_fifos[each_rank].transfer_matrix.offset = link->rank_fifos[each_rank].dpu_fifo_address;
        // divide by 4 as we want to the size in 32bit words
        if (is_input)
            link->rank_fifos[each_rank].transfer_matrix.size = fifo_data_size >> 2;
        else
            link->rank_fifos[each_rank].transfer_matrix.size = (fifo_data_size << fifo_ptr_size) >> 2;
        // add for each DPU that fifo pointer matrix points to dpu_fifo_pointers array
        link->rank_fifos[each_rank].fifo_pointers_matrix.offset = symbol.address >> 2;
        // size is equal to the size of read pointer (64bit) plus size of write pointer (64bit)
        // see struct dpu_input_fifo_t in dpu-rt
        link->rank_fifos[each_rank].fifo_pointers_matrix.size = (sizeof(uint64_t) * 2) >> 2;
        struct dpu_set_t dpu;
        DPU_FOREACH (rank, dpu) {
            dpu_transfer_matrix_add_dpu(dpu_from_set(dpu),
                &(link->rank_fifos[each_rank].fifo_pointers_matrix),
                &(link->rank_fifos[each_rank].dpu_fifo_pointers[DPU_ID(dpu_from_set(dpu)) * 2]));
        }
    }

    return status;
}

__API_SYMBOL__
dpu_error_t
dpu_link_input_fifo(struct dpu_set_t dpu_set, struct dpu_fifo_link_t *link, char *fifo_name)
{
    link->direction = DPU_INPUT_FIFO;
    return init_fifo(dpu_set, link, fifo_name, true);
}

__API_SYMBOL__
dpu_error_t
dpu_link_output_fifo(struct dpu_set_t dpu_set, struct dpu_fifo_link_t *link, char *fifo_name)
{
    link->direction = DPU_OUTPUT_FIFO;
    return init_fifo(dpu_set, link, fifo_name, false);
}

__API_SYMBOL__
dpu_error_t
dpu_fifo_link_free(struct dpu_fifo_link_t *fifo_link)
{

    free(fifo_link->rank_fifos);
    return DPU_OK;
}
