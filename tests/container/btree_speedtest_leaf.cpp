#include <random>
#include <iostream>
#include <getopt.h>
#include <stdlib.h>
#include <string>
#include <unordered_set>
#include <map>
#include "btree_test.hpp"

const char* help_message = R"(
Usage:
  -t --test [update|lookup|maplize] Test option, update means insert and delete
  -m --is-mapl                      For update/lookup, whether run maplized version
  -i --iteration [num]              Number of iterations
  -s --slot-max [num]               Maximum slot value
  -v --val-size [num]               Value size
  -h --help                         Show this help message
)";

#define STRINGIZE(arg)  STRINGIZE1(arg)
#define STRINGIZE1(arg) STRINGIZE2(arg)
#define STRINGIZE2(arg) #arg

#define CONCATENATE(arg1, arg2)   CONCATENATE1(arg1, arg2)
#define CONCATENATE1(arg1, arg2)  CONCATENATE2(arg1, arg2)
#define CONCATENATE2(arg1, arg2)  arg1##arg2

#define FOR_EACH_1(what, x, ...) what(x)
#define FOR_EACH_2(what, x, ...)\
  what(x);\
  FOR_EACH_1(what,  __VA_ARGS__);
#define FOR_EACH_3(what, x, ...)\
  what(x);\
  FOR_EACH_2(what, __VA_ARGS__);
#define FOR_EACH_4(what, x, ...)\
  what(x);\
  FOR_EACH_3(what,  __VA_ARGS__);
#define FOR_EACH_5(what, x, ...)\
  what(x);\
 FOR_EACH_4(what,  __VA_ARGS__);
#define FOR_EACH_6(what, x, ...)\
  what(x);\
  FOR_EACH_5(what,  __VA_ARGS__);
#define FOR_EACH_7(what, x, ...)\
  what(x);\
  FOR_EACH_6(what,  __VA_ARGS__);
#define FOR_EACH_8(what, x, ...)\
  what(x);\
  FOR_EACH_7(what,  __VA_ARGS__);

#define FOR_EACH_NARG(...) FOR_EACH_NARG_(__VA_ARGS__, FOR_EACH_RSEQ_N())
#define FOR_EACH_NARG_(...) FOR_EACH_ARG_N(__VA_ARGS__)
#define FOR_EACH_ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, N, ...) N
#define FOR_EACH_RSEQ_N() 8, 7, 6, 5, 4, 3, 2, 1, 0

#define FOR_EACH_(N, what, x, ...) CONCATENATE(FOR_EACH_, N)(what, x, __VA_ARGS__)
#define FOR_EACH(what, x, ...) FOR_EACH_(FOR_EACH_NARG(x, __VA_ARGS__), what, x, __VA_ARGS__)

const int MAX_KEY_RANGE = 1000;
const int MAX_SHORT_VALUE_RANGE = 1000;
const int LEAF_ARRAY_SIZE = 10000;
int NUM_ITERATIONS = 1000000;

// Function to generate a single random value of type val_type
template<int TestSlotMax, typename val_type, int ValSize>
val_type generate_random_value(std::mt19937& rng) {
    std::uniform_int_distribution<int> key_dist(1, MAX_KEY_RANGE); // Random keys in the range [1..100]

    if constexpr (std::is_same_v<val_type, short_val_type>) {
        // Generate random value for short_val_type
        std::uniform_int_distribution<short_val_type> short_dist(1, MAX_SHORT_VALUE_RANGE);
        return short_dist(rng);
    } else if constexpr (std::is_same_v<val_type, long_val_type<ValSize>>) {
        // Generate random value for long_val_type
        long_val_type<ValSize> val;
        val.key = key_dist(rng); // Random key

        // Fill the value array with random characters
        std::generate(std::begin(val.value), std::end(val.value), [&]() { return static_cast<char>(key_dist(rng)); });

        return val;
    }
}

