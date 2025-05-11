

#include "host/host_fifo.h"

std::vector<std::shared_ptr<QUERY_INFO>> query_info;

struct dpu_set_t fifo_set;

void DPUWrapper::shard_dataset()
{
    std::ifstream infile(workload_path.c_str());

    int workload_pershard[mconfig.getMaxCluster()];
    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        infile >> workload_pershard[i];
    }
    infile.close();

    int offset = 0;
    int offset2 = 0;
    for (int i = 0; i < fifo_index->invlists->nlist; i++)
    {
        int shard_num = 0;
        offset2 = 0;
        int list_size = fifo_index->invlists->list_size(i);

        assert(list_size > 0);

        shard_num = (list_size > SLOT_L) ? CEIL_DIV(list_size, SLOT_L) : 1;
        if (shard_num % 2 != 0)
        {
       
            // promise it is even
            shard_num = shard_num + 1;
        }

        std::shared_ptr<CLUSTER_INFO> cluster = cluster_info[i];

        cluster->workload_pershard = workload_pershard[i];
        cluster->c_len = list_size;
        cluster->c_offset = offset;
        offset += list_size;
        int avg_len = 0;
        if (list_size % shard_num == 0)
        {
            avg_len = list_size / shard_num;
        }
        else
        {
            avg_len = list_size / shard_num + 1;
        }

        assert(avg_len > 0);
        cluster->s_len = new int[shard_num];
        cluster->s_offset = new int[shard_num];

        for (int j = 0; j < shard_num; j++)
        {
            cluster->s_len[j] =
                (j == shard_num - 1) ? list_size - j * avg_len : avg_len;
            cluster->s_offset[j] = offset2;
            assert(cluster->s_len[j] > 0 && cluster->s_len[j] <= SLOT_L);

            offset2 += cluster->s_len[j];
        }
        cluster->s_num = shard_num;
    }
}

void DPUWrapper::replica_dataset()
{
    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        std::shared_ptr<CLUSTER_INFO> cluster = cluster_info[i];

        int shard_num = cluster->s_num;
        if (cluster->replica > 0)
        {
            int pair_shard_num =
                shard_num % 2 == 0 ? shard_num / 2 : (shard_num / 2) + 1;
            cluster->pair_shard_num = pair_shard_num;
        }
    }
}

void DPUWrapper::place_dataset()
{
    int total_slot = 0;

    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        total_slot += cluster_info[i]->s_num * cluster_info[i]->replica;
    }
    int dpu_enabled_num = 0;
    for (int i = 0; i < MAX_DPU; i++)
    {
        if (dpu_info[i].is_enabled)
        {
            dpu_enabled_num++;
        }
    }

    int avg_slot = CEIL_DIV(total_slot, dpu_enabled_num);
    assert(avg_slot > 0 && avg_slot <= SLOT_NUM);

    int dpu_ptr = 0;
    PAIR_LOCATION pair_location;

    xmh::Timer timer("place_dataset");

    /*------------------begin place----------------------*/
    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        for (int j = 0; j < cluster_info[i]->replica; j++)
        {
            for (int k = 0; k < cluster_info[i]->s_num; k += 2)
            {
                bool place_ok = false;
                int s_num = cluster_info[i]->s_num;
                int pair_shard_num = cluster_info[i]->pair_shard_num;

                int loop_count = 0; 
                while (!place_ok)
                {
                    /*-------pick dpu_pair-----------------*/
                    if (loop_count >= 2 * MAX_DPU)
                    { 
                        printf("Error: Deadlock detected! Failed to place shard after two full loops. set copy_rate smaller!\n");
                        exit(1);
                    }

                    assert(dpu_ptr % 2 == 0);
                    int friend_dpu_ptr = dpu_ptr + 1;

                    
                    // have a dpu slot to use
                    bool is_ok =
                        dpu_info[dpu_ptr].enable_slot_num <= SLOT_NUM - 1;

                    bool is_friend_ok =
                        dpu_info[friend_dpu_ptr].enable_slot_num <=
                        SLOT_NUM - 1;

                    bool is_enabled = dpu_info[dpu_ptr].is_enabled;
                    bool is_friend_enabled =
                        dpu_info[friend_dpu_ptr].is_enabled;

                    bool is_smaller_than_friend = dpu_info[dpu_ptr].workload <
                                                  dpu_info[friend_dpu_ptr].workload;

                    if (k <= cluster_info[i]->s_num - 2)
                    {
                        if (is_ok && is_friend_ok && is_enabled &&
                            is_friend_enabled)
                        {
                            dpu_info[dpu_ptr].workload +=
                                cluster_info[i]->workload_pershard;

                            dpu_info[friend_dpu_ptr].workload +=
                                cluster_info[i]->workload_pershard;

                            pair_location.dpu_id = dpu_ptr;
                            pair_location.slot_id = dpu_info[dpu_ptr]
                                                        .enable_slot_num;
                            dpu_info[dpu_ptr].enable_slot_num++;
                            int shard_l = cluster_info[i]->s_len[k];
                            dpu_info[dpu_ptr].slot_info[pair_location.slot_id] =
                                {i, k, shard_l, true};

                            pair_location.dpu_id1 = friend_dpu_ptr;
                            pair_location.slot_id1 =
                                dpu_info[friend_dpu_ptr].enable_slot_num;

                            dpu_info[friend_dpu_ptr].enable_slot_num++;
                            shard_l = cluster_info[i]->s_len[k + 1];
                            dpu_info[friend_dpu_ptr]
                                .slot_info[pair_location.slot_id1] = {
                                i, k + 1, shard_l, true};

                            int pair_shard_id = k / 2;

                            cluster_info[i]->pair_shard_info[pair_shard_id].pair_location.push_back(pair_location);

                            place_ok = true;
                        }
                        else
                        {
                        }
                    }
                    else if (k == cluster_info[i]->s_num - 1)
                    {
                        // will not come here, because s_num is even
                        if (is_ok && is_enabled)
                        {
                            dpu_info[dpu_ptr].workload +=
                                cluster_info[i]->workload_pershard;
                            pair_location.dpu_id = dpu_ptr;
                            pair_location.slot_id =
                                dpu_info[dpu_ptr].enable_slot_num;
                            dpu_info[dpu_ptr].enable_slot_num++;
                            int shard_l = cluster_info[i]->s_len[k];
                            dpu_info[dpu_ptr].slot_info[pair_location.slot_id] =
                                {i, k, shard_l, true};

                            int pair_shard_id = k / 2;
                            cluster_info[i]->pair_shard_info[pair_shard_id].pair_location.push_back(pair_location);

                            place_ok = true;
                        }
                        else if (is_friend_ok && is_friend_enabled)
                        {
                            dpu_info[friend_dpu_ptr].workload +=
                                cluster_info[i]->workload_pershard;

                            pair_location.dpu_id1 = friend_dpu_ptr;
                            pair_location.slot_id1 =
                                dpu_info[friend_dpu_ptr].enable_slot_num;
                            dpu_info[friend_dpu_ptr].enable_slot_num++;
                            int shard_l = cluster_info[i]->s_len[k];
                            dpu_info[friend_dpu_ptr]
                                .slot_info[pair_location.slot_id1] = {
                                i, k, shard_l, true};

                            int pair_shard_id = k / 2;

                            cluster_info[i]->pair_shard_info[pair_shard_id].pair_location.push_back(pair_location);

                            place_ok = true;
                        }
                        else
                        {
                        }
                    }

                    /*-------------dpu_ptr move-----------------*/
                    dpu_ptr = dpu_ptr + 2;
                    if (dpu_ptr >= MAX_DPU)
                    {
                        dpu_ptr = 0;
                        loop_count++; 
                    }
                }
            }
        }
    }

    timer.end();

    int max_slot_num = 0;
    for (int i = 0; i < MAX_DPU; i++)
    {
        if (dpu_info[i].is_enabled)
        {
            assert(dpu_info[i].enable_slot_num <= SLOT_NUM);
            max_slot_num = std::max(max_slot_num, dpu_info[i].enable_slot_num);
        }
    }
    int slot_num_micro = SLOT_NUM;
    printf("max_slot_num: %d, SLOT_NUM: %d\n", max_slot_num, slot_num_micro);
}

DATA_TYPE *DPUWrapper::getdata(int c_id, int s_id)
{
    faiss::InvertedLists::ScopedCodes scodes(fifo_index->invlists, c_id);
    const uint8_t *codes = scodes.get();
    assert(fifo_index->code_size == MY_PQ_M * sizeof(DATA_TYPE));
    return (DATA_TYPE *)codes + cluster_info[c_id]->s_offset[s_id] * MY_PQ_M;
}

ID_TYPE *DPUWrapper::getids(int c_id, int s_id)
{
    faiss::InvertedLists::ScopedIds sids(fifo_index->invlists, c_id);
    const faiss::idx_t *ids = sids.get();
    return (ID_TYPE *)ids + cluster_info[c_id]->s_offset[s_id];
}

void DPUWrapper::copy_dataset()
{
    int type = 1;
    /*------------------slow copy------------------------*/
    if (type == 0)
    {
        for (int i = 0; i < mconfig.getMaxCluster(); i++)
        {
            if (cluster_info[i]->replica == 0)
            {
                continue;
            }
            for (int j = 0; j < cluster_info[i]->s_num; j += 2)
            {

                for (auto &pair_location : cluster_info[i]->pair_shard_info[j / 2].pair_location)
                {
                    // DATA_TYPE *data = getdata(i, j);
                    // ID_TYPE *ids = getids(i, j);
                    // int dpu_id = pair_location.dpu_id;
                    // int slot_id = pair_location.slot_id;
                    // int alihn8 = (cluster_info[i]->s_len[j] + 7) & ~7;
                    // dpu_set_t dpu = dpu_info[dpu_id].dpu;

                    // DPU_ASSERT(dpu_copy_to(
                    //     dpu,
                    //     "data",
                    //     slot_id * SLOT_DATA_SIZE,
                    //     data,
                    //     alihn8 * MY_PQ_M * sizeof(DATA_TYPE)));
                    // DPU_ASSERT(dpu_copy_to(
                    //     dpu,
                    //     "data_id",
                    //     slot_id * SLOT_ID_SIZE,
                    //     ids,
                    //     alihn8 * sizeof(ID_TYPE)));

                    // data = getdata(i, j + 1);
                    // ids = getids(i, j + 1);
                    // dpu_id = pair_location.dpu_id1;
                    // slot_id = pair_location.slot_id1;
                    // alihn8 = (cluster_info[i]->s_len[j + 1] + 7) & ~7;
                    // dpu = dpu_info[dpu_id].dpu;

                    // DPU_ASSERT(dpu_copy_to(
                    //     dpu,
                    //     "data",
                    //     slot_id * SLOT_DATA_SIZE,
                    //     data,
                    //     alihn8 * MY_PQ_M * sizeof(DATA_TYPE)));
                    // DPU_ASSERT(dpu_copy_to(
                    //     dpu,
                    //     "data_id",
                    //     slot_id * SLOT_ID_SIZE,
                    //     ids,
                    //     alihn8 * sizeof(ID_TYPE)));

                    {
                        int cluster_id = i;
                        int shard_id = j;
                        LOCATION location;
                        location.dpu_id = pair_location.dpu_id;
                        location.slot_id = pair_location.slot_id;

                        int dpu_id = location.dpu_id;

                        uint32_t data_offset = location.slot_id * SLOT_DATA_SIZE;
                        DATA_TYPE *data1 = getdata(cluster_id, shard_id);
                        DATA_TYPE *data2 = getdata(cluster_id, shard_id + 1);
                        int len1 = cluster_info[cluster_id]->s_len[shard_id];
                        int len2 = cluster_info[cluster_id]->s_len[shard_id + 1];

                        int len = len1 > len2 ? len1 : len2;
                        int alihn8 = (len + 7) / 8 * 8;

                        size_t data_size = alihn8 * MY_PQ_M * sizeof(DATA_TYPE);

                        fifo_dpu_copy_to(
                            dpu_id,
                            dpu_id + 1,
                            2,
                            "data",
                            data_offset,
                            (void *)data1,
                            (void *)data2,
                            data_size,
                            0);

                        uint32_t id_offset = location.slot_id * SLOT_ID_SIZE;
                        ID_TYPE *id1 = getids(cluster_id, shard_id);
                        ID_TYPE *id2 = getids(cluster_id, shard_id + 1);

                        size_t id_size = alihn8 * sizeof(ID_TYPE);

                        fifo_dpu_copy_to(
                            dpu_id,
                            dpu_id + 1,
                            2,
                            "data_id",
                            id_offset,
                            (void *)id1,
                            (void *)id2,
                            id_size,
                            0);
                    }
                }
            }
        }
    }
    else if (type == 1)
    {
        /*------------------fast copy------------------------*/
        int xfer_l = 0;
        for (int i = 0; i < MAX_DPU; i++)
        {
            if (dpu_info[i].is_enabled)
            {
                xfer_l = std::max(xfer_l, dpu_info[i].enable_slot_num);
            }
        }
        int batch_num = 5;
        int xfer_l_start = 0;
        int xfer_l_len = xfer_l / batch_num;
        for (int i = 0; i < batch_num; i++)
        {
            if (i == batch_num - 1)
            {
                xfer_l_len = xfer_l - xfer_l_start;
            }
            DATA_TYPE *data = new DATA_TYPE
                [MAX_DPU * xfer_l_len * SLOT_DATA_SIZE / sizeof(DATA_TYPE)];
            ID_TYPE *ids = new ID_TYPE
                [MAX_DPU * xfer_l_len * SLOT_ID_SIZE / sizeof(ID_TYPE)];

            for (int j = 0; j < MAX_DPU; j++)
            {
                int slot_start, slot_end;
                SLOT_INFO *slot_info = dpu_info[j].slot_info;
                int enable_slot_num = dpu_info[j].enable_slot_num;
                if (enable_slot_num > xfer_l_start + xfer_l_len)
                {
                    slot_start = xfer_l_start;
                    slot_end = xfer_l_start + xfer_l_len;
                }
                else if (enable_slot_num <= xfer_l_start)
                {
                    slot_start = 0;
                    slot_end = 0;
                    continue;
                }
                else
                {
                    slot_start = xfer_l_start;
                    slot_end = enable_slot_num;
                }
                for (int slot_id = slot_start; slot_id < slot_end; slot_id++)
                {
                    int c_id = slot_info[slot_id].c_id;
                    int s_id = slot_info[slot_id].shard_id;
                    int shard_l = slot_info[slot_id].shard_l;

                    int align8 = (shard_l + 7) & ~7;

                    DATA_TYPE *data_tmp = getdata(c_id, s_id);
                    ID_TYPE *ids_tmp = getids(c_id, s_id);

                    memcpy(&data[(j * xfer_l_len + slot_id - slot_start) *
                                 SLOT_DATA_SIZE / sizeof(DATA_TYPE)],
                           (void *)data_tmp,
                           align8 * MY_PQ_M * sizeof(DATA_TYPE));

                    memcpy(&ids[(j * xfer_l_len + slot_id - slot_start) *
                                SLOT_ID_SIZE / sizeof(ID_TYPE)],
                           (void *)ids_tmp,
                           align8 * sizeof(ID_TYPE));
                }
            }

            dpu_set_t dpu;

            DPU_FOREACH(fifo_set, dpu)
            {
                int dpu_id = GET_DPU_ID_BY_DPU(dpu);
                DPU_ASSERT(dpu_prepare_xfer(
                    dpu,
                    &data[dpu_id * xfer_l_len * SLOT_DATA_SIZE /
                          sizeof(DATA_TYPE)]));
            }
            DPU_ASSERT(dpu_push_xfer(
                fifo_set,
                DPU_XFER_TO_DPU,
                "data",
                xfer_l_start * SLOT_DATA_SIZE,
                xfer_l_len * SLOT_DATA_SIZE,
                DPU_XFER_DEFAULT));

            DPU_FOREACH(fifo_set, dpu)
            {
                int dpu_id = GET_DPU_ID_BY_DPU(dpu);
                DPU_ASSERT(dpu_prepare_xfer(
                    dpu,
                    &ids[dpu_id * xfer_l_len * SLOT_ID_SIZE /
                         sizeof(ID_TYPE)]));
            }
            DPU_ASSERT(dpu_push_xfer(
                fifo_set,
                DPU_XFER_TO_DPU,
                "data_id",
                xfer_l_start * SLOT_ID_SIZE,
                xfer_l_len * SLOT_ID_SIZE,
                DPU_XFER_DEFAULT));

            // last do
            xfer_l_start += xfer_l_len;

            delete[] data;
            delete[] ids;
        }
    }
}

