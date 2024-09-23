#ifndef TLX_BTREE_SPEEDTEST_CONCURRENT_HEADER
#define TLX_BTREE_SPEEDTEST_CONCURRENT_HEADER

#include <string>
#include <tlx/die.hpp>
#include <tlx/timestamp.hpp>

#include <tests/container/btree_speedtest_controller.hpp>
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
size_t LOOKUP_PROP = 17;
size_t INSERT_PROP = 33;
size_t SCAN_PROP = 17;
size_t scan_len = 32;

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

    struct alignas(128) thread_state { // align to cache line
        int count;
        int rc;
        int scan_count;
        uint64_t num_total_next_leaf;
        uint64_t num_no_wait_next_leaf;

        uint64_t total_leaf_read_lock_ns;
        uint64_t total_leaf_write_lock_ns;
        uint64_t total_leaf_read_lock_ct;
        uint64_t total_leaf_write_lock_ct;

        uint64_t total_inner_read_lock_ns;
        uint64_t total_inner_write_lock_ns;
        uint64_t total_inner_read_lock_ct;
        uint64_t total_inner_write_lock_ct;
    };

    std::vector<thread_state> thread_states;

    MapType my_map;

private:
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
    int key_space_factor = 2;
    std::atomic<int> num_running = 0;
    std::atomic<int> num_stopped = 0;
    double ts_start = 0.0, ts_stop = 0.0;
    size_t cur_numthreads = 0;
    TestOption dist_option = ZIPF;

    enum Operation {
        OP_INSERT,
        OP_DELETE,
        OP_LOOKUP,
        OP_SCAN
    };

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

        std::uniform_int_distribution<size_t> op_dist(0, 99); //which operation to use

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

            size_t op_prob = op_dist(gen);
            if (op_prob < INSERT_PROP) {
                op.first = OP_INSERT;
            } else if (op_prob < INSERT_PROP + LOOKUP_PROP) {
                op.first = OP_LOOKUP;
            } else if (op_prob < INSERT_PROP + LOOKUP_PROP + SCAN_PROP) {
                op.first = OP_SCAN;
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

        for (size_t i = 0; i < operations.size() && !stop; ++i) {
            auto& op = operations[i];
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
            case OP_SCAN: {
                uint16_t num_total_next_leaf = 0;
                uint16_t num_no_wait_next_leaf = 0;
                my_map.map_range_length_safe(op.second, //key
                                             scan_len,
                                             &num_total_next_leaf,
                                             &num_no_wait_next_leaf,
                    [id, this]
                    (const ValType&) {
                        ++this->thread_states[id].scan_count;
                    }
                );

                thread_states[id].num_total_next_leaf += num_total_next_leaf;
                thread_states[id].num_no_wait_next_leaf += num_no_wait_next_leaf;
                ++thread_states[id].count;
                break;
            }
            case OP_DELETE: {
                bool erased = my_map.erase(op.second);
                ++thread_states[id].count;
                thread_states[id].rc += erased;
                break;
            }
            } // switch operation
        } // for each operation

        old_val = num_stopped.fetch_add(1, std::memory_order_relaxed);
        if (old_val == 0) { // this is the first thread stops
            ts_stop = tlx::timestamp();
            if (ts_stop > ts_start && ts_start != 0.0) {
                duration += ts_stop - ts_start;
                stop = true; // stop all threads
            }
        }

        thread_states[id].total_leaf_read_lock_ns = localLockStat.total_leaf_read_lock_ns.count();
        thread_states[id].total_leaf_write_lock_ns = localLockStat.total_leaf_write_lock_ns.count();
        thread_states[id].total_leaf_read_lock_ct = localLockStat.total_leaf_read_lock_ct;
        thread_states[id].total_leaf_write_lock_ct = localLockStat.total_leaf_write_lock_ct;

        thread_states[id].total_inner_read_lock_ns = localLockStat.total_inner_read_lock_ns.count();
        thread_states[id].total_inner_write_lock_ns = localLockStat.total_inner_write_lock_ns.count();
        thread_states[id].total_inner_read_lock_ct = localLockStat.total_inner_read_lock_ct;
        thread_states[id].total_inner_write_lock_ct = localLockStat.total_inner_write_lock_ct;
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

    double duration;
    size_t actual_items = 0;
    double min_run_time = 1.0;
    size_t repeat_until = items * start_repeat;
    uint64_t total_next_leaf = 0, total_no_wait_next_leaf = 0;
    size_t leaves_count, mapl_leaves_count;
    size_t read_count, mapl_read_count;
    size_t write_count, mapl_write_count;
    double mapl_pct, mapl_read_pct, mapl_write_pct;
    uint64_t total_leaf_read_lock_ns = 0, total_leaf_write_lock_ns = 0;
    uint64_t total_leaf_read_lock_ct = 0, total_leaf_write_lock_ct = 0;
    uint64_t total_inner_read_lock_ns = 0, total_inner_write_lock_ns = 0;
    uint64_t total_inner_read_lock_ct = 0, total_inner_write_lock_ct = 0;

    do {
        // count timed tests
        duration = 0.0;
        actual_items = items;

        {
            // initialize test structures
            TestClass test(items, num_threads, dist_option);

            // run timed test procedure
            test.run(items, repeat_until);

            duration = test.duration;
            actual_items = test.actual_items;

            for (const auto& ts : test.thread_states) {
                total_next_leaf += ts.num_total_next_leaf;

                total_no_wait_next_leaf += ts.num_no_wait_next_leaf;

                total_leaf_read_lock_ns += ts.total_leaf_read_lock_ns;
                total_leaf_write_lock_ns += ts.total_leaf_write_lock_ns;
                total_leaf_read_lock_ct += ts.total_leaf_read_lock_ct;
                total_leaf_write_lock_ct += ts.total_leaf_write_lock_ct;

                total_inner_read_lock_ns += ts.total_inner_read_lock_ns;
                total_inner_write_lock_ns += ts.total_inner_write_lock_ns;
                total_inner_read_lock_ct += ts.total_inner_read_lock_ct;
                total_inner_write_lock_ct += ts.total_inner_write_lock_ct;
            }

            auto stat = test.my_map.get_stats();

            leaves_count = stat->leaves;
            mapl_leaves_count = stat->mapl_leaves;

            mapl_read_count = stat->read_mapl.get();
            read_count = mapl_read_count + stat->read_leaf.get();

            mapl_write_count = stat->write_mapl.get();
            write_count = mapl_write_count + stat->write_leaf.get();
        }

        mapl_pct = 100.0 * mapl_leaves_count / leaves_count;
        mapl_read_pct = 100.0 * mapl_read_count / read_count;
        mapl_write_pct = 100.0 * mapl_write_count / write_count;
        std::cout << "Insert=" << items << " repeat=" << repeat_until / items
                  << " repeat_until=" << repeat_until
                  << " real time " << std::setprecision(9) << duration
                  << " real total items " << actual_items
                  << " mapl leaves:" << std::setprecision(4) << mapl_pct << "%"
                  << " mapl reads:" << std::setprecision(4)
                  << mapl_read_pct << "%"
                  << " mapl writes:" << std::setprecision(4)
                  << mapl_write_pct << "%"
                  << std::endl;

        // discard and repeat if test took less than one second.
        if (duration < min_run_time) repeat_until *= 2;
    }
    while (duration < min_run_time);

    std::string dist_option_string = "";
    if (dist_option == ZIPF) {
        dist_option_string = "Zipf";
    } else if (dist_option == UNIFORM) {
        dist_option_string = "Uniform";
    }

    double wait_percent = total_next_leaf ?
        (total_next_leaf - total_no_wait_next_leaf) * 100.0 /
        total_next_leaf :
        0;

    double avg_leaf_read_lock_time = total_leaf_read_lock_ns * 1.0 / total_leaf_read_lock_ct;
    double avg_leaf_write_lock_time = total_leaf_write_lock_ns * 1.0 / total_leaf_write_lock_ct;

    double avg_inner_read_lock_time = total_inner_read_lock_ns * 1.0 / total_inner_read_lock_ct;
    double avg_inner_write_lock_time = total_inner_write_lock_ns * 1.0 / total_inner_write_lock_ct;

    float million_ops_per_sec = (actual_items / duration) / 1e6;
    std::cout << "RESULT"
              << " container=" << container_name
              << " op=" << TestClass::op()
              << " INSERT_PROP=" << INSERT_PROP
              << " LOOKUP_PROP=" << LOOKUP_PROP
              << " SCAN_PROP=" << SCAN_PROP
              << " dist=" << dist_option_string
              //<< " total_next_leaf=" << total_next_leaf
              //<< " total_no_wait_next_leaf=" << total_no_wait_next_leaf
              << " wait_pct(%)=" << std::setprecision(2) << wait_percent << "%"
              << " time_total=" << std::setprecision(2) << duration
              << " leaf_read_lock_time=" << std::fixed << std::setprecision(1) << avg_leaf_read_lock_time << "ns"
              << " leaf_write_lock_time=" << std::fixed << std::setprecision(1) << avg_leaf_write_lock_time << "ns"
              << " inner_read_lock_time=" << std::fixed << std::setprecision(1) << avg_inner_read_lock_time << "ns"
              << " inner_write_lock_time=" << std::fixed << std::setprecision(1) << avg_inner_write_lock_time << "ns"
              << " time(ns)/item="
              << std::fixed << std::setprecision(2)
              << (duration * 1e9 / actual_items)
              << " items_per_sec(m)=" << std::setprecision(2)
              << million_ops_per_sec
              << std::endl;

    std::cout << "[Throughput] slot_max="<< g_slot_max << "; num_thread=" << num_threads << "; throughput="
              << million_ops_per_sec << " Mops/s"
              << std::endl;

    std::cout << "Test\tSlotMax\tValSize\tSliceSz\tSlcSzMx\tThreads\tMplThrh\tDist\tInsertP\tLookupP\tScanP\tScanLen\tWaitPct\tMaplPct\tMaplRd\tMaplWt\tLfRdLk\tLfWtLk\tInRdLk\tInWtLk\tMops/s\n"
              << container_name << "\t"
              << dist_option_string << "\t"
              << INSERT_PROP << "\t"
              << LOOKUP_PROP << "\t"
              << SCAN_PROP << "\t"
              << scan_len << "\t"
              << std::setprecision(2) << wait_percent << "%" <<"\t"
              << std::setprecision(2) << mapl_pct << "%" <<"\t"
              << std::setprecision(2) << mapl_read_pct << "%" <<"\t"
              << std::setprecision(2) << mapl_write_pct << "%" <<"\t"
              << std::fixed << std::setprecision(1) << avg_leaf_read_lock_time << "ns" << "\t"
              << std::fixed << std::setprecision(1) << avg_leaf_write_lock_time << "ns" << "\t"
              << std::fixed << std::setprecision(1) << avg_inner_read_lock_time << "ns" << "\t"
              << std::fixed << std::setprecision(1) << avg_inner_write_lock_time << "ns" << "\t"
              << std::setprecision(2) << million_ops_per_sec << "\t"
              << std::endl;
}

#endif
