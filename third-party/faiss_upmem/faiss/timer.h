#pragma once
#include <assert.h>
// #include <cuda_runtime_api.h>

#include <sys/time.h>

#include <gflags/gflags.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef XMH_PERF
#define XMH_PERF
#endif

#ifndef XMH_DEBUG
// #define XMH_DEBUG
#endif

#ifndef XMH_PERF_GPU
// #define XMH_PERF_GPU
#endif

// #include "base/log.h"

class XDebug {
   public:
    static void AssertTensorEq(
            const float* emb,
            int dim,
            uint64_t value,
            const std::string& debug_str) {
        for (int i = 0; i < dim; i++) {
            assert(std::abs(emb[i] - value) < 1e-6);
        }
    }
};

#ifndef unlikely
#define unlikely(expr) __builtin_expect(!!(expr), 0)
#define likely(expr) __builtin_expect(!!(expr), 1)
#endif

namespace xmh {

class DrawTable {
    static void DrawLine(
            std::stringstream& ss,
            const std::vector<int>& max,
            int columns) {
        for (int i = 0; i < columns; i++) {
            ss << "+-";
            for (int j = 0; j <= max[i]; j++) {
                ss << '-';
            }
        }
        ss << '+' << std::endl;
    }

   public:
    static void DrawTB(
            std::stringstream& ss,
            const std::vector<int>& max,
            const std::vector<std::string>& header,
            const std::vector<std::vector<std::string>>& str) {
        int row = str.size();
        int columns = str[0].size();
        DrawLine(ss, max, columns);
        for (int i = 0; i < header.size(); i++) {
            ss << "| " << std::setw(max[i]) << std::setiosflags(std::ios::left)
               << std::setfill(' ') << header[i] << ' ';
        }
        ss << '|' << std::endl;
        DrawLine(ss, max, columns);
        for (int i = 0; i < row; i++) {
            for (int j = 0; j < columns; j++) {
                ss << "| " << std::setw(max[j])
                   << std::setiosflags(std::ios::left) << std::setfill(' ');
                ss << str[i][j] << ' ';
            }
            ss << '|' << std::endl;
        }
        DrawLine(ss, max, columns);
    }
};

class SpinLock {
    std::atomic_flag locked = ATOMIC_FLAG_INIT;

   public:
    void Lock() {
        while (locked.test_and_set(std::memory_order_acquire)) {
            ;
        }
    }
    void Unlock() {
        locked.clear(std::memory_order_release);
    }
};

class Histogram {
   public:
    Histogram() {}
    ~Histogram() {}

    void Clear() {
        min_ = kBucketLimit(kNumBuckets - 1);
        max_ = 0;
        num_ = 0;
        sum_ = 0;
        sum_squares_ = 0;
        for (int i = 0; i < kNumBuckets; i++) {
            buckets_[i] = 0;
        }
    }
    void Add(double value) {
        // Linear search is fast enough for our usage in db_bench
        int b = 0;
        while (b < kNumBuckets - 1 && kBucketLimit(b) <= value) {
            b++;
        }
        buckets_[b] += 1.0;
        if (min_ > value)
            min_ = value;
        if (max_ < value)
            max_ = value;
        num_++;
        sum_ += value;
        sum_squares_ += (value * value);
    }
    void Merge(const Histogram& other) {
        if (other.min_ < min_)
            min_ = other.min_;
        if (other.max_ > max_)
            max_ = other.max_;
        num_ += other.num_;
        sum_ += other.sum_;
        sum_squares_ += other.sum_squares_;
        for (int b = 0; b < kNumBuckets; b++) {
            buckets_[b] += other.buckets_[b];
        }
    }