void DPUWrapper::init_balance(int nprobe, int top_k)
{
    /*-----------get balance info-----------*/

    int nq;
    int dim;

    float *query_data = read_query(history_query_path.c_str(), QUERY_TYPE, nq, dim);

    assert(dim == DIM);

    faiss::idx_t *idx = new faiss::idx_t[nq * nprobe];
    float *dis = new float[nq * nprobe];

    fifo_index->quantizer->search(nq, query_data, nprobe, dis, idx, nullptr);
    for (int i = 0; i < nq; i++)
    {
        for (int j = 0; j < nprobe; j++)
        {
            cluster_info[idx[i * nprobe + j]]->history_freq++;
        }
    }

    std::ofstream outfile(freq_path, std::ios::trunc);
    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        outfile << cluster_info[i]->history_freq << std::endl;
    }
    outfile.close();

    outfile.open(size_path, std::ios::trunc);

    int64_t size_all = 0;
    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        int size = fifo_index->get_list_size(i);
        outfile << size << std::endl;
        size_all += size;
    }
    outfile.close();

    // assert(size_all == 1000000000);

    std::ifstream infile(freq_path);
    int freq[mconfig.getMaxCluster()];
    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        infile >> freq[i];
    }
    infile.close();

    infile.open(size_path);
    int size[mconfig.getMaxCluster()];
    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        infile >> size[i];
    }
    infile.close();

    int shard_num[mconfig.getMaxCluster()];
    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        int slots = (int)ceil((double)size[i] / SLOT_L);
        slots = std::max(1, slots);
        shard_num[i] = (slots % 2 == 0) ? slots : slots + 1;
    }
    int shard_size[mconfig.getMaxCluster()];

    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        shard_size[i] = size[i] / shard_num[i];
        cluster_info[i]->shard_size = shard_size[i];
    }
    int shard_workload[mconfig.getMaxCluster()];
    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        shard_workload[i] = freq[i] * shard_size[i];
    }
    int64_t total_shard_workload = 0;
    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        total_shard_workload += shard_workload[i];
    }
    double rate_each_shard[mconfig.getMaxCluster()];
    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        rate_each_shard[i] = (double)shard_workload[i] / total_shard_workload;
    }

    double sum_rate = 0.0;
    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        sum_rate += rate_each_shard[i];
    }
    printf("sum_rate: %f\n", sum_rate);

    double to1_rate_each_shard[mconfig.getMaxCluster()];
    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        to1_rate_each_shard[i] = rate_each_shard[i] / sum_rate;
    }

    int nr_dpu_pair = 0;
    for (int i = 0; i < MAX_DPU; i += 2)
    {
        if (dpu_info[i].is_enabled)
        {
            if (dpu_info[i + 1].is_enabled)
            {
                nr_dpu_pair++;
            }
        }
    }
    have_slot = nr_dpu_pair * 2 * SLOT_NUM;

    double copy_rate = COPY_RATE;

    int copy_num[mconfig.getMaxCluster()];
    int shard_copy_num[mconfig.getMaxCluster()];
    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        copy_num[i] = 0;
        shard_copy_num[i] = 0;
    }


    // add copy num

    while (true)
    {
        need_slot = 0;
        for (int i = 0; i < mconfig.getMaxCluster(); i++)
        {
            copy_num[i] = to1_rate_each_shard[i] * mconfig.getMaxCluster() * copy_rate;
            if (copy_num[i] < 1)
            {
                copy_num[i] = 1;
            }
            if (freq[i] == 0)
            {
                copy_num[i] = 0;
            }
            shard_copy_num[i] = copy_num[i] * shard_num[i];
            need_slot += shard_copy_num[i];
        }

        if (need_slot <= have_slot)
        {
            break;
        }
        copy_rate -= 0.5;
        if (copy_rate <= 1.0)
        {
            for (int i = 0; i < mconfig.getMaxCluster(); i++)
            {
                copy_num[i] = 1;
            }
            need_slot = 0;
            for (int i = 0; i < mconfig.getMaxCluster(); i++)
            {
                shard_copy_num[i] = copy_num[i] * shard_num[i];
                need_slot += shard_copy_num[i];
            }
            if (need_slot <= have_slot)
            {
                break;
            }
            else
            {
                printf("you should set COPY_RATE larger\n");
                exit(0);
            }
        }
    }
    copy_rate_select = copy_rate;

    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        cluster_info[i]->replica = copy_num[i];
    }

    if (ENABLE_REPLICA == 0)
    {

        for (int i = 0; i < mconfig.getMaxCluster(); i++)
        {
            cluster_info[i]->replica = 1;
        }
    }
    else
    {
        // if (copy_rate < COPY_RATE)
        // {
        //     printf("COPY RATE is set to large\n");
        //     exit(0);
        // }
        printf("copy_rate = %f, need_slot = %d, have_slot = %d\n",
               copy_rate,
               need_slot,
               have_slot);
    }

    int workload_perreplica[mconfig.getMaxCluster()];
    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        if (copy_num[i] == 0)
        {
            workload_perreplica[i] = 0;
        }
        else
        {
            workload_perreplica[i] = shard_workload[i] / copy_num[i];
        }
    }

    outfile.open(workload_path, std::ios::trunc);
    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        outfile << workload_perreplica[i] << std::endl;
    }
    outfile.close();

    outfile.open(replica_path, std::ios::trunc);
    for (int i = 0; i < mconfig.getMaxCluster(); i++)
    {
        outfile << copy_num[i] << std::endl;
    }
    outfile.close();
}

void DPUWrapper::init_dataset()
{
    shard_dataset();
    replica_dataset();
    place_dataset();

    // use fake_send_task
    copy_dataset();
}

void DPUWrapper::dpu_init()
{
    /*--------file init--------*/
    std::string json_path = std::string(PROJECT_SOURCE_DIR) + "/config.json";
    printf("json_path: %s\n", json_path.c_str());
    mconfig = mConfig(json_path);

    cluster_info.resize(mconfig.getMaxCluster());
    for (auto &cluster : cluster_info)
    {
        cluster = std::make_shared<CLUSTER_INFO>();
    }

    /*---------init dpu--------*/

    index_path = std::string(mconfig.getIndexPath());
    query_path = std::string(mconfig.getQueryPath());
    history_query_path = std::string(mconfig.getHistoryQueryPath());

    groundtruth_path = std::string(mconfig.getGroundTruthPath());
    freq_path = std::string(mconfig.getFreqPath()) + ".txt";
    size_path = std::string(mconfig.getSizePath()) + ".txt";
    replica_path = std::string(mconfig.getReplicaPath()) + ".txt";
    workload_path = std::string(mconfig.getWorkloadPath()) + ".txt";
    dpu_result_path = std::string(mconfig.getDpuResultPath());
    cpu_result_path = std::string(mconfig.getCpuResultPath());

#if defined(TEST_BATCH_DPU)
    DPU_ASSERT(dpu_alloc(NR_DPU, NULL, &fifo_set));
    DPU_ASSERT(dpu_load(fifo_set, BATCH_DPU_BINARY, NULL));
#elif defined(TEST_DPU)
    DPU_ASSERT(dpu_alloc(NR_DPU, NULL, &fifo_set));
    DPU_ASSERT(dpu_load(fifo_set, DPU_BINARY, NULL));
#else
    // do nothing
#endif

    outfile_log_out.open(debug_path_log_out, std::ios::trunc);
    outfile_log_in.open(debug_path_log_in, std::ios::trunc);
    outfile_log_out1.open(debug_path_log_out1, std::ios::trunc);
    outfile_log_in1.open(debug_path_log_in1, std::ios::trunc);

    /*----- index and QUERY INFO----------------------------------*/

    fifo_index = dynamic_cast<faiss::IndexIVFPQ *>(faiss::read_index(index_path.c_str()));

    int dim;
    float *query_data = read_query(query_path.c_str(), QUERY_TYPE, query_num, dim);

    printf("query_num: %d, dim: %d\n", query_num, dim);

    query_info.resize(query_num);

    for (auto &query_info_ : query_info)
    {
        query_info_ = std::make_shared<QUERY_INFO>();
    }

    assert(dim == DIM);

    int nq_gt;
    int k_gt;
    ID_TYPE *groundtruth_ids_arr =
        read_groundtruth(groundtruth_path.c_str(), nq_gt, k_gt);

    assert(nq_gt >= query_num);
    assert(k_gt >= MAX_K);

    for (int i = 0; i < query_num; i++)
    {
        query_info[i]->ground_truth_ids = groundtruth_ids_arr + i * k_gt;
    }

    for (int i = 0; i < query_num; i++)
    {
        query_info[i]->query_data = query_data + i * DIM;
        query_info[i]->q_id = i;

        for (int j = 0; j < MAX_K; j++)
        {
            query_info[i]->simi_values[j] =
                std::numeric_limits<DIST_TYPE>::max();
            query_info[i]->simi_ids[j] = -1;
        }

        query_info[i]->idx = new faiss::idx_t[MAX_NPROBE];
        query_info[i]->coarse_dis = new float[MAX_NPROBE];
    }

#ifdef TEST_CPU

    return;
#endif

    /*--------init value------*/

    for (int i = 0; i < FRONT_THREAD * MAX_COROUTINE; i++)
    {
        float *sim_table_tmp = new float[mconfig.getMaxCluster() * LUT_SIZE];
        sim_table_buffer[i] = sim_table_tmp;
        float *dis0_tmp = new float[mconfig.getMaxCluster()];
        dis0_buffer[i] = dis0_tmp;

        DIST_TYPE *sim_table_tmp_dynamic =
            new DIST_TYPE[mconfig.getMaxCluster() * LUT_SIZE];
        sim_table_dynamictype[i] = sim_table_tmp_dynamic;

        DIST_TYPE *dis0_tmp_dynamic = new DIST_TYPE[mconfig.getMaxCluster()];
        dis0_dynamictype[i] = dis0_tmp_dynamic;

        bool *send_flag_tmp = new bool[mconfig.getMaxCluster() * MAX_SHARD];
        send_flag_buffer[i] = send_flag_tmp;
    }

    /*----------------init scanner----------------*/

    for (int i = 0; i < FRONT_THREAD * MAX_COROUTINE; i++)
    {
        scanner[i] = fifo_index->get_InvertedListScanner(false, NULL);
    }

    /*------------------init symbol------------*/

    uint32_t offset_query =
        fifo_get_symbol_offset(fifo_set, symbol1.c_str(), 0);
    offset_query &= ~(0x08000000u);

    pre_symbol_offset.insert(std::make_pair(symbol1, offset_query));

    /*---------------init DPU INFO-------------------*/
    struct dpu_set_t dpu;
    DPU_FOREACH(fifo_set, dpu)
    {
        int dpu_id = GET_DPU_ID_BY_DPU(dpu);

        DPU_INFO_COMMON info;
        info.dpu_id = dpu_id;
        info.rank_id = dpu_get_rank_allocator_id(dpu.dpu);
        info.chip_id = dpu_get_slice_id(dpu.dpu);
        info.chip_dpu_id = dpu_get_member_id(dpu.dpu);
        DPU_ASSERT(dpu_copy_to(
            dpu, "dpu_info_common", 0, &info, sizeof(DPU_INFO_COMMON)));
        dpu_info[dpu_id].dpu_info_common = info;
        dpu_info[dpu_id].is_enabled = true;

        dpu_info[dpu_id].dpu = dpu;

        // /*----------------disable dpu 7,0,4--------------*/
        if (dpu_id == GET_DPU_ID(7, 0, 4))
        {
            dpu_info[dpu_id].is_enabled = false;
            dpu_disable_one_dpu(dpu.dpu);
        }

        dpu_info[dpu_id].dpu_id = dpu_id;
        dpu_info[dpu_id].pair_id = GET_PAIR_ID_BY_DPU(dpu);

        dpu_info[dpu_id].line_id = GET_LINE_ID_BY_DPU(dpu);
        dpu_info[dpu_id].pair_line_id = GET_PAIR_LINE_ID_BY_DPUS(dpu);
        dpu_info[dpu_id].rank_id = dpu_get_rank_allocator_id(dpu.dpu);
    }

    /*----------------------init PAIR INFO-----------------------*/
    for (int i = 0; i < MAX_PAIR; i++)
    {
        int dpu_id = i * 2;
        int friend_dpu_id = i * 2 + 1;
        pair_info[i].is_first_dpu_enable = dpu_info[dpu_id].is_enabled;
        pair_info[i].is_second_dpu_enable = dpu_info[friend_dpu_id].is_enabled;
        if (pair_info[i].is_first_dpu_enable)
        {
            pair_info[i].enable_num++;
        }
        if (pair_info[i].is_second_dpu_enable)
        {
            pair_info[i].enable_num++;
        }
    }

    /*----------------------init RANK INFO-----------------------*/
    struct dpu_set_t rank;

    DPU_RANK_FOREACH(fifo_set, rank)
    {
        int rank_id = dpu_get_rank_allocator_id2(rank.list.ranks[0]);
        int rank_id_hw = dpu_get_rank_id(rank.list.ranks[0]);
        // printf("rank_id: %d, rank_id_hw: %d\n", rank_id, rank_id_hw);

        rank_info[rank_id].rank = rank;
        rank_info[rank_id].rank_id = rank_id;
        rank_info[rank_id].is_Enabled = true;

        uint64_t *ptr = get_rank_ptr(rank.list.ranks[0]);
        rank_info[rank_id].region_ptr = ptr;
    }

    /*-------------------init fifo------------------------*/
    FIFOinit();

    /*------------------------init CLUSTER INFO---------------------*/
}

