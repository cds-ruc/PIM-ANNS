#include <boost/coroutine/all.hpp>
#include <functional> 
#include <iostream>
#include "perfv2.h"

const int kMaxWorker = 8;
const int test_count = 10000;
using boost::coroutines::coroutine;
coroutine<void>::pull_type* worker[kMaxWorker];
int global_in[kMaxWorker];
int i_in_master[kMaxWorker];

void worker_func(coroutine<void>::push_type& sink, int coro_id)
{
    int cnt = 0;
    int step = coro_id;
    while (true){
        i_in_master[coro_id] = (cnt+=step);
        sink();
        global_in[coro_id] = i_in_master[coro_id];
    }
    printf("coro %d end\n", coro_id);
}

int main()
{
    
    using std::placeholders::_1;
    for (int i = 0; i < kMaxWorker; i++) {
        worker[i] = new coroutine<void>::pull_type{ std::bind(worker_func, _1, i) };
    }
    
    puts("begin running");
    perf::Timer timer;
    timer.begin();

    for (int i = 0; i < kMaxWorker * test_count; i++){
        (*worker[i % kMaxWorker])();
    }

    uint64_t res = timer.end();
    puts("----------result show----------");
    for (int i = 0; i < kMaxWorker; i++)
    {
        std::cout << i_in_master[i] << " " << global_in[i] << std::endl;
    }
    puts("----------perf----------");
    std::cout << res * 1.0 / (test_count * kMaxWorker) << "ns per [2-switch]" << std::endl;

}