// Function to generate random values
template<int TestSlotMax, typename val_type, int ValSize>
std::vector<val_type> generate_random_values() {
    // Determine the number of slots to fill
    size_t num_slots = static_cast<size_t>(TestSlotMax * 0.75); // 75% of TestSlotMax

    std::vector<val_type> values;
    values.reserve(num_slots);

    // Random number generator setup
    std::mt19937 rng(std::random_device{}());

    for (size_t i = 0; i < num_slots; ++i) {
        // Use the extracted function to generate a random value
        values.push_back(generate_random_value<TestSlotMax, val_type, ValSize>(rng));
    }

    return values;
}

/*
 * Each leaf is filled with 75% of TestSlotMax slots of val_type data with
 * random keys within [1..100] and random values.
*/
template<int TestSlotMax, typename val_type, int ValSize>
void initialize_leaf_array(std::vector<typename TestType<TestSlotMax, val_type, ValSize>::test_leaf_type>& leaf_array,
        bool sorted = false) {
    // Initialize the leaf array
    for (auto& leaf : leaf_array) {
        const std::vector<val_type> values = generate_random_values<TestSlotMax, val_type, ValSize>();
        //TestType<TestSlotMax>::set_leaf_data(&leaf, {10, 20, 30, 40, 50, 60});
        TestType<TestSlotMax, val_type, ValSize>::set_leaf_data(&leaf, values, sorted);
    }
}

/*
    * This function test_maplize_insert_delete_perf tests the performance of
    * the insert and the delete opeations of maplized leaves. The test is performed as follows:
    * 1. A leaf array is initialized with a size defined as a variable with value of 1,000,000.
    * 2. Each leaf is filled with 75% of TestSlotMax slots of long_valu_type data with
    *   random keys with [1..100] and random characters as values, and then maplized.
    * 3. The test will run for number of iterations defined as a variable with value of 1,000,000.
    * 4. In each iteration
    *   a. A random leaf is selected from the leaf array.
    *   b. If the slotuse of the leaf is between 50% and 100% of TestSlotMax,
    *      randomly conduct either insert or delete operation.
    *   c. If the slotuse of the leaf is equal to 50% of TestSlotMax, insert operation is conducted.
    *   d. If the slotuse of the leaf is equal to 100% of TestSlotMax, delete operation is conducted.
    * 5. The insert operation is conducted as follows:
    *     a. a sliceNo is selected within [0..leaf.numslices).
    *     b. a pos is selected within [0..leaf.mapl->slices[sliceNo].slotuse].
    *     c. start time is recorded.
    *     d. leaf.mapl->slice_insert(sliceNo, pos, val) is called.
    *     e. end time is recorded.
    * 6. The delete operation is conducted as follows:
    *    a. a sliceNo is selected within [0..leaf.numslices).
    *    b. If the slotuse of the slice is 0, the slice is skipped and the next slice is selected.
    *    b. a pos is selected within [0..leaf.mapl->slices[sliceNo].slotuse).
    *    c. start time is recorded.
    *    d. leaf.mapl->slice_erase(sliceNo, pos) is called.
    *    e. end time is recorded.
    * 7. The average time for insert and delete operations for maplized leaves are
    * calculated and printed.
*/
// Function to perform the insert operation
template<int TestSlotMax, typename val_type, int ValSize>
void perform_mapl_insert_operation(typename TestType<TestSlotMax, val_type, ValSize>::test_leaf_type& leaf,
                              std::mt19937& rng,
                              const val_type& val,
                              std::chrono::duration<double>& total_insert_time,
                              size_t& insert_count) {
    size_t sliceNo = leaf.mapl->numslices > 0 ? rng() % leaf.mapl->numslices : 0;
    size_t pos = leaf.mapl->slices[sliceNo].slotuse > 0 ? rng() % (leaf.mapl->slices[sliceNo].slotuse + 1) : 0;

    auto start_time = std::chrono::high_resolution_clock::now();
    leaf.mapl->slice_insert(sliceNo, pos, val); // Perform insertion
    auto end_time = std::chrono::high_resolution_clock::now();

    total_insert_time += end_time - start_time;
    ++insert_count;
}

