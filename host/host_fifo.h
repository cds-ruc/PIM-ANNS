#pragma once

extern "C"
{
#include <assert.h>
#include <dpu.h>
#include <dpu_management.h>
#include <ufi/ufi_config.h>
}

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <ctime>
#include <assert.h>
#include <omp.h>
#include <stdlib.h>
#include <unistd.h>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <fstream>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include <functional>

#include <sstream>
#include <string>

#include <boost/coroutine/all.hpp>

#include "third-party/faiss_upmem/faiss/IndexFlat.h"
#include "third-party/faiss_upmem/faiss/index_io.h"
#include "third-party/faiss_upmem/faiss/utils/utils.h"
#include "third-party/faiss_upmem/faiss/IndexIVF.h"
#include "third-party/faiss_upmem/faiss/IndexIVFPQ.h"
#include "host/host_common.h"
#include "host/util.h"
#include <shared_mutex>

#define GET_DPU_ID(r_id, c_id, d_id) (r_id * 64 + c_id * 8 + d_id)
#define GET_DPU_ID_BY_DPU(d)                  \
        GET_DPU_ID(                           \
            dpu_get_rank_allocator_id(d.dpu), \
            dpu_get_slice_id(d.dpu),          \
            dpu_get_member_id(d.dpu))

#define GET_SLOT_ID(dpu_id, s_id) (dpu_id * SLOT_NUM + s_id)
#define DPU_ID_LOCATION(s_id) (s_id / SLOT_NUM)
#define SLOT_ID_LOCATION(s_id) (s_id % SLOT_NUM)

#define CEIL_DIV(a, b) (((a) + (b) - 1) / (b))

#define GET_PAIR_ID_BY_DPU(dpu) (GET_DPU_ID_BY_DPU(dpu) / 2)
#define GET_PAIR_ID(dpu_id) (dpu_id / 2)

#define GET_LINE_ID_BY_DPU(d) \
        (dpu_get_rank_allocator_id(d.dpu) * 8 + dpu_get_member_id(d.dpu))

#define GET_PAIR_LINE_ID_BY_DPUS(d) \
        (dpu_get_rank_allocator_id(d.dpu) * 4 + dpu_get_member_id(d.dpu) / 2)

using namespace std;

using boost::coroutines::coroutine;
using namespace faiss;

extern "C"
{
        dpu_error_t ci_disable_dpu(struct dpu_t *dpu);
        uint32_t get_nr_of_dpus_in_rank(struct dpu_rank_t *rank);
        void dpu_lock_rank(struct dpu_rank_t *rank);
        void dpu_unlock_rank(struct dpu_rank_t *rank);
        dpu_error_t dpu_switch_mux_for_rank(
            struct dpu_rank_t *rank,
            bool set_mux_for_host);
        uint32_t _transfer_matrix_index(struct dpu_t *dpu);
        void fifo_write_to_rank(
            void **ptr,
            uint64_t *base_region_addr,
            uint32_t offset,
            uint32_t size,
            uint8_t dpu_id_start,
            uint8_t dpu_id_stop,
            uint32_t data_id_start,
            __attribute__((unused)) uint32_t data_id_stop);
        void fifo_read_from_rank(
            void **ptr,
            uint64_t *base_region_addr,
            uint32_t offset,
            uint32_t size,
            uint8_t dpu_id_start,
            uint8_t dpu_id_stop,
            uint32_t data_id_start,
            __attribute__((unused)) uint32_t data_id_stop);
        dpu_error_t fifo_host_get_access_for_transfer_matrix(
            struct dpu_rank_t *rank,
            void **ptr);
        dpu_error_t fifo_host_release_access_for_transfer_matrix(
            struct dpu_rank_t *rank,
            void **ptr);
        uint32_t fifo_get_symbol_offset(
            struct dpu_set_t fifo_set,
            const char *symbol_name,
            uint32_t symbol_offset);
        struct hw_dpu_rank_context_t;
        uint64_t *get_rank_ptr(struct dpu_rank_t *rank);
        dpu_id_t dpu_get_id(struct dpu_t *dpu);
        dpu_slice_id_t dpu_get_slice_id(struct dpu_t *dpu);
        dpu_id_t dpu_get_rank_id(struct dpu_rank_t *rank);

        dpu_member_id_t dpu_get_member_id(struct dpu_t *dpu);
        dpu_error_t dpu_check_wavegen_mux_status_for_dpu(
            struct dpu_rank_t *rank,
            uint8_t dpu_id,
            uint8_t *expected,
            int dpu_id2);
        dpu_error_t fifo_dpu_check_wavegen_mux_status_for_dpu(
            struct dpu_rank_t *rank,
            uint8_t dpu_id,
            uint8_t *expected,
            uint8_t ci_mask);
        void byte_interleave_avx512(uint64_t *input, uint64_t *output, bool use_stream);
        void dpu_transfer_matrix_clear_all(
            struct dpu_rank_t *rank,
            struct dpu_transfer_matrix *transfer_matrix);
        struct dpu_fifo_rank_t *get_rank_fifo(
            struct dpu_fifo_link_t *fifo_link,
            struct dpu_rank_t *rank);

        dpu_error_t dpu_disable_one_dpu(struct dpu_t *dpu);
}

