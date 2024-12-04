#include <iostream>
#include <getopt.h>
#include <unordered_set>

#include "btree_speedtest_leaf.hpp"
#include "btree_speedtest_concurrent.hpp"

const char* help_message = R"(
Usage:
  -c --scan-prop [num]              Scan Proportion
  -d --dist [zipf|uniform]          Workload distribution
  -e --early-unlock [0/1]           Whether run top-down early-unlock for mapl erase
  -h --help                         Show this help message
  -i --iteration [num]              Number of iterations
  -I --insert-prop [num]            Insert Proportion
  -l --scan-len [num]               Scan Length
  -L --lookup-prop [num]            Lookup Proportion
  -m --is-mapl [0/1]                For update/lookup, whether run maplized version
  -M --slice-size-max [num]         Max Slice Size
  -n --inner-max [num]              Maximum inner node slot value
  -p --test [update|lookup|maplize|scan|btreemix|rebalance] \
                                    Test option, \
                                    update means insert and delete, \
                                    btreemix means btree concurrent mixed operations \
                                    insert\delete\lookup\scan\rebalance \
  -r --repeats  <real num>          Set Repeats, can be a decimal number (default: 1)
  -s --slot-max [num]               Maximum leaf node slot value
  -S --slice-size [num]             Slice Size
  -t --num-threads [num]            Number of threads
  -T --maplize-threshhold [num]     Maplize Proportion
  -y --try-lock                     Whether try to lock parent during erase for better concurrency
  -v --val-size [num]               Value size
)";

// Function to map string to enum
TestOption stringToTestOption(const std::string& str) {
    if (str == "update") return UPDATE;
    else if (str == "lookup") return LOOKUP;
    else if (str == "maplize") return MAPLIZE;
    else if (str == "scan") return SCAN;
    else if (str == "btreemix") return BTREEMIX;
    else if (str == "rebalance") return REBALANCE;
    else if (str == "zipf") return ZIPF;
    else if (str == "uniform") return UNIFORM;
    else return INVALID;
}

// Variables to store the parsed options
std::unordered_set<TestOption> testOptions;

int slot_max = 0;
int inner_max = 0;
int val_size = 0;
int slice_size = 0;
int slice_size_max = 0;
int is_mapl = 0;
int early_unlock = 0;
int try_lock = 0;
int num_threads = 0;
int g_lock_flags = 0;
int check_only = 0;
std::string test_option = "";
TestOption dist_option = ZIPF;
bool test_invoked = false;

extern void run_all_args(void);

