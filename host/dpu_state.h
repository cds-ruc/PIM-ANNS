

#ifndef DPU_STATE_H
#define DPU_STATE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __cplusplus
extern "C" {
#endif

static inline void mark_stop(int dpu_id) {
    char key[30];
    sprintf(key, "DPU_%d_STATE", dpu_id);
    setenv(key, "stop", 1);
    // char* state = getenv(key);
    // printf("DPU_%d_STATE set to stop, state: %s\n", dpu_id, state);
}
static inline void mark_running(int dpu_id) {
    char key[30];
    sprintf(key, "DPU_%d_STATE", dpu_id);
    setenv(key, "running", 1);
    // char* state = getenv(key);
    // printf("DPU_%d_STATE set to running, state: %s\n", dpu_id, state);
}
static inline bool is_dpu_running(int dpu_id) {
    char key[30];
    sprintf(key, "DPU_%d_STATE", dpu_id);
    char* state = getenv(key);
    bool res = state && strcmp(state, "running") == 0;
    
    return res;
}

#ifdef __cplusplus
}
#endif

#endif