// Function to perform the delete operation
template<int TestSlotMax, typename val_type, int ValSize>
void perform_mapl_delete_operation(typename TestType<TestSlotMax, val_type, ValSize>::test_leaf_type& leaf,
                              std::mt19937& rng,
                              std::chrono::duration<double>& total_delete_time,
                              size_t& delete_count) {
    size_t sliceNo = leaf.mapl->numslices > 0 ? rng() % leaf.mapl->numslices : 0;

    // Find a slice with a non-zero slotuse
    while (leaf.mapl->slices[sliceNo].slotuse == 0) {
        sliceNo = (sliceNo + 1) % leaf.mapl->numslices;
    }

    size_t pos = leaf.mapl->slices[sliceNo].slotuse > 0 ? rng() % leaf.mapl->slices[sliceNo].slotuse : 0;

    auto start_time = std::chrono::high_resolution_clock::now();
    leaf.mapl->slice_erase(sliceNo, pos); // Perform deletion
    auto end_time = std::chrono::high_resolution_clock::now();

    total_delete_time += end_time - start_time;
    ++delete_count;
}

// Main performance test function
template<int TestSlotMax, typename val_type = short_val_type, int ValSize = 0>
void test_maplize_insert_delete_perf() {
    constexpr size_t array_size = LEAF_ARRAY_SIZE; // Size of the leaf array
    size_t num_iterations = NUM_ITERATIONS; // Number of iterations

    // Create a leaf array with the specified size
    std::vector<typename TestType<TestSlotMax, val_type, ValSize>::test_leaf_type> leaf_array(array_size);

    // Initialize the leaf array
    initialize_leaf_array<TestSlotMax, val_type, ValSize>(leaf_array);

    // Maplize each leaf
    for (auto& leaf : leaf_array) {
        leaf.maplize();
    }

    // Random number generator setup
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> leaf_dist(0, array_size - 1);
    std::uniform_real_distribution<double> action_dist(0.0, 1.0);

    // Time measurement variables
    size_t insert_count = 0, delete_count = 0;
    std::chrono::duration<double> total_insert_time(0), total_delete_time(0);

    // Perform the operations for the specified number of iterations
    for (size_t i = 0; i < num_iterations; ++i) {
        // Select a random leaf
        auto& leaf = leaf_array[leaf_dist(rng)];

        // Calculate 50% and 75% of TestSlotMax
        size_t slotuse_50 = static_cast<size_t>(TestSlotMax * 0.5);
        //size_t slotuse_75 = static_cast<size_t>(TestSlotMax * 0.75);
        val_type val = generate_random_value<TestSlotMax, val_type, ValSize>(rng);

        // Check the slotuse of the leaf and decide the operation
        if (leaf.slotuse > slotuse_50 && leaf.slotuse < TestSlotMax) {
            // Randomly decide to insert or delete
            if (action_dist(rng) < 0.5) {
                perform_mapl_insert_operation<TestSlotMax, val_type, ValSize>(leaf, rng, val, total_insert_time, insert_count);
            } else {
                perform_mapl_delete_operation<TestSlotMax, val_type, ValSize>(leaf, rng, total_delete_time, delete_count);
            }
        } else if (leaf.slotuse <= slotuse_50) {
            perform_mapl_insert_operation<TestSlotMax, val_type, ValSize>(leaf, rng, val, total_insert_time, insert_count);
        } else if (leaf.slotuse == TestSlotMax) {
            perform_mapl_delete_operation<TestSlotMax, val_type, ValSize>(leaf, rng, total_delete_time, delete_count);
        }
    }

    // Calculate and print average times
    double avg_insert_time = (insert_count > 0) ? total_insert_time.count() / insert_count : 0.0;
    double avg_delete_time = (delete_count > 0) ? total_delete_time.count() / delete_count : 0.0;

    std::cout << "Max Slots: " << TestSlotMax << " Value Size: " << ValSize << " Average maplize insert time: " << avg_insert_time * 1e6 << " us" << std::endl;
    std::cout << "Max Slots: " << TestSlotMax << " Value Size: " << ValSize << " Average maplize delete time: " << avg_delete_time * 1e6 << " us" << std::endl;
}

