#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <unistd.h>

#define VTX_BTREE_CONCUR_TEST

#include <tlx/container/slow_lock_btree_set.hpp>
#include <tlx/container/slow_lock_btree_map.hpp>

#include <tlx/container/btree_set.hpp>

#include <set>

#include <tlx/die.hpp>
#include <tlx/timestamp.hpp>

// *** Settings

bool g_use_slbtree = false;

//! starting number of items to insert
size_t min_items = 125;

//! maximum number of items to insert
size_t max_items = 1024000 * 64;

size_t start_repeat = 1;

ssize_t g_root_slot = 0;

size_t g_slot_max = 64;

//! number of threads operating at a time
size_t cur_numthreads = 1;

bool skip_std_set = false;

lock_requirement g_lock_req = lock_all;
std::string g_lock_req_str = "all";

size_t LOOKUP_PROP = 70;
size_t INSERT_PROP = 15;

//! random seed
const int seed = 34234235; //std::random_device{}();

//! Traits used for the speed tests, BTREE_DEBUG is not defined.
template <int InnerSlots, int LeafSlots>
struct btree_traits_speed : tlx::btree_default_traits<size_t, size_t> {
    static const bool self_verify = false;
    static const bool debug = false;

    static const int leaf_slots = InnerSlots; // TODO why are these swapped?
    static const int inner_slots = LeafSlots;

    static const size_t binsearch_threshold = 256 * 1024 * 1024; // never
};

// -----------------------------------------------------------------------------

//! Test a generic set type with insertions
template <typename SetType>
class Test_Set_Insert
{
public:
    double duration = 0.0;
    size_t actual_items = 0;

public:
    Test_Set_Insert(size_t) { }

    static const char * op() { return "set_insert"; }

private:
    SetType set;
    std::vector<int> order;

    void thread_func(int lower, int upper) {
        for (int i = lower; i < upper; ++i) {
            set.insert(order[i]);
        }
    }

public:
    void run(size_t items, size_t repeat_until) {
        for (size_t r = 0; r < repeat_until; r += items) {
            run_one(items);
        }
    }

    void run_one(size_t items) {
        std::mt19937 gen(seed);

        order.resize(items);
        std::iota(order.begin(), order.end(), 0);
        std::ranges::shuffle(order, gen);

        std::vector<std::thread> threads;

        size_t per_thread = items / cur_numthreads;

        int cur = 0;
        for (size_t i = 0; i < cur_numthreads - 1; ++i) {
            int newcur = cur + per_thread;
            threads.emplace_back(&Test_Set_Insert::thread_func, this, cur, newcur);
            cur = newcur;
        }
        threads.emplace_back(&Test_Set_Insert::thread_func, this, cur, items);


        for (auto& t : threads) t.join();

        die_unless(set.size() == items);
    }
};

//! Test a generic set type with insert, find and delete sequences
template <typename SetType>
class Test_Set_InsertFindDelete
{
public:
    double duration = 0.0;
    size_t actual_items = 0;

public:
    Test_Set_InsertFindDelete(size_t) { }

    static const char * op() { return "set_insert_find_delete"; }
private:
    SetType set;
    std::vector<int> order;

    void insert(int lower, int upper) {
        for (int i = lower; i < upper; ++i) {
            set.insert(order[i]);
        }
    }

    void find(int lower, int upper) {
        for (int i = lower; i < upper; ++i) {
            set.find(order[i]); // TODO will this actually happen
        }
    }

    void erase(int lower, int upper) {
        for (int i = lower; i < upper; ++i) {
            set.erase(order[i]);
        }
    }
public:
    void run(size_t items, size_t repeat_until) {
        for (size_t r = 0; r < repeat_until; r += items) {
            run_one(items);
        }
    }