#ifndef DPU_BINARY
#define DPU_BINARY PROJECT_SOURCE_DIR "/build/dpu_adc_code"
#endif

#ifndef BATCH_DPU_BINARY
#define BATCH_DPU_BINARY PROJECT_SOURCE_DIR "/build/batch_dpu_adc_code"
#endif

typedef struct PAIR_LOCATION
{
        int dpu_id;
        int slot_id;

        int dpu_id1;
        int slot_id1;
} PAIR_LOCATION;

struct LOCATION
{
        int dpu_id;
        int slot_id;
};

typedef struct PAIR_TASK
{
        dpu_fifo_input_t task[2];

        //this should be 2
        int enable_num = 0;

} PAIR_TASK;

typedef struct PAIR_SHARD_INFO
{

        vector<PAIR_LOCATION> pair_location;

        std::shared_mutex rw_mutex;

        //     rw_mutex.lock_shared();  
        //     rw_mutex.unlock_shared(); 

        //     rw_mutex.lock();  
        //     rw_mutex.unlock(); 

} PAIR_SHARD_INFO;

typedef struct CLUSTER_INFO
{
        /*------------shard_dataset----------------*/
        int c_len;
        int c_offset;
        int *s_len;
        int *s_offset;
        int s_num;
        int pair_shard_num;

        int workload_pershard;

        /*--------------replica_dataset---------------------*/
        int replica;

        atomic<int> history_freq = 0;

        int shard_size;

        /*-----------dynamic---------------*/

        atomic<int> dynamic_freq = 0;

        /*---------------place_dataset----------------------*/

        PAIR_SHARD_INFO pair_shard_info[MAX_SHARD / 2];

} CLUSTER_INFO;

typedef struct QUERY_INFO
{
        /*-------init------*/
        float *query_data;

        int q_id;

        /*----------timer--------*/
        std::chrono::time_point<std::chrono::steady_clock> start_;
        std::chrono::time_point<std::chrono::steady_clock> end_;

        std::unordered_map<int, std::chrono::time_point<std::chrono::steady_clock>>
            start1_;
        std::unordered_map<int, std::chrono::time_point<std::chrono::steady_clock>>
            end1_;

        ~QUERY_INFO()
        {
                end1_.clear();
                start1_.clear();
        }

        void start1()
        {
                start_ = std::chrono::steady_clock::now();
        }
        // void start2(int dpu_id) {
        //     int id = q_id * MAX_DPU + dpu_id;
        //     start1_.insert(std::make_pair(id, std::chrono::steady_clock::now()));
        // }

        void end1()
        {
                end_ = std::chrono::steady_clock::now();
                double ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                end_ - start_)
                                .count();
                assert(ns > 0);
                xmh::PerfCounter::Record("latency (ms)", ns / (1000 * 1000));
        }
        // void end2(int dpu_id) {
        //     return;
        //     int id = q_id * MAX_DPU + dpu_id;
        //     end1_.insert(std::make_pair(id, std::chrono::steady_clock::now()));
        //     double ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        //                         end1_[id] - start1_[id])
        //                         .count();
        //     // assert(ns > 0);
        //     if (ns < 0) {
        //         printf("q_id %d, dpu_id %d, ns %f\n", q_id, dpu_id, ns);
        //     }
        //     counter.Record("one task (ms)", ns / (1000 * 1000));

        //     end1_.erase(id);
        // }

        /*----------search param---------------*/
        int k;
        int nprobe;

        /*------------Level1 search------------------*/
        float *coarse_dis;
        faiss::idx_t *idx;

        /*------------wait Level2 search-------------------*/

        int wait_dpu_num;

        bool send_over = false;

        atomic<int> complete_num{0};

        /*-------------merge topk-----------------------*/
        DIST_TYPE simi_values[MAX_K];
        ID_TYPE simi_ids[MAX_K];

        ID_TYPE *ground_truth_ids;

        std::mutex mtx_merge_topk;

} QUERY_INFO;

typedef struct SLOT_INFO
{
        int c_id;
        int shard_id;
        int shard_l;

        bool active = false;
} SLOT_INFO;

