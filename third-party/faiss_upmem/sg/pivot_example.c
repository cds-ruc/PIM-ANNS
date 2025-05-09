#include <mram.h>

#define DPU_BUFFER_SIZE (1 << 6)

__host int pivot;
__host uint64_t metadata[2];
__mram_noinit uint32_t buffer[DPU_BUFFER_SIZE];

int main(void) {
  /* load the data from MRAM */
  uint32_t work_buffer[DPU_BUFFER_SIZE];
  mram_read(&buffer, &work_buffer, DPU_BUFFER_SIZE * sizeof(uint32_t));

  /* perform the pivot */
  uint32_t i = 0;
  uint32_t j = DPU_BUFFER_SIZE - 1;
  while (i < j) {
    while (i < DPU_BUFFER_SIZE - 1 && work_buffer[i] <= pivot) {
      i++;
    }
    while (j > 0 && work_buffer[j] > pivot) {
      j--;
    }
    if (i < j) {
      uint32_t tmp = work_buffer[i];
      work_buffer[i] = work_buffer[j];
      work_buffer[j] = tmp;
    }
  }

  /* store the metadata */
  metadata[0] = i;                   /* length of left partition */
  metadata[1] = DPU_BUFFER_SIZE - i; /* length of right partition */

  /* store the data back to MRAM */
  mram_write(&work_buffer, &buffer, DPU_BUFFER_SIZE * sizeof(uint32_t));

  return 0;
}