
// #include <cstdint>
#include <dpu.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
// #include <string>

#include "../common/common.h"

#ifndef DPU_BINARY
#define DPU_BINARY "build/checksum_dpu"
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 全局测试数据
// uint8_t data[SLOT_L * MY_PQ_M];
// int64_t data_id[SLOT_L];
// int32_t LUT[MY_PQ_M * MY_PQ_CLUSTER];

uint8_t *data = NULL;
int64_t *data_id = NULL;
int32_t *LUT = NULL;

int32_t part_dis[SLOT_L];
int32_t result_v[MAX_K];
int64_t result_id[MAX_K];

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

// 假设这些宏已经被正确定义
// #define MY_PQ_M 10
// #define MY_PQ_CLUSTER 100
// int32_t lut_array[MY_PQ_M * MY_PQ_CLUSTER];

void analyze_by_groups(int32_t *lut_array)
{
    size_t cluster_size = MY_PQ_CLUSTER;

    printf("\n[Group-wise Analysis]\n");
    for (size_t g = 0; g < MY_PQ_M; ++g) {
        int32_t group_max = INT32_MIN;
        int32_t group_min = INT32_MAX;
        long long group_sum = 0;

        size_t group_start = g * cluster_size;
        for (size_t i = 0; i < cluster_size; ++i) {
            int32_t val = lut_array[group_start + i];
            if (val > group_max)
                group_max = val;
            if (val < group_min)
                group_min = val;
            group_sum += val;
        }

        for (size_t i = 0; i < cluster_size; ++i) {

            lut_array[group_start + i] -= group_min;
        }

        double group_avg = (double)group_sum / cluster_size;

        printf("Group %3zu: Max=%5d, Min=%5d, Avg=%.2f\n", g, group_max, group_min, group_avg);
    }
}

static int compare_int32(const void *a, const void *b)
{
    int32_t arg1 = *(const int32_t *)a;
    int32_t arg2 = *(const int32_t *)b;
    if (arg1 < arg2)
        return -1;
    if (arg1 > arg2)
        return 1;
    return 0;
}

void analyze_lut(int32_t *lut_array)
{
    size_t total_elements = MY_PQ_M * MY_PQ_CLUSTER;
    int32_t max_val = INT32_MIN;
    int32_t min_val = INT32_MAX;
    long long sum = 0;

    // 用于中位数计算的临时数组
    int32_t *sorted_array = (int32_t *)malloc(total_elements * sizeof(int32_t));
    if (!sorted_array) {
        perror("Failed to allocate memory for sorting");
        return;
    }

    // 第一次遍历：计算 max, min, sum，并拷贝数组用于排序
    for (size_t i = 0; i < total_elements; ++i) {
        int32_t val = lut_array[i];
        if (val > max_val)
            max_val = val;
        if (val < min_val)
            min_val = val;
        sum += val;
        sorted_array[i] = val;
    }

    double average = (double)sum / total_elements;

    // ✅ 使用正确的比较函数，而不是 lambda
    qsort(sorted_array, total_elements, sizeof(int32_t), compare_int32);

    // 计算中位数
    double median;
    if (total_elements % 2 == 0) {
        median = (sorted_array[total_elements / 2 - 1] + sorted_array[total_elements / 2]) / 2.0;
    } else {
        median = sorted_array[total_elements / 2];
    }

    printf("[Overall] Max: %d, Min: %d, Avg: %.2f, Median: %.2f\n", max_val, min_val, average, median);

    free(sorted_array);

    // 接下来可以继续按组分析，如之前所说
    analyze_by_groups(lut_array);
}

#include <assert.h>