// Function to perform the insert operation
template<int TestSlotMax, typename val_type, int ValSize>
void perform_insert_operation(typename TestType<TestSlotMax, val_type, ValSize>::test_leaf_type& leaf,
                              size_t slot,
                              const val_type& value,
                              std::chrono::duration<double>& total_insert_time,
                              size_t& insert_count) {
    auto start_time = std::chrono::high_resolution_clock::now();

    // Perform insertion using std::copy_backward
    std::copy_backward(leaf.slotdata + slot, leaf.slotdata + leaf.slotuse, leaf.slotdata + leaf.slotuse + 1);
    leaf.slotdata[slot] = value;
    leaf.slotuse++;

    auto end_time = std::chrono::high_resolution_clock::now();

    total_insert_time += end_time - start_time;
    ++insert_count;
}

// Function to perform the delete operation
template<int TestSlotMax, typename val_type, int ValSize>
void perform_delete_operation(typename TestType<TestSlotMax, val_type, ValSize>::test_leaf_type& leaf,
                              size_t slot,
                              std::chrono::duration<double>& total_delete_time,
                              size_t& delete_count) {
    auto start_time = std::chrono::high_resolution_clock::now();

    // Perform deletion using std::copy
    std::copy(leaf.slotdata + slot + 1, leaf.slotdata + leaf.slotuse, leaf.slotdata + slot);
    leaf.slotuse--;

    auto end_time = std::chrono::high_resolution_clock::now();

    total_delete_time += end_time - start_time;
    ++delete_count;
}

// Main performance test function
template<int TestSlotMax, typename val_type = short_val_type, int ValSize = 0>
void test_insert_delete_perf() {
    constexpr size_t array_size = LEAF_ARRAY_SIZE; // Size of the leaf array
    size_t num_iterations = NUM_ITERATIONS; // Number of iterations

    // Create a leaf array with the specified size
    std::vector<typename TestType<TestSlotMax, val_type, ValSize>::test_leaf_type> leaf_array(array_size);

    // Initialize the leaf array
    initialize_leaf_array<TestSlotMax, val_type, ValSize>(leaf_array);

    // Random number generator setup
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> leaf_dist(0, array_size - 1);
    std::uniform_real_distribution<double> action_dist(0.0, 1.0);

    // Time measurement variables
    size_t insert_count = 0, delete_count = 0;
    std::chrono::duration<double> total_insert_time(0), total_delete_time(0);

    // Perform the operations for the specified number of iterations
    for (size_t i = 0; i < num_iterations; ++i) {
        // Select a random leaf
        auto& leaf = leaf_array[leaf_dist(rng)];

        // Calculate 50% and 100% of TestSlotMax
        size_t slotuse_50 = static_cast<size_t>(TestSlotMax * 0.5);
        size_t slotuse_100 = TestSlotMax;

        // Generate a random slot and value for insertion
        size_t slot = rng() % leaf.slotuse; // Random slot within current slotuse
        val_type val = generate_random_value<TestSlotMax, val_type, ValSize>(rng); //random value

        // Check the slotuse of the leaf and decide the operation
        if (leaf.slotuse > slotuse_50 && leaf.slotuse < slotuse_100) {
            // Randomly decide to insert or delete
            if (action_dist(rng) < 0.5) {
                perform_insert_operation<TestSlotMax, val_type, ValSize>(leaf, slot, val, total_insert_time, insert_count);
            } else {
                perform_delete_operation<TestSlotMax, val_type, ValSize>(leaf, slot, total_delete_time, delete_count);
            }
        } else if (leaf.slotuse == slotuse_50) {
            perform_insert_operation<TestSlotMax, val_type, ValSize>(leaf, slot, val, total_insert_time, insert_count);
        } else if (leaf.slotuse == slotuse_100) {
            perform_delete_operation<TestSlotMax, val_type, ValSize>(leaf, slot, total_delete_time, delete_count);
        }
    }

    // Calculate and print average times
    double avg_insert_time = (insert_count > 0) ? total_insert_time.count() / insert_count : 0.0;
    double avg_delete_time = (delete_count > 0) ? total_delete_time.count() / delete_count : 0.0;

    std::cout << "Max Slots: " << TestSlotMax << " Value Size: " << ValSize << " Average insert time: " << avg_insert_time * 1e6 << " us" << std::endl;
    std::cout << "Max Slots: " << TestSlotMax << " Value Size: " << ValSize << " Average delete time: " << avg_delete_time * 1e6 << " us" << std::endl;
}