    std::string ToString() const {
        std::string r;
        char buf[200];
        std::snprintf(
                buf,
                sizeof(buf),
                "Count: %.0f  Average: %.4f  StdDev: %.2f\n",
                num_,
                Average(),
                StandardDeviation());
        r.append(buf);
        std::snprintf(
                buf,
                sizeof(buf),
                "Min: %.4f  Median: %.4f  Max: %.4f\n",
                (num_ == 0.0 ? 0.0 : min_),
                Median(),
                max_);
        r.append(buf);
        r.append("------------------------------------------------------\n");
        const double mult = 100.0 / num_;
        double sum = 0;
        for (int b = 0; b < kNumBuckets; b++) {
            if (buckets_[b] <= 0.0)
                continue;
            sum += buckets_[b];
            std::snprintf(
                    buf,
                    sizeof(buf),
                    "[ %7.0f, %7.0f ) %7.0f %7.3f%% %7.3f%% ",
                    ((b == 0) ? 0.0 : kBucketLimit(b - 1)), // left
                    kBucketLimit(b),                        // right
                    buckets_[b],                            // count
                    mult * buckets_[b],                     // percentage
                    mult * sum); // cumulative percentage
            r.append(buf);

            // Add hash marks based on percentage; 20 marks for 100%.
            int marks = static_cast<int>(20 * (buckets_[b] / num_) + 0.5);
            r.append(marks, '#');
            r.push_back('\n');
        }
        return r;
    }

   private:
    enum { kNumBuckets = 154 };

    double Median() const {
        return Percentile(50.0);
    }
    double Percentile(double p) const;
    double Average() const {
        if (num_ == 0.0)
            return 0;
        return sum_ / num_;
    }
    double StandardDeviation() const {
        if (num_ == 0.0)
            return 0;
        double variance = (sum_squares_ * num_ - sum_ * sum_) / (num_ * num_);
        return sqrt(variance);
    }

    static auto kBucketLimit(int index) -> double {
        // clang-format off
    static const double static_kNumBuckets[kNumBuckets] = {
        // 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20, 25, 30, 35, 40, 45, 50, 60, 70, 80, 90, 100, 120, 140, 160, 180, 200, 250, 300, 350, 400, 450, 500, 600, 700, 800, 900, 1000, 1200, 1400, 1600, 1800, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 6000, 7000, 8000, 9000, 10000, 12000, 14000, 16000, 18000, 20000, 25000, 30000, 35000, 40000, 45000, 50000, 60000, 70000, 80000, 90000, 100000, 120000, 140000, 160000, 180000, 200000, 250000, 300000, 350000, 400000, 450000, 500000, 600000, 700000, 800000, 900000, 1000000, 1200000, 1400000, 1600000, 1800000, 2000000, 2500000, 3000000, 3500000, 4000000, 4500000, 5000000, 6000000, 7000000, 8000000, 9000000, 10000000, 12000000, 14000000, 16000000, 18000000, 20000000, 25000000, 30000000, 35000000, 40000000, 45000000, 50000000, 60000000, 70000000, 80000000, 90000000, 100000000, 120000000, 140000000, 160000000, 180000000, 200000000, 250000000, 300000000, 350000000, 400000000, 450000000, 500000000, 600000000, 700000000, 800000000, 900000000, 1000000000, 1200000000, 1400000000, 1600000000, 1800000000, 2000000000, 2500000000.0, 3000000000.0, 3500000000.0, 4000000000.0, 4500000000.0, 5000000000.0, 6000000000.0, 7000000000.0, 8000000000.0, 9000000000.0, 1e200,
        100,200,300,400,500,600,700,800,900,1000,1100,1200,1300,1400,1500,1600,1700,1800,1900,2000,2100,2200,2300,2400,2500,2600,   
    };
        // clang-format on
        return static_kNumBuckets[index];
    }

    double min_;
    double max_;
    double num_;
    double sum_;
    double sum_squares_;

