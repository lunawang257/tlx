#include <iostream>
#include <getopt.h>
#include <unordered_set>

#include "btree_speedtest_leaf.hpp"
#include "btree_speedtest_concurrent.hpp"

const char* help_message = R"(
Usage:
  -d --dist [num]                   Workload distribution, zipf|uniform
  -h --help                         Show this help message
  -i --iteration [num]              Number of iterations
  -I --insert-prop [num]            Insert Proportion
  -L --lookup-prop [num]            Lookup Proportion
  -m --is-mapl                      For update/lookup, whether run maplized version
  -M --slice-size-max [num]         Max Slice Size
  -p --test [update|lookup|maplize|scan|btreemix] Test option, \
                                    update means insert and delete, \
                                    btreemix means btree concurrent mixed operations \
                                    insert\delete\lookup
  -r --repeats  <num>               Set Repeats (default: 0)
  -s --slot-max [num]               Maximum slot value
  -S --slice-size [num]             Slice Size
  -t --num-threads [num]            Number of threads
  -T --maplize-threshhold [num]     Maplize Proportion
  -v --val-size [num]               Value size
)";

// Function to map string to enum
TestOption stringToTestOption(const std::string& str) {
    if (str == "update") return UPDATE;
    else if (str == "lookup") return LOOKUP;
    else if (str == "maplize") return MAPLIZE;
    else if (str == "scan") return SCAN;
    else if (str == "btreemix") return BTREEMIX;
    else if (str == "zipf") return ZIPF;
    else if (str == "uniform") return UNIFORM;
    else return INVALID;
}