void DPUWrapper::dpu_reset()
{
    /*----- QUERY INFO----------------------------------*/

    for (int i = 0; i < query_num; i++)
    {
        for (int j = 0; j < MAX_K; j++)
        {
            query_info[i]->simi_values[j] =
                std::numeric_limits<DIST_TYPE>::max();
            query_info[i]->simi_ids[j] = -1;
        }
        query_info[i]->complete_num = 0;
        query_info[i]->wait_dpu_num = 0;
    }

    next_qid = 0;

    num_is_running = 0;

    stopBackthread = false;

    /*---------init PAIR INFO---------------*/
    for (int i = 0; i < MAX_PAIR; i++)
    {
        assert(pair_info[i].is_running == false);
        assert(pair_info[i].first_dpu_back == false);
        assert(pair_info[i].second_dpu_back == false);
    }

    for (int i = 0; i < BACK_THREAD; i++)
    {
        back_thread_info[i].stopFlag = false;
        back_thread_info[i].already_stop = false;
    }
}

void DPUWrapper::level1_search(int q_id, int k, int nprobe)
{
    std::shared_ptr<QUERY_INFO> query = query_info[q_id];
    query->k = k;
    query->nprobe = nprobe;
    for (int i = 0; i < nprobe; i++)
    {
        query->idx[i] = -1;
        query->coarse_dis[i] = std::numeric_limits<float>::max();
    }

    fifo_index->quantizer->search(1, query->query_data, nprobe, query->coarse_dis, query->idx, nullptr);
}

void DPUWrapper::get_freq_num(int q_id, int k, int nprobe)
{
    std::shared_ptr<QUERY_INFO> query = query_info[q_id];
    query->k = k;
    query->nprobe = nprobe;

    for (int i = 0; i < nprobe; i++)
    {
        query->idx[i] = -1;
        query->coarse_dis[i] = std::numeric_limits<float>::max();
    }

    fifo_index->quantizer->search(1, query->query_data, nprobe, query->coarse_dis, query->idx, nullptr);

    for (int i = 0; i < nprobe; i++)
    {
        cluster_info[query->idx[i]]->history_freq++;
    }
}

void DPUWrapper::fake_send_task(dpu_fifo_input_t &query)
{
    int k = query.k;
    int q_id = query.q_id;

    DIST_TYPE *simi_values = query_info[q_id]->simi_values;
    ID_TYPE *simi_ids = query_info[q_id]->simi_ids;

    DIST_TYPE *LUT = query.LUT;
    int shard_l = query.shard_l;
    int dpu_id = query.dpu_id;
    int slot_id = query.slot_id;

    int c_id = dpu_info[dpu_id].slot_info[slot_id].c_id;
    int shard_id = dpu_info[dpu_id].slot_info[slot_id].shard_id;

    DATA_TYPE *data_this_slot = getdata(c_id, shard_id);
    ID_TYPE *data_id_this_slot = getids(c_id, shard_id);

    for (int j = 0; j < shard_l; j++)
    {
        DIST_TYPE sum = query.dis0;
        ID_TYPE id = data_id_this_slot[j];
        for (int l = 0; l < MY_PQ_M; l++)
        {
            uint8_t pqcode = data_this_slot[j * MY_PQ_M + l];
            sum += LUT[l * MY_PQ_CLUSTER + pqcode];
        }
        if (simi_values[0] > sum)
        {
            faiss::heap_replace_top<faiss::CMax<DIST_TYPE, ID_TYPE>>(
                k, simi_values, simi_ids, sum, id);
        }
    }
}

void DPUWrapper::test_fifo()
{
    for (int dpu_id = 0; dpu_id < MAX_DPU; dpu_id++)
    {
        if (dpu_info[dpu_id].is_enabled == false)
        {
            continue;
        }
        if (dpu_id > 0)
        {
            continue;
        }

        mtx_input_link_rankid[dpu_info[dpu_id].rank_id].lock();

        dpu_set_t dpu = dpu_info[dpu_id].dpu;

        dpu_set_t rank = rank_info[dpu_info[dpu_id].rank_id].rank;

        DPU_ASSERT(dpu_fifo_push_xfer(rank, &output_link, DPU_XFER_NO_RESET));

        DPU_FOREACH(rank, dpu)
        {
            int sz = get_fifo_size(&output_link, dpu);
            int id_ = GET_DPU_ID_BY_DPU(dpu);
            printf("sz = %d\n", sz);
            for (int i = 0; i < sz; i++)
            {
                uint64_t *out_data = (uint64_t *)get_fifo_elem(
                    &output_link,
                    dpu,
                    &output_fifo_data[OFFSET_OUT_FIFO(id_)],
                    0);

                printf("out_data[%d] = %lu\n", i, out_data[i]);
            }
        }

        mtx_input_link_rankid[dpu_info[dpu_id].rank_id].unlock();
    }
}

void DPUWrapper::level2_search(
    coroutine<void>::push_type &sink,
    int q_id,
    int k,
    int nprobe,
    int thread_id,
    int coroutine_id)
{
    // xmh::Timer timer("   1.2.1:compute LUT");
    auto start = std::chrono::high_resolution_clock::now(); 

    int t_c_id = thread_id * MAX_COROUTINE + coroutine_id;

    std::shared_ptr<QUERY_INFO> query = query_info[q_id];

    scanner[t_c_id]->set_query(query->query_data);

    float *dis0_ = dis0_buffer[t_c_id];

    float *sim_table_ = sim_table_buffer[t_c_id];
    DIST_TYPE *sim_table_dynamic = sim_table_dynamictype[t_c_id];

    DIST_TYPE *dis0_dynamic = dis0_dynamictype[t_c_id];

    for (int i = 0; i < query->nprobe; i++)
    {
        scanner[t_c_id]->set_list(query->idx[i], query->coarse_dis[i]);
        scanner[t_c_id]->get_sim_table(
            &sim_table_[query->idx[i] * LUT_SIZE], LUT_SIZE);

        // memcpy(
        //     &sim_table_[query->idx[i] * LUT_SIZE],
        //     scanner[t_c_id]->sim_table,
        //     LUT_SIZE * sizeof(float));

        int c_id = query->idx[i];

        scanner[t_c_id]->get_dis0(dis0_[c_id]);

        // dis0_[c_id] = scanner[t_c_id]->dis0;

        for (int j = 0; j < LUT_SIZE; j++)
        {
            sim_table_dynamic[c_id * LUT_SIZE + j] =
                (DIST_TYPE)(sim_table_[c_id * LUT_SIZE + j]);
        }
        dis0_dynamic[c_id] = (DIST_TYPE)(dis0_[c_id]);
    }

    // timer.end();

    auto end = std::chrono::high_resolution_clock::now(); 
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    xmh::PerfCounter::Record("LUT construct", duration.count());

    xmh::Timer timer1("   1.2.2:send task");

    bool *send_flag_ = send_flag_buffer[t_c_id];
    int need_send_num = 0;

    for (int i = 0; i < query->nprobe; i++)
    {
        faiss::idx_t key = query->idx[i];

        std::shared_ptr<CLUSTER_INFO> cluster = cluster_info[key];

        int pair_shard_num = cluster->pair_shard_num;

        for (int j = 0; j < pair_shard_num; j++)
        {
            send_flag_[key * MAX_SHARD + j] = true;
            need_send_num++;
        }
    }
    xmh::PerfCounter::Record("need_send_num", need_send_num);

    int enter_loop_num = 0;
    double rate_first_loop = 0;
    int get_lock_num = 0;
    int all_num = 0;
    bool is_first_loop = true;

    while (true)
    {
        enter_loop_num++;

        for (int i = 0; i < query->nprobe; i++)
        {
            faiss::idx_t key = query->idx[i];
            std::shared_ptr<CLUSTER_INFO> cluster = cluster_info[key];

            int pair_shard_num = cluster->pair_shard_num;

            if (MAX_COROUTINE == 1)
            {
                for (int j = 0; j < pair_shard_num; j++)
                {
                    if (is_first_loop)
                    {
                        all_num++;
                    }

                    bool send_success = false;

                    cluster->pair_shard_info[j].rw_mutex.lock_shared();

                    while (true)
                    {
                        if (send_success)
                        {
                            break;
                        }

                        for (auto pair_location : cluster->pair_shard_info[j].pair_location)
                        {
                            if (send_flag_[key * MAX_SHARD + j] == false)
                            {
                                continue;
                            }

                            PAIR_TASK pair_task;
                            int dpu_id = pair_location.dpu_id;

                            int pair_id = dpu_id / 2;
                            int rank_id = dpu_id / 64;
                            pair_task.enable_num = 2;

                            pair_info[pair_id].mtx_dpu_run.lock();

                            if (pair_info[pair_id].is_running == false)
                            {
                                pair_info[pair_id].is_running = true;
                                assert(pair_info[pair_id].enable_num == 2);

                                pair_info[pair_id].mtx_dpu_run.unlock();
                                if (is_first_loop)
                                {
                                    get_lock_num++;
                                }

                                num_is_running += 2;

                                // if (num_is_running > 2400)
                                // {
                                //     xmh::PerfCounter counter;
                                //     counter.Record(
                                //         "num_is_running", num_is_running);
                                // }

                                send_flag_[key * MAX_SHARD + j] = false;
                                need_send_num--;
                                send_success = true;
                            }
                            else
                            {
                                pair_info[pair_id].mtx_dpu_run.unlock();

                                continue;
                            }

                            xmh::Timer timer4("   mram to dpu");

                            int enable_num = pair_task.enable_num;
                            pair_task.task[0].slot_id = pair_location.slot_id;
                            pair_task.task[1].slot_id = pair_location.slot_id1;

                            for (int task_id = 0; task_id < enable_num; task_id++)
                            {
                                pair_task.task[task_id].dpu_id = dpu_id + task_id;
                                pair_task.task[task_id].pair_id = pair_id;
                                pair_task.task[task_id].rank_id = rank_id;
                                pair_task.task[task_id].c_id = key;
                                int shard_id = j * 2 + task_id;
                                pair_task.task[task_id].shard_id = shard_id;
                                pair_task.task[task_id].shard_l = cluster->s_len[shard_id];

                                pair_task.task[task_id].q_id = q_id;
                                pair_task.task[task_id].k = k;
                                pair_task.task[task_id].dis0 =
                                    dis0_dynamic[key];
                                memcpy(pair_task.task[task_id].LUT,
                                       &sim_table_dynamic[key * LUT_SIZE],
                                       sizeof(DIST_TYPE) * LUT_SIZE);
                            }

                            if (enable_num == 2)
                            {
                                dpu_fifo_input_t &task_ = pair_task.task[0];

                                int dpu_id = task_.dpu_id;

                                if (dpu_id == 0)
                                {
                                    // dpu_info[dpu_id].real_time_when_dpu_send.push_back(
                                    //         std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    //                 std::chrono::steady_clock::now());

                                    // dpu_info[dpu_id].real_time_when_dpu_back.push_back(
                                    //         std::chrono::duration_cast<
                                    //                 std::chrono::nanoseconds>(
                                    //                 std::chrono::steady_clock::now()
                                    //                         .time_since_epoch()));

                                    // auto now =
                                    // std::chrono::system_clock::now(); auto
                                    // duration =
                                    // std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
                                    // std::cout<<"back: " << duration.count()
                                    // << " ms" << std::endl;
                                }

                                dpu_fifo_input_t &friend_task_ =
                                    pair_task.task[1];
                                int friend_dpu_id = friend_task_.dpu_id;

                                fifo_dpu_copy_to(
                                    dpu_id,
                                    friend_dpu_id,
                                    2,
                                    "query",
                                    0,
                                    (void *)&task_,
                                    (void *)&friend_task_,
                                    sizeof(dpu_fifo_input_t),
                                    0);
                            }
                            else if (enable_num == 1)
                            {
                                dpu_fifo_input_t &task_ = pair_task.task[0];

                                int dpu_id = task_.dpu_id;
                                fifo_dpu_copy_to(
                                    dpu_id,
                                    -1,
                                    1,
                                    "query",
                                    0,
                                    (void *)&task_,
                                    NULL,
                                    sizeof(dpu_fifo_input_t),
                                    0);
                            }
                            else
                            {
                                exit(-1);
                            }
                            timer4.end();

                            xmh::Timer timer3("   wram to dpu");

                            mtx_input_link_rankid[rank_id].lock();

                            query->wait_dpu_num += enable_num;

                            xmh::Timer timer5(
                                "   actual wram to dpu, enable num = 2");

                            for (int j = 0; j < enable_num; j++)
                            {
                                dpu_fifo_input_t &task_ = pair_task.task[j];

                                int dpu_id = task_.dpu_id;

                                dpu_set_t &dpu = dpu_info[dpu_id].dpu;

                                DPU_ASSERT(dpu_fifo_prepare_xfer(
                                    dpu,
                                    &input_link,
                                    (void *)&input_fifo_data[OFFSET_IN_FIFO(
                                        dpu_id)]));

                                DPU_ASSERT(dpu_fifo_push_xfer(
                                    dpu, &input_link, DPU_XFER_DEFAULT));
                            }
                            timer5.end();

                            mtx_input_link_rankid[rank_id].unlock();

                            timer3.end();
                        }
                    }

                    cluster->pair_shard_info[j].rw_mutex.unlock_shared();
                }
            }
            else
            {
                for (int j = 0; j < pair_shard_num; j++)
                {
                    if (is_first_loop)
                    {
                        all_num++;
                    }

                    cluster->pair_shard_info[j].rw_mutex.lock_shared();

                    for (auto pair_location : cluster->pair_shard_info[j].pair_location)
                    {
                        if (send_flag_[key * MAX_SHARD + j] == false)
                        {
                            continue;
                        }

                        PAIR_TASK pair_task;
                        pair_task.enable_num = 2;

                        int dpu_id = pair_location.dpu_id;
                        int pair_id = dpu_id / 2;
                        int rank_id = dpu_id / 64;

                        pair_info[pair_id].mtx_dpu_run.lock();

                        if (pair_info[pair_id].is_running == false)
                        {
                            pair_info[pair_id].is_running = true;
                            assert(pair_info[pair_id].enable_num == 2);

                            pair_info[pair_id].mtx_dpu_run.unlock();
                            if (is_first_loop)
                            {
                                get_lock_num++;
                            }

                            num_is_running += 2;

                            // if (num_is_running > 2400)
                            // {
                            //     xmh::PerfCounter counter;
                            //     counter.Record(
                            //         "num_is_running", num_is_running);
                            // }

                            send_flag_[key * MAX_SHARD + j] = false;
                            need_send_num--;
                        }
                        else
                        {
                            pair_info[pair_id].mtx_dpu_run.unlock();

                            continue;
                        }

                        // xmh::Timer timer4("   mram to dpu");
                        start = std::chrono::high_resolution_clock::now(); 

                        int enable_num = pair_task.enable_num;

                        pair_task.task[0].slot_id = pair_location.slot_id;
                        pair_task.task[1].slot_id = pair_location.slot_id1;

                        for (int task_id = 0; task_id < enable_num; task_id++)
                        {
                            pair_task.task[task_id].dpu_id = dpu_id + task_id;
                            pair_task.task[task_id].pair_id = pair_id;
                            pair_task.task[task_id].rank_id = rank_id;

                            pair_task.task[task_id].c_id = key;
                            int shard_id = j * 2 + task_id;
                            pair_task.task[task_id].shard_id = shard_id;

                            pair_task.task[task_id].shard_l = cluster->s_len[shard_id];

                            pair_task.task[task_id].q_id = q_id;
                            pair_task.task[task_id].k = k;
                            pair_task.task[task_id].dis0 = dis0_dynamic[key];
                            memcpy(pair_task.task[task_id].LUT,
                                   &sim_table_dynamic[key * LUT_SIZE],
                                   sizeof(DIST_TYPE) * LUT_SIZE);
                        }

                        end = std::chrono::high_resolution_clock::now(); 
                        duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

                        xmh::PerfCounter::Record("Task construct", duration.count());

                        start = std::chrono::high_resolution_clock::now(); 

                        if (enable_num == 2)
                        {
                            dpu_fifo_input_t &task_ = pair_task.task[0];

                            int dpu_id = task_.dpu_id;

                            dpu_info[dpu_id].need_compute_distance_num +=
                                task_.shard_l;

                            if (dpu_id == 0)
                            {
                                // dpu_info[dpu_id].real_time_when_dpu_send.push_back(
                                //         std::chrono::duration_cast<std::chrono::nanoseconds>(
                                //                 std::chrono::steady_clock::now());

                                // dpu_info[dpu_id].real_time_when_dpu_back.push_back(
                                //         std::chrono::duration_cast<
                                //                 std::chrono::nanoseconds>(
                                //                 std::chrono::steady_clock::now()
                                //                         .time_since_epoch()));

                                // auto now = std::chrono::system_clock::now();
                                // auto duration =
                                // std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
                                // std::cout<<"back: " << duration.count() << "
                                // ms" << std::endl;
                            }

                            dpu_fifo_input_t &friend_task_ = pair_task.task[1];
                            int friend_dpu_id = friend_task_.dpu_id;
                            dpu_info[friend_dpu_id].need_compute_distance_num +=
                                friend_task_.shard_l;

                            fifo_dpu_copy_to(
                                dpu_id,
                                friend_dpu_id,
                                2,
                                "query",
                                0,
                                (void *)&task_,
                                (void *)&friend_task_,
                                sizeof(dpu_fifo_input_t),
                                0);
                        }
                        else if (enable_num == 1)
                        {

                            printf("enable_num == 1, should not happen\n");
                            dpu_fifo_input_t &task_ = pair_task.task[0];

                            int dpu_id = task_.dpu_id;
                            fifo_dpu_copy_to(
                                dpu_id,
                                -1,
                                1,
                                "query",
                                0,
                                (void *)&task_,
                                NULL,
                                sizeof(dpu_fifo_input_t),
                                0);
                        }
                        else
                        {
                            exit(-1);
                        }
                        // timer4.end();

                       

                        xmh::Timer timer3("   wram to dpu");

                        mtx_input_link_rankid[rank_id].lock();

                        query->wait_dpu_num += enable_num;

                        xmh::Timer timer5(
                            "   actual wram to dpu, enable num = 2");

                        for (int j = 0; j < enable_num; j++)
                        {
                            dpu_fifo_input_t &task_ = pair_task.task[j];

                            int dpu_id = task_.dpu_id;

                            dpu_set_t &dpu = dpu_info[dpu_id].dpu;

                            DPU_ASSERT(dpu_fifo_prepare_xfer(
                                dpu,
                                &input_link,
                                (void *)&input_fifo_data[OFFSET_IN_FIFO(
                                    dpu_id)]));

                            DPU_ASSERT(dpu_fifo_push_xfer(
                                dpu, &input_link, DPU_XFER_DEFAULT));
                        }
                        timer5.end();

                        mtx_input_link_rankid[rank_id].unlock();

                        timer3.end();

                        end = std::chrono::high_resolution_clock::now(); 
                        duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

                        xmh::PerfCounter::Record("Copy data", duration.count());
                    }

                    cluster->pair_shard_info[j].rw_mutex.unlock_shared();
                }
            }
        }

        if (need_send_num == 0)
        {
            break;
        }
        is_first_loop = false;

        sink();
    }

    rate_first_loop = (double)get_lock_num / all_num;

    xmh::PerfCounter counter2;
    counter2.Record("enter_loop_num", enter_loop_num);
    counter2.Record("rate_first_loop", rate_first_loop);

    timer1.end();
};

