/* Pivot using multiple DPUs */
/* Showcases the use of scatter/gather transfers when the output arrays don't
 * align. */
/* Distributes a collection of integers in MRAM, */
/* requests the DPUs to perform a pivot, */
/* then aggregates the results. */

#include <assert.h>
#include <dpu.h>

#ifndef DPU_BINARY
#define DPU_BINARY "pivot_example"
#endif

/* Size of the buffer that we want to pivot: 1KByte. */
#define BUFFER_SIZE (1 << 8)

/* Number of DPUs to use. */
#define NB_DPUS 4

/* Range of the random numbers. */
#define MAX_RANDOM 1000

/* Number of blocks */
#define NB_BLOCKS 2 // we're going to partition the buffer in 2 blocks

/* Fill a buffer with random numbers between 1 and MAX_RANDOM. */
void fill_random_buffer(int* buffer, size_t size) {
    srand(42);
    for (size_t i = 0; i < size; i++) {
        buffer[i] = rand() % MAX_RANDOM + 1;
    }
}

void* bidimensional_malloc(size_t x, size_t y, size_t type_size) {
    void** array = malloc(x * sizeof(void*));
    for (size_t i = 0; i < x; i++) {
        array[i] = malloc(y * type_size);
    }
    return array;
}

void bidimensional_free(void* array, size_t x) {
    for (size_t i = 0; i < x; i++) {
        free(((void**)array)[i]);
    }
    free(array);
}

void populate_mram(struct dpu_set_t set, int* buffer) {
    struct dpu_set_t dpu;
    uint32_t each_dpu;

    /* Distribute the buffer across the DPUs. */
    DPU_FOREACH(set, dpu, each_dpu) {
        DPU_ASSERT(dpu_prepare_xfer(
                dpu, &buffer[each_dpu * BUFFER_SIZE / NB_DPUS]));
    }
    DPU_ASSERT(dpu_push_xfer(
            set,
            DPU_XFER_TO_DPU,
            "buffer",
            0,
            BUFFER_SIZE / NB_DPUS * sizeof(int),
            DPU_XFER_DEFAULT));
}

size_t** get_metadata(struct dpu_set_t set, uint32_t nr_dpus) {
    struct dpu_set_t dpu;
    uint32_t each_dpu;
    size_t** metadata = bidimensional_malloc(nr_dpus, 2, sizeof(size_t));

    DPU_FOREACH(set, dpu, each_dpu) {
        DPU_ASSERT(dpu_prepare_xfer(dpu, metadata[each_dpu]));
    }
    DPU_ASSERT(dpu_push_xfer(
            set,
            DPU_XFER_FROM_DPU,
            "metadata",
            0,
            sizeof(**metadata) * 2,
            DPU_XFER_DEFAULT));

    return metadata;
}

size_t get_left_length(size_t** metadata) {
    size_t lower_length = 0;
    for (int i = 0; i < NB_DPUS; i++) {
        lower_length += metadata[i][0];
    }

    return lower_length;
}

/* Compute the addresses of inbound blocks in the output buffer. */
void compute_block_addresses(
        size_t** metadata,          /* [in] array of block lengths */
        uint8_t*** block_addresses, /* [out] indexes to store the blocks */
        int* out_buffer,            /* [in] output buffer */
        size_t lower_length         /* [in] length of the lower partition */
) {
    block_addresses[0][0] = (uint8_t*)out_buffer;
    block_addresses[0][1] = (uint8_t*)&out_buffer[lower_length];

    for (int i = 1; i < NB_DPUS; i++) {
        for (int j = 0; j < NB_BLOCKS; j++) {
            size_t previous_length = metadata[i - 1][j] * sizeof(*out_buffer);
            block_addresses[i][j] = block_addresses[i - 1][j] + previous_length;
        }
    }
}

/* User structure that stores the get_block function arguments */
typedef struct sg_xfer_context {
    size_t** metadata;          /* [in] array of block lengths */
    uint8_t*** block_addresses; /* [in] indexes to store the next block */
} sg_xfer_context;

/* Callback function that returns the block information for a given DPU and
 * block index. */
bool get_block(
        struct sg_block_info* out,
        uint32_t dpu_index,
        uint32_t block_index,
        void* args) {
    if (block_index >= NB_BLOCKS) {
        return false;
    }

    /* Unpack the arguments */
    sg_xfer_context* sc_args = (sg_xfer_context*)args;
    size_t** metadata = sc_args->metadata;
    size_t length = metadata[dpu_index][block_index];
    uint8_t*** block_addresses = sc_args->block_addresses;

    /* Set the output block */
    out->length = length * sizeof(int);
    out->addr = block_addresses[dpu_index][block_index];

    return true;
}

/* Validate the partition. */
void validate_partition(int pivot, const int* buffer, size_t lower_length) {
    for (size_t i = 0; i < lower_length; i++) {
        assert(buffer[i] <= pivot);
    }
    for (size_t i = lower_length; i < BUFFER_SIZE; i++) {
        assert(buffer[i] > pivot);
    }
    printf("Succeeded with lower_length = %zu\n", lower_length);
}

int main() {
    struct dpu_set_t set;

    /* Generate random data */
    int buffer[BUFFER_SIZE];
    fill_random_buffer(buffer, BUFFER_SIZE);

    int pivot = MAX_RANDOM / 2;

    /* Initialize and run */
    DPU_ASSERT(dpu_alloc(NB_DPUS, "sgXferEnable=true", &set));
    DPU_ASSERT(dpu_load(set, DPU_BINARY, NULL));
    populate_mram(set, buffer);
    dpu_broadcast_to(set, "pivot", 0, &pivot, sizeof(pivot), DPU_XFER_DEFAULT);

    DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));

    /* Retrieve metadata and compute length of the left partition. */
    size_t** metadata = get_metadata(set, NB_DPUS);
    size_t lower_length = get_left_length(metadata);

    /* Compute where to store the incoming blocks. */
    uint8_t*** block_addresses =
            bidimensional_malloc(NB_DPUS, NB_BLOCKS, sizeof(uint8_t*));
    compute_block_addresses(metadata, block_addresses, buffer, lower_length);

    /* Retrieve the result. */
    sg_xfer_context sc_args = {
            .metadata = metadata, .block_addresses = block_addresses};
    get_block_t get_block_info = {
            .f = &get_block, .args = &sc_args, .args_size = sizeof(sc_args)};

    DPU_ASSERT(dpu_push_sg_xfer(
            set,
            DPU_XFER_FROM_DPU,
            "buffer",
            0,
            BUFFER_SIZE / NB_DPUS * sizeof(int),
            &get_block_info,
            DPU_SG_XFER_DEFAULT));

    /* Validate the results. */
    validate_partition(pivot, buffer, lower_length);

    bidimensional_free(metadata, NB_DPUS);
    bidimensional_free(block_addresses, NB_DPUS);
    DPU_ASSERT(dpu_free(set));
    return 0;
}