    double buckets_[kNumBuckets];
};

#ifdef XMH_PERF

class Counters {
    struct PerThreadCounter {
        static constexpr int kCounterLen_ = 1000;
        void Record(double ns) {
            mutex_.Lock();
            isEnabled_ = true;
            durations_[index_] = ns;
            if (unlikely(index_ == kCounterLen_ - 1)) {
                isOverflow_ = true;
                // XXX(xieminhui): not reordered store-store in x86-64 TSO.
                index_ = 0;
            } else {
                index_++;
            }
            mutex_.Unlock();
        }

        std::array<double, kCounterLen_> durations_;
        bool isOverflow_ = false;
        int index_ = 0;
        SpinLock mutex_;
        bool isEnabled_ = false;
    };

    static constexpr int kShards = 32;

   public:
    Counters() = default;
    Counters(const Counters&) = default;

    double mean() {
        double mean = 0;
        int count = 0;
        for (int tid = 0; tid < kShards; tid++) {
            perThreadCounter_[tid].mutex_.Lock();
            int isOverflow = perThreadCounter_[tid].isOverflow_;
            int index = perThreadCounter_[tid].index_;
            int last = isOverflow ? PerThreadCounter::kCounterLen_ - 1 : index;
            auto& durations = perThreadCounter_[tid].durations_;
            for (int i = 0; i < last; i++) {
                mean = (mean * count + durations[i]) / (count + 1);
                count++;
            }
            perThreadCounter_[tid].mutex_.Unlock();
        }
        return mean;
    }

    double now() {
        for (int tid = 0; tid < kShards; tid++) {
            int index = perThreadCounter_[tid].index_;
            if (index != 0)
                return perThreadCounter_[tid].durations_[index - 1];
            if (perThreadCounter_[tid].isOverflow_)
                return perThreadCounter_[tid]
                        .durations_[PerThreadCounter::kCounterLen_ - 1];
        }
        assert(0);
        return -1;
    }

    double p(int quant = 99) {
        assert(quant <= 100 && quant >= 0), "0 <= quant <= 100";
        std::vector<double> all_durations;
        for (int tid = 0; tid < kShards; tid++) {
            if (perThreadCounter_[tid].isEnabled_ == false) {
                continue;
            }
            bool isOverflow = perThreadCounter_[tid].isOverflow_;
            int index = perThreadCounter_[tid].index_;
            int last = isOverflow ? PerThreadCounter::kCounterLen_ - 1 : index;
            auto& durations = perThreadCounter_[tid].durations_;
            for (int i = 0; i < last; i++)
                all_durations.push_back(durations[i]);
        }
        auto it = (all_durations.size() * quant + 50) / 100 - 1;
        std::nth_element(
                all_durations.begin(),
                all_durations.begin() + it,
                all_durations.end());
        return all_durations[it];
    }
    void Record(double ns) {
        perThreadCounter_[threadIDHash()].Record(ns);
    }

    int threadIDHash() {
        static std::hash<std::thread::id> hasher;
        return hasher(std::this_thread::get_id()) % kShards;
    }

   private:
    PerThreadCounter perThreadCounter_[kShards];
};

class PerfCounter {
   public:
    using static_map =
            std::unordered_map<std::string, std::unique_ptr<Counters>>;
    // Meyers' singleton:
    static auto staticMap() -> static_map& {
        static static_map map(100);
        return map;
    }
    static auto staticSpinLock() -> SpinLock& {
        static SpinLock spinLock;
        return spinLock;
    }

    static auto staticOrderedKeyList() -> std::vector<std::string>& {
        static std::vector<std::string> list;
        return list;
    }

    static void Init() {
        staticMap().clear();
        staticSpinLock().Unlock();
        staticOrderedKeyList().clear();
    }

    static void Record(const std::string& name, double count) {
        auto& map = staticMap();
        auto& orderedKeyList = staticOrderedKeyList();
        auto& spin_lock = staticSpinLock();
        spin_lock.Lock();
        if (map.find(name) == map.end()) {
            map[name] = std::make_unique<Counters>();
            orderedKeyList.push_back(name);
        }
        auto& value = map[name];
        spin_lock.Unlock();
        value->Record(count);
    }