int DPUWrapper::get_nextq()
{
    if (next_qid >= query_num)
    {
        return -1;
    }
    return next_qid++;
}
void DPUWrapper::cooperative(
    coroutine<void>::push_type &sink,
    int k,
    int nprobe,
    int thread_id,
    int coroutine_id)
{
    while (true)
    {
        xmh::Timer timer(" 1:one query send_task");

        mtx_nextq.lock();
        int q_id = get_nextq();
        mtx_nextq.unlock();

        if (q_id == -1)
        {
            return;
        }

        query_info[q_id]->start1();
        // xmh::Timer timer1("  1.1:level1_search");
        auto start = std::chrono::high_resolution_clock::now(); 

        level1_search(q_id, k, nprobe);

        auto end = std::chrono::high_resolution_clock::now(); 
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        xmh::PerfCounter::Record("level1_search", duration.count());

        xmh::Timer timer2("  1.2:level2_search");
        level2_search(sink, q_id, k, nprobe, thread_id, coroutine_id);
        timer2.end();

        query_info[q_id]->send_over = true;

        timer.end();
    }
}
void DPUWrapper::search(int k, int nprobe, int thread_id)
{
    bind_core(thread_id);
    xmh::Timer timer(" 1:one_thread_send_task");

    coroutine<void>::pull_type worker[MAX_COROUTINE];

    for (int i = 0; i < MAX_COROUTINE; i++)
    {
        worker[i] = coroutine<void>::pull_type(std::bind(
            &DPUWrapper::cooperative,
            this,
            std::placeholders::_1,
            k,
            nprobe,
            thread_id,
            i));
    }

    bool is_end = false;
    while (true)
    {
        is_end = true;
        for (int i = 0; i < MAX_COROUTINE; i++)
        {
            // TODO:
            if (worker[i])
            {
                worker[i]();
                is_end = false;
            }
        }
        if (is_end)
        {
            break;
        }
    }
    timer.end();
}

void DPUWrapper::cpu_search(int *q_id, int k, int nprobe, int batch_size)
{
    faiss::IVFPQSearchParameters params;
    params.nprobe = nprobe;
    float *simi_values = new float[k * batch_size];
    faiss::idx_t *simi_ids = new faiss::idx_t[k * batch_size];

    float *q_data = new float[batch_size * DIM];

    for (int i = 0; i < batch_size; i++)
    {
        memcpy(q_data + i * DIM,
               query_info[q_id[i]]->query_data,
               DIM * sizeof(float));
    }

    // for (int i = 0; i < batch_size; i++) {
    //     level1_search(q_id[i], k, nprobe);
    // }

    // return;

    fifo_index->search(batch_size, q_data, k, simi_values, simi_ids, &params);

    // copy to query_info
    for (int i = 0; i < batch_size; i++)
    {
        for (int j = 0; j < k; j++)
        {
            query_info[q_id[i]]->simi_values[j] = simi_values[i * k + j];
            query_info[q_id[i]]->simi_ids[j] = simi_ids[i * k + j];
        }
    }
}

void DPUWrapper::FIFOinit()
{
    for (int i = 0; i < FRONT_THREAD; i++)
    {
        xfer_matrix[i] = (struct fifo_dpu_transfer_matrix *)malloc(
            MAX_RANK * sizeof(struct fifo_dpu_transfer_matrix));
        xfer_matrix_from[i] = (struct fifo_dpu_transfer_matrix *)malloc(
            MAX_RANK * sizeof(struct fifo_dpu_transfer_matrix));
    }

    DPU_ASSERT(dpu_link_input_fifo(fifo_set, &input_link, "input_fifo"));
    DPU_ASSERT(dpu_link_output_fifo(fifo_set, &output_link, "output_fifo"));

    output_fifo_data = (uint8_t *)calloc(
        MAX_DPU * OUTPUT_FIFO_NUM * OUTPUT_FIFO_DATA_SIZE, 1);
    input_fifo_data = (uint8_t *)calloc(
        MAX_DPU * INPUT_FIFO_NUM * INPUT_FIFO_DATA_SIZE, 1);

    dpu_set_t dpu;

    /*---------------output FIFO------------------*/

    for (int i = 0; i < MAX_DPU; i++)
    {
        if (dpu_info[i].is_enabled == false)
        {
            continue;
        }
        dpu_set_t dpu = dpu_info[i].dpu;
        DPU_ASSERT(dpu_fifo_prepare_xfer(
            dpu,
            &output_link,
            (void *)&output_fifo_data[OFFSET_OUT_FIFO(i)]));
    }

    /*--------------input FIFO------------------*/

    // for (int i = 0; i < MAX_DPU; i++) {
    //     if (dpu_info[i].is_enabled == false) {
    //         continue;
    //     }
    //     dpu_set_t dpu = dpu_info[i].dpu;
    //     DPU_ASSERT(dpu_fifo_prepare_xfer(
    //             dpu, &input_link,
    //             (void*)&input_fifo_data[OFFSET_IN_FIFO(i)]));
    // }
    for (int i = 0; i < MAX_DPU; i++)
    {
        if (dpu_info[i].is_enabled == false)
        {
            continue;
        }

        uint32_t *input_data =
            (uint32_t *)(&(input_fifo_data[OFFSET_IN_FIFO(i)]));

        input_data[0] = i + OFFSET_INPUT_FIFO;
    }
};

void DPUWrapper::clear_matrix(int rank_id, int thread_id, bool is_from)
{
    if (is_from)
    {
        for (int i = 0; i < MAX_NR_DPUS_PER_RANK; i++)
        {
            xfer_matrix_from[thread_id][rank_id].ptr[i] = NULL;
        }
        xfer_matrix_from[thread_id][rank_id].offset = 0;
        xfer_matrix_from[thread_id][rank_id].size = 0;
        xfer_matrix_from[thread_id][rank_id].type = 0;
    }
    else
    {
        for (int i = 0; i < MAX_NR_DPUS_PER_RANK; i++)
        {
            xfer_matrix[thread_id][rank_id].ptr[i] = NULL;
        }
        xfer_matrix[thread_id][rank_id].offset = 0;
        xfer_matrix[thread_id][rank_id].size = 0;
        xfer_matrix[thread_id][rank_id].type = 0;
    }
}

void DPUWrapper::fifo_dpu_copy_to(
    int dpu_id,
    int friend_dpu_id,
    int enable_num,
    const char *symbol_name,
    uint32_t symbol_offset,
    const void *src,
    const void *friend_src,
    size_t length,
    int thread_id)
{
    int chip_dpu_id = (dpu_id % 64) % 8;
    int chip_id = (dpu_id % 64) / 8;
    int ptr_index = chip_dpu_id * 8 + chip_id;
    int friend_ptr_index = dpu_id % 2 == 0 ? (ptr_index + 8) : (ptr_index - 8);

    int rank_id = dpu_info[dpu_id].rank_id;
    // dpu_rank_t *rank = fifo_set.list.ranks[rank_id];
    dpu_rank_t *rank = rank_info[rank_id].rank.list.ranks[0];

    void **ptr = (void **)malloc(64 * sizeof(void *));
    for (size_t i = 0; i < 64; i++)
    {
        ptr[i] = NULL;
    }
    if (enable_num == 2)
    {
        ptr[ptr_index] = (void *)src;
        ptr[friend_ptr_index] = (void *)friend_src;
    }
    else if (enable_num == 1)
    {
        ptr[ptr_index] = (void *)src;
    }
    uint64_t *base_region_addr = rank_info[rank_id].region_ptr;
    uint32_t offset = fifo_get_symbol_offset(fifo_set, symbol_name, symbol_offset);
    offset &= ~(0x08000000u);

    mtx_fifo_mram_pairlineid[dpu_info[dpu_id].pair_line_id].lock();

    xmh::Timer timer2("    2.2.2.0:fifo_dpu_copy_to");

    fifo_host_get_access_for_transfer_matrix(rank, ptr);

    xmh::Timer timer("    2.2.2.1:fifo_write_to_rank");
    fifo_write_to_rank(
        ptr,
        base_region_addr,
        offset,
        length,
        chip_dpu_id,
        chip_dpu_id + 2,
        0,
        length);

    timer.end();

    fifo_host_release_access_for_transfer_matrix(rank, ptr);
    timer2.end();

    mtx_fifo_mram_pairlineid[dpu_info[dpu_id].pair_line_id].unlock();
}

void DPUWrapper::fifo_dpu_copy_from(
    int dpu_id,
    const char *symbol_name,
    uint32_t symbol_offset,
    const void *src,
    size_t length,
    int thread_id)
{
    int chip_dpu_id = (dpu_id % 64) % 8;
    int chip_id = (dpu_id % 64) / 8;
    int ptr_index = chip_dpu_id * 8 + chip_id;
    int rank_id = dpu_info[dpu_id].rank_id;
    // dpu_rank_t *rank = fifo_set.list.ranks[rank_id];

    dpu_rank_t *rank = rank_info[rank_id].rank.list.ranks[0];

    void **ptr = (void **)malloc(64 * sizeof(void *));
    for (size_t i = 0; i < 64; i++)
    {
        ptr[i] = NULL;
    }
    ptr[ptr_index] = (void *)src;

    uint64_t *base_region_addr = rank_info[rank_id].region_ptr;

    uint32_t offset = fifo_get_symbol_offset(fifo_set, symbol_name, symbol_offset);
    offset &= ~(0x08000000u);

    mtx_fifo_mram_pairlineid[dpu_info[dpu_id].pair_line_id].lock();

    fifo_host_get_access_for_transfer_matrix(rank, ptr);

    xmh::Timer timer("fifo_read_from_rank");
    fifo_read_from_rank(
        ptr,
        base_region_addr,
        offset,
        length,
        chip_dpu_id,
        chip_dpu_id + 1,
        0,
        length);

    timer.end();

    timer.end();

    fifo_host_release_access_for_transfer_matrix(rank, ptr);

    mtx_fifo_mram_pairlineid[dpu_info[dpu_id].pair_line_id].unlock();
}

void DPUWrapper::shutdown_fifo()
{
    printf("shutdown_fifo\n");

    uint64_t loop = 0;
    DPU_ASSERT(dpu_prepare_xfer(fifo_set, &loop));
    DPU_ASSERT(dpu_push_xfer(
        fifo_set,
        DPU_XFER_TO_DPU,
        "loop",
        0,
        sizeof(uint64_t),
        DPU_XFER_PARALLEL));
    dpu_set_t rank;
    DPU_RANK_FOREACH(fifo_set, rank)
    {
        dpu_sync(rank);
    }
}