// Unit test function
template<int TestSlotMax, typename val_type = short_val_type, int ValSize = 0>
void test_maplize_perf() {
    const size_t array_size = LEAF_ARRAY_SIZE;  // Size of the leaf array
    size_t num_selections = NUM_ITERATIONS;  // Number of selections

    // Initialize the leaf array
    std::vector<typename TestType<TestSlotMax, val_type, ValSize>::test_leaf_type> leaf_array(array_size);
    initialize_leaf_array<TestSlotMax, val_type, ValSize>(leaf_array);

    // Random number generator for selecting leaves
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, array_size - 1);

    // Time measurements
    size_t maplize_count = 0, unmaplize_count = 0;
    std::chrono::duration<double> total_maplize_time(0), total_unmaplize_time(0);

    // Perform random selections and maplize/unmaplize operations
    for (size_t i = 0; i < num_selections; ++i) {
        size_t index = dist(rng);
        auto& leaf = leaf_array[index];

        if (leaf.mapl) {
            auto start_time = std::chrono::high_resolution_clock::now();
            leaf.unmaplize();
            auto end_time = std::chrono::high_resolution_clock::now();
            total_unmaplize_time += end_time - start_time;
            ++unmaplize_count;
        } else {
            auto start_time = std::chrono::high_resolution_clock::now();
            leaf.maplize();
            auto end_time = std::chrono::high_resolution_clock::now();
            total_maplize_time += end_time - start_time;
            ++maplize_count;
        }
    }

    // Calculate average times
    double avg_maplize_time = (maplize_count > 0) ? total_maplize_time.count() / maplize_count : 0.0;
    double avg_unmaplize_time = (unmaplize_count > 0) ? total_unmaplize_time.count() / unmaplize_count : 0.0;

    // Print results
    std::cout << "Max Slots: " << TestSlotMax << " Value Size: " << ValSize << " Average maplize time: " << avg_maplize_time * 1e6 << " us" << std::endl;
    std::cout << "Max Slots: " << TestSlotMax << " Value Size: " << ValSize << " Average unmaplize time: " << avg_unmaplize_time * 1e6 << " us" << std::endl;
}