    static std::string Report() {
        auto& spin_lock = staticSpinLock();
        auto& map = staticMap();
        auto& orderedKeyList = staticOrderedKeyList();
        std::stringstream ss;
        std::vector<std::vector<std::string>> vec;
        spin_lock.Lock();
        if (map.size() == 0) {
            spin_lock.Unlock();
            return "";
        }
        for (auto key = orderedKeyList.begin(); key != orderedKeyList.end();
             ++key) {
            vec.push_back({
                    *key,
                    std::to_string(map[*key]->mean()),
                    std::to_string(map[*key]->p(99)),
                    //  std::to_string(map[*key]->now())
            });
        }
        spin_lock.Unlock();
        DrawTable::DrawTB(ss, {35, 20, 20}, {"Name", "Mean", "P99"}, vec);
        return ss.str();
    }
};

class Timer {
   protected:
    using static_map =
            std::unordered_map<std::string, std::unique_ptr<Counters>>;
    // Meyers' singleton:
    static auto staticMap() -> static_map& {
        static static_map map(100);
        return map;
    }

    static auto staticSpinLock() -> SpinLock& {
        static SpinLock spinLock;
        return spinLock;
    }
    static auto staticOrderedKeyList() -> std::vector<std::string>& {
        static std::vector<std::string> list;
        return list;
    }

    static inline std::string double2StringWithPrecision(
            double num,
            int precision) {
        std::stringstream ss;
        ss << std::fixed;
        ss << std::setprecision(precision);
        ss << num;
        return ss.str();
    }

    static std::string beautifyNs(double ns) {
        if (int(ns / 1000) == 0) {
            return double2StringWithPrecision(ns, 3) + " ns";
        }
        ns /= 1000;
        if (int(ns / 1000) == 0) {
            return double2StringWithPrecision(ns, 3) + " us";
        }
        ns /= 1000;
        if (int(ns / 1000) == 0) {
            return double2StringWithPrecision(ns, 3) + " ms";
        }
        ns /= 1000;
        return double2StringWithPrecision(ns, 3) + " s";
    }

   public:
    Timer(std::string timerName, int sampling_times = 1)
            : timerName_(timerName), sampling_times_(sampling_times) {
        start();
    }
    Timer() = default;

    void set_name(const std::string& name) {
        timerName_ = name;
    }

    static void Init() {
        staticMap().clear();
        staticSpinLock().Unlock();
        staticOrderedKeyList().clear();
    }

    ~Timer() {
        if (!isEnd_) {
            // std::cerr << "timer name is " << timerName_ << std::endl;
            // assert(isEnd_);
        }
    }