void DPUWrapper::stop_thread_backend()
{
    for (int i = 0; i < BACK_THREAD; i++)
    {
        back_thread_info[i].stopFlag.store(true);
        while (back_thread_info[i].already_stop.load() == false)
        {
            usleep(1000);
        }
    }

    for (int i = 0; i < DynamicBalance_THREAD; i++)
    {
        dynamic_balance_thread_info[i].stopFlag.store(true);
        while (dynamic_balance_thread_info[i].already_stop.load() == false)
        {
            usleep(1000);
        }
    }

    stopBackthread.store(true);
}

void drawLatencyChart(std::ofstream &outfile, const std::vector<double> &latency_fig)
{
    if (!outfile)
        return;

    const int height = 15;
    double max_lat = *std::max_element(latency_fig.begin(), latency_fig.end());
    double min_lat = *std::min_element(latency_fig.begin(), latency_fig.end());
    double range = std::max(max_lat - min_lat, 1.0);

    outfile << "\n average latency change curve (range: " << min_lat << " - " << max_lat << " ms)\n";

    for (int y = 0; y < height; y++)
    {
        double current_level = max_lat - y * (range / (height - 1));
        outfile << std::fixed << std::setprecision(0)
                << std::setw(5) << current_level << " |";

        for (int x = 0; x < latency_fig.size(); x++)
        {
            double value = latency_fig[x];
            double pixel_pos = (value - min_lat) / range * (height - 1);

            if (std::abs(pixel_pos - (height - 1 - y)) < 0.5)
            {
                outfile << "o";
            }
            else if (x > 0)
            {
                double prev_pixel_pos = (latency_fig[x - 1] - min_lat) / range * (height - 1);
                if ((prev_pixel_pos > pixel_pos && (height - 1 - y) > pixel_pos && (height - 1 - y) < prev_pixel_pos) ||
                    (prev_pixel_pos < pixel_pos && (height - 1 - y) < pixel_pos && (height - 1 - y) > prev_pixel_pos))
                {
                    outfile << "|";
                }
                else if (std::abs((height - 1 - y) - (prev_pixel_pos + pixel_pos) / 2) < 0.5)
                {
                    outfile << "/";
                }
                else
                {
                    outfile << " ";
                }
            }
            else
            {
                outfile << " ";
            }
        }
        outfile << "\n";
    }

    outfile << "      +";
    for (int x = 0; x < latency_fig.size(); x++)
        outfile << "-";
    outfile << "\n       ";
    for (int x = 0; x < latency_fig.size(); x++)
        outfile << (x % 10 == 0 ? std::to_string(x / 10) : " ");
    outfile << "\n       ";
    for (int x = 0; x < latency_fig.size(); x++)
        outfile << (x % 10);
    outfile << "\n       Batch number\n\n";
}

void drawQPSLineChart(std::ofstream &outfile, const std::vector<double> &qps_fig)
{
    if (!outfile)
        return;

    const int height = 15; 
    double max_qps = *std::max_element(qps_fig.begin(), qps_fig.end());
    double min_qps = *std::min_element(qps_fig.begin(), qps_fig.end());
    double range = std::max(max_qps - min_qps, 1.0); // avoid division by zero

    outfile << "\n QPS change curve (range: " << min_qps << " - " << max_qps << " queries/s)\n";

  
    for (int y = 0; y < height; y++)
    {
        double current_level = max_qps - y * (range / (height - 1));

       
        outfile << std::fixed << std::setprecision(0)
                << std::setw(5) << current_level << " |";

     
        for (int x = 0; x < qps_fig.size(); x++)
        {
            double value = qps_fig[x];
            double pixel_pos = (value - min_qps) / range * (height - 1);

            if (std::abs(pixel_pos - (height - 1 - y)) < 0.5)
            {
                outfile << "o"; 
            }
            else if (x > 0)
            {
                double prev_pixel_pos = (qps_fig[x - 1] - min_qps) / range * (height - 1);
                if ((prev_pixel_pos > pixel_pos && (height - 1 - y) > pixel_pos && (height - 1 - y) < prev_pixel_pos) ||
                    (prev_pixel_pos < pixel_pos && (height - 1 - y) < pixel_pos && (height - 1 - y) > prev_pixel_pos))
                {
                    outfile << "|"; 
                }
                else if (std::abs((height - 1 - y) - (prev_pixel_pos + pixel_pos) / 2) < 0.5)
                {
                    outfile << "/"; 
                }
                else
                {
                    outfile << " "; 
                }
            }
            else
            {
                outfile << " "; 
            }
        }
        outfile << "\n";
    }


    outfile << "      +";
    for (int x = 0; x < qps_fig.size(); x++)
    {
        outfile << "-";
    }
    outfile << "\n       ";
    for (int x = 0; x < qps_fig.size(); x++)
    {
        outfile << (x % 10 == 0 ? std::to_string(x / 10) : " ");
    }
    outfile << "\n       ";
    for (int x = 0; x < qps_fig.size(); x++)
    {
        outfile << (x % 10);
    }
    outfile << "\n       Batch number: " << "\n";
}

// type=0,cpu
// type=1,dpu
// type=2,batch_dpu
void DPUWrapper::print_result(int nprobe, int top_k, int type)
{
    /*-------------------check task----------------------------*/

    std::string result_path_nprobe;
    if (type == 0)
    {
        // result_path_nprobe = std::string(mconfig.getCpuResultPath()) + "-nprobe" +
        //                      std::to_string(nprobe) + "-topk" + std::to_string(top_k) +
        //                      ".txt";

        result_path_nprobe = std::string(mconfig.getCpuResultPath()) + "-nprobe" +
                             std::to_string(nprobe) + ".txt";
    }
    else if (type == 1)
    {
        // std::string enable_flag = mconfig.getEnableDynamic() ? "YES" : "NO";
        // result_path_nprobe = std::string(mconfig.getDpuResultPath()) + "-nprobe" +
        //                      std::to_string(nprobe) + "-topk" + std::to_string(top_k) +
        //                      "-EnableDynamic" + enable_flag + ".txt";

        result_path_nprobe = std::string(mconfig.getDpuResultPath()) + "-nprobe" +
                             std::to_string(nprobe) + ".txt";

        
        // auto now = std::chrono::system_clock::now();
        // auto now_time_t = std::chrono::system_clock::to_time_t(now);
        // std::tm now_tm = *std::localtime(&now_time_t);
        // char time_str[20];
        // std::strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", &now_tm);
        // result_path_nprobe = std::string(mconfig.getDpuResultPath()) + "-nprobe" +
        //                      std::to_string(nprobe) + "-topk" + std::to_string(top_k) +
        //                      "-" + std::string(time_str) + ".txt";
    }
    else if (type == 2)
    {
        // result_path_nprobe = std::string(mconfig.getBatchDpuResultPath()) + "-nprobe" +
        //                      std::to_string(nprobe) + "-topk" + std::to_string(top_k) +
        //                      ".txt";

        result_path_nprobe = std::string(mconfig.getBatchDpuResultPath()) + "-nprobe" +
                             std::to_string(nprobe) + ".txt";
    }
    else
    {
        std::cout << "type error" << std::endl;
        return;
    }

    std::ofstream outfile(result_path_nprobe, std::ios::trunc);

    if (type == 1 && ENABLE_DPU_LOAD == 1)
    {
        std::string dpuload_path = std::string(mconfig.getProjectSourceDir()) + "/" + std::string(mconfig.getReplaceDir()) + "/DPU_DIR/dpu-load-nprobe" + std::to_string(nprobe) + ".txt";

        if (ENABLE_REPLICA != 1 && CHANGE_ENABLE_REPLICA == 1)
        {
            dpuload_path = std::string(mconfig.getProjectSourceDir()) + "/" + std::string(mconfig.getReplaceDir()) + "/DPU_DIR/dpu-load-nprobe" + std::to_string(nprobe) + "-ENABLE_REPLICA" + std::to_string(ENABLE_REPLICA) +
                           ".txt";
            printf("dpuload_path: %s\n", dpuload_path.c_str());
        }

        std::ofstream dpuload_file(dpuload_path, std::ios::trunc);

        for (int i = 0; i < MAX_DPU; i++)
        {
            if (dpu_info[i].is_enabled == false)
            {
                continue;
            }
            // printf("dpu_id = %d, need_compute_distance_num = %ld\n",
            //        i,
            //        dpu_info[i].need_compute_distance_num);

            dpuload_file << dpu_info[i].need_compute_distance_num << std::endl;
        }

        dpuload_file.close();
    }

    // printf("===========================\n");
    // for (int i = 0; i < query_num; i++)
    // {

    //     for (int j = 0; j < nprobe; j++)
    //     {
    //         printf("%ld ", query_info[i]->idx[j]);
    //     }
    //     printf("\n");
    // }

    // printf("===========================\n");

    // outfile << "path: " << result_path_nprobe << std::endl;

    for (int i = 0; i < query_num; i++)
    {

        for (int j = 0; j < top_k; j++)
        {
            outfile << "q_id = " << i << ", simi_values[" << j
                    << "]: " << query_info[i]->simi_values[j] << std::endl;
        }
        for (int j = 0; j < top_k; j++)
        {
            outfile << "q_id = " << i << ", simi_ids[" << j
                    << "]: " << query_info[i]->simi_ids[j] << std::endl;
        }
    }

    // int batch_size = 1000;
    // std::vector<double> qps_fig;
    // std::vector<double> latency_fig;

    // for (int batch_start = 0; batch_start < query_num; batch_start += batch_size)
    // {
    //     int batch_end = std::min(batch_start + batch_size, query_num);


    //     auto earliest_start = query_info[batch_start]->start_;
    //     auto latest_end = query_info[batch_start]->end_;


    //     double total_latency = 0;
    //     int valid_queries = 0;

    // for (int i = batch_start; i < batch_end; i++)
    // {
    //     std::cout << "query id = " << i
    //               << ", start = "
    //               << std::chrono::duration_cast<std::chrono::milliseconds>(query_info[i]->start_.time_since_epoch()).count()
    //               << " ms, end = "
    //               << std::chrono::duration_cast<std::chrono::milliseconds>(query_info[i]->end_.time_since_epoch()).count()
    //               << " ms" << std::endl;

    //     if (query_info[i]->start_ < earliest_start)
    //     {
    //         earliest_start = query_info[i]->start_;
    //     }
    //     if (query_info[i]->end_ > latest_end)
    //     {
    //         latest_end = query_info[i]->end_;
    //     }

    //     if (query_info[i]->end_ < query_info[i]->start_)
    //     {
    //         continue; 
    //     }

    //     auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    //         query_info[i]->end_ - query_info[i]->start_);
    //     total_latency += duration.count();
    //     valid_queries++;
    // }

  
    // auto batch_duration = std::chrono::duration_cast<std::chrono::milliseconds>(latest_end - earliest_start);
    // double throughput = (batch_end - batch_start) / (batch_duration.count() / 1000.0);
    // double avg_latency = total_latency / valid_queries;

    // qps_fig.push_back(throughput);
    // latency_fig.push_back(avg_latency);

    // outfile << "batch " << (batch_start / batch_size) + 1 << " ("
    //         << batch_start << "-" << (batch_end - 1) << "):\n"
    //         << "  earliest start: "
    //         << std::chrono::duration_cast<std::chrono::milliseconds>(earliest_start.time_since_epoch()).count()
    //         << "ms\n"
    //         << "  latest end: "
    //         << std::chrono::duration_cast<std::chrono::milliseconds>(latest_end.time_since_epoch()).count()
    //         << "ms\n"
    //         << "  time: " << batch_duration.count() << "ms\n"
    //         << "  qps: " << throughput << " queries/s\n"
    //         << "  average latency: " << avg_latency << " ms\n\n";
    // }

 
    // drawQPSLineChart(outfile, qps_fig);


    // drawLatencyChart(outfile, latency_fig);

    outfile.close();
}

void DPUWrapper::recall(int nprobe, int k, int type, double time_s)
{
    std::string time_path_nprobe;
    if (type == 0)
    {
        time_path_nprobe = std::string(mconfig.getCpuTimePath()) + "-nprobe" +
                           std::to_string(nprobe) + ".txt";
    }
    else if (type == 1)
    {
        // std::string enable_flag = mconfig.getEnableDynamic() ? "YES" : "NO";
        // time_path_nprobe = std::string(mconfig.getDpuTimePath()) + "-nprobe" +
        //                    std::to_string(nprobe) + "-topk" + std::to_string(k) +
        //                    "-slotlen" + std::to_string(SLOT_L) + "-COROUTINE" +
        //                    std::to_string(MAX_COROUTINE) + "-BACK_THREAD" +
        //                    std::to_string(BACK_THREAD) + "-FRONT_THREAD" +
        //                    std::to_string(FRONT_THREAD) + "-EnableDynamic" +
        //                    enable_flag + ".txt";
        time_path_nprobe = std::string(mconfig.getDpuTimePath()) + "-nprobe" +
                           std::to_string(nprobe) + ".txt";
        if (MAX_COROUTINE != 4 && CHANGE_MAX_COROUTINE == 1)
        {
            time_path_nprobe = std::string(mconfig.getDpuTimePath()) + "-nprobe" +
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
            time_path_nprobe = std::string(mconfig.getDpuTimePath()) + "-nprobe" +
                               std::to_string(nprobe) + "-COPY_RATE" + result +
                               ".txt";
        }
        else if (ENABLE_REPLICA != 1 && CHANGE_ENABLE_REPLICA == 1)
        {
            time_path_nprobe = std::string(mconfig.getDpuTimePath()) + "-nprobe" +
                               std::to_string(nprobe) + "-ENABLE_REPLICA" + std::to_string(ENABLE_REPLICA) +
                               ".txt";
        }
    }
    else if (type == 2)
    {
        // time_path_nprobe = std::string(mconfig.getBatchDpuTimePath()) + "-nprobe" +
        //                    std::to_string(nprobe) + "-topk" + std::to_string(k) +
        //                    "-slotlen" + std::to_string(SLOT_L) + ".txt";

        time_path_nprobe = std::string(mconfig.getBatchDpuTimePath()) + "-nprobe" +
                           std::to_string(nprobe) + ".txt";
    }
    else
    {
        std::cout << "type error" << std::endl;
        return;
    }

    std::ofstream outfile_time(time_path_nprobe, std::ios::trunc);
    /*-------compute recall-------*/
    int n_1 = 0, n_10 = 0, n_100 = 0, n_10_100 = 0;

    for (int i = 0; i < query_num; i++)
    {
        if (type != 0)
        {
            faiss::maxheap_reorder(k, query_info[i]->simi_values, query_info[i]->simi_ids);
        }
        int gt_nn = query_info[i]->ground_truth_ids[0];
        for (int j = 0; j < k; j++)
        {
            if (query_info[i]->simi_ids[j] == gt_nn)
            {
                if (j < 1)
                    n_1++;
                if (j < 10)
                    n_10++;
                if (j < 100)
                    n_100++;
            }
            
            if (k >= 100)
            {
                for (int j = 0; j < 10; j++)
                {
                    int gt_nn_10 = query_info[i]->ground_truth_ids[j];
                    for (int l = 0; l < k; l++)
                    {
                        if (query_info[i]->simi_ids[l] == gt_nn_10)
                        {
                            n_10_100++;
                            break;
                        }
                    }
                }
            }
        }
    }

    outfile_time << "time_s=" << time_s << endl;
    int qps = query_num / time_s;
    outfile_time << "qps=" << qps << endl;
    outfile_time << "nprobe=" << nprobe << endl;
   
    if (k >= 10)
    {
        outfile_time << "R@10=" << n_10 / float(query_num);
    }
    
    outfile_time << endl;
}

