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

double elapsed() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}



int main(int argc, char** argv) {
    double t0 = elapsed();

    int PQ_M = 16; // 16, 32, 64
    int PQ_PQ = 8;
    int n_cluster = 2500;
    int vec_dim = 128; // rand1B 128 dim

    faiss::IndexFlatL2 coarse_quantizer(vec_dim);

    faiss::IndexIVFPQ index(
            &coarse_quantizer,
            vec_dim,
            n_cluster,
            PQ_M,
            PQ_PQ,
            faiss::METRIC_INNER_PRODUCT);
    index.by_residual = false;

    int d = vec_dim;
    float* xb;
    size_t nb = 100 * 1000 * 1000;
    printf("[%.3f s] Generating %ld random vectors in %dD, type = float\n",
           elapsed() - t0,
           nb,
           vec_dim);

    generate_centroid(n_cluster, vec_dim);

    generate_dataset(nb, vec_dim, n_cluster, &xb);

    std::string index_path;

    printf("[%.3f s] complete generating %ld random vectors\n",
           elapsed() - t0,
           nb);

    {
        // generate

        float* xt = xb;
        size_t nt = 100 * 1000 * 1000;
        if (nt > nb) {
            nt = nb;
        }
        if (nt % n_cluster != 0) {
            printf("nt %ld is not a multiple of n_cluster %d\n", nt, n_cluster);
            return 0;
        }

        printf("[%.3f s] Training on %ld vectors\n", elapsed() - t0, nt);

        index.train(nt, xt);

        index_path =
                "/home/wupuqing/workspace/dataset/balance1B/no_data_balance1B_M" +
                std::to_string(PQ_M) + "_PQ" + std::to_string(PQ_PQ) + "_C" +
                std::to_string(n_cluster) + ".faissindex";

        printf("[%.3f s] Saving index to %s\n",
               elapsed() - t0,
               index_path.c_str());
        faiss::write_index(&index, index_path.c_str());
    }
    return 0;
}