// 函数：从二进制文件中读取一个 LUT
// 参数：
//   lut_path: LUT 文件路径，比如 "/home/.../lut.bin"
//   lut_array: 输出参数，用于接收读取到的 LUT 数据（必须提前分配内存！）
// 返回值：
//   0 表示成功，-1 表示失败（如文件打开失败或读取失败）
int read_lut_from_file(const char *lut_path, int32_t *lut_array)
{
    FILE *file = fopen(lut_path, "rb"); // 以二进制只读模式打开
    if (file == NULL) {
        perror("Error: Unable to open LUT file");
        return -1;
    }

    size_t elements_to_read = MY_PQ_M * MY_PQ_CLUSTER;
    size_t read_count = fread(lut_array, sizeof(int32_t), elements_to_read, file);

    fclose(file);

    if (read_count != elements_to_read) {
        fprintf(stderr, "Error: Failed to read full LUT. Expected %zu elements, but read %zu.\n", elements_to_read, read_count);
        return -1;
    }

    printf("Successfully read LUT from %s\n", lut_path);
    return 0; // 成功
}
int read_data_from_file(const char *data_path, uint8_t *data_array)
{
    FILE *file = fopen(data_path, "rb");
    if (file == NULL) {
        perror("Error: Unable to open data.bin file");
        return -1;
    }

    assert(SLOT_L == 100000); // 仅支持100000测试

    size_t expected_elements = SLOT_L * MY_PQ_M;
    size_t read_count = fread(data_array, sizeof(uint8_t), expected_elements, file);

    fclose(file);

    if (read_count != expected_elements) {
        fprintf(stderr, "Error: Failed to read full data. Expected %zu elements, got %zu.\n", expected_elements, read_count);
        return -1;
    }

    printf("Successfully read data from %s\n", data_path);
    return 0;
}

int read_ids_from_file(const char *id_path, int64_t *ids_array)
{
    FILE *file = fopen(id_path, "rb");
    if (file == NULL) {
        perror("Error: Unable to open id.bin file");
        return -1;
    }

    size_t expected_elements = SLOT_L;
    size_t read_count = fread(ids_array, sizeof(int64_t), expected_elements, file);

    fclose(file);

    if (read_count != expected_elements) {
        fprintf(stderr, "Error: Failed to read full IDs. Expected %zu elements, got %zu.\n", expected_elements, read_count);
        return -1;
    }

    printf("Successfully read IDs from %s\n", id_path);
    return 0;
}

// 生成测试文件
void create_test_file()
{

    data = (uint8_t *)malloc(SLOT_L * MY_PQ_M * sizeof(uint8_t));
    data_id = (int64_t *)malloc(SLOT_L * sizeof(int64_t));
    LUT = (int32_t *)malloc(MY_PQ_M * MY_PQ_CLUSTER * sizeof(int32_t));

    printf("Generating test data...\n");

    srand(0); // 固定种子确保结果可重现

    // 生成data数组 (SLOT_L * MY_PQ_M 个 uint8_t)
    for (int i = 0; i < SLOT_L * MY_PQ_M; i++) {
        data[i] = (uint8_t)(rand() % 256); // 生成0-255的随机字节
    }

    // 生成data_id数组 (SLOT_L 个 int64_t)
    for (int i = 0; i < SLOT_L; i++) {
        data_id[i] = (int64_t)i; // 可以用连续ID或随机ID
        // 或者使用随机ID: data_id[i] = (int64_t)(rand() % 1000000);
    }

    // 生成LUT查找表 (MY_PQ_M * MY_PQ_CLUSTER 个 int32_t)
    for (int i = 0; i < MY_PQ_M * MY_PQ_CLUSTER; i++) {
        // LUT通常包含距离值，这里生成一些测试用的距离
        LUT[i] = (int32_t)(rand() % 1000); // 生成0-999的随机距离
    }

    printf("Test file created:\n");
    printf("  - data: %d bytes\n", SLOT_L * MY_PQ_M);
    printf("  - data_id: %d entries\n", SLOT_L);
    printf("  - LUT: %d entries\n", MY_PQ_M * MY_PQ_CLUSTER);
}

// PQ算法计算函数
void pq_search()
{
    // 初始化结果数组
    for (int i = 0; i < MAX_K; i++) {
        result_v[i] = INT32_MAX; // 初始化为最大距离
        result_id[i] = -1; // 初始化为无效ID
    }

    // 对每个数据向量计算距离
    for (int i = 0; i < SLOT_L; i++) {
        int32_t total_distance = 0;

        // 计算PQ距离：对每个子空间查表并累加
        for (int m = 0; m < MY_PQ_M; m++) {
            // 获取当前向量在当前子空间的编码值
            uint8_t code = data[i * MY_PQ_M + m];
            // 查表获取距离值
            total_distance += LUT[m * MY_PQ_CLUSTER + code];
        }

        part_dis[i] = total_distance; // 保存部分距离用于调试

        // 维护前K个最小距离（最小堆或插入排序）
        // 这里使用简单的插入排序方法
        if (total_distance < result_v[MAX_K - 1]) {
            // 找到插入位置
            int insert_pos = MAX_K - 1;
            while (insert_pos > 0 && total_distance < result_v[insert_pos - 1]) {
                insert_pos--;
            }

            // 向后移动元素
            for (int j = MAX_K - 1; j > insert_pos; j--) {
                result_v[j] = result_v[j - 1];
                result_id[j] = result_id[j - 1];
            }

            // 插入新结果
            result_v[insert_pos] = total_distance;
            result_id[insert_pos] = data_id[i];
        }
    }
}