void DPUWrapper::read_log()
{
    dpu_set_t dpu;
    DPU_FOREACH(fifo_set, dpu)
    {
        DPU_ASSERT(dpu_log_read(dpu, stdout));
    }
}

void DPUWrapper::bind_core(int n)
{
    return;
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(n, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) == -1)
    {
        std::cout << "Could not set CPU affinity" << std::endl;
    }
}

void DPUWrapper::log_num_dpu_running(int nprobe, int topk)
{
    // std::string log_num_dpu_running_path_nprobe =
    //     std::string(mconfig.getDpuActiveNumPath()) + "-nprobe" +
    //     std::to_string(nprobe) + "-topk" + std::to_string(topk) +
    //     "-slotlen" + std::to_string(SLOT_L) + "-COROUTINE" +
    //     std::to_string(MAX_COROUTINE) + "-BACK_THREAD" +
    //     std::to_string(BACK_THREAD) + "-FRONT_THREAD" +
    //     std::to_string(FRONT_THREAD) + ".txt";

    std::string log_num_dpu_running_path_nprobe =
        std::string(mconfig.getDpuActiveNumPath()) + "-nprobe" +
        std::to_string(nprobe) + ".txt";

    if (MAX_COROUTINE != 4 && CHANGE_MAX_COROUTINE == 1)
    {
        log_num_dpu_running_path_nprobe =
            std::string(mconfig.getDpuActiveNumPath()) + "-nprobe" +
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
        log_num_dpu_running_path_nprobe = std::string(mconfig.getDpuActiveNumPath()) +
                                          "-nprobe" + std::to_string(nprobe) + "-COPY_RATE" +
                                          result + ".txt";
    }
    else if (ENABLE_REPLICA != 1 && CHANGE_ENABLE_REPLICA == 1)
    {
        log_num_dpu_running_path_nprobe = std::string(mconfig.getDpuActiveNumPath()) +
                                          "-nprobe" + std::to_string(nprobe) + "-ENABLE_REPLICA" +
                                          std::to_string(ENABLE_REPLICA) + ".txt";
    }

    std::ofstream outfile(log_num_dpu_running_path_nprobe, std::ios::trunc);

    while (true)
    {
        if (stopBackthread.load())
        {
            break;
        }
        // usleep(1000 * SAMPLE_INTERVAL_MS);
        int mini_sample = 1;
        int max_num_is_running = 0;
        for (int i = 0; i < SAMPLE_INTERVAL_MS / mini_sample; i++)
        {
            if (num_is_running.load() > max_num_is_running)
            {
                max_num_is_running = num_is_running.load();
            }
            usleep(1000 * mini_sample);
        }
        outfile << max_num_is_running << std::endl;
    }
    outfile.close();
}

void DPUWrapper::read_dpu_log_()
{
    for (int i = 0; i < MAX_DPU; i++)
    {
        if (dpu_info[i].is_enabled)
        {
            std::string file_path = std::string(mconfig.getProjectSourceDir()) +
                                    "/log/dpu_log_" + std::to_string(i) + ".txt";

            std::ofstream outfile(file_path);

            outfile << endl
                    << "=============================================" << endl;

            for (int j = 0; j < PERDPU_LOG_SIZE; j++)
            {
                outfile << debug_m[i * PERDPU_LOG_SIZE + j];
            }

            outfile << endl
                    << "=============================================" << endl;
        }
    }
    while (true)
    {
        if (stopBackthread.load())
        {
            break;
        }
        usleep(1000 * 1000 * 2);

        xmh::Timer timer("read_dpu_log_");

        for (int i = 0; i < MAX_DPU; i++)
        {
            dpu_set_t dpu = dpu_info[i].dpu;
            if (dpu_info[i].is_enabled)
            {
                DPU_ASSERT(
                    dpu_prepare_xfer(dpu, &debug_m[i * PERDPU_LOG_SIZE]));
            }
        }

        dpu_set_t rank;
        DPU_RANK_FOREACH(fifo_set, rank)
        {
            DPU_ASSERT(dpu_push_xfer(
                rank,
                DPU_XFER_FROM_DPU,
                "debug_m",
                0,
                sizeof(char) * PERDPU_LOG_SIZE,
                DPU_XFER_PARALLEL));
        }

        timer.end();

        for (int i = 0; i < MAX_DPU; i++)
        {
            if (dpu_info[i].is_enabled)
            {
                std::string file_path = std::string(mconfig.getProjectSourceDir()) +
                                        "/log/dpu_log_" + std::to_string(i) + ".txt";

                std::ofstream outfile(
                    file_path, std::ios_base::app); 

                outfile << endl
                        << "============================================="
                        << endl;

                for (int j = 0; j < PERDPU_LOG_SIZE; j++)
                {
                    outfile << debug_m[i * PERDPU_LOG_SIZE + j];
                }

                outfile << endl
                        << "============================================="
                        << endl;
            }
        }
    }
}

dpu_fifo_output_t result[2560];

void DPUWrapper::thread_fifo_out(const std::vector<int> &rank_id_v, int t_id)
{
    bind_core(t_id + FRONT_THREAD);

    bool is_last = false;

    while (true)
    {
        xmh::Timer timer1("_loop check wram");

        dpu_set_t rank;

        dpu_set_t dpu;

        int sz_num = 0;

        for (auto rank_id : rank_id_v)
        {
            rank = rank_info[rank_id].rank;

            xmh::Timer timer2("_read wram");

            DPU_ASSERT(
                dpu_fifo_push_xfer(rank, &output_link, DPU_XFER_NO_RESET));

            timer2.end();

            DPU_FOREACH(rank, dpu)
            {
                int sz = get_fifo_size(&output_link, dpu);
                int dpu_id = GET_DPU_ID_BY_DPU(dpu);
                int pair_id = dpu_id / 2;
                PAIR_INFO &pair_info_ = pair_info[pair_id];
                bool is_first_dpu = (dpu_id % 2) == true;

                if (sz > 0)
                {
                    sz_num++;
                    assert(sz == 1);
                    if (is_first_dpu)
                    {
                        pair_info_.first_dpu_back = true;
                    }
                    else
                    {
                        pair_info_.second_dpu_back = true;
                    }
                    if ((pair_info_.first_dpu_back ||
                         pair_info_.is_first_dpu_enable == false) &&
                        (pair_info_.second_dpu_back ||
                         pair_info_.is_second_dpu_enable == false))
                    {
                        pair_info_.mtx_dpu_run.lock();
                        pair_info_.is_running = false;
                        pair_info_.mtx_dpu_run.unlock();

                        num_is_running -= 2;
                        pair_info_.first_dpu_back = false;
                        pair_info_.second_dpu_back = false;
                    }
                }
            }
            DPU_FOREACH(rank, dpu)
            {
                int sz = get_fifo_size(&output_link, dpu);
                int dpu_id = GET_DPU_ID_BY_DPU(dpu);

                if (sz > 0)
                {
                    // xmh::Timer timer("   2.3.2: reorder");
                    auto start = std::chrono::high_resolution_clock::now(); 

                    assert(sz == 1);

                    uint8_t *out_data = (uint8_t *)get_fifo_elem(
                        &output_link,
                        dpu,
                        &output_fifo_data[OFFSET_OUT_FIFO(dpu_id)],
                        0);

                    // dpu_fifo_output_t *out_data_fifo =
                    //     (dpu_fifo_output_t *)out_data;

                    // typedef struct dpu_fifo_output_t
                    // {
                    //     DIST_TYPE result_v[MAX_K];
                    //     ID_TYPE result_id[MAX_K];

                    //     int k;
                    //     int q_id;
                    //     int dpu_id;
                    //     uint64_t cycles;
                    //     int64_t cycle_per_vec;

                    //     uint64_t start;
                    //     uint64_t end;

                    //     uint64_t dis;
                    //     uint64_t sort;

                    // } __attribute__((aligned(8))) dpu_fifo_output_t;

                    memcpy((void *)&result[dpu_id],
                           (void *)out_data,
                           sizeof(dpu_fifo_output_t));

                    // assert(dpu_id ==
                    //        *(int*)(out_data + sizeof(dpu_fifo_output_t)));
                    if (dpu_id !=
                        *(int *)(out_data + sizeof(dpu_fifo_output_t)))
                    {
                        printf("dpu_id != return, dpu_id = %d, q_id = %d\n",
                               dpu_id,
                               *(int *)(out_data + sizeof(dpu_fifo_output_t)));
                    }
                    if (dpu_id == 0)
                    {
                        // dpu_info[dpu_id].real_time_when_dpu_back.push_back(
                        //         std::chrono::duration_cast<
                        //                 std::chrono::nanoseconds>(
                        //                 std::chrono::steady_clock::now()
                        //                         .time_since_epoch()));

                        // auto now = std::chrono::system_clock::now();
                        // auto duration =
                        // std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
                        // std::cout<<"send: " << duration.count() << " ms" <<
                        // std::endl;
                    }

                    int q_id = result[dpu_id].q_id;

                    std::shared_ptr<QUERY_INFO> query = query_info[q_id];
                    query->mtx_merge_topk.lock();

                    int k = result[dpu_id].k;
                    query->complete_num++;

                    for (int i = 0; i < k; i++)
                    {
                        if (query->simi_values[0] > result[dpu_id].result_v[i])
                        {
                            faiss::heap_replace_top<
                                faiss::CMax<DIST_TYPE, ID_TYPE>>(
                                k,
                                query->simi_values,
                                query->simi_ids,
                                result[dpu_id].result_v[i],
                                result[dpu_id].result_id[i]);
                        }
                    }

                    query->mtx_merge_topk.unlock();

                    // query_info[q_id]->end2(dpu_id);

                    double dputime_ns = result[dpu_id].cycles / (0.4);
                    // xmh::Timer::ManualRecordNs("   2.3.1:dpu time", dputime_ns);

                    double dputime_ms = dputime_ns / 1000000.0;
                    xmh::PerfCounter::Record(
                        "dpu_time", dputime_ms);

                    double dputime_per_vec_ns =
                        result[dpu_id].cycle_per_vec / (0.4);

                    // xmh::Timer::ManualRecordNs(
                    //     "   2.3.1:dpu time per vec", dputime_per_vec_ns);

                    if (query->complete_num.load() == query->wait_dpu_num)
                    {
                        if (query->send_over == true)
                        {
                            query_info[q_id]->end1();
                        }
                    }

                    // timer.end();

                    auto end = std::chrono::high_resolution_clock::now(); 
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

                    xmh::PerfCounter::Record("Merge topk", duration.count());
                }
            }
        }

        xmh::PerfCounter counter;

        counter.Record("sz_num", sz_num);

        if (is_last)
        {
            break;
        }

        if (back_thread_info[t_id].stopFlag.load())
        {
            is_last = true;
        }

        timer1.end();
    }

    back_thread_info[t_id].already_stop.store(true);
}