    void run_one(size_t items) {
        std::mt19937 gen(seed);

        order.resize(items);
        std::iota(order.begin(), order.end(), 0);
        std::ranges::shuffle(order, gen);

        std::vector<std::thread> threads;

        size_t per_thread = items / cur_numthreads;

        int cur = 0;
        for (size_t i = 0; i < cur_numthreads - 1; ++i) {
            int newcur = cur + per_thread;
            threads.emplace_back(&Test_Set_InsertFindDelete::insert,
                    this, cur, newcur);
            cur = newcur;
        }
        threads.emplace_back(&Test_Set_InsertFindDelete::insert,
                this, cur, items);

        for (auto& t : threads) t.join();

        die_unless(set.size() == items);

        threads.resize(0);
        std::ranges::shuffle(order, gen);
        for (size_t i = 0; i < cur_numthreads - 1; ++i) {
            int newcur = cur + per_thread;
            threads.emplace_back(&Test_Set_InsertFindDelete::find,
                    this, cur, newcur);
            cur = newcur;
        }
        threads.emplace_back(&Test_Set_InsertFindDelete::find,
                this, cur, items);
        for (auto& t : threads) t.join();

        threads.resize(0);
        std::ranges::shuffle(order, gen);
        for (size_t i = 0; i < cur_numthreads - 1; ++i) {
            int newcur = cur + per_thread;
            threads.emplace_back(&Test_Set_InsertFindDelete::erase,
                    this, cur, newcur);
            cur = newcur;
        }
        threads.emplace_back(&Test_Set_InsertFindDelete::erase,
                this, cur, items);
        for (auto& t : threads) t.join();

        die_unless(set.empty());
    }
};

unsigned short g_level, g_slotuse;

//! Test a generic set type with insert, find and delete sequences
template <typename SetType>
class Test_Set_MixedOp
{
public:
    double duration = 0.0;
    size_t actual_items = 0;

public:
    Test_Set_MixedOp(size_t items) {
        std::mt19937 gen(seed);

        max_key = items * key_space_factor;

        std::uniform_int_distribution<> key(0, max_key);

        /*
        // prepare the set to start with random items
        while (my_set.size() < items) {
            auto k = key(gen);
            my_set.insert(k);
        }
        */

        // prepare the set with sequential items for lookup only testing
        for (size_t i = 0; i < items; ++i) {
            my_set.insert(i);
        }

        if (g_root_slot != 0) { // insert more until reach the desired slots in root
            if (g_root_slot < 0) {
                std::cout << "slotmax=" << my_set.inner_slotmax << ","
                          << my_set.leaf_slotmax << " ";
                std::cout << "adjust root_slot " << g_root_slot;
                g_root_slot += my_set.leaf_slotmax;
                std::cout << " to " << g_root_slot << "\n" << std::flush;
            }
            for (size_t i = items; i < items * 1000; ++i) {
                my_set.insert(i);
                unsigned short level, cur_root_slot;
                my_set.get_root_info(&level, &cur_root_slot);
                if (i % 10000000ull == 0) {
                    std::cout << "level: " << level << "  root_slot: " << cur_root_slot
                              << " expected root slot: " << g_root_slot
                              << std::endl << std::flush;
                }
                if (g_root_slot == cur_root_slot) {
                    break;
                }
            }
        }

        size_t new_items = my_set.size();

        my_set.get_root_info(&g_level, &g_slotuse);

        std::cout << "Initialized " << items << " actual size=" << new_items
                  << " root: level=" << g_level
                  << " slots=" << g_slotuse << "\n";
        reset();
    }

    static const char * op() { return "set_mixed_ops"; }

private:
    SetType my_set;
    int insert_prob = INSERT_PROP;
    int lookup_prob = LOOKUP_PROP;
    int key_space_factor{2};
    int max_key;
    std::atomic<int> num_running = 0;
    std::atomic<int> num_stopped = 0;
    double ts_start = 0.0, ts_stop = 0.0;

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

