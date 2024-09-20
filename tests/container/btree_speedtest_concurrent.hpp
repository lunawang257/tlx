#ifndef TLX_BTREE_SPEEDTEST_CONCURRENT_HEADER
#define TLX_BTREE_SPEEDTEST_CONCURRENT_HEADER

#include <string>
#include <tlx/die.hpp>
#include <tlx/timestamp.hpp>

#include "btree_speedtest_controller.hpp"
#include "trace.h"

// *** Settings
bool g_use_slbtree = false;
size_t min_items = 125; //! starting number of items to insert
size_t max_items = 1024000 * 64; //! maximum number of items to insert
size_t start_repeat = 1;
size_t g_slot_max = 64;
ssize_t g_root_slot = 0;
bool skip_std_set = false;
unsigned short g_level = 0;
unsigned short g_slotuse = 32;
std::string g_lock_req_str = "all";
size_t LOOKUP_PROP = 0;
size_t INSERT_PROP = 50;

const int seed = 34234235; //std::random_device{}();

//! Test a generic set type with insert, find and delete sequences
template <typename SpeedTestT>
class Test_Set_MixedOp {
private:
    using MapType = SpeedTestT::test_map_type;
    using ValType = SpeedTestT::test_value_type; // pair of key and data
    using DataType = SpeedTestT::data_type; // data

public:
    double duration = 0.0;
    size_t actual_items = 0;

private:
    MapType my_map;
    key_type max_key;

    void insert_random_values(const size_t num_items) {
        std::mt19937 gen(seed);

        typename SpeedTestT::UniDistKeyT uniform_dist(0, max_key);

        while (my_map.tree_.size() < num_items) {
            key_type key = uniform_dist(gen);
            ValType val = ValType(key, DataType());

            my_map.insert(val);
        }

        //my_map.tree_.print(std::cout);
    }

public:
    Test_Set_MixedOp(size_t items,
                    size_t num_threads = 1,
                    const TestOption d_option = ZIPF) {

        max_key = items * key_space_factor;
        insert_random_values(items);

        cur_numthreads = num_threads;
        dist_option = d_option;

        reset();
    }

    static const char * op() { return "set_mixed_ops"; }

private:
    int insert_prob = INSERT_PROP;
    int lookup_prob = LOOKUP_PROP;
    int key_space_factor = 2;
    std::atomic<int> num_running = 0;
    std::atomic<int> num_stopped = 0;
    double ts_start = 0.0, ts_stop = 0.0;
    size_t cur_numthreads = 0;
    TestOption dist_option = ZIPF;

    enum Operation {
        OP_INSERT,
        OP_DELETE,
        OP_LOOKUP
    };

    struct alignas(128) thread_state { // align to cache line
        int count;
        int rc;
    };
    std::vector<thread_state> thread_states;
    bool stop = false;

    void reset() {
        ts_start = ts_stop = 0.0;
        num_running = 0;
        num_stopped = 0;
        actual_items = 0;
        stop = false;
    }

    void preload_ops(int id, int iterations,
                    std::vector<std::pair<Operation, key_type>>& operations) {
        std::mt19937 gen(seed + id);

        std::uniform_int_distribution<> op_dist(0, 99); //which operation to use

        typename SpeedTestT::UniDistKeyT uniform_dist(0, max_key);

        int zipseed = static_cast<int>(std::time(nullptr));
        util::TraceZipfian zipf_dist(zipseed, 0, max_key, 0.99);

        for (int i = 0; i < iterations; i++) {
            std::pair<Operation, key_type> op;

            if (dist_option == ZIPF) {
                op.second = zipf_dist.Next();
            } else {
                op.second = uniform_dist(gen);
            }

            int op_prob = op_dist(gen);
            if (op_prob < insert_prob) {
                op.first = OP_INSERT;
            } else if (op_prob < insert_prob + lookup_prob) {
                op.first = OP_LOOKUP;
            } else {
                op.first = OP_DELETE;
            }

            operations.push_back(op);
        }
    }

