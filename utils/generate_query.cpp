#include "host/host_fifo.h"
#include "host/zipf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <iostream>

#include <sstream>

using namespace std;
string query_path = "/mnt/optane/wpq/dataset/space/query10K.i8bin";
string index_path = "/mnt/optane/wpq/dataset/space/index/space1B_M20_PQ8_C4096_R_L2.faissindex";

int max_query = 10000;
int dim = 100;
int nprobe = 11;
int max_nprobe_value = 4096;


uint64_t num_samples = 1000;

double theta = 0.99; 

#define MAX_VALUE 10000 

string nprobe_path = std::string(PROJECT_SOURCE_DIR) + "/space-nprobe=11.txt";

string gt_path = "/mnt/optane/wpq/dataset/space/space1B_L2_gt_k100.bin";
int main()
{
    // string generate_query_path = "/mnt/optane/wpq/dataset/space/query10K-theta-0.8.i8bin";
    string generate_query_path = "/mnt/optane/wpq/dataset/space/dynamic-query"+to_string(max_query)+"-theta-"+to_string(theta)+".i8bin";
    // string history_query_path = "/mnt/optane/wpq/dataset/space/query10K-history-theta-0.8.i8bin";
    string history_query_path = "/mnt/optane/wpq/dataset/space/history-query"+to_string(max_query)+"-theta-"+to_string(theta)+".i8bin";

    // string generate_gt_path = "/mnt/optane/wpq/dataset/space/space1B-theta-0.8_L2_gt_k100.bin";
    string generate_gt_path = "/mnt/optane/wpq/dataset/space/space1B-query"+to_string(max_query)+"-theta-"+to_string(theta)+"_L2_gt_k100.bin";

    int nq_1;
    int dim_1;
    float *query_data = read_query(query_path.c_str(), 1, nq_1, dim_1);
    assert(dim_1 == dim);

    int nq_2;
    int k;
    ID_TYPE *ground_truth_ids = read_groundtruth(gt_path.c_str(), nq_2, k);

    assert(nq_1 == nq_2);

    float coarse_dis[MAX_VALUE][nprobe];
    faiss::idx_t coarse_ids[MAX_VALUE][nprobe];

    // wramup
    // {
    // faiss::IndexIVFPQ *fifo_index = dynamic_cast<faiss::IndexIVFPQ *>(
    //     faiss::read_index(index_path.c_str()));

    //     fifo_index->quantizer->search(MAX_VALUE, query_data + dim * sizeof(int8_t), nprobe, coarse_dis[0], coarse_ids[0], nullptr);

    //     ofstream outfile;
    //     outfile.open(nprobe_path, std::ios::trunc); 

    //     for (int i = 0; i < MAX_VALUE; i++)
    //     {
    //         for (int j = 0; j < nprobe; j++)
    //         {
    //             outfile << coarse_ids[i][j] << " ";
    //         }
    //         outfile << endl;
    //     }

    //     outfile.close();
    // }

    ifstream infile;
    infile.open(nprobe_path);
    for (int i = 0; i < MAX_VALUE; i++)
    {
        for (int j = 0; j < nprobe; j++)
        {
            infile >> coarse_ids[i][j];
        }
    }
    infile.close();


    struct zipf_gen_state state;
    uint64_t n = MAX_VALUE; 

    // uint64_t seed = 12345;   
    uint64_t seed = time(NULL);

    mehcached_zipf_init(&state, n, theta, seed);


    uint64_t *frequency = (uint64_t *)calloc(MAX_VALUE, sizeof(uint64_t));
    if (frequency == NULL)
    {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }

    uint64_t repeat_times = max_query / 2 / num_samples;

    uint64_t *frequency_id = (uint64_t *)calloc(max_nprobe_value, sizeof(uint64_t));
    if (frequency_id == NULL)
    {
        fprintf(stderr, "Memory allocation failed\n");
        free(frequency);
        return 1;
    }

    ofstream outfile_generate_query;
    ofstream outfile_hostory_query;
    ofstream outfile_generate_gt;

    outfile_generate_query.open(generate_query_path, std::ios::binary);
    outfile_hostory_query.open(history_query_path, std::ios::binary);
    outfile_generate_gt.open(generate_gt_path, std::ios::binary);

    outfile_generate_query.write((char *)&max_query, sizeof(max_query));
    outfile_generate_query.write((char *)&dim, sizeof(dim));

    outfile_hostory_query.write((char *)&max_query, sizeof(max_query));
    outfile_hostory_query.write((char *)&dim, sizeof(dim));

    outfile_generate_gt.write((char *)&max_query, sizeof(max_query));
    outfile_generate_gt.write((char *)&k, sizeof(k));

    uint64_t value_store[num_samples];
    for (uint64_t i = 0; i < num_samples; i++)
    {
        uint64_t value = mehcached_zipf_next(&state);

        if (value < MAX_VALUE)
        {
            value_store[i] = value;
        }
        else
        {
            fprintf(stderr, "Warning: Generated value %lu out of range\n", value);
        }
    }
    for (int r_idx = 0; r_idx < repeat_times; r_idx++)
    {

        for (uint64_t i = 0; i < num_samples; i++)
        {
            uint64_t value = value_store[i];

            if (value < MAX_VALUE)
            {
                frequency[value]++;

                for (int j = 0; j < dim; j++)
                {
                    int8_t val = static_cast<int8_t>(query_data[value * dim + j]);
                    outfile_generate_query.write(reinterpret_cast<const char *>(&val), sizeof(int8_t));
                    outfile_hostory_query.write(reinterpret_cast<const char *>(&val), sizeof(int8_t));
                }
                for (int j = 0; j < dim; j++)
                {
                    int8_t val = static_cast<int8_t>(query_data[value * dim + j]);
                    outfile_hostory_query.write(reinterpret_cast<const char *>(&val), sizeof(int8_t));
                }

                for (int j = 0; j < nprobe; j++)
                {
                    if (coarse_ids[value][j] < max_nprobe_value)
                    {
                        frequency_id[coarse_ids[value][j]]++;
                    }
                }

                for (int j = 0; j < k; j++)
                {
                    int val = ground_truth_ids[value * k + j];
                    outfile_generate_gt.write(reinterpret_cast<const char *>(&val), sizeof(int));
                }
            }
            else
            {
                fprintf(stderr, "Warning: Generated value %lu out of range\n", value);
            }
        }
    }

    for (uint64_t i = 0; i < num_samples; i++)
    {
        uint64_t value = mehcached_zipf_next(&state);

        if (value < MAX_VALUE)
        {
            value = MAX_VALUE - 1 - value;
            value_store[i] = value;
        }
        else
        {
            fprintf(stderr, "Warning: Generated value %lu out of range\n", value);
        }
    }
    for (int r_idx = 0; r_idx < repeat_times; r_idx++)
    {

        for (uint64_t i = 0; i < num_samples; i++)
        {
            uint64_t value = value_store[i];

            if (value < MAX_VALUE)
            {

                frequency[value]++;

                for (int j = 0; j < dim; j++)
                {
                    int8_t val = static_cast<int8_t>(query_data[value * dim + j]);
                    outfile_generate_query.write(reinterpret_cast<const char *>(&val), sizeof(int8_t));
                }

                for (int j = 0; j < nprobe; j++)
                {
                    if (coarse_ids[value][j] < max_nprobe_value)
                    {
                        frequency_id[coarse_ids[value][j]]++;
                    }
                }

                for (int j = 0; j < k; j++)
                {
                    int val = ground_truth_ids[value * k + j];
                    outfile_generate_gt.write(reinterpret_cast<const char *>(&val), sizeof(int));
                }
            }
            else
            {
                fprintf(stderr, "Warning: Generated value %lu out of range\n", value);
            }
        }
    }

    outfile_generate_query.close();
    outfile_hostory_query.close();
    outfile_generate_gt.close();


    printf("Top 20 most frequent values:\n");
    printf("Rank\tValue\tFrequency\tPercentage\n");


    typedef struct
    {
        uint64_t value;
        uint64_t count;
    } ValueCount;

    ValueCount *sorted = (ValueCount *)malloc(MAX_VALUE * sizeof(ValueCount));
    if (sorted == NULL)
    {
        fprintf(stderr, "Memory allocation failed\n");
        free(frequency);
        return 1;
    }

    for (uint64_t i = 0; i < MAX_VALUE; i++)
    {
        sorted[i].value = i;
        sorted[i].count = frequency[i];
    }

    for (uint64_t i = 0; i < 20; i++)
    {
        for (uint64_t j = i + 1; j < MAX_VALUE; j++)
        {
            if (sorted[j].count > sorted[i].count)
            {
                ValueCount temp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = temp;
            }
        }
        printf("%lu\t%lu\t%lu\t\t%.2f%%\n",
               i + 1,
               sorted[i].value,
               sorted[i].count,
               (double)sorted[i].count * 100.0 / num_samples);
    }


    // for (uint64_t i = 0; i < MAX_VALUE; i++) {
    //     if (frequency[i] > 0) {
    //         printf("Value %lu: %lu times (%.2f%%)\n",
    //                i, frequency[i],
    //                (double)frequency[i] * 100.0 / num_samples);
    //     }
    // }

    printf("\nTop 20 most frequent IDs in coarse_ids:\n");
    printf("Rank\tID\tFrequency\tPercentage\n");

    ValueCount *sorted_ids = (ValueCount *)malloc(max_nprobe_value * sizeof(ValueCount));
    if (sorted_ids == NULL)
    {
        fprintf(stderr, "Memory allocation failed\n");
        free(frequency);
        free(frequency_id);
        free(sorted);
        return 1;
    }

    for (uint64_t i = 0; i < max_nprobe_value; i++)
    {
        sorted_ids[i].value = i;
        sorted_ids[i].count = frequency_id[i];
    }


    for (uint64_t i = 0; i < 4096 && i < max_nprobe_value; i++)
    {
        for (uint64_t j = i + 1; j < max_nprobe_value; j++)
        {
            if (sorted_ids[j].count > sorted_ids[i].count)
            {
                ValueCount temp = sorted_ids[i];
                sorted_ids[i] = sorted_ids[j];
                sorted_ids[j] = temp;
            }
        }
        printf("%lu\t%lu\t%lu\t\t%.2f%%\n",
               i + 1,
               sorted_ids[i].value,
               sorted_ids[i].count,
               (double)sorted_ids[i].count * 100.0 / (num_samples * nprobe)); 
    }

    free(frequency_id);
    free(sorted_ids);

    free(frequency);
    free(sorted);

    return 0;
}