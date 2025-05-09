#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/time.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <fstream>



#include "third-party/faiss_upmem/faiss/IndexPQ.h"
#include "third-party/faiss_upmem/faiss/index_io.h"
#include "third-party/faiss_upmem/faiss/IndexFlat.h"
#include "third-party/faiss_upmem/faiss/IndexIVFPQ.h"
#include "third-party/faiss_upmem/faiss/index_io.h"
#include "third-party/faiss_upmem/faiss/AutoTune.h"
#include "third-party/faiss_upmem/faiss/index_factory.h"


using namespace std;
double elapsed()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

float *fread_u8bin(
    const char *filename,
    int start_idx,
    int chunk_size)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
    {
        perror("Error opening file");
        return NULL;
    }

    int nvecs, dim;
    fread(&nvecs, sizeof(int), 1, f);
    fread(&dim, sizeof(int), 1, f);

    if (chunk_size > nvecs - start_idx)
    {
        printf("chunk_size %d should be less than nvecs %d\n", chunk_size, nvecs);
        exit(1);
    }

    uint8_t *u8 = (uint8_t *)malloc(chunk_size * dim);
    fseek(f, start_idx * dim, SEEK_CUR);
    fread(u8, sizeof(uint8_t), chunk_size * dim, f);

    fclose(f);

    float *float_data = (float *)malloc(chunk_size * dim * sizeof(float));
    for (int i = 0; i < chunk_size * dim; i++)
    {
        float_data[i] = (float)u8[i];
    }

    free(u8);

    return float_data;
}

string trainset_path = "/mnt/optane/wpq/dataset/sift/learn.100M.u8bin";
// string dataset_path = "/mnt/optane/wpq/dataset/sift/base.1B.u8bin";
string dataset_path = "/mnt/optane/wpq/dataset/sift/learn.100M.u8bin";

string q_path = "/mnt/optane/wpq/dataset/sift/gt/query.public.10K.u8bin";

string index_path_prefix ="/mnt/optane/wpq/dataset/sift/";


void train_ivfpq(int d, int n_cluster, int pq_m = 16, int pq_ksub = 8)
{
    if (d % pq_m != 0)
    {
        printf("d %d should be divisible by pq_m %d\n", d, pq_m);
        exit(1);
    }
    double t0 = elapsed();

    faiss::IndexFlatL2 coarse_quantizer(d);

    faiss::IndexIVFPQ index(
        &coarse_quantizer,
        d,
        n_cluster,
        pq_m,
        pq_ksub,
        faiss::METRIC_L2);
    index.verbose = true;

    // train
    {
        int start_idx = 0;
        int chunk_size = 100 * 1000 * 100;

        float *xt = fread_u8bin(
            trainset_path.c_str(), start_idx, chunk_size);

        printf("[%.3f s] read %d vectors of dimension %d\n",
               elapsed() - t0,
               chunk_size,
               d);

        index.train(chunk_size, xt);

        // string index_path = index_path_prefix + "empty_sift1B_M" +
        //                     to_string(pq_m) + "_PQ" + to_string(pq_ksub) + "_C" +
        //                     to_string(n_cluster) + ".faissindex";

        // printf("[%.3f s] Saving empty index to %s\n",
        //        elapsed() - t0,
        //        index_path.c_str());
        // faiss::write_index(&index, index_path.c_str());
    }

    // add
    {
        int batch = 100;
        int start_idx, end_idx;
        int nb = 1000 * 1000 * 1000;
        int num_per_batch = nb / batch;
        for (int i = 0; i < batch; i++)
        {
            start_idx = i * num_per_batch;
            end_idx = (i + 1) * num_per_batch;
            if (i == batch - 1)
            {
                end_idx = nb;
                assert(end_idx - start_idx == num_per_batch);
            }
            float *xb = fread_u8bin(
                dataset_path.c_str(), start_idx, num_per_batch);

            printf("[%.3f s] %d th batch, read %d vectors of dimension %d\n",
                   elapsed() - t0,
                   i,
                   num_per_batch,
                   d);

            index.add((end_idx - start_idx), xb);
            free(xb);
        }


        string index_path = index_path_prefix + "sift1B_M" +
                            to_string(pq_m) + "_PQ" + to_string(pq_ksub) + "_C" +
                            to_string(n_cluster) + ".faissindex";

        printf("[%.3f s] Saving index to %s\n",
               elapsed() - t0,
               index_path.c_str());
        // faiss::write_index(&index, index_path.c_str());
    }
}

int main(){
    train_ivfpq(128, 4096, 16, 8);
    return 0;

}