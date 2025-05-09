#pragma once

#include <fstream>
#include <iostream>
#include "gnuplot-iostream.h"

#include "host/host_fifo.h"
#include "third-party/faiss_upmem/faiss/impl/IDSelector.h"

namespace faiss
{

    template <class C, bool use_sel>
    struct KnnSearchResults
    {
        idx_t key;
        const idx_t *ids;
        const IDSelector *sel;

        // heap params
        size_t k;
        float *heap_sim;
        idx_t *heap_ids;

        size_t nup;

        inline void add(idx_t j, float dis)
        {
            if (C::cmp(heap_sim[0], dis))
            {
                idx_t id = ids ? ids[j] : lo_build(key, j);
                heap_replace_top<C>(k, heap_sim, heap_ids, dis, id);
                nup++;
            }
        }
    };

    template <class SearchResultType>
    void scan_list_with_table_(
        size_t ncode,
        const uint8_t *codes,
        SearchResultType &res,
        float *sim_table,
        float dis0)
    {
        for (size_t j = 0; j < ncode; j++, codes += 16)
        {
            // float dis =
            //         dis0 + distance_single_code<PQDecoder>(pq, sim_table, codes);
            float dis = dis0;
            for (int k = 0; k < 16; k++)
            {
                int pq_code = codes[k];
                dis += sim_table[k * 256 + pq_code];
            }
            if (j < 10)
            {
                printf("dis: %f\n", dis);
            }

            res.add(j, dis);
        }
    }
} // namespace faiss

void scan_codes(
    size_t ncode,
    const uint8_t *codes,
    const faiss::idx_t *ids,
    float *heap_sim,
    faiss::idx_t *heap_ids,
    size_t k,
    int key,
    float *sim_table,
    float dis0)
{
    // print heap_sim
    for (int i = 0; i < k; i++)
    {
        printf("heap_sim[%d]: %f\n", i, heap_sim[i]);
    }
    faiss::KnnSearchResults<faiss::CMax<float, faiss::idx_t>, false> res = {
        key,
        ids,
        /* sel */ NULL,
        /* k */ k,
        heap_sim,
        heap_ids,
        0};

    faiss::scan_list_with_table_(ncode, codes, res, sim_table, dis0);
}