// Main performance test function for lookup
template<int TestSlotMax, typename val_type = short_val_type, int ValSize = 0>
void test_lookup_perf() {
    constexpr size_t array_size = LEAF_ARRAY_SIZE; // Size of the leaf array
    size_t num_iterations = NUM_ITERATIONS; // Number of iterations
    constexpr int key_range = MAX_KEY_RANGE;

    // Create a leaf array with the specified size
    std::vector<typename TestType<TestSlotMax, val_type, ValSize>::test_leaf_type> leaf_array(array_size);

    // Initialize the leaf array with sorted values
    initialize_leaf_array<TestSlotMax, val_type, ValSize>(leaf_array, true); // Pass true for sorted

    // Random number generator setup
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> leaf_dist(0, array_size - 1);
    std::uniform_int_distribution<unsigned short> key_dist(1, key_range); // Assuming key range is [1, 100]

    // Time measurement variables
    std::chrono::duration<double> total_lookup_time(0);
    size_t total_slots = 0;

    // Perform the operations for the specified number of iterations
    for (size_t i = 0; i < num_iterations; ++i) {
        // Select a random leaf
        auto& leaf = leaf_array[leaf_dist(rng)];

        // Generate a random key
        val_type key = generate_random_value<TestSlotMax, val_type, ValSize>(rng);

        // Start time measurement
        auto start_time = std::chrono::high_resolution_clock::now();

        // Perform lookup using the find_lower function
        typename TestType<TestSlotMax, val_type, ValSize>::test_set_type ts;
        unsigned short slot = ts.tree_.find_lower(&leaf, key);

        // End time measurement
        auto end_time = std::chrono::high_resolution_clock::now();

        total_lookup_time += end_time - start_time;
        total_slots += slot; // Increment slot to count the accessed slot
    }

    // Calculate and print the average lookup time
    double avg_lookup_time = total_lookup_time.count() / num_iterations;

    std::cout << "Max Slots: " << TestSlotMax << " Value Size: " << ValSize << " Average lookup time: " << avg_lookup_time * 1e6 << " us" << std::endl;
    std::cout << "Max Slots: " << TestSlotMax << " Value Size: " << ValSize << " Total number of slots accessed: " << total_slots << std::endl;
}

// Main performance test function for lookup
template<int TestSlotMax, typename val_type = short_val_type, int ValSize = 0>
void test_maplize_lookup_perf() {
    constexpr size_t array_size = LEAF_ARRAY_SIZE; // Size of the leaf array
    size_t num_iterations = NUM_ITERATIONS; // Number of iterations
    constexpr int key_range = MAX_KEY_RANGE;

    // Create a leaf array with the specified size
    std::vector<typename TestType<TestSlotMax, val_type, ValSize>::test_leaf_type> leaf_array(array_size);

    // Initialize the leaf array with sorted values
    initialize_leaf_array<TestSlotMax, val_type, ValSize>(leaf_array, true); // Pass true for sorted

    // Maplize each leaf
    for (auto& leaf : leaf_array) {
        leaf.maplize();
    }

    // Random number generator setup
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> leaf_dist(0, array_size - 1);
    std::uniform_int_distribution<unsigned short> key_dist(1, key_range); // Assuming key range is [1, 100]

    // Time measurement variables
    std::chrono::duration<double> total_lookup_time(0);
    size_t total_pos = 0;

    // Perform lookup using the find_lower function
    typename TestType<TestSlotMax, val_type, ValSize>::test_set_type ts;

    // Perform the operations for the specified number of iterations
    for (size_t i = 0; i < num_iterations; ++i) {
        // Select a random leaf
        auto& leaf = leaf_array[leaf_dist(rng)];

        // Generate a random key
        val_type key = generate_random_value<TestSlotMax, val_type, ValSize>(rng);

        // Start time measurement
        auto start_time = std::chrono::high_resolution_clock::now();

        int slicenum = leaf.mapl->get_slicenum(key);
        auto* slice = leaf.mapl->slices + slicenum;
        int pos = ts.tree_.find_lower(slice, key);

        // End time measurement
        auto end_time = std::chrono::high_resolution_clock::now();

        total_lookup_time += end_time - start_time;
        total_pos += pos; // Increment slot to count the accessed slot
    }

    // Calculate and print the average lookup time
    double avg_lookup_time = total_lookup_time.count() / num_iterations;

    std::cout << "Max Slots: " << TestSlotMax << " Value Size: " << ValSize << " Average maplized lookup time: " << avg_lookup_time * 1e6 << " us" << std::endl;
    std::cout << "Max Slots: " << TestSlotMax << " Value Size: " << ValSize << " Total number of pos accessed: " << total_pos << std::endl;
}

#define FOR_EACH_SIZE(f) \
    f(32)                \
    f(64)                \
    f(128)               \
    f(256)               \
    f(512)

// Define an enum to represent test options
enum TestOption {
    UPDATE,
    LOOKUP,
    MAPLIZE,
    INVALID
};