typedef struct PAIR_INFO
{
        /*------------protect is_running value----------------*/
        std::mutex mtx_dpu_run;
        //     bool is_running = false;
        atomic<bool> is_running{false};

        // used when fifo output
        bool first_dpu_back = false;
        bool second_dpu_back = false;
        bool is_first_dpu_enable = false;
        bool is_second_dpu_enable = false;

        int enable_num = 0;

} PAIR_INFO;

typedef struct DPU_INFO
{
        /*--------------init----------------------*/
        dpu_set_t dpu;
        DPU_INFO_COMMON dpu_info_common;
        bool is_enabled = false;
        int dpu_id;
        int pair_id;
        int line_id;
        int pair_line_id;
        int rank_id;

        /*---------workload------------------------*/
        int64_t workload;

        /*--------------slot info------------------------*/
        int enable_slot_num = 0;

        /*------------slot_id -> (c_id + shard_id)------------*/
        // use for copy data
        SLOT_INFO slot_info[SLOT_NUM];

        /*----------------test---------*/
        int send_id = 0;

        int mark_running_id = 0;
        int mark_stop_id = 0;

        uint64_t task_num = 0;
        uint64_t need_compute_distance_num = 0;

        std::vector<std::chrono::nanoseconds> real_time_when_dpu_back;
        std::vector<std::chrono::nanoseconds> real_time_when_dpu_send;

} DPU_INFO;

typedef struct RANK_INFO
{
        /*--------------init----------------------*/
        dpu_set_t rank;

        int rank_id;

        bool is_Enabled;

        uint64_t *region_ptr;

} RANK_INFO;

typedef struct BACK_THREAD_INFO
{
        std::atomic<bool> stopFlag{false};
        std::atomic<bool> already_stop{false};

} BACK_THREAD_INFO;

typedef struct DynamicBalance_THREAD_INFO
{
        std::atomic<bool> stopFlag{false};
        std::atomic<bool> already_stop{false};

} DynamicBalance_THREAD_INFO;

typedef struct fifo_dpu_transfer_matrix
{
        void *ptr[MAX_NR_DPUS_PER_RANK];
        uint32_t offset;
        uint32_t size;
        uint8_t type;
} fifo_dpu_transfer_matrix;

/*-------query info------------*/
extern std::vector<std::shared_ptr<QUERY_INFO>> query_info;

extern struct dpu_set_t fifo_set;


class DPUWrapper
{
public:
        int have_slot = 0;
        int need_slot = 0;
        int copy_rate_select = 0;

        bool is_first_dynamic = true;

        int query_num;

        mConfig mconfig;
        std::string index_path;
        std::string query_path;

        std::string history_query_path;
        std::string groundtruth_path;

        std::string freq_path;
        std::string size_path;

        std::string replica_path;
        std::string workload_path;

        std::string dpu_result_path;

        std::string cpu_result_path;

        /*------just for debug------------*/
        std::string debug_path_log_out =
            std::string(PROJECT_SOURCE_DIR) + std::string("/log/out.txt");
        std::string debug_path_log_in =
            std::string(PROJECT_SOURCE_DIR) + std::string("/log/in.txt");

        std::ofstream outfile_log_out;
        std::ofstream outfile_log_in;

        std::mutex mtx_log_out;
        std::mutex mtx_log_in;

        std::string debug_path_log_out1 =
            std::string(PROJECT_SOURCE_DIR) + std::string("/log/out1.txt");
        std::string debug_path_log_in1 =
            std::string(PROJECT_SOURCE_DIR) + std::string("/log/in1.txt");

        std::ofstream outfile_log_out1;
        std::ofstream outfile_log_in1;

        std::mutex mtx_log_out1;
        std::mutex mtx_log_in1;

        ~DPUWrapper()
        {
                // DPU_ASSERT(dpu_free(fifo_set));
        }

        DPUWrapper()
        {
                dpu_init();
        }

        int next_qid = 0;

        atomic<int> num_is_running{0};

   
        /*------------stack space is limited-----*/

        vector<float *> sim_table_buffer{
            vector<float *>(FRONT_THREAD * MAX_COROUTINE)};

        vector<float *> dis0_buffer{vector<float *>(FRONT_THREAD * MAX_COROUTINE)};

        vector<DIST_TYPE *> sim_table_dynamictype{
            vector<DIST_TYPE *>(FRONT_THREAD * MAX_COROUTINE)};

        vector<DIST_TYPE *> dis0_dynamictype{
            vector<DIST_TYPE *>(FRONT_THREAD * MAX_COROUTINE)};

        vector<bool *> send_flag_buffer{vector<bool *>(FRONT_THREAD * MAX_COROUTINE)};

        vector<char> debug_m{vector<char>(PERDPU_LOG_SIZE * MAX_DPU)};

        std::mutex mtx_nextq;

        atomic<bool> stopBackthread{false};

        /*----------------symbol offset--------------------*/
        std::unordered_map<std::string, uint32_t> pre_symbol_offset;