// 打印结果
void print_results()
{
    printf("\nPQ Search Results (Top %d):\n", MAX_K);
    printf("Rank\tID\tDistance\n");
    printf("----\t--\t--------\n");

    for (int i = 0; i < MAX_K; i++) {
        if (result_id[i] != -1) {
            printf("%d\t%ld\t%d\n", i + 1, result_id[i], result_v[i]);
        }
    }
}

void read_test_files()
{
    data = (uint8_t *)malloc(SLOT_L * MY_PQ_M * sizeof(uint8_t));
    data_id = (int64_t *)malloc(SLOT_L * sizeof(int64_t));
    LUT = (int32_t *)malloc(MY_PQ_M * MY_PQ_CLUSTER * sizeof(int32_t));

    const char *data_file = DATA_FILE;
    const char *id_file = ID_FILE;
    const char *lut_file = LUT_FILE;

    if (read_data_from_file(data_file, data) != 0) {
        fprintf(stderr, "Failed to read data from %s\n", data_file);
        exit(EXIT_FAILURE);
    }

    if (read_ids_from_file(id_file, data_id) != 0) {
        fprintf(stderr, "Failed to read IDs from %s\n", id_file);
        exit(EXIT_FAILURE);
    }

    if (read_lut_from_file(lut_file, LUT) != 0) {
        fprintf(stderr, "Failed to read LUT from %s\n", lut_file);
        exit(EXIT_FAILURE);
    }

    analyze_lut(LUT); // 进行整体 + 分组统计分析
}

// 主函数
void prepare()
{
    printf("=== PQ Algorithm Test ===\n");

    // 第一步：生成测试数据
    // create_test_file();

    read_test_files();

    // 第二步：执行PQ算法计算
    printf("\nRunning PQ search algorithm...\n");
    pq_search();

    // 第三步：打印结果
    print_results();
}

// uint8_t data[SLOT_L * MY_PQ_M];
// int64_t data_id[SLOT_L];
// int32_t LUT[MY_PQ_M * MY_PQ_CLUSTER];
// int32_t part_dis[SLOT_L];
// int32_t result_v[MAX_K];
// int64_t result_id[MAX_K];

int32_t read_result_v[MAX_K];
int64_t read_result_id[MAX_K];
uint64_t cycles;

int32_t read_part_dis[SLOT_L];

uint8_t *rearranged_data_flat = NULL;
uint8_t ***rearranged_data = NULL;
void allo_rearranged_data()
{
    // 分配单块连续内存
    size_t total_size = SEG_NUM * SLOT_L * (MY_PQ_M / SEG_NUM);
    rearranged_data_flat = (uint8_t *)malloc(total_size * sizeof(uint8_t));
    if (!rearranged_data_flat) {
        // 错误处理
        return;
    }

    // 分配指针数组来维护三维结构
    rearranged_data = (uint8_t ***)malloc(SEG_NUM * sizeof(uint8_t **));
    if (!rearranged_data) {
        free(rearranged_data_flat);
        return;
    }

    for (int i = 0; i < SEG_NUM; i++) {
        rearranged_data[i] = (uint8_t **)malloc(SLOT_L * sizeof(uint8_t *));
        if (!rearranged_data[i]) {
            // 错误处理，需要释放已分配的内存
            return;
        }

        for (int j = 0; j < SLOT_L; j++) {
            size_t offset = i * SLOT_L * (MY_PQ_M / SEG_NUM) + j * (MY_PQ_M / SEG_NUM);
            rearranged_data[i][j] = &rearranged_data_flat[offset];
        }
    }
}