void dpu_search(int k, int nprobe)
{
    DPUWrapper::GetInstance()->launch();
    DPUWrapper::GetInstance()->dpu_reset();

    std::vector<std::thread> fifo_out_thread;

    {

        std::vector<int> rank_id_v[BACK_THREAD];
        int thread_index = 0;
        for (int i = 0; i < MAX_RANK; i++)
        {
            if (DPUWrapper::GetInstance()->rank_info[i].is_Enabled)
            {
                rank_id_v[thread_index].push_back(
                    DPUWrapper::GetInstance()->rank_info[i].rank_id);
                thread_index = (thread_index + 1) % BACK_THREAD;
            }
        }

        for (int i = 0; i < BACK_THREAD; i++)
        {

            std::string thread_name = "FIFO_OUT_" + std::to_string(i);
            std::thread t(
                &DPUWrapper::thread_fifo_out,
                DPUWrapper::GetInstance(),
                rank_id_v[i],
                i);

            pthread_t pthread_handle =
                *reinterpret_cast<pthread_t *>(t.native_handle());

            pthread_setname_np(pthread_handle, thread_name.c_str());
            fifo_out_thread.push_back(std::move(t));
        }
    }

    // dynamic balance
    std::vector<std::thread> dynamic_balance_thread;
    {

        std::vector<int> cluster_id_v[DynamicBalance_THREAD];

        const int total_clusters = DPUWrapper::GetInstance()->mconfig.getMaxCluster();
        const int clusters_per_thread = total_clusters / DynamicBalance_THREAD;
        const int remaining_clusters = total_clusters % DynamicBalance_THREAD;

        for (int thread_idx = 0; thread_idx < DynamicBalance_THREAD; thread_idx++)
        {
            const int start_cluster = thread_idx * clusters_per_thread;
            const int end_cluster = start_cluster + clusters_per_thread;

            for (int cluster_idx = start_cluster; cluster_idx < end_cluster; cluster_idx++)
            {
                cluster_id_v[thread_idx].push_back(cluster_idx);
            }
        }

        if (remaining_clusters > 0)
        {
            const int last_thread_idx = DynamicBalance_THREAD - 1;
            const int start_cluster = total_clusters - remaining_clusters;

            for (int cluster_idx = start_cluster; cluster_idx < total_clusters; cluster_idx++)
            {
                cluster_id_v[last_thread_idx].push_back(cluster_idx);
            }
        }

        for (int thread_idx = 0; thread_idx < DynamicBalance_THREAD; thread_idx++)
        {

            std::string thread_name = "Dynamic_Balance_" + std::to_string(thread_idx);
            std::thread t(
                &DPUWrapper::thread_dynamic_balance,
                DPUWrapper::GetInstance(),
                cluster_id_v[thread_idx],
                thread_idx);

            pthread_t pthread_handle =
                *reinterpret_cast<pthread_t *>(t.native_handle());

            pthread_setname_np(pthread_handle, thread_name.c_str());
            dynamic_balance_thread.push_back(std::move(t));
        }
    }

    std::vector<std::thread> fifo_in_thread;

    xmh::Timer timer("all query");

    xmh::Timer timer1(" 1:send all task");

    std::thread log_thread(
        &DPUWrapper::log_num_dpu_running,
        DPUWrapper::GetInstance(),
        nprobe,
        k);

    for (int i = 0; i < FRONT_THREAD; i++)
    {
        std::string thread_name = "FIFO_IN_" + std::to_string(i);
        std::thread t(
            &DPUWrapper::search, DPUWrapper::GetInstance(), k, nprobe, i);

        pthread_t pthread_handle =
            *reinterpret_cast<pthread_t *>(t.native_handle());

        pthread_setname_np(pthread_handle, thread_name.c_str());
        fifo_in_thread.push_back(std::move(t));
    }

    for (auto &t : fifo_in_thread)
    {
        t.join();
    }
    timer1.end();

    xmh::Timer timer2(" 2:read_result");

    DPUWrapper::GetInstance()->shutdown_fifo();

    DPUWrapper::GetInstance()->stop_thread_backend();

    for (auto &t : fifo_out_thread)
    {
        t.join();
    }

    for (auto &t : dynamic_balance_thread)
    {
        t.join();
    }

    timer2.end();

    timer.end();

    log_thread.join();
}

void kernel_cpu_search(int k, int nprobe)
{
    int batch_size = 40;

    DPUWrapper::GetInstance();

    int *q_id = new int[batch_size];

    auto start_ = std::chrono::steady_clock::now();

    xmh::Timer timer("all query");

    for (int i = 0; i < DPUWrapper::GetInstance()->query_num; i += batch_size)
    {
        auto start = std::chrono::steady_clock::now();

        for (int j = 0; j < batch_size; j++)
        {
            q_id[j] = i + j;
        }
        DPUWrapper::GetInstance()->cpu_search(q_id, k, nprobe, batch_size);

        auto end = std::chrono::steady_clock::now();
        auto time_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
                .count();

        xmh::PerfCounter::Record("latency (ms)", time_ns / (1000 * 1000));
    }

    auto end_ = std::chrono::steady_clock::now();
    auto time_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end_ - start_)
            .count();
    auto time_s = time_ns / 1e9;

    DPUWrapper::GetInstance()->recall(nprobe, k, 0, time_s);

    timer.end();

    DPUWrapper::GetInstance()->print_result(nprobe, k, 0);
    delete[] q_id;
}