        std::string symbol1 = "query";

        /*---------------fifo_mram------------------------*/

        vector<std::mutex> mtx_fifo_mram_rankid{vector<std::mutex>(MAX_RANK)};
        vector<std::mutex> mtx_fifo_mram_pairlineid{
            vector<std::mutex>(MAX_PAIR_LINE)};

        /*-----------------input link----------------*/
        vector<std::mutex> mtx_input_link_rankid{vector<std::mutex>(MAX_RANK)};

        faiss::IndexIVFPQ *fifo_index;

        /*----------------mram fifo-------------------------*/
        struct dpu_fifo_link_t input_link, output_link;
        uint8_t *output_fifo_data;
        uint8_t *input_fifo_data;

        fifo_dpu_transfer_matrix *xfer_matrix[FRONT_THREAD];
        fifo_dpu_transfer_matrix *xfer_matrix_from[FRONT_THREAD];
        /*----------------mram fifo end-------------------*/

        faiss::InvertedListScanner *scanner[FRONT_THREAD * MAX_COROUTINE];

        /*----------dataset----------*/

        vector<DPU_INFO> dpu_info{vector<DPU_INFO>(MAX_DPU)};
        vector<PAIR_INFO> pair_info{vector<PAIR_INFO>(MAX_PAIR)};
        vector<RANK_INFO> rank_info{vector<RANK_INFO>(MAX_RANK)};

        std::vector<std::shared_ptr<CLUSTER_INFO>> cluster_info;

        BACK_THREAD_INFO back_thread_info[BACK_THREAD];

        DynamicBalance_THREAD_INFO dynamic_balance_thread_info[DynamicBalance_THREAD];

        /*---------function-----------*/

        void read_dpu_log_();

        int get_nextq();

        void log_num_dpu_running(int nprobe, int topk);

        /*---------------debug info----------*/
        int get_cluster_info(ID_TYPE id)
        {
                for (int i = 0; i < mconfig.getMaxCluster(); i++)
                {
                        for (int j = 0; j < cluster_info[i]->s_num; j++)
                        {
                                ID_TYPE *ids = getids(i, j);
                                for (int k = 0; k < cluster_info[i]->s_len[j]; k++)
                                {
                                        if (id == ids[k])
                                        {
                                                int len = cluster_info[i]->s_len[j];
                                                printf("cluster %d, len %d, shard %d\n", i, len, j);
                                                return j;
                                        }
                                }
                        }
                }
        }

        void FIFOinit();

        void clear_matrix(int rank_id, int thread_id, bool is_from);

        /*---------------already init-----------------*/
        void level1_search(int q_id, int k, int nprobe);
        void get_freq_num(int q_id, int k, int nprobe);
        void test_fifo();

        void level2_search(
            coroutine<void>::push_type &sink,
            int q_id,
            int k,
            int nprobe,
            int thread_id,
            int coroutine_id);
        void init_balance(int nprobe, int top_k);

        void search(int k, int nprobe, int thread_id);

        void cpu_search(int *q_id, int k, int nprobe, int batch_size);

        void fake_send_task(dpu_fifo_input_t &query);

        void cooperative(
            coroutine<void>::push_type &sink,
            int k,
            int nprobe,
            int thread_id,
            int coroutine_id);

        void fifo_dpu_copy_to(
            int dpu_id,
            int friend_dpu_id,
            int enable_num,
            const char *symbol_name,
            uint32_t symbol_offset,
            const void *src,
            const void *friend_src,
            size_t length,
            int thread_id);

        void fifo_dpu_copy_from(
            int dpu_id,
            const char *symbol_name,
            uint32_t symbol_offset,
            const void *src,
            size_t length,
            int thread_id);

        void shutdown_fifo();

        void stop_thread_backend();
        void bind_core(int n);

        void read_log();

        void thread_fifo_out(const std::vector<int> &rank_id_v, int t_id);

        void thread_dynamic_balance(const std::vector<int> &cluster_id_v, int t_id);

        static inline DPUWrapper *GetInstance()
        {
                static DPUWrapper instance;
                return &instance;
        }

        void dpu_init();

        void dpu_reset();

        void inline launch()
        {
                DPU_ASSERT(dpu_launch(fifo_set, DPU_ASYNCHRONOUS));
        }

        void shard_dataset();
        void init_dataset();

        void replica_dataset();

        void dpu_search_batch(
            int *q_id,
            int k,
            int nprobe,
            int batch_size,
            int type,
            std::ofstream &outfile_active_num);

        void place_dataset();

        void copy_dataset();

        DATA_TYPE *getdata(int c_id, int s_id);

        ID_TYPE *getids(int c_id, int s_id);

        void print_result(int nprobe, int top_k, int type);

        void recall(int nprobe, int top_k, int type, double time_s);
};
