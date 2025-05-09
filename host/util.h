#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <cassert>
#include <fstream>
#include <string>

#include "third-party/faiss_upmem/faiss/timer.h"
#include "host/host_common.h"
#include "common/common.h"

float *read_query(
    const char *filename,
    int type,
    int &nvecs,
    int &dim);

ID_TYPE *read_groundtruth(const char *filename, int &n, int &k);