void DPUWrapper::thread_dynamic_balance(const std::vector<int> &cluster_id_v, int t_id)
{
    bind_core(t_id + DynamicBalance_THREAD);

    bool is_last = false;
    int check_interval = 1000;

    while (true)
    {
        if (mconfig.getEnableDynamic() == false)
        {
            break;
        }
        if (is_last)
        {
            break;
        }

        if (dynamic_balance_thread_info[t_id].stopFlag.load())
        {
            is_last = true;
        }

        if (next_qid == 0 || next_qid == query_num)
        {
            continue;
        }

        int now_qid = next_qid;

        if (now_qid % check_interval != 0)
        {
            continue;
        }

        xmh::Timer timer1("_loop check dynamic balance");

        dpu_set_t rank;

        dpu_set_t dpu;

        int q_start = now_qid - check_interval;
        int q_end = now_qid;

        int freq[mconfig.getMaxCluster()] = {0};

        for (int i = q_start; i < q_end; i++)
        {
            for (int j = 0; j < query_info[i]->nprobe; j++)
            {
                int idx = query_info[i]->idx[j];
                if (idx >= mconfig.getMaxCluster() || idx < 0)
                {
                    continue;
                }

                freq[idx]++;
            }
        }

        // when q_id == 5000, we will trigger dynamic balance
        // {
        //     if (now_qid != 5000)
        //     {
        //         continue;
        //     }

        //     std::string change_freq_path = "/mnt/optane/wpq/dataset/space/change_freq.txt";

        //     std::ifstream infile;
        //     infile.open(change_freq_path);
        //     for (int i = 0; i < mconfig.getMaxCluster(); i++)
        //     {
        //         infile >> freq[i];
        //     }
        //     infile.close();
        // }

        int shard_size[mconfig.getMaxCluster()];
        for (int i = 0; i < mconfig.getMaxCluster(); i++)
        {
            shard_size[i] = cluster_info[i]->shard_size;
        }

        int shard_num[mconfig.getMaxCluster()];
        for (int i = 0; i < mconfig.getMaxCluster(); i++)
        {
            shard_num[i] = cluster_info[i]->pair_shard_num * 2;
        }

        int shard_workload[mconfig.getMaxCluster()];
        for (int i = 0; i < mconfig.getMaxCluster(); i++)
        {
            shard_workload[i] = freq[i] * shard_size[i];
        }
        int64_t total_shard_workload = 0;
        for (int i = 0; i < mconfig.getMaxCluster(); i++)
        {
            total_shard_workload += shard_workload[i];
        }
        double rate_each_shard[mconfig.getMaxCluster()];
        for (int i = 0; i < mconfig.getMaxCluster(); i++)
        {
            rate_each_shard[i] = (double)shard_workload[i] / total_shard_workload;
        }

        double sum_rate = 0.0;
        for (int i = 0; i < mconfig.getMaxCluster(); i++)
        {
            sum_rate += rate_each_shard[i];
        }
        printf("now_qid = %d, sum_rate = %f\n", now_qid, sum_rate);

        double to1_rate_each_shard[mconfig.getMaxCluster()];
        for (int i = 0; i < mconfig.getMaxCluster(); i++)
        {
            to1_rate_each_shard[i] = rate_each_shard[i] / sum_rate;
        }

        double copy_rate = copy_rate_select;

        int copy_num[mconfig.getMaxCluster()];
        int shard_copy_num[mconfig.getMaxCluster()];

        int before_copy_num[mconfig.getMaxCluster()];
        for (int i = 0; i < mconfig.getMaxCluster(); i++)
        {
            before_copy_num[i] = cluster_info[i]->replica;
        }


        int have_slot_here = need_slot;
        int need_slot_here = 0;

        while (true)
        {
            need_slot_here = 0;
            for (int i = 0; i < mconfig.getMaxCluster(); i++)
            {
                copy_num[i] = to1_rate_each_shard[i] * mconfig.getMaxCluster() * copy_rate;

                if (copy_num[i] < 1)
                {
                    copy_num[i] = 1;
                }
               
                if (cluster_info[i]->history_freq == 0)
                {
                    copy_num[i] = 0;
                }
                shard_copy_num[i] = copy_num[i] * shard_num[i];
                need_slot_here += shard_copy_num[i];
            }

            if (need_slot_here <= have_slot_here)
            {
                break;
            }
            copy_rate -= 0.5;
            if (copy_rate <= 1.0)
            {
                printf("you should set NR_DPU larger\n");
                exit(0);
            }
        }

        const int TOP_N = 20; 

     
        auto print_top_n = [](const int *arr, int size, const std::string &name, int top_n)
        {
            std::vector<std::pair<int, int>> vec;
            for (int i = 0; i < size; i++)
            {
                vec.emplace_back(arr[i], i);
            }
            std::sort(vec.rbegin(), vec.rend());
            printf("Top %d %s:\n", top_n, name.c_str());
            for (int i = 0; i < top_n && i < vec.size(); i++)
            {
                printf("Cluster %d: %d\n", vec[i].second, vec[i].first);
            }
        };

        print_top_n(copy_num, mconfig.getMaxCluster(), "copy_num", TOP_N);
        print_top_n(before_copy_num, mconfig.getMaxCluster(), "before_copy_num", TOP_N);


        auto get_top_n_set = [](const int *arr, int size, int top_n)
        {
            std::vector<std::pair<int, int>> vec;
            for (int i = 0; i < size; i++)
            {
                vec.emplace_back(arr[i], i);
            }
            std::sort(vec.rbegin(), vec.rend());
            std::unordered_set<int> top_set;
            for (int i = 0; i < top_n && i < vec.size(); i++)
            {
                top_set.insert(vec[i].second);
            }
            return top_set;
        };

        auto copy_num_set = get_top_n_set(copy_num, mconfig.getMaxCluster(), TOP_N);
        auto before_copy_num_set = get_top_n_set(before_copy_num, mconfig.getMaxCluster(), TOP_N);

        int intersection_count = 0;
        for (auto cluster_id : copy_num_set)
        {
            if (before_copy_num_set.count(cluster_id))
            {
                intersection_count++;
            }
        }

        double similarity = (double)intersection_count / TOP_N;
        printf("Top %d similarity: %.2f\n", TOP_N, similarity);

        // is_first_dynamic = false;

        if (similarity < 0.5)
        {
            if (is_first_dynamic == false)
            {
                break;
            }
            is_first_dynamic = false;
            printf("Similarity is below 50 percent, triggering adjustment...\n");

           
            int diff_replica[mconfig.getMaxCluster()] = {0};

            for (int i = 0; i < mconfig.getMaxCluster(); i++)
            {
                diff_replica[i] = copy_num[i] - before_copy_num[i];
            }

            printf("Can remove replica:\n");
            for (int i = 0; i < mconfig.getMaxCluster(); i++)
            {
                if (diff_replica[i] < 0)
                {
                    printf("Cluster %d: %d\n", i, diff_replica[i]);
                }
            }

            xmh::Timer timer("remove replica");

            for (int i = 0; i < mconfig.getMaxCluster(); i++)
            {
                if (diff_replica[i] < 0)
                {
                    std::shared_ptr<CLUSTER_INFO> cluster = cluster_info[i];
                    int pair_shard_num = cluster->pair_shard_num;

                    int delete_num = -diff_replica[i];

                    for (int j = 0; j < pair_shard_num; j++)
                    {
                        cluster->pair_shard_info[j].rw_mutex.lock();
                        int size_before = cluster->pair_shard_info[j].pair_location.size();
                        int size_after;

                        for (int del_id = 0; del_id < delete_num; del_id++)
                        {
                            if (cluster->pair_shard_info[j].pair_location.size() <= 1)
                            {
                                continue; 
                            }

                            PAIR_LOCATION pair_location = cluster->pair_shard_info[j].pair_location[0];
                            int dpu_id = pair_location.dpu_id;
                            int slot_id = pair_location.slot_id;
                            dpu_info[dpu_id].slot_info[slot_id].active = false;
                            dpu_info[dpu_id].enable_slot_num--;
                            dpu_id = pair_location.dpu_id1;
                            slot_id = pair_location.slot_id1;
                            dpu_info[dpu_id].slot_info[slot_id].active = false;
                            dpu_info[dpu_id].enable_slot_num--;

                        
                            cluster->pair_shard_info[j].pair_location.erase(
                                cluster->pair_shard_info[j].pair_location.begin());
                        }

                        size_after = cluster->pair_shard_info[j].pair_location.size();

                        assert(size_before == size_after + delete_num);

                        cluster->pair_shard_info[j].rw_mutex.unlock();
                    }
                }
            }

            int active_slot[MAX_PAIR] = {-1};
            for (int i = 0; i < MAX_PAIR; i++)
            {
                if (dpu_info[i * 2].is_enabled)
                {
                    active_slot[i] = dpu_info[i * 2].enable_slot_num;
                }
            }
            // print
            for (int i = 0; i < MAX_PAIR; i++)
            {
                if (active_slot[i] != -1)
                {
                    printf("dpu %d: %d\n", i * 2, active_slot[i]);
                }
            }

            timer.end();

        
            printf("Need to add replica:\n");
            for (int i = 0; i < mconfig.getMaxCluster(); i++)
            {
                if (diff_replica[i] > 0)
                {
                    printf("Cluster %d: +%d\n", i, diff_replica[i]);
                }
            }

            std::unordered_map<int, std::unordered_map<int, std::vector<LOCATION>>> add_plans;

           
           
            std::unordered_map<int, std::vector<std::tuple<int, int, LOCATION>>> dpu_add_tasks;

            int expect_max_slot_num = SLOT_NUM;

         

            {

                int dpu_ptr = 0;
                LOCATION location;

                xmh::Timer timer1("place_dataset when dynamic balance");

                /*------------------begin place----------------------*/
                for (int i = 0; i < mconfig.getMaxCluster(); i++)
                {
                    if (diff_replica[i] > 0)
                    {
                        int add_num = diff_replica[i];
                        for (int k = 0; k < cluster_info[i]->s_num; k += 2)
                        {
                            for (int j = 0; j < add_num; j++)
                            {
                                bool place_ok = false;

                                int loop_count = 0; 
                                while (!place_ok)
                                {
                                    /*-------pick dpu_pair-----------------*/
                                    if (loop_count >= 2)
                                    { 
                                        printf("Error: Deadlock detected! Failed to place shard after two full loops. set copy_rate smaller!\n");
                                        exit(1);
                                    }

                                    int friend_dpu_ptr = dpu_ptr + 1;

                                   
                                    bool is_ok =
                                        dpu_info[dpu_ptr].enable_slot_num <= expect_max_slot_num - 1;

                                    bool is_friend_ok =
                                        dpu_info[friend_dpu_ptr].enable_slot_num <=
                                        expect_max_slot_num - 1;

                                    bool is_enabled = dpu_info[dpu_ptr].is_enabled;
                                    bool is_friend_enabled =
                                        dpu_info[friend_dpu_ptr].is_enabled;

                                    if (is_ok && is_friend_ok && is_enabled &&
                                        is_friend_enabled)
                                    {
                                        
                                        int min_slot_id = -1;
                                        {
                                            bool same_replica = false;

                                            for (int slot_id = 0; slot_id < SLOT_NUM; slot_id++)
                                            {
                                                if (dpu_info[dpu_ptr].slot_info[slot_id].active)
                                                {

                                                    int c_id = dpu_info[dpu_ptr].slot_info[slot_id].c_id;
                                                    int shard_id = dpu_info[dpu_ptr].slot_info[slot_id].shard_id;
                                                    int c_id_toplace = i;
                                                    int shard_id_toplace = k;
                                                    if (c_id == c_id_toplace &&
                                                        shard_id == shard_id_toplace)
                                                    {
                                                        same_replica = true;
                                                    }
                                                }
                                            }

                                            for (int slot_id = 0; slot_id < SLOT_NUM; slot_id++)
                                            {
                                                if (dpu_info[dpu_ptr].slot_info[slot_id].active == false)
                                                {
                                                    min_slot_id = slot_id;
                                                    break;
                                                }
                                            }

                                            if (same_replica || min_slot_id == -1)
                                            {
                                                dpu_ptr = dpu_ptr + 2;
                                                if (dpu_ptr >= MAX_DPU)
                                                {
                                                    dpu_ptr = 0;
                                                    loop_count++; 
                                                }
                                                continue;
                                            }
                                        }

                                      
                                        {

                                            location.dpu_id = dpu_ptr;
                                            location.slot_id = min_slot_id;

                                            place_ok = true;

                                            add_plans[i][k].push_back(location);

                                            dpu_add_tasks[dpu_ptr].emplace_back(i, k, location); 

                                            dpu_info[dpu_ptr].slot_info[min_slot_id].active = true;
                                            dpu_info[dpu_ptr].enable_slot_num++;
                                        }
                                    }

                                    /*-------------dpu_ptr move-----------------*/
                                    dpu_ptr = dpu_ptr + 2;
                                    if (dpu_ptr >= MAX_DPU)
                                    {
                                        dpu_ptr = 0;
                                        loop_count++; 
                                    }
                                }
                            }
                        }
                    }
                }

                timer1.end();
            }

            xmh::Timer timer2("copy data when dynamic balance");

            double total_size_GB = 0;

            int dpu_add_num = dpu_add_tasks.size();
            bool is_ok[MAX_DPU] = {false};

          
            auto process_dpu_tasks = [&](int start, int end)
            {
                for (int i = start; i < end; i++)
                {
                    auto it = std::next(dpu_add_tasks.begin(), i);
                    int dpu_id = it->first;

                    if (is_ok[dpu_id])
                    {
                        continue;
                    }
                    assert(dpu_info[dpu_id].is_enabled);
                    const auto &tasks = it->second;

                    printf("Processing DPU %d, task count: %zu\n", dpu_id, tasks.size());

                    int pair_id = dpu_id / 2;

                    pair_info[pair_id].mtx_dpu_run.lock();

                    if (pair_info[pair_id].is_running == false)
                    {
                        pair_info[pair_id].is_running = true;
                        pair_info[pair_id].mtx_dpu_run.unlock();
                    }
                    else
                    {
                        pair_info[pair_id].mtx_dpu_run.unlock();
                        continue;
                    }

                    for (const auto &task : tasks)
                    {
                        int cluster_id = std::get<0>(task);
                        int shard_id = std::get<1>(task);
                        const LOCATION &location = std::get<2>(task);

                        printf("  Task: cluster_id=%d, shard_id=%d, dpu_id=%d, slot_id=%d\n",
                               cluster_id, shard_id, location.dpu_id, location.slot_id);

                      
                        {
                            uint32_t data_offset = location.slot_id * SLOT_DATA_SIZE;
                            DATA_TYPE *data1 = getdata(cluster_id, shard_id);
                            DATA_TYPE *data2 = getdata(cluster_id, shard_id + 1);
                            int len1 = cluster_info[cluster_id]->s_len[shard_id];
                            int len2 = cluster_info[cluster_id]->s_len[shard_id + 1];

                            int len = len1 > len2 ? len1 : len2;
                            int alihn8 = (len + 7) / 8 * 8;

                            size_t data_size = alihn8 * MY_PQ_M * sizeof(DATA_TYPE);

                            fifo_dpu_copy_to(
                                dpu_id,
                                dpu_id + 1,
                                2,
                                "data",
                                data_offset,
                                (void *)data1,
                                (void *)data2,
                                data_size,
                                0);

                            uint32_t id_offset = location.slot_id * SLOT_ID_SIZE;
                            ID_TYPE *id1 = getids(cluster_id, shard_id);
                            ID_TYPE *id2 = getids(cluster_id, shard_id + 1);

                            size_t id_size = alihn8 * sizeof(ID_TYPE);

                            fifo_dpu_copy_to(
                                dpu_id,
                                dpu_id + 1,
                                2,
                                "data_id",
                                id_offset,
                                (void *)id1,
                                (void *)id2,
                                id_size,
                                0);

                            total_size_GB += (double)data_size / (1024 * 1024 * 1024);
                            total_size_GB += (double)id_size / (1024 * 1024 * 1024);
                        }
                    }

                    
                    for (const auto &task : tasks)
                    {
                        int cluster_id = std::get<0>(task);
                        int shard_id = std::get<1>(task);
                        const LOCATION &location = std::get<2>(task);

                        PAIR_LOCATION pair_location;
                        pair_location.dpu_id = location.dpu_id;
                        pair_location.slot_id = location.slot_id;
                        pair_location.dpu_id1 = location.dpu_id + 1;
                        pair_location.slot_id1 = location.slot_id;

                        cluster_info[cluster_id]->pair_shard_info[shard_id / 2].rw_mutex.lock();
                        cluster_info[cluster_id]->pair_shard_info[shard_id / 2].pair_location.push_back(pair_location);
                        cluster_info[cluster_id]->pair_shard_info[shard_id / 2].rw_mutex.unlock();
                    }

                    is_ok[dpu_id] = true;
                    dpu_add_num--;

                    pair_info[pair_id].mtx_dpu_run.lock();
                    pair_info[pair_id].is_running = false;
                    pair_info[pair_id].mtx_dpu_run.unlock();
                }
            };

       
            int num_threads = std::thread::hardware_concurrency();
            int tasks_per_thread = (dpu_add_tasks.size() + num_threads - 1) / num_threads;

            std::vector<std::thread> threads;
            for (int i = 0; i < num_threads; i++)
            {
                int start = i * tasks_per_thread;
                int end = std::min((i + 1) * tasks_per_thread, static_cast<int>(dpu_add_tasks.size()));
                threads.emplace_back(process_dpu_tasks, start, end);
            }

   
            for (auto &t : threads)
            {
                t.join();
            }

            timer2.end();

            xmh::PerfCounter counter;

            counter.Record("xfer size when dynamic balance(GB)", total_size_GB);
        }
    }

    dynamic_balance_thread_info[t_id].already_stop.store(true);
}

