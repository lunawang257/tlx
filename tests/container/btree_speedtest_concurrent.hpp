#ifndef TLX_BTREE_SPEEDTEST_CONCURRENT_HEADER
#define TLX_BTREE_SPEEDTEST_CONCURRENT_HEADER

#include <string>
#include <tlx/die.hpp>
#include <tlx/timestamp.hpp>

#include <tests/container/btree_speedtest_controller.hpp>
#include <tests/container/btree_fast_log.hpp>
#include "trace.h"

const size_t NUM_THREADS = 32; // just set a max value to make btree_fast_log.hpp happy

// *** Settings
bool g_use_slbtree = false;
size_t min_items = 125; //! starting number of items to insert
size_t max_items = 1024000 * 64; //! maximum number of items to insert
double start_repeat = 1;
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
size_t BENCH_LOOKUP_PROP = 0;
size_t BENCH_INSERT_PROP = 10;
size_t BENCH_SCAN_PROP = 80;
uint8_t benchmarking = 0;

const int seed = 34234235; //std::random_device{}();
const uint8_t NUM_PHASES = 3;

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

    unsigned short start_height = 0;
    unsigned short end_height = 0;

    struct alignas(128) thread_state { // align to cache line
        int count = 0;
        int rc = 0;
        int scan_count;
        uint64_t num_total_next_leaf = 0;
        uint64_t num_no_wait_next_leaf = 0;

        uint64_t total_leaf_read_lock_ns;
        uint64_t total_leaf_write_lock_ns;
        uint64_t total_leaf_read_lock_ct;
        uint64_t total_leaf_write_lock_ct;

        uint64_t total_inner_read_lock_ns;
        uint64_t total_inner_write_lock_ns;
        uint64_t total_inner_read_lock_ct;
        uint64_t total_inner_write_lock_ct;

        std::chrono::duration<uint64_t, std::nano> insert_op_ns[NUM_PHASES] = {};
        std::chrono::duration<uint64_t, std::nano> delete_op_ns[NUM_PHASES] = {};
        std::chrono::duration<uint64_t, std::nano> lookup_op_ns[NUM_PHASES] = {};
        std::chrono::duration<uint64_t, std::nano> scan_op_ns[NUM_PHASES] = {};

        uint64_t insert_op_ct[NUM_PHASES] = {0};
        uint64_t delete_op_ct[NUM_PHASES] = {0};
        uint64_t lookup_op_ct[NUM_PHASES] = {0};
        uint64_t scan_op_ct[NUM_PHASES] = {0};
    };

    std::vector<thread_state> thread_states;

    MapType my_map;

public:
    Test_Set_MixedOp(size_t items,
                    size_t n_threads = 1,
                    const TestOption d_option = ZIPF) {

        MAX_KEY = items * KEY_SPACE_FACTOR;

        cur_numthreads = n_threads;
        dist_option = d_option;

        reset();
    }

    static const char * op() { return "set_mixed_ops"; }