void kernel_dpu_search(int k, int nprobe)
{
    DPUWrapper::GetInstance();

    DPUWrapper::GetInstance()->init_balance(nprobe, k);
    DPUWrapper::GetInstance()->init_dataset();

    auto start_ = std::chrono::steady_clock::now();

    dpu_search(k, nprobe);

    auto end_ = std::chrono::steady_clock::now();
    auto time_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end_ - start_)
            .count();
    auto time_s = time_ns / 1e9;

    // std::string log_num_dpu_running_path_nprobe =
    //     std::string(DPUWrapper::GetInstance()->mconfig.getDpuActiveNumPath()) + "-nprobe" +
    //     std::to_string(nprobe) + "-topk" + std::to_string(k) + "-slotlen" +
    //     std::to_string(SLOT_L) + "-COROUTINE" +
    //     std::to_string(MAX_COROUTINE) + "-BACK_THREAD" +
    //     std::to_string(BACK_THREAD) + "-FRONT_THREAD" +
    //     std::to_string(FRONT_THREAD) + ".txt";

    std::string log_num_dpu_running_path_nprobe =
        std::string(DPUWrapper::GetInstance()->mconfig.getDpuActiveNumPath()) + "-nprobe" +
        std::to_string(nprobe) + ".txt";

    if (MAX_COROUTINE != 4 && CHANGE_MAX_COROUTINE == 1)
    {

        log_num_dpu_running_path_nprobe =
            std::string(DPUWrapper::GetInstance()->mconfig.getDpuActiveNumPath()) + "-nprobe" +
            std::to_string(nprobe) + "-COROUTINE" + std::to_string(MAX_COROUTINE) + ".txt";
    }
    else if (COPY_RATE != 10 && CHANGE_COPY_RATE == 1)
    {
        std::string result = std::to_string(COPY_RATE);

        bool is_integer = (std::floor(COPY_RATE) == COPY_RATE);

        if (is_integer)
        {
            result = result.substr(0, 1);
        }
        else
        {
            result = result.substr(0, 3);
        }
        log_num_dpu_running_path_nprobe = std::string(DPUWrapper::GetInstance()->mconfig.getDpuActiveNumPath()) +
                                          "-nprobe" + std::to_string(nprobe) + "-COPY_RATE" +
                                          result + ".txt";
    }
    else if (ENABLE_REPLICA != 1 && CHANGE_ENABLE_REPLICA == 1)
    {
        log_num_dpu_running_path_nprobe = std::string(DPUWrapper::GetInstance()->mconfig.getDpuActiveNumPath()) +
                                          "-nprobe" + std::to_string(nprobe) + "-ENABLE_REPLICA" +
                                          std::to_string(ENABLE_REPLICA) + ".txt";
    }

    std::ifstream infile_active_num(log_num_dpu_running_path_nprobe);

    std::string line;
    float sum = 0;
    int count = 0;
    while (std::getline(infile_active_num, line))
    {
        sum += std::stoi(line);
        count++;
    }
    infile_active_num.close();
    xmh::PerfCounter::Record("avg_active_num", sum / count);
    double active_rate = ((double)(sum / count)) / (double)NR_DPU;

    xmh::PerfCounter::Record("avg_active_rate", active_rate);

    DPUWrapper::GetInstance()->recall(nprobe, k, 1, time_s);

    DPUWrapper::GetInstance()->print_result(nprobe, k, 1);

    return;
}

void kernel_dpu_search_batch(int k, int nprobe)
{

    // when nprobe is too large, this should be set smaller
    int batch_size = 1000;

    DPUWrapper::GetInstance();

    DPUWrapper::GetInstance()->init_balance(nprobe, k);
    DPUWrapper::GetInstance()->init_dataset();

    int *q_id = new int[batch_size];

    auto start_ = std::chrono::steady_clock::now();

    // std::string active_num_path = std::string(DPUWrapper::GetInstance()->mconfig.getBatchDpuActiveNumPath()) +
    //                               "-nprobe" + std::to_string(nprobe) + "-topk" + std::to_string(k) +
    //                               "-slotlen" + std::to_string(SLOT_L) + ".txt";

    std::string active_num_path = std::string(DPUWrapper::GetInstance()->mconfig.getBatchDpuActiveNumPath()) +
                                  "-nprobe" + std::to_string(nprobe) + ".txt";

    std::ofstream outfile_active_num(active_num_path, std::ios::app);

    xmh::Timer timer("all query");

    for (int i = 0; i < DPUWrapper::GetInstance()->query_num; i += batch_size)
    {
        auto start = std::chrono::steady_clock::now();

        for (int j = 0; j < batch_size; j++)
        {
            q_id[j] = i + j;
        }
        DPUWrapper::GetInstance()->dpu_search_batch(
            q_id, k, nprobe, batch_size, 2, outfile_active_num);

        auto end = std::chrono::steady_clock::now();
        auto time_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
                .count();

        xmh::PerfCounter::Record("latency (ms)", time_ns / (1000 * 1000));
    }

    // printf("=============================================================================\n");

    // for (int i = 0; i < NR_DPU; i++) {
    //     printf("dpuid = %d, task_num = %d, need_compute_distance_num = %d\n",
    //            i,
    //            DPUWrapper::GetInstance()->dpu_info[i].task_num,
    //            DPUWrapper::GetInstance()
    //                    ->dpu_info[i]
    //                    .need_compute_distance_num);
    // }

    // printf("=============================================================================\n");

    timer.end();
    outfile_active_num.close();

    auto end_ = std::chrono::steady_clock::now();
    auto time_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end_ - start_)
            .count();
    auto time_s = time_ns / 1e9;

    std::ifstream infile_active_num(active_num_path);

    std::string line;
    int64_t active_num = 0;
    int count = 0;
    while (std::getline(infile_active_num, line))
    {
        active_num += std::stoi(line);
        count++;
        // check is overflow than int64_t
        if (active_num < 0)
        {
            printf("active_num overflow\n");
            exit(1);
        }
    }
    infile_active_num.close();

    xmh::PerfCounter::Record("avg_active_num", active_num / count);

    double active_rate = ((double)(active_num / count)) / (double)NR_DPU;
    xmh::PerfCounter::Record("avg_active_rate", active_rate);

    DPUWrapper::GetInstance()->recall(nprobe, k, 2, time_s);

    DPUWrapper::GetInstance()->print_result(nprobe, k, 2);

    delete[] q_id;
}