void DPUWrapper::dpu_search_batch(
    int *q_id,
    int k,
    int nprobe,
    int batch_size,
    int type,
    std::ofstream &outfile_active_num)
{
    std::string active_num_path;

    auto start_before = std::chrono::high_resolution_clock::now();

    float *simi_values = new float[k * batch_size];
    faiss::idx_t *simi_ids = new faiss::idx_t[k * batch_size];

    float *q_data = new float[batch_size * DIM];

    for (int i = 0; i < batch_size; i++)
    {
        memcpy(q_data + i * DIM,
               query_info[q_id[i]]->query_data,
               DIM * sizeof(float));
    }

    /*------level 1 search------*/

    // xmh::Timer timer1("   1:level 1 search");
    auto start = std::chrono::high_resolution_clock::now(); 

    faiss::idx_t *idx = new faiss::idx_t[batch_size * nprobe];
    float *coarse_dis = new float[batch_size * nprobe];

    // fifo_index->filter(batch_size, q_data, nprobe, coarse_dis, idx);
    fifo_index->quantizer->search(batch_size, q_data, nprobe, coarse_dis, idx, nullptr);

    for (int i = 0; i < batch_size; i++)
    {
        query_info[q_id[i]]->idx = new faiss::idx_t[nprobe];
        query_info[q_id[i]]->coarse_dis = new float[nprobe];
        query_info[q_id[i]]->nprobe = nprobe;

        memcpy(query_info[q_id[i]]->idx,
               idx + i * nprobe,
               nprobe * sizeof(faiss::idx_t));
        memcpy(query_info[q_id[i]]->coarse_dis,
               coarse_dis + i * nprobe,
               nprobe * sizeof(float));
    }
    // timer1.end();

    auto end = std::chrono::high_resolution_clock::now(); 
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    xmh::PerfCounter::Record("level1_search", duration.count());

    /*------get LUT------*/
    // xmh::Timer timer2("   2:get LUT");

    start = std::chrono::high_resolution_clock::now(); 

    DIST_TYPE *dis0_buffer_tmp = new DIST_TYPE[batch_size * nprobe];
    DIST_TYPE *sim_table_buffer_tmp =
        new DIST_TYPE[batch_size * nprobe * LUT_SIZE];

    for (int i = 0; i < batch_size; i++)
    {
        int q_id_i = q_id[i];
        std::shared_ptr<QUERY_INFO> query = query_info[q_id_i];

        scanner[0]->set_query(query->query_data);

        float *dis0_ = dis0_buffer[0];

        float *sim_table_ = sim_table_buffer[0];

        DIST_TYPE *dis0_dynamic = &dis0_buffer_tmp[i * nprobe];

        DIST_TYPE *sim_table_dynamic =
            &sim_table_buffer_tmp[i * nprobe * LUT_SIZE];

        for (int j = 0; j < query->nprobe; j++)
        {
            int c_id = query->idx[j];

            scanner[0]->set_list(c_id, query->coarse_dis[j]);
            scanner[0]->get_dis0(dis0_[c_id]);
            // dis0_[c_id] = scanner[0]->dis0;

            dis0_dynamic[j] = (DIST_TYPE)(dis0_[c_id]);

            scanner[0]->get_sim_table(
                &sim_table_[c_id * LUT_SIZE], LUT_SIZE);

            // memcpy(
            //     &sim_table_[c_id * LUT_SIZE],
            //     scanner[0]->sim_table,
            //     LUT_SIZE * sizeof(float));

            for (int j1 = 0; j1 < LUT_SIZE; j1++)
            {
                sim_table_dynamic[j * LUT_SIZE + j1] =
                    (DIST_TYPE)(sim_table_[c_id * LUT_SIZE + j1]);
            }
        }
    }

    // timer2.end();

    end = std::chrono::high_resolution_clock::now(); 
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    xmh::PerfCounter::Record("LUT construct", duration.count());

    /*------level 2 search------*/

    xmh::Timer timer3("   3:level 2 search");

    // xmh::Timer timer4("     prepare task");

    start = std::chrono::high_resolution_clock::now(); 

    dpu_fifo_input_t *task_all_dpu =
        new dpu_fifo_input_t[MAX_DPU * MAX_DPUBATCH];

    int task_all_dpu_index[MAX_DPU];
    for (int i = 0; i < MAX_DPU; i++)
    {
        task_all_dpu_index[i] = 0;
    }
    int *load_per_dpu = new int[MAX_DPU];
    for (int i = 0; i < MAX_DPU; i++)
    {
        load_per_dpu[i] = 0;
    }

    for (int i = 0; i < batch_size; i++)
    {
        int q_id_i = q_id[i];

        DIST_TYPE *dis0_dynamic = &dis0_buffer_tmp[i * nprobe];

        DIST_TYPE *sim_table_dynamic =
            &sim_table_buffer_tmp[i * nprobe * LUT_SIZE];

        std::shared_ptr<QUERY_INFO> query = query_info[q_id_i];
        for (int j = 0; j < query->nprobe; j++)
        {
            faiss::idx_t key = query->idx[j];
            std::shared_ptr<CLUSTER_INFO> cluster = cluster_info[key];

            int pair_shard_num = cluster->pair_shard_num;

            for (int j1 = 0; j1 < pair_shard_num; j1++)
            {
                int minload_replica_id = -1;
                int minload = INT_MAX;
                int minload_dpu_id = -1;

                for (int j2 = 0; j2 < cluster->replica; j2++)
                {
                    int dpu_id =
                        cluster->pair_shard_info[j1].pair_location[j2].dpu_id;

                    int load = load_per_dpu[dpu_id];
                    if (minload_replica_id == -1 || load < minload)
                    {
                        if (task_all_dpu_index[dpu_id] < MAX_DPUBATCH)
                        {
                            minload_replica_id = j2;
                            minload = load;
                            minload_dpu_id = dpu_id;
                        }
                    }
                }

                assert(minload_dpu_id != -1);

                load_per_dpu[minload_dpu_id] += cluster->workload_pershard;

                int replica_id = minload_replica_id;
                // int replica_id = 0;

                int pair_id = minload_dpu_id / 2;
                int rank_id = minload_dpu_id / 64;

                PAIR_TASK pair_task;
                pair_task.enable_num = 2;

                pair_task.task[0].slot_id = cluster->pair_shard_info[j1].pair_location[replica_id].slot_id;
                pair_task.task[1].slot_id = cluster->pair_shard_info[j1].pair_location[replica_id].slot_id1;

                int enable_num = pair_task.enable_num;
                for (int task_id = 0; task_id < enable_num; task_id++)
                {
                    pair_task.task[task_id].dpu_id = minload_dpu_id + task_id;
                    pair_task.task[task_id].pair_id = pair_id;
                    pair_task.task[task_id].rank_id = rank_id;

                    pair_task.task[task_id].c_id = key;
                    int shard_id = j1 * 2 + task_id;
                    pair_task.task[task_id].shard_id = shard_id;

                    pair_task.task[task_id].shard_l = cluster->s_len[shard_id];

                    pair_task.task[task_id].q_id = q_id_i;
                    pair_task.task[task_id].k = k;

                    pair_task.task[task_id].dis0 = dis0_dynamic[j];

                    memcpy(pair_task.task[task_id].LUT,
                           &sim_table_dynamic[j * LUT_SIZE],
                           sizeof(DIST_TYPE) * LUT_SIZE);

                    int dpu_id = pair_task.task[task_id].dpu_id;

                    dpu_info[dpu_id].task_num++;
                    dpu_info[dpu_id].need_compute_distance_num +=
                        pair_task.task[task_id].shard_l;

                    int index = task_all_dpu_index[dpu_id];

                    task_all_dpu[dpu_id * MAX_DPUBATCH + index] =
                        pair_task.task[task_id];

                    task_all_dpu_index[dpu_id]++;
                }
            }
        }
    }
    // timer4.end();

    end = std::chrono::high_resolution_clock::now(); 
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    xmh::PerfCounter::Record("prepare task", duration.count());

    // copy to dpu

    // xmh::Timer timer5("     host->dpu time");
    start = std::chrono::high_resolution_clock::now(); 

    int max_query_num = 0;

    for (int i = 0; i < MAX_DPU; i++)
    {
        if (dpu_info[i].is_enabled == false)
        {
            continue;
        }

        dpu_set_t dpu = dpu_info[i].dpu;

        DPU_ASSERT(dpu_prepare_xfer(dpu, &task_all_dpu[i * MAX_DPUBATCH]));

        if (task_all_dpu_index[i] > max_query_num)
        {
            max_query_num = task_all_dpu_index[i];
        }
    }
    if (max_query_num > MAX_DPUBATCH)
    {
        printf("max_query_num = %d, MAX_DPUBATCH = %d\n",
               max_query_num,
               MAX_DPUBATCH);
    }

    assert(max_query_num <= MAX_DPUBATCH);
    DPU_ASSERT(dpu_push_xfer(
        fifo_set,
        DPU_XFER_TO_DPU,
        "query",
        0,
        sizeof(dpu_fifo_input_t) * max_query_num,
        DPU_XFER_DEFAULT));

    delete[] task_all_dpu;

    int64_t num_query_this_batch[MAX_DPU];
    for (int i = 0; i < MAX_DPU; i++)
    {
        num_query_this_batch[i] = 0;
    }

    for (int i = 0; i < MAX_DPU; i++)
    {
        if (dpu_info[i].is_enabled == false)
        {
            continue;
        }

        dpu_set_t dpu = dpu_info[i].dpu;
        num_query_this_batch[i] = task_all_dpu_index[i];
        DPU_ASSERT(dpu_prepare_xfer(dpu, &num_query_this_batch[i]));
    }
    DPU_ASSERT(dpu_push_xfer(
        fifo_set,
        DPU_XFER_TO_DPU,
        "num_query_this_batch",
        0,
        sizeof(int64_t),
        DPU_XFER_DEFAULT));

    // timer5.end();

    end = std::chrono::high_resolution_clock::now(); 
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    xmh::PerfCounter::Record("Copy data", duration.count());

    // launch

    auto end_before = std::chrono::high_resolution_clock::now();

    start = std::chrono::high_resolution_clock::now();
    DPU_ASSERT(dpu_launch(fifo_set, DPU_SYNCHRONOUS));
    end = std::chrono::high_resolution_clock::now();
    double time =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count();
    printf("this batch time = %f ms\n", time);

    auto start_after = std::chrono::high_resolution_clock::now();

    // xmh::Timer timer6("     dpu->host time");
    start = std::chrono::high_resolution_clock::now(); 

    // copy from dpu
    int max_result_num = max_query_num;

    dpu_fifo_output_t *result = new dpu_fifo_output_t[MAX_DPU * MAX_DPUBATCH];
    for (int i = 0; i < MAX_DPU; i++)
    {
        if (dpu_info[i].is_enabled == false)
        {
            continue;
        }
        dpu_set_t dpu = dpu_info[i].dpu;
        DPU_ASSERT(dpu_prepare_xfer(dpu, &result[i * MAX_DPUBATCH]));
    }

    DPU_ASSERT(dpu_push_xfer(
        fifo_set,
        DPU_XFER_FROM_DPU,
        "result",
        0,
        sizeof(dpu_fifo_output_t) * max_result_num,
        DPU_XFER_DEFAULT));

    // timer6.end();

    end = std::chrono::high_resolution_clock::now(); 
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // xmh::PerfCounter::Record("dpu->host time", duration.count());

    // xmh::Timer timer7("     merge result");
    start = std::chrono::high_resolution_clock::now(); 

    for (int i = 0; i < MAX_DPU; i++)
    {
        if (dpu_info[i].is_enabled == false)
        {
            continue;
        }
        if (task_all_dpu_index[i] > 0)
        {
            int sz = task_all_dpu_index[i];
            assert(sz <= MAX_DPUBATCH);

            for (int j = 0; j < sz; j++)
            {
                int q_id = result[i * MAX_DPUBATCH + j].q_id;

                std::shared_ptr<QUERY_INFO> query = query_info[q_id];

                int k_fromdpu = result[i * MAX_DPUBATCH + j].k;
                for (int j1 = 0; j1 < k_fromdpu; j1++)
                {
                    DIST_TYPE value = result[i * MAX_DPUBATCH + j].result_v[j1];
                    faiss::idx_t id =
                        result[i * MAX_DPUBATCH + j].result_id[j1];

                    if (value < query->simi_values[0])
                    {
                        faiss::heap_replace_top<
                            faiss::CMax<DIST_TYPE, ID_TYPE>>(
                            k,
                            query->simi_values,
                            query->simi_ids,
                            value,
                            id);
                    }
                }
            }
        }
    }

    // timer7.end();

    // xmh::Timer timer8("     record timer");

    auto end_after = std::chrono::high_resolution_clock::now();

    auto time_ms_before = std::chrono::duration_cast<std::chrono::milliseconds>(
                              end_before - start_before)
                              .count();

    auto time_ms_after = std::chrono::duration_cast<std::chrono::milliseconds>(
                             end_after - start_after)
                             .count();

    int num_zero_before = time_ms_before / SAMPLE_INTERVAL_MS;
    for (int i = 0; i < num_zero_before; i++)
    {
        outfile_active_num << 0 << std::endl;
    }

    uint64_t start_eachdpu[MAX_DPU];
    uint64_t end_eachdpu[MAX_DPU];
    for (int i = 0; i < MAX_DPU; i++)
    {
        if (dpu_info[i].is_enabled == false)
        {
            continue;
        }
        end_eachdpu[i] = result[i * MAX_DPUBATCH + 0].end;
        start_eachdpu[i] = result[i * MAX_DPUBATCH + 0].start;
    }
    double max_time = 0;
    for (int i = 0; i < MAX_DPU; i++)
    {
        if (dpu_info[i].is_enabled == false)
        {
            continue;
        }

        double time = ((double)(end_eachdpu[i] - start_eachdpu[i])) /
                      MS_CLOCK_DPU_SELF;
        if (time > max_time)
        {
            max_time = time;
        }
    }
    printf("max_time = %f ms\n", max_time);
    xmh::PerfCounter::Record("max_dpu_time", max_time);

    const int total_time_ms = max_time; 
    const int num_samples = total_time_ms / SAMPLE_INTERVAL_MS;

    for (int sample = 0; sample < num_samples; sample++)
    {
        int busy_dpu_count = 0;
        for (int i = 0; i < MAX_DPU; i++)
        {
            if (dpu_info[i].is_enabled == false)
            {
                continue;
            }

            uint64_t sample_start_time =
                sample * SAMPLE_INTERVAL_MS * MS_CLOCK_DPU_SELF;
            uint64_t sample_end_time = sample_start_time +
                                       (SAMPLE_INTERVAL_MS * MS_CLOCK_DPU_SELF);

            if (sample_start_time <= end_eachdpu[i] &&
                sample_end_time >= start_eachdpu[i])
            {
                busy_dpu_count++;
            }
        }
        outfile_active_num << busy_dpu_count << std::endl;
    }

    int num_zero_after = time_ms_after / SAMPLE_INTERVAL_MS;
    for (int i = 0; i < num_zero_after; i++)
    {
        outfile_active_num << 0 << std::endl;
    }

    // timer8.end();

    end = std::chrono::high_resolution_clock::now(); 
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    xmh::PerfCounter::Record("merge result", duration.count());

    timer3.end();
}