private:
    static const uint8_t KEY_SPACE_FACTOR = 2;
    key_type MAX_KEY;
    std::vector<key_type> inserted_keys[NUM_THREADS];

    std::atomic<size_t> num_running = 0;
    std::atomic<size_t> num_stopped = 0;
    std::atomic<size_t> one_third_reached = 0;
    std::atomic<size_t> two_third_reached = 0;
    double ts_start = 0.0, ts_stop = 0.0;
    size_t cur_numthreads = 0;
    TestOption dist_option = ZIPF;

    enum TestOperation {
        TEST_OP_INSERT,
        TEST_OP_DELETE,
        TEST_OP_LOOKUP,
        TEST_OP_SCAN
    };

    bool stop = false;

    void reset() {
        ts_start = ts_stop = 0.0;
        num_running = 0;
        num_stopped = 0;
        one_third_reached = 0;
        two_third_reached = 0;
        actual_items = 0;
        stop = false;
    }

    void preload_mixed_ops(int thread_id, size_t iterations,
                           std::vector<std::pair<TestOperation, key_type>>& operations) {

        std::mt19937 gen(seed + thread_id);

        typename SpeedTestT::UniDistKeyT op_dist(0, 99); //which operation to use
        typename SpeedTestT::UniDistKeyT uniform_dist_first_half(0, MAX_KEY/2); // first half of the key
        typename SpeedTestT::UniDistKeyT uniform_dist_second_half(MAX_KEY/2+1, MAX_KEY); // second half of the key

        typename SpeedTestT::UniDistKeyT uniform_dist(0, MAX_KEY); // uniform key

        int zipseed = static_cast<int>(std::time(nullptr)); // zipf key
        util::TraceZipfian zipf_dist(zipseed, 0, MAX_KEY, 0.99);

        size_t insert_p = INSERT_PROP;
        size_t lookup_p = LOOKUP_PROP;
        size_t scan_p = SCAN_PROP;

        size_t one_third_mark = iterations / 3;
        size_t two_third_mark = iterations * 2 / 3;

        std::uniform_int_distribution<size_t> inserted_key_dist(0, inserted_keys[thread_id].size()-1);

        for (size_t op_idx = 0; op_idx < iterations; op_idx++) {
            std::pair<TestOperation, key_type> op;

            if (dist_option == ZIPF) {
                op.second = zipf_dist.Next();
            } else {
                op.second = uniform_dist(gen);
            }

            if ((benchmarking == 1) && (op_idx > one_third_mark)) {
                insert_p = BENCH_INSERT_PROP;
                lookup_p = BENCH_LOOKUP_PROP;
                scan_p = BENCH_SCAN_PROP;

                op.second = uniform_dist_first_half(gen);

                if (op_idx > two_third_mark) {
                    op.second = uniform_dist_second_half(gen);
                }
            }

            size_t op_prob = op_dist(gen);
            if (op_prob < insert_p) {
                op.first = TEST_OP_INSERT;
            } else if (op_prob < insert_p + lookup_p) {
                op.first = TEST_OP_LOOKUP;
            } else if (op_prob < insert_p + lookup_p + scan_p) {
                op.first = TEST_OP_SCAN;
            } else {
                op.first = TEST_OP_DELETE;
                op.second = inserted_keys[thread_id][op_idx];
            }

            operations.push_back(op);
        }
    }

    void update_thread_states(int thread_id,
                              uint8_t phase_idx,
                              TestOperation op_type,
                              std::chrono::time_point<std::chrono::high_resolution_clock> start,
                              std::chrono::time_point<std::chrono::high_resolution_clock> end) {

        ++thread_states[thread_id].count;
        switch (op_type) {
            case TEST_OP_INSERT: {
                thread_states[thread_id].insert_op_ns[phase_idx] += (end - start);
                ++thread_states[thread_id].insert_op_ct[phase_idx];
                break;
            }
            case TEST_OP_LOOKUP: {
                thread_states[thread_id].lookup_op_ns[phase_idx] += (end - start);
                ++thread_states[thread_id].lookup_op_ct[phase_idx];
                break;
            }
            case TEST_OP_DELETE: {
                thread_states[thread_id].delete_op_ns[phase_idx] += (end - start);
                ++thread_states[thread_id].delete_op_ct[phase_idx];
                break;
            }
            case TEST_OP_SCAN: {
                thread_states[thread_id].scan_op_ns[phase_idx] += (end - start);
                ++thread_states[thread_id].scan_op_ct[phase_idx];
                break;
            }
        }
    }

    void run_mixed_ops(int thread_id, const std::vector<std::pair<TestOperation, key_type>>& operations) {
        for (size_t op_idx = 0; op_idx < operations.size() && !stop; ++op_idx) {
            std::chrono::time_point<std::chrono::high_resolution_clock> start, end;

            auto& op = operations[op_idx];
            switch (op.first) {
            case TEST_OP_INSERT: {
                ValType val = ValType(op.second, DataType());

                start = std::chrono::high_resolution_clock::now();
                bool succeeded = my_map.insert(val).second;
                end = std::chrono::high_resolution_clock::now();

                thread_states[thread_id].rc += succeeded;
                break;
            }
            case TEST_OP_LOOKUP: {
                start = std::chrono::high_resolution_clock::now();
                bool found = my_map.exists(op.second);
                end = std::chrono::high_resolution_clock::now();

                thread_states[thread_id].rc += found;
                break;
            }
            case TEST_OP_SCAN: {
                uint16_t num_total_next_leaf = 0;
                uint16_t num_no_wait_next_leaf = 0;

                start = std::chrono::high_resolution_clock::now();
                my_map.map_range_length_safe(op.second, //key
                                            scan_len,
                                            &num_total_next_leaf,
                                            &num_no_wait_next_leaf,
                    [thread_id, this]
                    (const ValType*) noexcept {
                        ++this->thread_states[thread_id].scan_count;
                    }
                );
                end = std::chrono::high_resolution_clock::now();

                thread_states[thread_id].num_total_next_leaf += num_total_next_leaf;
                thread_states[thread_id].num_no_wait_next_leaf += num_no_wait_next_leaf;
                break;
            }
            case TEST_OP_DELETE: {
                TLX_BTREE_ASSERT(my_map.exists(op.second));

                start = std::chrono::high_resolution_clock::now();
                bool erased = my_map.erase(op.second);
                end = std::chrono::high_resolution_clock::now();

                thread_states[thread_id].rc += erased;
                break;
            }
            } // switch operation

            size_t one_third_mark = operations.size() / 3;
            size_t two_third_mark = operations.size() * 2 / 3;

            if (op_idx <= one_third_mark) {
                update_thread_states(thread_id, 0, op.first, start, end);
            } else if (op_idx > two_third_mark) {
                update_thread_states(thread_id, 2, op.first, start, end);
            } else {
                update_thread_states(thread_id, 1, op.first, start, end);
            }

        } // for each operation
    }

    void initialize_btree(int thread_id, const size_t items) {
        std::mt19937 gen(seed + thread_id);
        typename SpeedTestT::UniDistKeyT uniform_dist(0, MAX_KEY);

       for (size_t item_idx = 0; item_idx < items; item_idx++) {
            key_type key = uniform_dist(gen);
            ValType val = ValType(key, DataType());

            my_map.insert(val);

            inserted_keys[thread_id].push_back(key);
        }
    }

    void mixed_ops(int thread_id, const size_t num_items, size_t iterations, size_t total_threads) {

        initialize_btree(thread_id, num_items);

        std::vector<std::pair<TestOperation, key_type>> operations; // operations by non-phased threads
        preload_mixed_ops(thread_id, iterations, operations);

        local_thread_id = thread_id;

        auto old_val = num_running.fetch_add(1, std::memory_order_relaxed);
        if (old_val + 1 == total_threads) { // this is the last thread starts running
            start_height = my_map.get_height(); // retrieve the btree height before running starts
            ts_start = tlx::timestamp();
        } else { // wait for other thread to get to this point
           while (num_running < total_threads) {
              std::this_thread::yield();
           }
        }

        run_mixed_ops(thread_id, operations);

        old_val = num_stopped.fetch_add(1, std::memory_order_relaxed);
        if (old_val == 0) { // this is the first thread stops
            ts_stop = tlx::timestamp();
            if (ts_stop > ts_start && ts_start != 0.0) {
                duration += ts_stop - ts_start;
                stop = true; // stop all threads
            }
        }

        end_height = my_map.get_height(); // retrieve the btree height after running stops

        thread_states[thread_id].total_leaf_read_lock_ns = localLockStat.total_leaf_read_lock_ns.count();
        thread_states[thread_id].total_leaf_write_lock_ns = localLockStat.total_leaf_write_lock_ns.count();
        thread_states[thread_id].total_leaf_read_lock_ct = localLockStat.total_leaf_read_lock_ct;
        thread_states[thread_id].total_leaf_write_lock_ct = localLockStat.total_leaf_write_lock_ct;

        thread_states[thread_id].total_inner_read_lock_ns = localLockStat.total_inner_read_lock_ns.count();
        thread_states[thread_id].total_inner_write_lock_ns = localLockStat.total_inner_write_lock_ns.count();
        thread_states[thread_id].total_inner_read_lock_ct = localLockStat.total_inner_read_lock_ct;
        thread_states[thread_id].total_inner_write_lock_ct = localLockStat.total_inner_write_lock_ct;
    }