int main(int argc, char* argv[]) {
    // Define long options
    static struct option long_options[] = {
        {"dist", required_argument, nullptr, 'd'},
        {"help", no_argument, nullptr, 'h'},
        {"iteration", required_argument, nullptr, 'i'},
        {"insert-prop", required_argument, nullptr, 'I'},
        {"lookup-prop", required_argument, nullptr, 'L'},
        {"is-mapl", required_argument, nullptr, 'm'},
        {"slice-size-max", required_argument, nullptr, 'M'},
        {"test", required_argument, nullptr, 'p'},
        {"repeats", required_argument, nullptr, 'r'},
        {"slot-max", required_argument, nullptr, 's'},
        {"slice-size", required_argument, nullptr, 'S'},
        {"num-threads", required_argument, nullptr, 't'},
        {"maplize-threshhold", required_argument, nullptr, 'T'},
        {"val-size", required_argument, nullptr, 'v'},
        {nullptr, 0, nullptr, 0} // End of options
    };

    // Variables to store the parsed options
    std::unordered_set<TestOption> testOptions;

    int slot_max = 0;
    int val_size = 0;
    int slice_size = 0;
    int slice_size_max = 0;
    int is_mapl = 0;
    int num_threads = 0;
    std::string test_option = "";
    TestOption dist_option = ZIPF;

    int option_index = 0;
    int c;

    bool test_invoked = false;

    // Parse command line arguments
    while ((c = getopt_long(argc, argv, "d:m:p:i:s:S:v:h:M:t:T:h:r:I:L:", long_options, &option_index)) != -1) {
        switch (c) {
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
        case 'i': // iteration
            NUM_ITERATIONS = std::atoi(optarg);
            break;
        case 'I':
            INSERT_PROP = atol(optarg);
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
        case 'M': //slotmax
            slice_size_max = std::atoi(optarg);
            break;
        case 'r': // iteration
            start_repeat = std::atoi(optarg);
            break;
        case 's': // slotmax
            slot_max = std::atoi(optarg);
            break;
        case 'S': // slotsize
            slice_size = std::atoi(optarg);
            break;
        case 't': // iteration
            num_threads = std::atoi(optarg);
            break;
        case 'T':
            maplize_threshold = atol(optarg);
            if (maplize_threshold > 100) {
                std::cerr << "Invalid maplize threshold " << optarg << " must be 0-100\n";
            }
            break;
        case 'v': // valsize
            val_size = std::atoi(optarg);
            break;
        case 'h': // help
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

    std::cout << "slot_max=" << slot_max << "\t"
              << "val_size=" << val_size << "\t"
              << "slice_size=" << slice_size << "\t"
              << "slice_size_max=" << slice_size_max << "\t"
              << "test_option=" << test_option << "\t"
              << "dist_option=" << dist_option << "\t"
              << "NUM_ITERATIONS=" << NUM_ITERATIONS << "\t"
              << "is_mapl=" << is_mapl << "\t"
              << "num_threads=" << num_threads << "\t"
              << "start_repeat=" << start_repeat << "\t"
              << "maplize_threshold=" << maplize_threshold << "\t"
              << "INSERT_PROP=" << INSERT_PROP << "\t"
              << "LOOKUP_PROP=" << LOOKUP_PROP << "\t"
              << std::endl;

    std::cout << "pid: " << getpid() << std::endl;

#define RUN_MAPLIZE(slots, size, slice, slice_max)                      \
    if (testOptions.contains(MAPLIZE) &&                                \
        slot_max == (slots) &&                                          \
        val_size == (size) &&                                           \
        slice_size == (slice) &&                                        \
        slice_size_max == (slice_max)) {                                \
        TestLeafPerf<slots, size, slice, slice_max>::                   \
            test_maplize_perf();                                        \
        test_invoked = true;                                            \
    }

#define RUN_UPDATE(slots, size, slice, slice_max)                       \
    if (testOptions.contains(UPDATE) &&                                 \
        slot_max == (slots) &&                                          \
        val_size == (size) &&                                           \
        slice_size == (slice) &&                                        \
        slice_size_max == (slice_max)) {                                \
        if (is_mapl) {                                                  \
            TestLeafPerf<slots, size, slice, slice_max>::               \
                test_maplize_insert_delete_perf();                      \
        } else {                                                        \
            TestLeafPerf<slots, size, slice, slice_max>::               \
                test_insert_delete_perf();                              \
        }                                                               \
        test_invoked = true;                                            \
    }

#define RUN_LOOKUP(slots, size, slice, slice_max)                       \
    if (testOptions.contains(LOOKUP) &&                                 \
        slot_max == (slots) &&                                          \
        val_size == (size) &&                                           \
        slice_size == (slice) &&                                        \
        slice_size_max == (slice_max)) {                                \
        if (is_mapl) {                                                  \
            TestLeafPerf<slots, size, slice, slice_max>::               \
                test_maplize_lookup_perf();                             \
        } else {                                                        \
            TestLeafPerf<slots, size, slice, slice_max>::               \
                test_lookup_perf();                                     \
        }                                                               \
        test_invoked = true;                                            \
    }

#define RUN_SCAN(slots, size, slice, slice_max)                         \
    if (testOptions.contains(SCAN) &&                                   \
        slot_max == (slots) &&                                          \
        val_size == (size) &&                                           \
        slice_size == (slice) &&                                        \
        slice_size_max == (slice_max)) {                                \
        if (is_mapl) {                                                  \
            TestLeafPerf<slots, size, slice, slice_max>::               \
                test_maplize_scan_perf();                               \
        } else {                                                        \
            TestLeafPerf<slots, size, slice, slice_max>::               \
                test_scan_perf();                                       \
        }                                                               \
        test_invoked = true;                                            \
    }

#define RUN_BTREEMIX(slots, size, slice, slice_max)                     \
    if (testOptions.contains(BTREEMIX) &&                               \
        slot_max == (slots) &&                                          \
        val_size == (size) &&                                           \
        slice_size == (slice) &&                                        \
        slice_size_max == (slice_max)) {                                \
            std::stringstream ss;                                       \
            ss << "btreemix" << "\t"                                    \
               << slots << "\t"                                         \
               << size << "\t"                                          \
               << slice << "\t"                                         \
               << slice_max << "\t"                                     \
               << num_threads << "\t"                                   \
               << maplize_threshold;                                    \
            btreemix_runner_loop<                                       \
                Test_Set_MixedOp<SpeedTestType<                         \
                    slots, size, slice, slice_max>>>(                   \
                    NUM_ITERATIONS, ss.str(), num_threads, dist_option);\
        test_invoked = true;                                            \
    }

    // use python3 genleafcmd.py to generate
    #include <tests/container/leaf-perf-run-all-options.hpp>

    if (!test_invoked) {
        std::cout << "No tests were invoked. Maybe didn't specify the right slots or value size?\n";
    }

    return 0;
}