void get_detail_path(std::string &detail_path, int nprobe)
{

    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm = *std::localtime(&now_time_t);
    char time_str[20];
    std::strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", &now_tm);

#if defined(TEST_CPU)
    // detail_path = std::string(DPUWrapper::GetInstance()->mconfig.getCpuDetailPath()) + "-nprobe" +
    //               std::to_string(nprobe) + "_" + time_str + ".txt";

    detail_path = std::string(DPUWrapper::GetInstance()->mconfig.getCpuDetailPath()) + "-nprobe" +
                  std::to_string(nprobe) + ".txt";

#elif defined(TEST_DPU)
    // std::string enable_flag = DPUWrapper::GetInstance()->mconfig.getEnableDynamic() ? "YES" : "NO";
    // detail_path = std::string(DPUWrapper::GetInstance()->mconfig.getDpuDetailPath()) + "-nprobe" +
    //               std::to_string(nprobe) + "-slotlen" + std::to_string(SLOT_L) +
    //               "-COROUTINE" + std::to_string(MAX_COROUTINE) + "-BACK_THREAD" +
    //               std::to_string(BACK_THREAD) + "-FRONT_THREAD" +
    //               std::to_string(FRONT_THREAD) + "-COPY_RATE" +
    //               std::to_string(COPY_RATE) + "-EnableDynamic" +
    //               enable_flag + "-" + time_str +
    //               ".txt";

    detail_path = std::string(DPUWrapper::GetInstance()->mconfig.getDpuDetailPath()) + "-nprobe" +
                  std::to_string(nprobe) + ".txt";

    if (MAX_COROUTINE != 4 && CHANGE_MAX_COROUTINE == 1)
    {
        detail_path = std::string(DPUWrapper::GetInstance()->mconfig.getDpuDetailPath()) + "-nprobe" +
                      std::to_string(nprobe) + "-COROUTINE" + std::to_string(MAX_COROUTINE) + ".txt";
    }

    else if (COPY_RATE != 10 && CHANGE_COPY_RATE == 1)
    {
        std::string result = std::to_string(COPY_RATE);

        bool is_integer = (std::floor(COPY_RATE) == COPY_RATE);

        if (is_integer)
        {
            result = result.substr(0, 1);
        }
        else
        {
            result = result.substr(0, 3);
        }
        detail_path = std::string(DPUWrapper::GetInstance()->mconfig.getDpuDetailPath()) + "-nprobe" +
                      std::to_string(nprobe) + "-COPY_RATE" + result + ".txt";
    }
    else if (ENABLE_REPLICA != 1 && CHANGE_ENABLE_REPLICA == 1)
    {

        detail_path = std::string(DPUWrapper::GetInstance()->mconfig.getDpuDetailPath()) + "-nprobe" +
                      std::to_string(nprobe) + "-ENABLE_REPLICA" + std::to_string(ENABLE_REPLICA) +
                      ".txt";
    }

#elif defined(TEST_BATCH_DPU)
    // detail_path = std::string(DPUWrapper::GetInstance()->mconfig.getBatchDpuDetailPath()) + "-nprobe" +
    //               std::to_string(nprobe) + "-slotlen" + std::to_string(SLOT_L) +
    //               "_" + time_str + ".txt";

    detail_path = std::string(DPUWrapper::GetInstance()->mconfig.getBatchDpuDetailPath()) + "-nprobe" +
                  std::to_string(nprobe) + ".txt";

#endif
}

