/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

#include <sys/time.h>

#include <faiss/gpu/GpuAutoTune.h>
#include <faiss/gpu/GpuCloner.h>
#include <faiss/gpu/GpuIndexIVFPQ.h>
#include <faiss/gpu/StandardGpuResources.h>
#include <faiss/index_io.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/time.h>

#include <faiss/AutoTune.h>
#include <faiss/index_factory.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>

#include <faiss/IndexPQ.h>
#include <faiss/index_io.h>

#include <faiss/IndexFlat.h>
#include <faiss/IndexIVFPQ.h>
#include <faiss/index_io.h>

#include <stdio.h>
#include <stdlib.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <fstream>
#include <iostream>
#include <vector>

#include <stdio.h>
#include <stdlib.h>

#include <faiss/gpu/GpuIndexFlat.h>
#include <faiss/gpu/StandardGpuResources.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include <omp.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// type==0: sift1
// type==1: space
// type == 2:sift1m
float* read_query(const char* filename, int type, int& nvecs, int& dim) {
    if (type == 0) {
        FILE* f = fopen(filename, "rb");
        if (!f) {
            perror("Error opening file");
            return NULL;
        }

        assert(fread(&nvecs, sizeof(int), 1, f) == 1);
        assert(fread(&dim, sizeof(int), 1, f) == 1);

        int total_elements = nvecs * dim;
        uint8_t* int_data = new uint8_t[total_elements]();

        assert(fread(int_data, sizeof(uint8_t), total_elements, f) ==
               total_elements);

        fclose(f);

        float* float_data = new float[total_elements]();
        for (int i = 0; i < total_elements; i++) {
            float_data[i] = (float)int_data[i];
        }

        free(int_data);

        return float_data;
    } else if (type == 1) {
        FILE* f = fopen(filename, "rb");
        printf("filename is %s\n", filename);
        if (!f) {
            perror("Error opening file");
            return NULL;
        }

        assert(fread(&nvecs, sizeof(int), 1, f) == 1);
        assert(fread(&dim, sizeof(int), 1, f) == 1);

        int total_elements = nvecs * dim;
        int8_t* int_data = new int8_t[total_elements]();
        assert(fread(int_data, sizeof(int8_t), total_elements, f) ==
               total_elements);

        fclose(f);

        float* float_data = new float[total_elements]();
        for (int i = 0; i < total_elements; i++) {
            float_data[i] = (float)int_data[i];
        }

        free(int_data);

        return float_data;
    } else if (type == 2) {
        std::ifstream infile;
        infile.open(filename, std::ios::binary);
        std::vector<std::vector<float>> vectors;

        int nq = 0;
        int dim_ = 128;

        while (infile) {
            // Read dimension
            int dim;
            infile.read(reinterpret_cast<char*>(&dim), sizeof(int));
            if (!infile)
                break;
            assert(dim == dim_);

            // Read vector data
            std::vector<float> vec(dim);
            infile.read(
                    reinterpret_cast<char*>(vec.data()), dim * sizeof(float));
            vectors.push_back(vec);
            nq++;
        }

        float* float_data = new float[nq * dim_]();
        for (int i = 0; i < nq; i++) {
            for (int j = 0; j < dim_; j++) {
                float_data[i * dim_ + j] = vectors[i][j];
            }
        }

        infile.close();

        return float_data;
    }
}

std::string query_path = "/mnt/data/gbase/wpq/dataset/space/query10K.i8bin";

std::string index_path_read =
        "/mnt/data/gbase/wpq/dataset/space/space1B_M20_PQ8_C4096_R_L2.faissindex";

int main(int argc, char** argv) {
    int nprobe = -1;
    if (argc > 1) {
        nprobe = atoi(argv[1]);
    } else {
        printf("Usage: %s <nprobe>\n", argv[0]);
        return 1;
    }

    int d = 100;

    int dev_no = 0;

    int ncentroids = 4096;

    int pq_m = 20;

    faiss::gpu::StandardGpuResources resources;

    faiss::gpu::GpuIndexIVFPQConfig config;
    config.usePrecomputedTables = true;
    config.device = dev_no;

    faiss::Index* cpu_index_begin1 = faiss::read_index(index_path_read.c_str());

    faiss::IndexIVFPQ* index1 =
            dynamic_cast<faiss::IndexIVFPQ*>(cpu_index_begin1);

    int n_cluster_for_yanzheng = index1->invlists->nlist;

    assert(ncentroids == n_cluster_for_yanzheng);

    int m_for_yanzheng = index1->pq.M;

    assert(m_for_yanzheng == pq_m);

    printf("cpu read index ok\n");

    faiss::gpu::GpuIndexIVFPQ gpuIndex(
            &resources, d, ncentroids, pq_m, 8, faiss::METRIC_L2, config);

    gpuIndex.copyFrom(index1);

    printf("gpu copy index ok\n");

    int k = 10;

    int nq;
    int dim;

    float* query_data = read_query(query_path.c_str(), 1, nq, dim);

    faiss::idx_t* nns = new faiss::idx_t[nq * k];
    float* dis = new float[nq * k];

    auto start = std::chrono::high_resolution_clock::now(); 

    faiss::SearchParametersIVF params;
    params.nprobe = nprobe;

 
    gpuIndex.search(nq, query_data, k, dis, nns, &params);

    auto end = std::chrono::high_resolution_clock::now(); 

    auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double time_ms = duration.count();

    double qps = nq / (time_ms / 1000.0);
    printf("qps=%.2f\n", qps);

    return 0;
}