int main()
{
    prepare();
    struct dpu_set_t dpu_set, dpu;
    uint32_t nr_of_dpus;
    
    DPU_ASSERT(dpu_alloc(1, NULL, &dpu_set));
    DPU_ASSERT(dpu_load(dpu_set, DPU_BINARY, NULL));

    DPU_ASSERT(dpu_get_nr_dpus(dpu_set, &nr_of_dpus));
    printf("Allocated %d DPU(s)\n", nr_of_dpus);

    printf("Load input data\n");

    allo_rearranged_data();

    for (int seg = 0; seg < SEG_NUM; seg++) {
        for (int slot = 0; slot < SLOT_L; slot++) {
            for (int part = 0; part < MY_PQ_M / SEG_NUM; part++) {
                rearranged_data[seg][slot][part] = data[slot * MY_PQ_M + seg * (MY_PQ_M / SEG_NUM) + part];
            }
        }
    }

    DPU_ASSERT(dpu_copy_to(dpu_set, XSTR(data), 0, rearranged_data_flat, SLOT_L * MY_PQ_M * sizeof(uint8_t)));

    DPU_ASSERT(dpu_copy_to(dpu_set, XSTR(data_id), 0, data_id, SLOT_L * sizeof(int64_t)));
    DPU_ASSERT(dpu_copy_to(dpu_set, XSTR(LUT), 0, LUT, MY_PQ_M * MY_PQ_CLUSTER * sizeof(int32_t)));

    printf("Run program on DPU(s)\n");
    DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));

    DPU_FOREACH (dpu_set, dpu) {
        DPU_ASSERT(dpu_log_read(dpu, stdout));
    }

    // DPU_ASSERT(dpu_copy_from(dpu_set, XSTR(result_v), 0, read_result_v, MAX_K * sizeof(int32_t)));
    // DPU_ASSERT(dpu_copy_from(dpu_set, XSTR(result_id), 0, read_result_id, MAX_K * sizeof(int64_t)));

    DPU_FOREACH (dpu_set, dpu) {
        DPU_ASSERT(dpu_copy_from(dpu, XSTR(result_v), 0, read_result_v, MAX_K * sizeof(int32_t)));
        DPU_ASSERT(dpu_copy_from(dpu, XSTR(result_id), 0, read_result_id, MAX_K * sizeof(int64_t)));
        DPU_ASSERT(dpu_copy_from(dpu, XSTR(cycles), 0, &cycles, sizeof(uint64_t)));
        DPU_ASSERT(dpu_copy_from(dpu, XSTR(part_dis), 0, read_part_dis, SLOT_L * sizeof(int32_t)));
    }
    // 打印part_dis和read_part_dis前10个用于调试
    printf("\nPart_dis (first 2 entries):\n");
    for (int i = 0; i < 2; i++) {
        printf("part_dis[%d] = %d, read_part_dis[%d] = %d\n", i, part_dis[i], i, read_part_dis[i]);
    }

    // 对result_v和result_id进行排序
    for (int i = 0; i < MAX_K - 1; i++) {
        for (int j = i + 1; j < MAX_K; j++) {
            if (read_result_v[i] > read_result_v[j]) {
                // 交换距离
                int32_t temp_v = read_result_v[i];
                read_result_v[i] = read_result_v[j];
                read_result_v[j] = temp_v;
                // 交换对应的ID
                int64_t temp_id = read_result_id[i];
                read_result_id[i] = read_result_id[j];
                read_result_id[j] = temp_id;
            }
        }
    }

    double dputime_ns = cycles / (0.4);

    double dputime_ms = dputime_ns / 1000000.0;
    printf("DPU time: %.2f ms\n", dputime_ms);

    for (int i = 0; i < MAX_K; i++) {
        printf("Host result: id=%ld, dist=%d | DPU result: id=%ld, dist=%d\n", result_id[i], result_v[i], read_result_id[i],
            read_result_v[i]);
    }

    // 如果id重叠5个就pass
    int match_count = 0;
    for (int i = 0; i < MAX_K; i++) {
        for (int j = 0; j < MAX_K; j++) {
            if (result_id[i] == read_result_id[j]) {
                match_count++;
                break;
            }
        }
    }
    // 高亮显示
    if (match_count >= 5) {
        printf("\033[0;32mTest PASSED! (%d/%d matches), DPU time: %.2f ms\033[0m\n", match_count, MAX_K, dputime_ms);
    } else {
        printf("\033[0;31mTest FAILED! (%d/%d matches), DPU time: %.2f ms\033[0m\n", match_count, MAX_K, dputime_ms);
    }

    DPU_ASSERT(dpu_free(dpu_set));
    return 0;
}