    void start() {
        start_ = std::chrono::steady_clock::now();
        isEnd_ = false;
    }
    double nsSinceStart() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now() - start_)
                .count();
    }

    void CumStart() {
        start_ = std::chrono::steady_clock::now();
    }

    void CumEnd() {
        end_ = std::chrono::steady_clock::now();
        cum_count_ += std::chrono::duration_cast<std::chrono::nanoseconds>(
                              end_ - start_)
                              .count();
    }

    void CumReport() {
        assert(!isEnd_);
        isEnd_ = true;
        auto& map = staticMap();
        auto& orderedKeyList = staticOrderedKeyList();
        auto& spin_lock = staticSpinLock();
        spin_lock.Lock();
        if (map.find(timerName_) == map.end()) {
            map[timerName_] = std::make_unique<Counters>();
            orderedKeyList.push_back(timerName_);
        }
        auto& value = map[timerName_];
        spin_lock.Unlock();
        value->Record(cum_count_);
        cum_count_ = 0;
    }

    void destroy() {
        assert(!isEnd_);
        isEnd_ = true;
    }

    virtual void end() {
        if (cum_sampling_times_ % sampling_times_ != 0) {
            return;
        }
        cum_sampling_times_++;

        assert(!isEnd_);
        isEnd_ = true;
        auto& map = staticMap();
        auto& orderedKeyList = staticOrderedKeyList();
        auto& spin_lock = staticSpinLock();
        end_ = std::chrono::steady_clock::now();
        spin_lock.Lock();
        if (map.find(timerName_) == map.end()) {
            map[timerName_] = std::make_unique<Counters>();
            orderedKeyList.push_back(timerName_);
        }
        auto& value = map[timerName_];
        // if (timerName_ == "compute 4 distance") {
        //     printf("(ns) end_ - start_ = %d\n", end_ - start_);
        // }
        spin_lock.Unlock();
        value->Record(std::chrono::duration_cast<std::chrono::nanoseconds>(
                              end_ - start_)
                              .count());
    }

    static void ManualRecordNs(const std::string& name, double ns) {
        auto& map = staticMap();
        auto& orderedKeyList = staticOrderedKeyList();
        auto& spin_lock = staticSpinLock();
        spin_lock.Lock();
        if (map.find(name) == map.end()) {
            map[name] = std::make_unique<Counters>();
            orderedKeyList.push_back(name);
        }
        auto& value = map[name];
        spin_lock.Unlock();
        value->Record(ns);
    }

    static double ManualQuery(const std::string& key) {
        auto& spin_lock = staticSpinLock();
        auto& map = staticMap();
        spin_lock.Lock();
        double ret = map[key]->mean();
        spin_lock.Unlock();
        return ret;
    }
    static bool compare_strings(const std::string& s1, const std::string& s2) {
        
        size_t start1 = 0;
        while (start1 < s1.length() && std::isspace(s1[start1])) {
            start1++;
        }
        std::string new_s1 = s1.substr(start1);
        
        size_t start2 = 0;
        while (start2 < s2.length() && std::isspace(s2[start2])) {
            start2++;
        }
        std::string new_s2 = s2.substr(start2);

        
        bool new_s1IsNumber = std::isdigit(new_s1[0]);
        bool new_s2IsNumber = std::isdigit(new_s2[0]);

      
        if (!new_s1IsNumber && new_s2IsNumber)
            return true;
        if (new_s1IsNumber && !new_s2IsNumber)
            return false;

       
        std::vector<int> nums1;
        std::vector<int> nums2;
        size_t startPos1 = 0;
        size_t startPos2 = 0;
        if (new_s1IsNumber) {
            size_t pos1 = new_s1.find(':');
            std::string numStr1 = new_s1.substr(0, pos1);
            std::stringstream ss1(numStr1);
            int num;
            char dot;
            while (ss1 >> num) {
                nums1.push_back(num);
                if (!(ss1 >> dot)) {
                    break;
                }
            }
        }
        if (new_s2IsNumber) {
            size_t pos2 = new_s2.find(':');
            std::string numStr2 = new_s2.substr(0, pos2);
            std::stringstream ss2(numStr2);
            int num;
            char dot;
            while (ss2 >> num) {
                nums2.push_back(num);
                if (!(ss2 >> dot)) {
                    break;
                }
            }
        }

        
        size_t size1 = nums1.size();
        size_t size2 = nums2.size();
        size_t minSize = std::min(size1, size2);
        for (size_t i = 0; i < minSize; ++i) {
            if (nums1[i] < nums2[i])
                return true;
            if (nums1[i] > nums2[i])
                return false;
        }

     
        if (size1 < size2)
            return true;
        if (size1 > size2)
            return false;

        return new_s1 < new_s2;
    }
    static std::string Report() {
        auto& spin_lock = staticSpinLock();
        auto& map = staticMap();
        auto& orderedKeyList = staticOrderedKeyList();
        std::stringstream ss;
        std::vector<std::vector<std::string>> vec;
        spin_lock.Lock();
        if (map.size() == 0) {
            spin_lock.Unlock();
            return "";
        }
        for (auto key = orderedKeyList.begin(); key != orderedKeyList.end();
             ++key) {
            std::string meanStr = beautifyNs(map[*key]->mean());
            std::string p99Str = beautifyNs(map[*key]->p(99));

         
            if (meanStr.length() > 1000) {
                meanStr = meanStr.substr(0, 1000);
            }
            if (p99Str.length() > 1000) {
                p99Str = p99Str.substr(0, 1000);
            }

            vec.push_back({*key, meanStr, p99Str});
        }
        spin_lock.Unlock();
        std::sort(
                vec.begin(),
                vec.end(),
                [](const std::vector<std::string>& a,
                   const std::vector<std::string>& b) {
                    return compare_strings(a[0], b[0]);
                });

        DrawTable::DrawTB(ss, {35, 25, 25}, {"Name", "AVG", "P99"}, vec);
        return ss.str();
    }

    //    protected:
   public:
    bool isEnd_ = false;
    std::string timerName_;
    std::chrono::time_point<std::chrono::steady_clock> start_;
    std::chrono::time_point<std::chrono::steady_clock> end_;
    double cum_count_ = 0;
    const int sampling_times_ = 1;
    int cum_sampling_times_ = 0;
};