int main(int argc, char* argv[]) {
    // Define long options
    static struct option long_options[] = {
        {"scan-prop", required_argument, nullptr, 'c'},
        {"check-only", required_argument, nullptr, 'C'},
        {"dist", required_argument, nullptr, 'd'},
        {"early-unlock", required_argument, nullptr, 'e'},
        {"help", no_argument, nullptr, 'h'},
        {"iteration", required_argument, nullptr, 'i'},
        {"insert-prop", required_argument, nullptr, 'I'},
        {"scan-len", required_argument, nullptr, 'l'},
        {"lookup-prop", required_argument, nullptr, 'L'},
        {"is-mapl", required_argument, nullptr, 'm'},
        {"slice-size-max", required_argument, nullptr, 'M'},
        {"inner-max", required_argument, nullptr, 'n'},
        {"test", required_argument, nullptr, 'p'},
        {"repeats", required_argument, nullptr, 'r'},
        {"slot-max", required_argument, nullptr, 's'},
        {"slice-size", required_argument, nullptr, 'S'},
        {"num-threads", required_argument, nullptr, 't'},
        {"maplize-threshhold", required_argument, nullptr, 'T'},
        {"try-lock", required_argument, nullptr, 'y'},
        {"val-size", required_argument, nullptr, 'v'},
        {nullptr, 0, nullptr, 0} // End of options
    };

    int option_index = 0;
    int c;

    // Parse command line arguments
    while ((c = getopt_long(argc, argv, "c:C:d:e:m:p:i:s:S:v:h:m:M:n:t:T:h:r:I:L:l:y:",
                            long_options, &option_index)) != -1) {
        switch (c) {
        case 'c':
            SCAN_PROP = atol(optarg);
            break;
        case 'C':
            check_only = atol(optarg);
            break;
        case 'd': { // dist
            dist_option = stringToTestOption(optarg);
            if (dist_option != ZIPF &&
                dist_option != UNIFORM) {
                std::cerr << "Invalid dist option: " << optarg << "\n";
                std::cerr << help_message;
                return 1;
            }
            break;
        }
        case 'e':
            early_unlock = atoi(optarg); // Convert argument to integer
            if (early_unlock != 0 && early_unlock != 1) {
                fprintf(stderr, "Error: early_unlock option must be 0 or 1.\n");
                return 1;
            }
            break;
        case 'p': { // test
            TestOption option = stringToTestOption(optarg);
            if (option != INVALID) {
                testOptions.insert(option);
                test_option = optarg;
            } else {
                std::cerr << "Invalid test option: " << optarg << "\n";
                std::cerr << help_message;
                return 1;
            }
            break;
        }
        case 'i':
            NUM_ITERATIONS = std::atoi(optarg);
            break;
        case 'I':
            INSERT_PROP = atol(optarg);
            break;
        case 'l':
            scan_len = std::atoi(optarg);
            break;
        case 'L':
            LOOKUP_PROP = atol(optarg);
            break;
        case 'm':
            is_mapl = atoi(optarg); // Convert argument to integer
            if (is_mapl != 0 && is_mapl != 1) {
                fprintf(stderr, "Error: ismapl option must be 0 or 1.\n");
                return 1;
            }
            break;
        case 'M':
            slice_size_max = std::atoi(optarg);
            break;
        case 'n':
            inner_max = std::atoi(optarg);
            break;
        case 'r': {
            char* end;
            start_repeat = std::strtod(optarg, &end);
            if (*end != '\0') {
                std::cout << "Conversion failed!" << std::endl;
                return 1;
            }
            break;
        }
        case 's':
            slot_max = std::atoi(optarg);
            break;
        case 'S':
            slice_size = std::atoi(optarg);
            break;
        case 't':
            num_threads = std::atoi(optarg);
            break;
        case 'T':
            maplize_threshold = atol(optarg);
            if (maplize_threshold > 100) {
                std::cerr << "Invalid maplize threshold " << optarg << " must be 0-100\n";
            }
            break;
        case 'y':
            try_lock = atoi(optarg); // Convert argument to integer
            if (try_lock != 0 && try_lock != 1) {
                fprintf(stderr, "Error: early_unlock option must be 0 or 1.\n");
                return 1;
            }
            break;
        case 'v':
            val_size = std::atoi(optarg);
            break;
        case 'h':
            std::cout << help_message;
            return 0;
        case '?':
            // Unrecognized option or missing required argument
            std::cerr << "Unknown option or missing argument. Use help for usage information.\n";
            return 1;
        default:
            break;
        }
    }

    if (early_unlock) {
        g_lock_flags |= LOCK_FLAG_EARLY_UNLOCK;
    }
    if (try_lock) {
        g_lock_flags |= LOCK_FLAG_TRY_LOCK;
    }

    std::cout << "slot_max=" << slot_max << "\t"
              << "inner_max=" << inner_max << "\t"
              << "val_size=" << val_size << "\t"
              << "slice_size=" << slice_size << "\t"
              << "slice_size_max=" << slice_size_max << "\t"
              << "test_option=" << test_option << "\t"
              << "dist_option=" << dist_option << "\t"
              << "NUM_ITERATIONS=" << NUM_ITERATIONS << "\t"
              << "is_mapl=" << is_mapl << "\t"
              << "early_unlock=" << early_unlock << "\t"
              << "try_lock=" << try_lock << "\t"
              << "num_threads=" << num_threads << "\t"
              << "start_repeat=" << std::setprecision(2) << start_repeat << "\t"
              << "maplize_threshold=" << maplize_threshold << "\t"
              << "INSERT_PROP=" << INSERT_PROP << "\t"
              << "LOOKUP_PROP=" << LOOKUP_PROP << "\t"
              << "SCAN_PROP=" << SCAN_PROP << "\t"
              << "Scan_len=" << scan_len
              << std::endl;

    std::cout << "pid: " << getpid() << std::endl;

#define RUN_MAPLIZE(leaf_slots, size, slice, slice_max)                 \
    if (testOptions.contains(MAPLIZE) &&                                \
        slot_max == (leaf_slots) &&                                     \
        val_size == (size) &&                                           \
        slice_size == (slice) &&                                        \
        slice_size_max == (slice_max)) {                                \
        TestLeafPerf<leaf_slots, size, slice, slice_max>::              \
            test_maplize_perf();                                        \
        test_invoked = true;                                            \
    }

#define RUN_UPDATE(leaf_slots, size, slice, slice_max)                  \
    if (testOptions.contains(UPDATE) &&                                 \
        slot_max == (leaf_slots) &&                                     \
        val_size == (size) &&                                           \
        slice_size == (slice) &&                                        \
        slice_size_max == (slice_max)) {                                \
        if (is_mapl) {                                                  \
            TestLeafPerf<leaf_slots, size, slice, slice_max>::          \
                test_maplize_insert_delete_perf();                      \
        } else {                                                        \
            TestLeafPerf<leaf_slots, size, slice, slice_max>::          \
                test_insert_delete_perf();                              \
        }                                                               \
        test_invoked = true;                                            \
    }

#define RUN_LOOKUP(leaf_slots, size, slice, slice_max)                  \
    if (testOptions.contains(LOOKUP) &&                                 \
        slot_max == (leaf_slots) &&                                     \
        val_size == (size) &&                                           \
        slice_size == (slice) &&                                        \
        slice_size_max == (slice_max)) {                                \
        if (is_mapl) {                                                  \
            TestLeafPerf<leaf_slots, size, slice, slice_max>::          \
                test_maplize_lookup_perf();                             \
        } else {                                                        \
            TestLeafPerf<leaf_slots, size, slice, slice_max>::          \
                test_lookup_perf();                                     \
        }                                                               \
        test_invoked = true;                                            \
    }

#define RUN_SCAN(leaf_slots, inner_slots, size, slice, slice_max)       \
    if (testOptions.contains(SCAN) &&                                   \
        slot_max == (leaf_slots) &&                                     \
        val_size == (size) &&                                           \
        slice_size == (slice) &&                                        \
        slice_size_max == (slice_max)) {                                \
        if (is_mapl) {                                                  \
            TestLeafPerf<leaf_slots, size, slice, slice_max>::          \
                test_maplize_scan_perf();                               \
        } else {                                                        \
            TestLeafPerf<leaf_slots, size, slice, slice_max>::          \
                test_scan_perf();                                       \
        }                                                               \
        test_invoked = true;                                            \
    }

#define RUN_BTREEMIX(leaf_slots,                                        \
                     inner_slots,                                       \
                     size,                                              \
                     slice,                                             \
                     slice_max)                                         \
    if (testOptions.contains(BTREEMIX) &&                               \
        slot_max == (leaf_slots) &&                                     \
        inner_max == (inner_slots) &&                                   \
        val_size == (size) &&                                           \
        slice_size == (slice) &&                                        \
        slice_size_max == (slice_max)) {                                \
            std::stringstream ss;                                       \
            ss << "treemix" << "\t"                                     \
               << leaf_slots << "\t"                                    \
               << inner_slots << "\t"                                   \
               << size << "\t"                                          \
               << slice << "\t"                                         \
               << slice_max << "\t"                                     \
               << early_unlock << "\t"                                  \
               << try_lock << "\t"                                      \
               << num_threads << "\t"                                   \
               << maplize_threshold;                                    \
            if (!check_only) {                                          \
                btreemix_runner_loop<                                   \
                    Test_Set_MixedOp<SpeedTestType<                     \
                        leaf_slots,                                     \
                        inner_slots,                                    \
                        size,                                           \
                        slice,                                          \
                        slice_max>>>(                                   \
                        NUM_ITERATIONS,                                 \
                        ss.str(),                                       \
                        g_lock_flags,                                   \
                        num_threads,                                    \
                        dist_option);                                   \
            }                                                           \
        test_invoked = true;                                            \
    }