// Function to map string to enum
TestOption stringToTestOption(const std::string& str) {
    if (str == "update") return UPDATE;
    else if (str == "lookup") return LOOKUP;
    else if (str == "maplize") return MAPLIZE;
    else return INVALID;
}

// Helper function to convert enum to string (for debugging)
std::string testOptionToString(TestOption opt) {
    switch (opt) {
        case UPDATE: return "update";
        case LOOKUP: return "lookup";
        case MAPLIZE: return "maplize";
        default: return "invalid";
    }
}

int main(int argc, char* argv[]) {
    // Define long options
    static struct option long_options[] = {
        {"test", required_argument, nullptr, 't'},
        {"is-mapl", required_argument, nullptr, 'm'},
        {"iteration", required_argument, nullptr, 'i'},
        {"slot-max", required_argument, nullptr, 's'},
        {"val-size", required_argument, nullptr, 'v'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0} // End of options
    };

    // Variables to store the parsed options
    std::unordered_set<TestOption> testOptions;
    int iteration = NUM_ITERATIONS;
    int slot_max = 0;
    int val_size = 0;
    int is_mapl = 0;

    int option_index = 0;
    int c;

    // Parse command line arguments
    while ((c = getopt_long(argc, argv, "m:t:i:s:v:h", long_options, &option_index)) != -1) {
        switch (c) {
        case 't': { // --test
            TestOption option = stringToTestOption(optarg);
            if (option != INVALID) {
                testOptions.insert(option);
            } else {
                std::cerr << "Invalid test option: " << optarg << "\n";
                std::cerr << help_message;
                return 1;
            }
            break;
        }
        case 'i': // --iteration
            iteration = std::atoi(optarg);
            break;
        case 'm':
            is_mapl = atoi(optarg); // Convert argument to integer
            if (is_mapl != 0 && is_mapl != 1) {
                fprintf(stderr, "Error: --is-mapl option must be 0 or 1.\n");
                return 1;
            }
            break;
        case 's': // --slot-max
            slot_max = std::atoi(optarg);
            break;
        case 'v': // --val-size
            val_size = std::atoi(optarg);
            break;
        case 'h': // --help
            std::cout << help_message;
            return 0;
        case '?':
            // Unrecognized option or missing required argument
            std::cerr << "Unknown option or missing argument. Use --help for usage information.\n";
            return 1;
        default:
            break;
        }
    }

    NUM_ITERATIONS = iteration;

    bool test_invoked = false;

    if (testOptions.contains(MAPLIZE)) {
#define RUN_MAPLIZE(slots, size)                                        \
        if (slot_max == (slots)) {                                      \
            if (val_size == (size)) {                                   \
                test_maplize_perf<slots, long_val_type<size>, size>();  \
                test_invoked = true;                                    \
            }                                                           \
        }

#define RUN_MAPLIZE_SLOT_32(size) RUN_MAPLIZE(32, size)
#define RUN_MAPLIZE_SLOT_64(size) RUN_MAPLIZE(64, size)
#define RUN_MAPLIZE_SLOT_128(size) RUN_MAPLIZE(128, size)
#define RUN_MAPLIZE_SLOT_256(size) RUN_MAPLIZE(256, size)
#define RUN_MAPLIZE_SLOT_512(size) RUN_MAPLIZE(512, size)
        FOR_EACH(RUN_MAPLIZE_SLOT_32, 16, 32, 64, 128, 256, 512);
        FOR_EACH(RUN_MAPLIZE_SLOT_64, 16, 32, 64, 128, 256, 512);
        FOR_EACH(RUN_MAPLIZE_SLOT_128, 16, 32, 64, 128, 256, 512);
        FOR_EACH(RUN_MAPLIZE_SLOT_256, 16, 32, 64, 128, 256, 512);
        FOR_EACH(RUN_MAPLIZE_SLOT_512, 16, 32, 64, 128, 256, 512);
    }

    if (testOptions.contains(UPDATE)) {

#define RUN_UPDATE(slots, size)                                 \
        if (slot_max == (slots)) {                              \
            if (val_size == (size)) {                           \
                if (is_mapl) {                                  \
                    test_maplize_insert_delete_perf             \
                        <slots, long_val_type<size>, size>();   \
                } else {                                        \
                    test_insert_delete_perf                     \
                        <slots, long_val_type<size>, size>();   \
                }                                               \
                test_invoked = true;                            \
            }                                                   \
        }

#define RUN_UPDATE_SLOT_32(size) RUN_UPDATE(32, size)
#define RUN_UPDATE_SLOT_64(size) RUN_UPDATE(64, size)
#define RUN_UPDATE_SLOT_128(size) RUN_UPDATE(128, size)
#define RUN_UPDATE_SLOT_256(size) RUN_UPDATE(256, size)
#define RUN_UPDATE_SLOT_512(size) RUN_UPDATE(512, size)
        FOR_EACH(RUN_UPDATE_SLOT_32, 16, 32, 64, 128, 256, 512);
        FOR_EACH(RUN_UPDATE_SLOT_64, 16, 32, 64, 128, 256, 512);
        FOR_EACH(RUN_UPDATE_SLOT_128, 16, 32, 64, 128, 256, 512);
        FOR_EACH(RUN_UPDATE_SLOT_256, 16, 32, 64, 128, 256, 512);
        FOR_EACH(RUN_UPDATE_SLOT_512, 16, 32, 64, 128, 256, 512);
    }

    if (testOptions.contains(LOOKUP)) {

#define RUN_LOOKUP(slots, size)                                 \
        if (slot_max == (slots)) {                              \
            if (val_size == (size)) {                           \
                if (is_mapl) {                                  \
                    test_maplize_lookup_perf                    \
                        <slots, long_val_type<size>, size>();   \
                } else {                                        \
                    test_lookup_perf                            \
                        <slots, long_val_type<size>, size>();   \
                }                                               \
                test_invoked = true;                            \
            }                                                   \
        }

#define RUN_LOOKUP_SLOT_32(size) RUN_LOOKUP(32, size)
#define RUN_LOOKUP_SLOT_64(size) RUN_LOOKUP(64, size)
#define RUN_LOOKUP_SLOT_128(size) RUN_LOOKUP(128, size)
#define RUN_LOOKUP_SLOT_256(size) RUN_LOOKUP(256, size)
#define RUN_LOOKUP_SLOT_512(size) RUN_LOOKUP(512, size)
        FOR_EACH(RUN_LOOKUP_SLOT_32, 16, 32, 64, 128, 256, 512);
        FOR_EACH(RUN_LOOKUP_SLOT_64, 16, 32, 64, 128, 256, 512);
        FOR_EACH(RUN_LOOKUP_SLOT_128, 16, 32, 64, 128, 256, 512);
        FOR_EACH(RUN_LOOKUP_SLOT_256, 16, 32, 64, 128, 256, 512);
        FOR_EACH(RUN_LOOKUP_SLOT_512, 16, 32, 64, 128, 256, 512);
    }

    /*
    //test_maplize_perf for TestSlotMax = 64, 128, 256, and 512 respectively
    //test_maplize_perf<64>();
    //test_maplize_perf<64, long_val_type<16>, 16>();

    //test_maplize_insert_delete_perf<64>();
    test_maplize_insert_delete_perf<128, long_val_type<64>, 64>();

    //test_insert_delete_perf<64>();
    test_insert_delete_perf<128, long_val_type<64>, 64>();

    //test_lookup_perf<64>();
    //test_lookup_perf<64, long_val_type<16>, 16>();

    //test_maplize_lookup_perf<64>();
    //test_maplize_lookup_perf<64, long_val_type<16>, 16>();
    */

    if (!test_invoked) {
        std::cout << "No tests were invoked. Maybe didn't specify the right slots or value size?\n";
    }

    return 0;
}