class TimerSample : public Timer {
   protected:
   public:
    TimerSample(std::string timerName, int sampling_times = 1)
            : Timer(timerName),
              timerName_(timerName),
              sampling_times_(sampling_times) {
        start();
    }

    ~TimerSample() {
        if (!isEnd_) {
            // std::cerr << "timer name is " << timerName_ << std::endl;
            // assert(isEnd_);
        }
    }

    void start() {
        start_ = std::chrono::steady_clock::now();
        isEnd_ = false;
    }
    double nsSinceStart() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now() - start_)
                .count();
    }

    void destroy() {
        assert(!isEnd_);
        isEnd_ = true;
    }

    virtual void end() {
        if (cum_sampling_times_ % sampling_times_ != 0) {
            return;
        }
        cum_sampling_times_++;

        assert(!isEnd_);
        isEnd_ = true;
        auto& map = staticMap();
        auto& orderedKeyList = staticOrderedKeyList();
        auto& spin_lock = staticSpinLock();
        end_ = std::chrono::steady_clock::now();
        spin_lock.Lock();
        if (map.find(timerName_) == map.end()) {
            map[timerName_] = std::make_unique<Counters>();
            orderedKeyList.push_back(timerName_);
        }
        auto& value = map[timerName_];
        spin_lock.Unlock();
        value->Record(std::chrono::duration_cast<std::chrono::nanoseconds>(
                              end_ - start_)
                              .count());
    }

    static void ManualRecordNs(const std::string& name, double ns) {
        auto& map = staticMap();
        auto& orderedKeyList = staticOrderedKeyList();
        auto& spin_lock = staticSpinLock();
        spin_lock.Lock();
        if (map.find(name) == map.end()) {
            map[name] = std::make_unique<Counters>();
            orderedKeyList.push_back(name);
        }
        auto& value = map[name];
        spin_lock.Unlock();
        value->Record(ns);
    }

    static double ManualQuery(const std::string& key) {
        auto& spin_lock = staticSpinLock();
        auto& map = staticMap();
        spin_lock.Lock();
        double ret = map[key]->mean();
        spin_lock.Unlock();
        return ret;
    }

    // static std::string Report() {
    //     auto& spin_lock = staticSpinLock();
    //     auto& map = staticMap();
    //     auto& orderedKeyList = staticOrderedKeyList();
    //     std::stringstream ss;
    //     std::vector<std::vector<std::string>> vec;
    //     spin_lock.Lock();
    //     if (map.size() == 0) {
    //         spin_lock.Unlock();
    //         return "";
    //     }
    //     for (auto key = orderedKeyList.begin(); key != orderedKeyList.end();
    //          ++key) {
    //         vec.push_back(
    //                 // {*key, beautifyNs(map[*key]->mean()),
    //                 // beautifyNs(map[*key]->p(99))});
    //                 {*key,
    //                  beautifyNs(map[*key]->p(50)),
    //                  beautifyNs(map[*key]->p(99))});
    //     }
    //     spin_lock.Unlock();
    //     DrawTable::DrawTB(ss, {25, 25, 25}, {"Name", "P50", "P99"}, vec);
    //     return ss.str();
    // }

   protected:
    bool isEnd_ = false;
    std::string timerName_;
    std::chrono::time_point<std::chrono::steady_clock> start_;
    std::chrono::time_point<std::chrono::steady_clock> end_;
    double cum_count_ = 0;
    const int sampling_times_ = 1;
    int cum_sampling_times_ = 0;
};