public:
    void run(size_t items __attribute__((unused)), size_t repeats) {
        std::vector<std::thread> threads;
        size_t per_thread = repeats / cur_numthreads;
        size_t per_thread_items = items / cur_numthreads;

        thread_states.resize(cur_numthreads);
        reset();

        for (size_t thread_id = 0; thread_id < cur_numthreads; ++thread_id) {
            threads.emplace_back(&Test_Set_MixedOp::mixed_ops,
                                 this, thread_id, per_thread_items, per_thread, cur_numthreads);
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
                          const int n_threads = 1,
                          const TestOption dist_option = ZIPF) {

    double duration;
    size_t actual_items = 0;
    double min_run_time = 0.0;
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

    std::chrono::duration<uint64_t, std::nano> total_insert_op_ns[NUM_PHASES] = {};
    std::chrono::duration<uint64_t, std::nano> total_delete_op_ns[NUM_PHASES] = {};
    std::chrono::duration<uint64_t, std::nano> total_lookup_op_ns[NUM_PHASES] = {};
    std::chrono::duration<uint64_t, std::nano> total_scan_op_ns[NUM_PHASES] = {};

    uint64_t total_insert_op_ct[NUM_PHASES] = {0};
    uint64_t total_delete_op_ct[NUM_PHASES] = {0};
    uint64_t total_lookup_op_ct[NUM_PHASES] = {0};
    uint64_t total_scan_op_ct[NUM_PHASES] = {0};

    uint64_t total_insert_ns = 0;
    uint64_t total_delete_ns = 0;
    uint64_t total_lookup_ns = 0;
    uint64_t total_scan_ns = 0;

    uint64_t total_insert_ct = 0;
    uint64_t total_delete_ct = 0;
    uint64_t total_lookup_ct = 0;
    uint64_t total_scan_ct = 0;

    unsigned short start_height = 0, end_height = 0;

    do {
        // count timed tests
        duration = 0.0;
        actual_items = items;

        total_next_leaf = total_no_wait_next_leaf = 0;
        total_leaf_read_lock_ns = total_leaf_write_lock_ns = 0;
        total_leaf_read_lock_ct = 0; total_leaf_write_lock_ct = 0;
        total_inner_read_lock_ns = 0; total_inner_write_lock_ns = 0;
        total_inner_read_lock_ct = 0; total_inner_write_lock_ct = 0;

        for (int i = 0; i < NUM_PHASES; ++i) {
            total_insert_op_ns[i] = std::chrono::nanoseconds(0);  // Set each element to 0 ns
            total_delete_op_ns[i] = std::chrono::nanoseconds(0);
            total_lookup_op_ns[i] = std::chrono::nanoseconds(0);
            total_scan_op_ns[i] = std::chrono::nanoseconds(0);

            total_insert_op_ct[i] = 0;
            total_delete_op_ct[i] = 0;
            total_lookup_op_ct[i] = 0;
            total_scan_op_ct[i] = 0;
        }

        total_insert_ns = 0;
        total_delete_ns = 0;
        total_lookup_ns = 0;
        total_scan_ns = 0;

        total_insert_ct = 0;
        total_delete_ct = 0;
        total_lookup_ct = 0;
        total_scan_ct = 0;

        // initialize test structures
        TestClass test(items, n_threads, dist_option);

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

            std::transform(total_insert_op_ns, total_insert_op_ns + NUM_PHASES,
                            ts.insert_op_ns, total_insert_op_ns, std::plus<>());
            std::transform(total_lookup_op_ns, total_lookup_op_ns + NUM_PHASES,
                            ts.lookup_op_ns, total_lookup_op_ns, std::plus<>());
            std::transform(total_delete_op_ns, total_delete_op_ns + NUM_PHASES,
                            ts.delete_op_ns, total_delete_op_ns, std::plus<>());
            std::transform(total_scan_op_ns, total_scan_op_ns + NUM_PHASES,
                            ts.scan_op_ns, total_scan_op_ns, std::plus<>());

            std::transform(total_insert_op_ct, total_insert_op_ct + NUM_PHASES,
                            ts.insert_op_ct, total_insert_op_ct, std::plus<>());
            std::transform(total_lookup_op_ct, total_lookup_op_ct + NUM_PHASES,
                            ts.lookup_op_ct, total_lookup_op_ct, std::plus<>());
            std::transform(total_delete_op_ct, total_delete_op_ct + NUM_PHASES,
                            ts.delete_op_ct, total_delete_op_ct, std::plus<>());
            std::transform(total_scan_op_ct, total_scan_op_ct + NUM_PHASES,
                            ts.scan_op_ct, total_scan_op_ct, std::plus<>());
        }

        auto stat = test.my_map.get_stats();

        leaves_count = stat->leaves;
        mapl_leaves_count = stat->mapl_leaves;

        mapl_read_count = stat->read_mapl.get();
        read_count = mapl_read_count + stat->read_leaf.get();

        mapl_write_count = stat->write_mapl.get();
        write_count = mapl_write_count + stat->write_leaf.get();

        for (int phase_idx = 0; phase_idx < NUM_PHASES; phase_idx++) {
            total_insert_ns += total_insert_op_ns[phase_idx].count();
            total_delete_ns += total_delete_op_ns[phase_idx].count();
            total_lookup_ns += total_lookup_op_ns[phase_idx].count();
            total_scan_ns += total_scan_op_ns[phase_idx].count();

            total_insert_ct += total_insert_op_ct[phase_idx];
            total_delete_ct += total_delete_op_ct[phase_idx];
            total_lookup_ct += total_lookup_op_ct[phase_idx];
            total_scan_ct += total_scan_op_ct[phase_idx];
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

        start_height = test.start_height;
        end_height = test.end_height;

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

    double insert_time[NUM_PHASES];
    double delete_time[NUM_PHASES];
    double lookup_time[NUM_PHASES];
    double scan_time[NUM_PHASES];
    std::transform(total_insert_op_ct, total_insert_op_ct + NUM_PHASES,
                   total_insert_op_ns, insert_time, [](auto op_ct, auto op_ns) {
                       return (op_ns.count() * 1.0 / op_ct / 1e3);
                   });
    std::transform(total_delete_op_ct, total_delete_op_ct + NUM_PHASES,
                   total_delete_op_ns, delete_time, [](auto op_ct, auto op_ns) {
                       return (op_ns.count() * 1.0 / op_ct / 1e3);
                   });
    std::transform(total_lookup_op_ct, total_lookup_op_ct + NUM_PHASES,
                   total_lookup_op_ns, lookup_time, [](auto op_ct, auto op_ns) {
                       return (op_ns.count() * 1.0 / op_ct / 1e3);
                   });
    std::transform(total_scan_op_ct, total_scan_op_ct + NUM_PHASES,
                   total_scan_op_ns, scan_time, [](auto op_ct, auto op_ns) {
                       return (op_ns.count() * 1.0 / op_ct / 1e3);
                   });

    double avg_insert_time = total_insert_ns * 1.0 / total_insert_ct / 1e3;
    double avg_delete_time = total_delete_ns * 1.0 / total_delete_ct / 1e3;
    double avg_lookup_time = total_lookup_ns * 1.0 / total_lookup_ct / 1e3;
    double avg_scan_time = total_scan_ns * 1.0 / total_scan_ct / 1e3;

    double avg_insert_mops = total_insert_ct * 1.0 * 1e3 / total_insert_ns;
    double avg_delete_mops = total_delete_ct * 1.0 * 1e3 / total_delete_ns;
    double avg_lookup_mops = total_lookup_ct * 1.0 * 1e3 / total_lookup_ns;
    double avg_scan_mops = total_scan_ct * 1.0 * 1e3 / total_scan_ns;

    float million_ops_per_sec = (actual_items / duration) / 1e6;
    std::cout << "RESULT"
              << " container=" << container_name
              << " op=" << TestClass::op()
              << " INSERT_PROP=" << INSERT_PROP
              << " LOOKUP_PROP=" << LOOKUP_PROP
              << " SCAN_PROP=" << SCAN_PROP
              << " dist=" << dist_option_string
              << " benchmarking=" << std::to_string(benchmarking)
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

    std::cout << "[Throughput] slot_max="<< g_slot_max << "; num_thread=" << n_threads << "; throughput="
              << million_ops_per_sec << " Mops/s"
              << std::endl;

    std::cout << "Test\tSlotMax\tValSize\tSliceSz\tSlcSzMx\tThreads\tMplThrh\tSHght\tEHght\tDist\tBch\tInsertP\tLookupP\tScnP\tScnLen\tMops\tIntMops\tDelPops\tLkpMops\tScnMops\tWaitPct\tMaplPct\tMaplRd\tMaplWt\tLfRdLk\tLfWtLk\tInRdLk\tInWtLk\tP1Inst\tP1Dlt\tP1LkP\tP1Scn\tP2Inst\tP2Dlt\tP2LkP\tP2Scn\tP3Inst\tP3Dlt\tP3LkP\tP3Scn\tInstT\tDelT\tLkpT\tScnT\titms\trpts\tactItms\tDrtion\n"
              << container_name << "\t"
              << start_height << "\t"
              << end_height << "\t"
              << dist_option_string << "\t"
              << std::to_string(benchmarking) << "\t"
              << INSERT_PROP << "\t"
              << LOOKUP_PROP << "\t"
              << SCAN_PROP << "\t"
              << scan_len << "\t"
              << std::setprecision(4) << million_ops_per_sec << "\t"
              << std::setprecision(3) << avg_insert_mops << "\t"
              << std::setprecision(3) << avg_delete_mops << "\t"
              << std::setprecision(3) << avg_lookup_mops << "\t"
              << std::setprecision(3) << avg_scan_mops << "\t"
              << std::setprecision(2) << wait_percent << "%" <<"\t"
              << std::setprecision(2) << mapl_pct << "%" <<"\t"
              << std::setprecision(2) << mapl_read_pct << "%" <<"\t"
              << std::setprecision(2) << mapl_write_pct << "%" <<"\t"
              << std::fixed << std::setprecision(1) << avg_leaf_read_lock_time << "ns" << "\t"
              << std::fixed << std::setprecision(1) << avg_leaf_write_lock_time << "ns" << "\t"
              << std::fixed << std::setprecision(1) << avg_inner_read_lock_time << "ns" << "\t"
              << std::fixed << std::setprecision(1) << avg_inner_write_lock_time << "ns" << "\t"
              << std::setprecision(2) << insert_time[0] << "us" << "\t"
              << std::setprecision(2) << delete_time[0] << "us" << "\t"
              << std::setprecision(2) << lookup_time[0] << "us" << "\t"
              << std::setprecision(2) << scan_time[0] << "us" << "\t"
              << std::setprecision(2) << insert_time[1] << "us" << "\t"
              << std::setprecision(2) << delete_time[1] << "us" << "\t"
              << std::setprecision(2) << lookup_time[1] << "us" << "\t"
              << std::setprecision(2) << scan_time[1] << "us" << "\t"
              << std::setprecision(2) << insert_time[2] << "us" << "\t"
              << std::setprecision(2) << delete_time[2] << "us" << "\t"
              << std::setprecision(2) << lookup_time[2] << "us" << "\t"
              << std::setprecision(2) << scan_time[2] << "us" << "\t"
              << std::setprecision(2) << avg_insert_time << "us" << "\t"
              << std::setprecision(2) << avg_delete_time << "us" << "\t"
              << std::setprecision(2) << avg_lookup_time << "us" << "\t"
              << std::setprecision(2) << avg_scan_time << "us" << "\t"
              << items << "\t" << std::setprecision(2) << start_repeat << "\t"
              << actual_items << "\t" << duration
              << std::endl;
}

#endif