    void mixed_ops(int id, int items, int total_threads) {
        // TODO std::mt19937 gen(seed + id);
        std::mt19937 gen(std::random_device{}() + id);
        std::uniform_int_distribution<> key_dist(0, max_key);
        std::uniform_int_distribution<> dist(0, 99);

        local_thread_id = id;

        auto old_val = num_running.fetch_add(1, std::memory_order_relaxed);
        if (old_val + 1 == total_threads) { // this is the last thread starts running
            ts_start = tlx::timestamp();
        } else { // wait for other thread to get to this point
           while (num_running < total_threads) {
              std::this_thread::yield();
           }
        }

        for (int i = 0; !stop && i < items; ++i) {
            int key = key_dist(gen);
            int operation = dist(gen);

            if (operation < insert_prob) {
                bool succeeded = my_set.insert(key).second;
                ++thread_states[id].count;
                thread_states[id].rc += succeeded;
            }
            else if (operation < insert_prob + lookup_prob) {
                bool found = my_set.contains(key);
                ++thread_states[id].count;
                thread_states[id].rc += found;
            } else {
                bool erased = my_set.erase(key);
                ++thread_states[id].count;
                thread_states[id].rc += erased;
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
    void run(size_t items __attribute__((unused)), size_t repeat_until) {
        std::vector<std::thread> threads;
        size_t per_thread = repeat_until / cur_numthreads;

        thread_states.resize(cur_numthreads);

        my_set.set_lock_requirement(g_lock_req);

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

//! Test a generic set type with insert, find and delete sequences TODO change in actual
template <typename SetType>
class Test_Set_Find
{
public:
    double duration = 0.0;
    size_t actual_items = 0;

public:
    SetType set;

    static const char * op() { return "set_find"; }

    Test_Set_Find(size_t items) {
        std::mt19937 gen(seed);

        order.resize(items);
        std::iota(order.begin(), order.end(), 0);
        std::ranges::shuffle(order, gen);

        for (auto& num : order) {
            set.insert(num);
        }
        die_unless(set.size() == items);
    }

private:
    std::vector<int> order;

    void thread_func(int lower, int upper) {
        for (int i = lower; i < upper; ++i) {
            set.find(order[i]); // TODO will this actually happen
        }
    }

public:
    void run(size_t items, size_t repeat_until) {
        for (size_t r = 0; r < repeat_until; r += items) {
            run_one(items);
        }
    }

    void run_one(size_t items) {
        std::vector<std::thread> threads;

        size_t per_thread = items / cur_numthreads;

        int cur = 0;
        for (size_t i = 0; i < cur_numthreads - 1; ++i) {
            int newcur = cur + per_thread;
            threads.emplace_back(&Test_Set_Find::thread_func, this, cur, newcur);
            cur = newcur;
        }
        threads.emplace_back(&Test_Set_Find::thread_func, this, cur, items);


        for (auto& t : threads) t.join();
    }
};

//! Construct different set types for a generic test class
template <template <typename SetType> class TestClass>
struct TestFactory_Set {

    //! Test the std::set
    typedef TestClass<std::set<size_t> > StdSet;

    //! Test the B+ tree with a specific leaf/inner slots
    template <int Slots>
    struct SLBtreeSet
        : TestClass<tlx::slbtree_set<
                        size_t, std::less<size_t>,
                        struct btree_traits_speed<Slots, Slots> > > {
        SLBtreeSet(size_t n)
            : TestClass<tlx::slbtree_set<
                            size_t, std::less<size_t>,
                            struct btree_traits_speed<Slots, Slots> > >(n) { }
    };

    //! Test the B+ tree with a specific leaf/inner slots
    template <int Slots>
    struct BtreeSet
        : TestClass<tlx::btree_set<
                        size_t, std::less<size_t>,
                        struct tlx::btree_default_traits<
                            size_t, size_t,
                            Slots * (sizeof(size_t) + sizeof(void*)),
                            Slots * sizeof(size_t)>,
                        std::allocator<size_t> /* Allocator */,
                        true /* concurrent */> > {
        BtreeSet(size_t n)
            : TestClass<tlx::btree_set<
                            size_t, std::less<size_t>,
                            struct tlx::btree_default_traits<
                                size_t, size_t,
                                Slots * (sizeof(size_t) + sizeof(void*)),
                                Slots * sizeof(size_t)>,
                                std::allocator<size_t> /* Allocator */,
                                true /* concurrent */> >(n) { }
    };

    //! Run tests on all set types
    void call_testrunner(size_t items);
};

// -----------------------------------------------------------------------------

//! Test a generic map type with insertions
template <typename MapType>
class Test_Map_Insert
{
public:
    Test_Map_Insert(size_t) { }

    static const char * op() { return "map_insert"; }

    void run(size_t items, size_t repeat_until) {
        for (size_t r = 0; r < repeat_until; r += items) {
            run_one(items);
        }
    }

    void run_one(size_t items) {
        MapType map;

        std::default_random_engine rng(seed);
        for (size_t i = 0; i < items; i++) {
            size_t r = rng();
            map.insert(std::make_pair(r, r));
        }

        die_unless(map.size() == items);
    }
};

//! Test a generic map type with insert, find and delete sequences
template <typename MapType>
class Test_Map_InsertFindDelete
{
public:
    Test_Map_InsertFindDelete(size_t) { }

    static const char * op() { return "map_insert_find_delete"; }

    void run(size_t items, size_t repeat_until) {
        for (size_t r = 0; r < repeat_until; r += items) {
            run_one(items);
        }
    }

    void run_one(size_t items) {
        MapType map;

        std::default_random_engine rng(seed);
        for (size_t i = 0; i < items; i++) {
            size_t r = rng();
            map.insert(std::make_pair(r, r));
        }

        die_unless(map.size() == items);

        rng.seed(seed);
        for (size_t i = 0; i < items; i++)
            map.find(rng());

        rng.seed(seed);
        for (size_t i = 0; i < items; i++)
            map.erase(map.find(rng()));

        die_unless(map.empty());
    }
};

//! Test a generic map type with insert, find and delete sequences
template <typename MapType>
class Test_Map_Find
{
public:
    MapType map;

    static const char * op() { return "map_find"; }

    Test_Map_Find(size_t items) {
        std::default_random_engine rng(seed);
        for (size_t i = 0; i < items; i++) {
            size_t r = rng();
            map.insert(std::make_pair(r, r));
        }

        die_unless(map.size() == items);
    }

    void run(size_t items, size_t repeat_until) {
        for (size_t r = 0; r < repeat_until; r += items) {
            run_one(items);
        }
    }

    void run_one(size_t items) {
        std::default_random_engine rng(seed);
        for (size_t i = 0; i < items; i++)
            map.find(rng());
    }
};

//! Construct different map types for a generic test class
template <template <typename MapType> class TestClass>
struct TestFactory_Map {
    //! Test the B+ tree with a specific leaf/inner slots
    template <int Slots>
    struct BtreeMap
        : TestClass<tlx::slbtree_map<
                        size_t, size_t, std::less<size_t>,
                        struct btree_traits_speed<Slots, Slots> > > {
        BtreeMap(size_t n)
            : TestClass<tlx::slbtree_map<
                            size_t, size_t, std::less<size_t>,
                            struct btree_traits_speed<Slots, Slots> > >(n) { }
    };

    //! Run tests on all map types
    void call_testrunner(size_t items);
};

// -----------------------------------------------------------------------------

size_t repeat_until;

//! Repeat (short) tests until enough time elapsed and divide by the repeat.
template <typename TestClass>
void testrunner_loop(size_t items, const std::string& container_name) {

    double ts1, ts2, duration;
    size_t actual_items = 0;
    double min_run_time = 1.0;

    do
    {
        // count timed tests
        duration = 0.0;
        actual_items = items;

        {
            // initialize test structures
            TestClass test(items);

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

    float million_ops_per_sec = (actual_items / (ts2 - ts1)) / 1e6;
    std::cout << "RESULT"
              << " container=" << container_name
              << " op=" << TestClass::op()
              << " time_total=" << std::setprecision(3) << (ts2 - ts1)
              << " time(ns)="
              << std::fixed << std::setprecision(3)
              << ((ts2 - ts1) * 1e9 / actual_items)
              << " items_per_sec(m)=" << std::setprecision(2)
              << million_ops_per_sec
              << std::endl;

    std::cout << "TreeName\tSlotMax\tLevel\tRootSlt\tThreads\tLockReq\tMops/s\n"
              << container_name << "\t"
              << g_slot_max << "\t"
              << g_level << "\t"
              << g_slotuse << "\t"
              << cur_numthreads << "\t"
              << g_lock_req_str << "\t"
              << million_ops_per_sec << "\t"
              << std::endl;
}

// Template magic to emulate a for_each slots. These templates will roll-out
// btree instantiations for each of the Low-High leaf/inner slot numbers.
template <template <int Slots> class Functional, int Low, int High>
struct btree_range {
    void operator () (size_t items, const std::string& container_name) {
        testrunner_loop<Functional<Low> >(
            items, container_name + "<" + std::to_string(Low) + ">"
            " slots=" + std::to_string(Low));
        btree_range<Functional, Low + 2, High>()(items, container_name);
    }
};

template <template <int Slots> class Functional, int Low>
struct btree_range<Functional, Low, Low> {
    void operator () (size_t items, const std::string& container_name) {
        testrunner_loop<Functional<Low> >(
            items, container_name + "<" + std::to_string(Low) + ">"
            " slots=" + std::to_string(Low));
    }
};

template <template <typename Type> class TestClass>
void TestFactory_Set<TestClass>::call_testrunner(size_t items) {
#if 0
    if (cur_numthreads == 1 && !skip_std_set) {
        testrunner_loop<StdSet>(items, "std::set");
    }

    btree_range<BtreeSet, min_nodeslots, max_nodeslots>()(
        items, "btree_set");
#else
    // just pick a few node sizes for quicker tests
    if (g_use_slbtree) {
        testrunner_loop<SLBtreeSet<16>>(items, "slbtree_set<16>");
    } else {
        if (g_slot_max == 4)
            testrunner_loop<BtreeSet<4> >(items, "btree_set<4>");
        if (g_slot_max == 8)
            testrunner_loop<BtreeSet<8> >(items, "btree_set<8>");
        if (g_slot_max == 16)
            testrunner_loop<BtreeSet<16> >(items, "btree_set<16>");
        if (g_slot_max == 32)
            testrunner_loop<BtreeSet<32> >(items, "btree_set<32>");
        if (g_slot_max == 64)
            testrunner_loop<BtreeSet<64> >(items, "btree_set<64>");
        if (g_slot_max == 128)
            testrunner_loop<BtreeSet<128> >(items, "btree_set<128>");
        if (g_slot_max == 256)
            testrunner_loop<BtreeSet<256> >(items, "btree_set<256>");
    }
#endif
}

template <template <typename Type> class TestClass>
void TestFactory_Map<TestClass>::call_testrunner(size_t items) {
#if 0
    btree_range<BtreeMap, min_nodeslots, max_nodeslots>()(
        items, "tlx::btree_multimap");
#else
    // just pick a few node sizes for quicker tests
    testrunner_loop<BtreeMap<4> >(items, "tlx::btree_multimap<4> slots=4");
    testrunner_loop<BtreeMap<8> >(items, "tlx::btree_multimap<8> slots=8");
    testrunner_loop<BtreeMap<16> >(items, "tlx::btree_multimap<16> slots=16");
    testrunner_loop<BtreeMap<32> >(items, "tlx::btree_multimap<32> slots=32");
    testrunner_loop<BtreeMap<64> >(items, "tlx::btree_multimap<64> slots=64");
    testrunner_loop<BtreeMap<128> >(
        items, "tlx::btree_multimap<128> slots=128");
    testrunner_loop<BtreeMap<256> >(
        items, "tlx::btree_multimap<256> slots=256");
#endif
}

void print_usage(const char *program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  -h        Print this help message and exit\n"
              << "  -i <num>  Set BT_INSERT_P (default: 0)\n"
              << "  -l <num>  Set BT_LOOKUP_P (default: 0)\n"
              << "  -L <root|no-root|all|none> Lock which nodes (default: all)\n"
              << "  -m <num>  Set BT_MIN (default: 0)\n"
              << "  -M <num>  Set BT_MAX (default: 0)\n"
              << "  -o        Use old Slow Lock Btree\n"
              << "  -r <num>  Set BT_REPEAT (default: 0)\n"
              << "  -R <num>  Set expected root slot, -2 means slotmax-2 (default: 0)\n"
              << "  -s        Skip std::set (now always skipped)\n"
              << "  -S <num>  Set slotmax, must be one of 4, 8, 16, 32, 64, 128, 256\n"
              << "  -t <num>  Set BT_THREADS (default: 1)\n"
              << "  -T <num>  Maplize threshold percentage, if locks waits more than this percent, maplize it";
}

//! Speed test them!
int main(int argc, char *argv[]) {
    std::set<size_t> valid_max_slots = {4, 8, 16, 32, 64, 128, 256};
    int opt;
    while ((opt = getopt(argc, argv, "hi:l:L:m:M:or:R:sS:t:T:")) != -1) {
        switch (opt) {
        case 'o':
            g_use_slbtree = true;
            break;
        case 't':
            cur_numthreads = atol(optarg);
            break;
        case 'm':
            min_items = atol(optarg);
            if (min_items <= 0) {
                std::cerr << "-m (min_items) must be positive! Got " << min_items << ".\n";
                return EXIT_FAILURE;
            }
            break;
        case 'M':
            max_items = atol(optarg);
            if (max_items <= 0) {
                std::cerr << "-M (max_items) must be positive! Got " << max_items << ".\n";
                return EXIT_FAILURE;
            }
            break;
        case 'r':
            start_repeat = atol(optarg);
            break;
        case 'R':
            g_root_slot = atol(optarg);
            std::cerr << "Sorry do not support -R before get_root_info() is added\n";
            return EXIT_FAILURE;
            break;
        case 'i':
            INSERT_PROP = atol(optarg);
            break;
        case 'l':
            LOOKUP_PROP = atol(optarg);
            break;
        case 'L':
            g_lock_req_str = optarg;
            if (strcmp(optarg, "all") ==0) {
                g_lock_req = lock_all;
            } else if (strcmp(optarg, "root") ==0) {
                g_lock_req = lock_root_only;
            } else if (strcmp(optarg, "no-root") ==0) {
                g_lock_req = lock_no_root_only;
            } else if (strcmp(optarg, "none") ==0) {
                g_lock_req = lock_none;
            } else {
                std::cerr << "Unknow option " << optarg << " for -L\n";
                return EXIT_FAILURE;
            }
            break;
        case 's':
            skip_std_set = true;
            break;
        case 'S':
            g_slot_max = atol(optarg);
            if (valid_max_slots.find(g_slot_max) == valid_max_slots.end()) {
                std::cerr << "Invalid slot max " << optarg << std::endl;
                return EXIT_FAILURE;
            }
            break;
        case 'T':
            maplize_threshold = atol(optarg);
            if (maplize_threshold < 0 || maplize_threshold > 100) {
                std::cerr << "Invalid maplize threshold " << optarg << " must be 0-100\n";
            }
            break;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }
    argc -= optind;
    argv += optind;
    if (argc != 0) {
        std::cerr << "Found extra " << argc << "arguments: ";
        while (argv[0]) {
            std::cerr << argv[0] << " ";
            ++argv;
        }
        std::cerr << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "pid: " << getpid() << std::endl;

    std::cout << "NumThreads: " << cur_numthreads << std::endl;

    std::cout << "InsertOp: " << INSERT_PROP << "% "
              << "LookupOp: " << LOOKUP_PROP << "% "
              << "DeleteOp: " << 100 - INSERT_PROP - LOOKUP_PROP << "%\n";

    {   // Set - speed test mixed insert, find, and erase

        repeat_until = min_items * start_repeat;
        for (size_t items = min_items; items <= max_items; items *= 2)
        {
            std::cout << "set: mixed op (insert/find/erase) " << items << "\n";
            TestFactory_Set<Test_Set_MixedOp>().call_testrunner(items);
        }
        return 0;
    }

    {   // Set - speed test only insertion

        repeat_until = min_items;
        for (size_t items = min_items; items <= max_items; items *= 2)
        {
            std::cout << "set: insert " << items << "\n";
            TestFactory_Set<Test_Set_Insert>().call_testrunner(items);
        }
    }

    {   // Set - speed test insert, find and delete

        repeat_until = min_items;

        for (size_t items = min_items; items <= max_items; items *= 2)
        {
            std::cout << "set: insert, find, delete " << items << "\n";
            TestFactory_Set<Test_Set_InsertFindDelete>().call_testrunner(items);
        }
    }

    {   // Set - speed test find only

        repeat_until = min_items;

        for (size_t items = min_items; items <= max_items; items *= 2)
        {
            std::cout << "set: find " << items << "\n";
            TestFactory_Set<Test_Set_Find>().call_testrunner(items);
        }
    }

    /*{   // Map - speed test only insertion

        repeat_until = min_items;

        for (size_t items = min_items; items <= max_items; items *= 2)
        {
            std::cout << "map: insert " << items << "\n";
            TestFactory_Map<Test_Map_Insert>().call_testrunner(items);
        }
    }

    {   // Map - speed test insert, find and delete

        repeat_until = min_items;

        for (size_t items = min_items; items <= max_items; items *= 2)
        {
            std::cout << "map: insert, find, delete " << items << "\n";
            TestFactory_Map<Test_Map_InsertFindDelete>().call_testrunner(items);
        }
    }

    {   // Map - speed test find only

        repeat_until = min_items;

        for (size_t items = min_items; items <= max_items; items *= 2)
        {
            std::cout << "map: find " << items << "\n";
            TestFactory_Map<Test_Map_Find>().call_testrunner(items);
        }
    }*/

    return 0;
}

/******************************************************************************/