    void mixed_ops(int id, int iterations, int total_threads) {
        std::vector<std::pair<Operation, key_type>> operations;
        //preload the operations(id, iterations)
        preload_ops(id, iterations, operations);

        local_thread_id = id;

        auto old_val = num_running.fetch_add(1, std::memory_order_relaxed);
        if (old_val + 1 == total_threads) { // this is the last thread starts running
            ts_start = tlx::timestamp();
        } else { // wait for other thread to get to this point
           while (num_running < total_threads) {
              std::this_thread::yield();
           }
        }

        for (const auto& op : operations) {
            switch (op.first) {
            case OP_INSERT: {
                ValType val = ValType(op.second, DataType());
                bool succeeded = my_map.insert(val).second;
                ++thread_states[id].count;
                thread_states[id].rc += succeeded;
                break;
            }
            case OP_LOOKUP: {
                bool found = my_map.exists(op.second);
                ++thread_states[id].count;
                thread_states[id].rc += found;
                break;
            }
            case OP_DELETE: {
                bool erased = my_map.erase(op.second);
                ++thread_states[id].count;
                thread_states[id].rc += erased;
                break;
            }
            }
        }

        old_val = num_stopped.fetch_add(1, std::memory_order_relaxed);
        if (old_val == 0) { // this is the first thread stops
            ts_stop = tlx::timestamp();
            if (ts_stop > ts_start && ts_start != 0.0) {
                duration += ts_stop - ts_start;
                stop = true; // stop all threads
            }
        }
    }

public:
    void run(size_t iterations __attribute__((unused)), size_t repeats) {
        std::vector<std::thread> threads;
        size_t per_thread = repeats / cur_numthreads;

        thread_states.resize(cur_numthreads);
        reset();

        for (size_t i = 0; i < cur_numthreads; ++i) {
            threads.emplace_back(&Test_Set_MixedOp::mixed_ops,
                                 this, i, per_thread, cur_numthreads);
        }

        for (auto& t : threads) t.join();

        size_t n = 0;
        for (auto st: thread_states) {
            n += st.rc;
            actual_items += st.count;
       }
        if (n == 1234567890ul) {
            std::cout << "Print dummy line to avoid code being optimized out\n";
        }
    }
};

//! Repeat (short) tests until enough time elapsed and divide by the repeat.
template <typename TestClass>
void btreemix_runner_loop(size_t items,
                          const std::string& container_name,
                          const int num_threads = 1,
                          const TestOption dist_option = ZIPF) {

    double ts1, ts2, duration;
    size_t actual_items = 0;
    double min_run_time = 1.0;
    size_t repeat_until = items * start_repeat;

    do {
        // count timed tests
        duration = 0.0;
        actual_items = items;

        {
            // initialize test structures
            TestClass test(items, num_threads, dist_option);

            ts1 = tlx::timestamp();

            // run timed test procedure
            test.run(items, repeat_until);

            ts2 = tlx::timestamp();

            if (test.duration != 0.0) {
                duration = test.duration;
                actual_items = test.actual_items;
            }
        }

        std::cout << "Insert=" << items << " repeat=" << repeat_until / items
                  << " repeat_until=" << repeat_until << " time=" << (ts2 - ts1);
        if (duration != 0.0) {
            std::cout << " real time " << std::setprecision(9) << duration
                      << " real total items " << actual_items;
        }
        std::cout << "\n";

        // discard and repeat if test took less than one second.
        if ((ts2 - ts1) < min_run_time || duration < min_run_time) repeat_until *= 2;
    }
    while ((ts2 - ts1) < min_run_time || duration < min_run_time);

    if (duration != 0) {
        ts1 = 0.0;
        ts2 = duration;
    }

    std::string dist_option_string = "";
    if (dist_option == ZIPF) {
        dist_option_string = "Zipf";
    } else if (dist_option == UNIFORM) {
        dist_option_string = "Uniform";
    }

    float million_ops_per_sec = (actual_items / (ts2 - ts1)) / 1e6;
    std::cout << "RESULT"
              << " container=" << container_name
              << " op=" << TestClass::op()
              << " insert_prob=" << INSERT_PROP
              << " lookup_prob=" << LOOKUP_PROP
              << " dist=" << dist_option_string
              << " time_total=" << std::setprecision(3) << (ts2 - ts1)
              << " time(ns)="
              << std::fixed << std::setprecision(3)
              << ((ts2 - ts1) * 1e9 / actual_items)
              << " items_per_sec(m)=" << std::setprecision(2)
              << million_ops_per_sec
              << std::endl;

    std::cout << "[Throughput] slot_max="<< g_slot_max << "; num_thread=" << num_threads << "; throughput="
              << million_ops_per_sec << " Mops/s"
              << std::endl;

    std::cout << "Test\tSlotMax\tValSize\tSliceSz\tSlcSzMx\tThreads\tMplThrh\tDist\tInsertP\tLookupP\tMops/s\n"
              << container_name << "\t"
              << dist_option_string << "\t"
              << INSERT_PROP << "\t"
              << LOOKUP_PROP << "\t"
              << million_ops_per_sec << "\t"
              << std::endl;
}

#endif