void kernel_fakehost()
{
    dpu_set_t dpu;
    uint32_t nr_of_dpus;

    DPU_ASSERT(dpu_alloc(1, NULL, &fifo_set));
    DPU_ASSERT(dpu_load(fifo_set, DPU_BINARY, NULL));

    DPU_ASSERT(dpu_get_nr_dpus(fifo_set, &nr_of_dpus));
    printf("Allocated %d DPU(s)\n", nr_of_dpus);

    DATA_TYPE data[SLOT_L];
    ID_TYPE data_id[SLOT_L];
    for (int i = 0; i < SLOT_L; i++)
    {
        data[i] = rand() % 256;
        data_id[i] = i;
    }

    dpu_fifo_input_t query;
    query.dpu_id = 0;
    query.q_id = 0;
    query.dis0 = (DIST_TYPE)(rand() % 100);
    for (int j = 0; j < LUT_SIZE; j++)
    {
        query.LUT[j] = (DIST_TYPE)(rand() % 100);
    }
    query.k = 10;
    query.shard_id = 0;
    query.slot_id = 0;
    query.shard_l = SLOT_L;

    DPU_FOREACH(fifo_set, dpu)
    {
        DPU_ASSERT(
            dpu_copy_to(dpu, "data", 0, data, SLOT_L * sizeof(DATA_TYPE)));
        DPU_ASSERT(dpu_copy_to(
            dpu, "data_id", 0, data_id, SLOT_L * sizeof(ID_TYPE)));
        DPU_ASSERT(
            dpu_copy_to(dpu, "query", 0, &query, sizeof(dpu_fifo_input_t)));
    }

    DPU_ASSERT(dpu_launch(fifo_set, DPU_SYNCHRONOUS));

    dpu_fifo_output_t result;
    DPU_ASSERT(dpu_copy_from(
        dpu, "result", 0, &result, sizeof(dpu_fifo_output_t)));

    double dputime_ns = result.cycles / (0.4);
    xmh::Timer::ManualRecordNs("1:dpu time", dputime_ns);

    double dputime_per_vec_ns = result.cycle_per_vec / (0.4);

    xmh::Timer::ManualRecordNs("2:dpu time per vec", dputime_per_vec_ns);

    DPU_FOREACH(fifo_set, dpu)
    {
        DPU_ASSERT(dpu_log_read(dpu, stdout));
    }
}

double findLastLineWithStringAndGetFirstNumber(
    const std::string &filename,
    const std::string &target)
{
    std::ifstream file(filename);
    std::string line;
    std::string last_line;
    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            if (line.find(target) != std::string::npos)
            {
                last_line = line;
            }
        }
        file.close();
    }
    else
    {
        std::cerr << "can not open: " << filename << std::endl;
        return -1.0;
    }
    if (last_line.empty())
    {
        std::cerr << "can not find " << target << std::endl;
        return -1.0;
    }
    size_t pos = last_line.find_first_of("0123456789");
    if (pos == std::string::npos)
    {
        std::cerr << "can not find number" << std::endl;
        return -1.0;
    }
    std::string number_str = last_line.substr(pos);

    // erase leading spaces
    while (number_str[0] == ' ')
        number_str.erase(0, 1);
    // erase trailing spaces
    while (number_str.back() == ' ')
        number_str.pop_back();
    try
    {
        double number = std::stod(number_str);
        return number;
    }
    catch (const std::invalid_argument &e)
    {
        std::cerr << "can not change string to number: " << number_str << std::endl;
        return -1.0;
    }
    catch (const std::out_of_range &e)
    {
        std::cerr << "number exceeds the range: " << number_str << std::endl;
        return -1.0;
    }
}