#define RUN_REBALANCE(leaf_slots, size, slice, slice_max)               \
    if (testOptions.contains(REBALANCE) &&                              \
        slot_max == (leaf_slots) &&                                     \
        val_size == (size) &&                                           \
        slice_size == (slice) &&                                        \
        slice_size_max == (slice_max)) {                                \
        TestLeafPerf<leaf_slots, size, slice, slice_max>::              \
            test_rebalance_perf();                                      \
        test_invoked = true;                                            \
    }

    run_all_args();

    if (!test_invoked) {
        std::cout << "No tests were invoked. Maybe didn't specify the right slots or value size?\t"
                  << "slot_max=" << slot_max << "\t"
                  << "inner_max=" << inner_max << "\t"
                  << "val_size=" << val_size << "\t"
                  << "slice_size=" << slice_size << "\t"
                  << "slice_size_max=" << slice_size_max << "\t"
                  << "early_unlock=" << early_unlock << "\t"
                  << "try_lock=" << try_lock << "\t"
                  << std::endl;

        if (testOptions.contains(BTREEMIX)) {
            std::cout
                << "\nAdd the following line to"
                << " btree_speedtest_btreemix_options.hpp"
                << " and rebuild with 'tests/build_test.sh -b'\n\n";

            std::stringstream ss;
            ss << "RUN_BTREEMIX("
               << slot_max << ", "
               << inner_max << ", "
               << val_size << ", "
               << slice_size << ", "
               << slice_size_max << ")\n";

            std::cout << ss.str() << "\n";

            return 1;
        }
    }

    return 0;
}