class GPUTimer : public Timer {
   public:
    GPUTimer(const std::string& name) : Timer(name) {}
    void end() override {
#ifndef XMH_PERF_GPU
        isEnd_ = true;
        return;
#else
        cudaDeviceSynchronize();
        assert(!isEnd_);
        isEnd_ = true;
        auto& map = staticMap();
        auto& orderedKeyList = staticOrderedKeyList();
        auto& spin_lock = staticSpinLock();
        end_ = std::chrono::steady_clock::now();
        spin_lock.Lock();
        if (map.find(timerName_) == map.end()) {
            map[timerName_] = std::make_unique<Counters>();
            orderedKeyList.push_back(timerName_);
        }
        auto& value = map[timerName_];
        spin_lock.Unlock();
        value->Record(std::chrono::duration_cast<std::chrono::nanoseconds>(
                              end_ - start_)
                              .count());
#endif
    }
};

class Reporter {
    static auto staticReportThread() -> std::thread*& {
        static std::thread* reportThread = nullptr;
        return reportThread;
    }

    static auto reportThreadFlag() -> std::atomic_bool& {
        static std::atomic_bool reportThreadFlag_{true};
        return reportThreadFlag_;
    }

    static void Init4GoogleTest() {
        StopReportThread();
        Clear();
        reportThreadFlag().store(true);
    }

   public:
    static void Clear() {
        Timer::Init();
        PerfCounter::Init();
    }

    static void StartReportThread(int intervalMilliSecond = 5000) {
        auto& t = staticReportThread();
        static std::mutex m;
        if (t == nullptr) {
            std::lock_guard<std::mutex> _(m);
            if (t == nullptr) {
                t = new std::thread(
                        &Reporter::ReportThread, intervalMilliSecond);
            }
        }
    }
    static void StopReportThread() {
        auto& t = staticReportThread();
        if (t != nullptr) {
            reportThreadFlag().store(false);
            t->join();
            delete t;
            t = nullptr;
        }
    }

    static void Report() {
        std::cout << Timer::Report() << std::flush;

        std::cout << PerfCounter::Report() << std::flush;
    }

   private:
    static void ReportThread(int intervalMilliSecond = 5000) {
        while (reportThreadFlag().load()) {
            Report();
            std::this_thread::sleep_for(
                    std::chrono::milliseconds(intervalMilliSecond));
        }
    }
};

#else

class PerfCounter {
   public:
    static void Init() {}

    static void Record(const std::string& name, double count) {}

    static std::string Report() {
        return "";
    }
};

class Timer {
   public:
    Timer(std::string timerName, int sampling_times = 1) {}

    static void Init() {}

    ~Timer() {}

    void start() {}
    double nsSinceStart() {
        return 0;
    }
    void end() {}

    void CumStart() {}

    void CumEnd() {}

    void CumReport() {}

    static std::string Report() {
        return "";
    }

   private:
};

class Reporter {
   public:
    static void StartReportThread(int intervalMilliSecond = 5000) {}
    static void StopReportThread() {}
    static void Report() {}
};

#endif
} // namespace xmh