double findLastLineWithStringAndGetSecondNumber(
    const std::string &filename,
    const std::string &target)
{
    std::ifstream file(filename);
    std::string line;
    std::string last_line;

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            if (line.find(target) != std::string::npos)
            {
                last_line = line;
            }
        }
        file.close();
    }
    else
    {
        std::cerr << "can not open: " << filename << std::endl;
        return -1.0;
    }

    if (last_line.empty())
    {
        std::cerr << "can not find " << target << std::endl;
        return -1.0;
    }

    std::vector<std::string> numbers;
    std::istringstream iss(last_line);
    std::string token;

    while (iss >> token)
    {

        bool is_number = !token.empty();
        bool has_decimal = false;

        for (size_t i = 0; i < token.size(); ++i)
        {
            char c = token[i];

            if (i == 0 && c == '-')
                continue;

            if (c == '.')
            {
                if (has_decimal)
                {
                    is_number = false;
                    break;
                }
                has_decimal = true;
                continue;
            }

            if (!isdigit(c))
            {
                is_number = false;
                break;
            }
        }

        if (is_number)
        {
            numbers.push_back(token);
        }
    }

    if (numbers.size() < 2)
    {
        std::cerr << "there should be two number" << std::endl;
        return -1.0;
    }

    try
    {
        double number = std::stod(numbers[1]);
        return number;
    }
    catch (const std::invalid_argument &e)
    {
        std::cerr << "can not change str to number: " << numbers[1] << std::endl;
        return -1.0;
    }
    catch (const std::out_of_range &e)
    {
        std::cerr << "can not change str to number: " << numbers[1] << std::endl;
        return -1.0;
    }
}

void CPPkernel(int k = 10, int nprobe = 1)
{
    DPUWrapper::GetInstance();

    std::string dataset_path = DPUWrapper::GetInstance()->mconfig.getReplaceDir();

    std::string dataset_path1;

    if (dataset_path.find("SPACE") == 0)
    {
        dataset_path1 = dataset_path.substr(0, 7);
    }
    else if (dataset_path.find("SIFT") == 0)
    {
        dataset_path1 = dataset_path.substr(0, 6);
    }

    printf("searching %s, nprobe = %d\n", dataset_path1.c_str(), nprobe);

    std::string detail_path;
    get_detail_path(detail_path, nprobe);

    if (freopen(detail_path.c_str(), "w", stdout) == nullptr)
    {
        std::cerr << "Error: Failed to redirect stdout to " << detail_path << std::endl;
        return;
    }

    xmh::Reporter::StartReportThread();
    xmh::Timer timer("total");

#if defined(TEST_CPU)

    kernel_cpu_search(k, nprobe);

#elif defined(TEST_DPU)

    kernel_dpu_search(k, nprobe);

    // DPUWrapper::GetInstance()->read_log();

#elif defined(TEST_BATCH_DPU)

    kernel_dpu_search_batch(k, nprobe);

#else
    printf("No test selected\n");
#endif

    timer.end();

    xmh::Reporter::Report();

    fclose(stdout);

    std::string time_path_nprobe;
    std::ifstream infile(detail_path);
    std::string target;
    double result;

#if defined(TEST_CPU)

    time_path_nprobe = std::string(DPUWrapper::GetInstance()->mconfig.getCpuTimePath()) + "-nprobe" +
                       std::to_string(nprobe) + ".txt";

    std::ofstream outfile(time_path_nprobe, std::ios::app);

    target = "latency";
    result = findLastLineWithStringAndGetFirstNumber(detail_path, target);
    outfile << "latency=" << result << endl;

    result = findLastLineWithStringAndGetSecondNumber(detail_path, target);
    outfile << "latency_tail=" << result << endl;

#elif defined(TEST_DPU)

    // std::string enable_flag = DPUWrapper::GetInstance()->mconfig.getEnableDynamic() ? "YES" : "NO";
    // time_path_nprobe = std::string(DPUWrapper::GetInstance()->mconfig.getDpuTimePath()) + "-nprobe" +
    //                    std::to_string(nprobe) + "-topk" + std::to_string(k) +
    //                    "-slotlen" + std::to_string(SLOT_L) + "-COROUTINE" +
    //                    std::to_string(MAX_COROUTINE) + "-BACK_THREAD" +
    //                    std::to_string(BACK_THREAD) + "-FRONT_THREAD" +
    //                    std::to_string(FRONT_THREAD) + "-EnableDynamic" +
    //                    enable_flag + ".txt";

    time_path_nprobe = std::string(DPUWrapper::GetInstance()->mconfig.getDpuTimePath()) + "-nprobe" +
                       std::to_string(nprobe) + ".txt";

    if (MAX_COROUTINE != 4 && CHANGE_MAX_COROUTINE == 1)
    {
        time_path_nprobe = std::string(DPUWrapper::GetInstance()->mconfig.getDpuTimePath()) + "-nprobe" +
                           std::to_string(nprobe) + "-COROUTINE" + std::to_string(MAX_COROUTINE) +
                           ".txt";
    }
    else if (COPY_RATE != 10 && CHANGE_COPY_RATE == 1)
    {
        std::string result = std::to_string(COPY_RATE);

        bool is_integer = (std::floor(COPY_RATE) == COPY_RATE);

        if (is_integer)
        {
            result = result.substr(0, 1);
        }
        else
        {
            result = result.substr(0, 3);
        }
        time_path_nprobe = std::string(DPUWrapper::GetInstance()->mconfig.getDpuTimePath()) + "-nprobe" +
                           std::to_string(nprobe) + "-COPY_RATE" + result + ".txt";
    }
    else if (ENABLE_REPLICA != 1 && CHANGE_ENABLE_REPLICA == 1)
    {
        time_path_nprobe = std::string(DPUWrapper::GetInstance()->mconfig.getDpuTimePath()) + "-nprobe" +
                           std::to_string(nprobe) + "-ENABLE_REPLICA" + std::to_string(ENABLE_REPLICA) +
                           ".txt";
    }

    std::ofstream outfile(time_path_nprobe, std::ios::app);

    target = "avg_active_rate";
    result = findLastLineWithStringAndGetFirstNumber(detail_path, target);
    outfile << "avg_active_rate=" << result << endl;

    target = "latency";
    result = findLastLineWithStringAndGetFirstNumber(detail_path, target);
    outfile << "latency=" << result << endl;

    result = findLastLineWithStringAndGetSecondNumber(detail_path, target);
    outfile << "latency_tail=" << result << endl;

#elif defined(TEST_BATCH_DPU)
    // time_path_nprobe = std::string(DPUWrapper::GetInstance()->mconfig.getBatchDpuTimePath()) + "-nprobe" +
    //                    std::to_string(nprobe) + "-topk" + std::to_string(k) + "-slotlen" +
    //                    std::to_string(SLOT_L) + ".txt";

    time_path_nprobe = std::string(DPUWrapper::GetInstance()->mconfig.getBatchDpuTimePath()) + "-nprobe" +
                       std::to_string(nprobe) + ".txt";

    std::ofstream outfile(time_path_nprobe, std::ios::app);

    target = "avg_active_rate";
    result = findLastLineWithStringAndGetFirstNumber(detail_path, target);
    outfile << "avg_active_rate=" << result << endl;

    target = "latency";
    result = findLastLineWithStringAndGetFirstNumber(detail_path, target);
    outfile << "latency=" << result << endl;

    result = findLastLineWithStringAndGetSecondNumber(detail_path, target);
    outfile << "latency_tail=" << result << endl;

#endif

    outfile.close();
}

// python kernel

void build(int d, int nlist, int pq_m, int pq_k)
{
}

void load(std::string path)
{
    xmh::Reporter::StartReportThread();
    xmh::Timer timer("load");
    sleep(1);
    timer.end();
    xmh::Reporter::Report();
    // return NULL;
}

// search return pair
std::pair<std::vector<DIST_TYPE>, std::vector<ID_TYPE>> searchPIM(int k, int nprobe, float *query = NULL, int nq = 100)
{

    kernel_dpu_search(k, nprobe);
    return std::make_pair(std::vector<DIST_TYPE>(), std::vector<ID_TYPE>());
}