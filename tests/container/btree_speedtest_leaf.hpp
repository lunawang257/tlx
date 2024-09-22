#ifndef TLX_BTREE_SPEEDTEST_LEAF_HEADER
#define TLX_BTREE_SPEEDTEST_LEAF_HEADER

#include <random>
#include <iostream>
#include <getopt.h>
#include <stdlib.h>
#include <string>
#include <unordered_set>
#include <map>

#include <tlx/container/btree.hpp>
#include <tests/container/btree_speedtest_controller.hpp>
#include "ParallelTools/Lock.hpp"

template<int TestSlotMax, int ValSize, unsigned short SliceSize, unsigned short SliceSizeMax>
class TestLeafPerf {
private:
    using SpeedTestT = SpeedTestType<TestSlotMax, ValSize, SliceSize, SliceSizeMax>;
    using LeafValueVector = std::vector<typename SpeedTestT::test_value_type>;
    using LeafVector = std::vector<typename SpeedTestT::test_leaf_type>;
    using UniDistKeyT = std::uniform_int_distribution<key_type>;
    using UniDistLeafT = std::uniform_int_distribution<size_t>;
    using UniDistActionT = std::uniform_real_distribution<double>;
    using DurationT = std::chrono::duration<double>;
    using MapT = typename SpeedTestT::test_map_type;
    using BTreeT = typename SpeedTestT::test_btree_type;

    static const int seed = 1;
    static const size_t LeafSize = sizeof(typename SpeedTestT::test_leaf_type) - sizeof(void*);
    static const size_t MaplSize = sizeof(typename SpeedTestT::test_mapl_type) + sizeof(void*);
    static const size_t SizeOfSlice = sizeof(typename SpeedTestT::test_slice_type);
    static const size_t LockSize = sizeof(ReaderWriterLock2);
    static constexpr double MaplOverhead = MaplSize * 1.0 / LeafSize;

public:
static void output_result(const std::string& operation,
                          const double avgTime = 0.0) {
    std::cout << "Op=" << operation << "\t"
              << "MaxSlots=" << TestSlotMax << "\t"
              << "ValueSize=" << ValSize << "\t"
              << "SliceSize=" << SliceSize << "\t"
              << "SliceSizeMax=" << SliceSizeMax << "\t"
              << "LeafSize=" << LeafSize << "\t"
              << "MaplSize=" << MaplSize << "\t"
              << "SizeOfSlice=" << SizeOfSlice << "\t"
              << "LockSize=" << LockSize << "\t"
              << "MaplOverhead=" << std::fixed << std::setprecision(2)
                                 << MaplOverhead * 100 << "%" << "\t"
              << "avgTime=" << std::fixed << std::setprecision(2) << avgTime*1e6 << "us" << "\t"
              << std::endl;

    std::cout << "Op\tSlotMx\tValSz\tSliceSz\tSlcSzMx\tLfSz\tMplOvrhd\tTime\n"
              << operation << "\t"
              << TestSlotMax << "\t"
              << ValSize << "\t"
              << SliceSize << "\t"
              << SliceSizeMax << "\t"
              << LeafSize << "\t"
              << std::fixed << std::setprecision(2)
                            << MaplOverhead * 100 << "%" << "\t"
              << std::fixed << std::setprecision(2) << avgTime*1e6 << "us" << "\t"
              << std::endl;
}

static void set_leaf_data(typename SpeedTestT::test_leaf_type *leaf,
                        const LeafValueVector& v, bool sorted = false) {
    TLX_BTREE_ASSERT(v.size() < test_btree_type::leaf_slotmax);

    // Sort the values if the sorted flag is true
    LeafValueVector sorted_values = v;  // Create a copy for sorting if necessary
    if (sorted) {
        std::sort(sorted_values.begin(), sorted_values.end(),
            typename SpeedTestT::ValueComparator());
    }

    // Copy the (optionally sorted) values into the leaf's slotdata
    for (size_t i = 0; i < sorted_values.size(); ++i) {
        leaf->slotdata[i] = sorted_values[i];
    }

    // Set the number of used slots
    leaf->slotuse = sorted_values.size();
}

// Function to generate random values
static LeafValueVector generate_random_values() {
    // Determine the number of slots to fill
    size_t num_slots = static_cast<size_t>(TestSlotMax * 0.75); // 75% of TestSlotMax

    LeafValueVector values;
    values.reserve(num_slots);

    // Random number generator setup
    std::mt19937 rng(seed);

    for (size_t i = 0; i < num_slots; ++i) {
        // Use the extracted function to generate a random value
        values.push_back(SpeedTestT::generate_random_value(rng));
    }

    return values;
}

/*
 * Each leaf is filled with 75% of TestSlotMax slots of val_type data with
 * random keys within [1..100] and random values.
*/

static void initialize_leaf_array(LeafVector& leaf_array, bool sorted = false) {
    // Initialize the leaf array
    for (auto& leaf : leaf_array) {
        // placement new to initialize the leaf
        const LeafValueVector values = generate_random_values();

        set_leaf_data(&leaf, values, sorted);
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
    *     a. a sliceNo is selected within [0..leaf.numslices()).
    *     b. a pos is selected within [0..leaf.mapl->slices[sliceNo].slotuse].
    *     c. start time is recorded.
    *     d. leaf.mapl->slice_insert(sliceNo, pos, val) is called.
    *     e. end time is recorded.
    * 6. The delete operation is conducted as follows:
    *    a. a sliceNo is selected within [0..leaf.numslices()).
    *    b. If the slotuse of the slice is 0, the slice is skipped and the next slice is selected.
    *    b. a pos is selected within [0..leaf.mapl->slices[sliceNo].slotuse).
    *    c. start time is recorded.
    *    d. leaf.mapl->slice_erase(sliceNo, pos) is called.
    *    e. end time is recorded.
    * 7. The average time for insert and delete operations for maplized leaves are
    * calculated and printed.
*/
// Function to perform the insert operation

static void perform_mapl_insert_operation(typename SpeedTestT::test_leaf_type& leaf,
                              std::mt19937& rng,
                              const typename SpeedTestT::test_value_type& val,
                              DurationT& total_insert_time,
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

static void perform_mapl_delete_operation(typename SpeedTestT::test_leaf_type& leaf,
                              std::mt19937& rng,
                              DurationT& total_delete_time,
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

static void test_maplize_insert_delete_perf() {
    constexpr size_t array_size = LEAF_ARRAY_SIZE; // Size of the leaf array
    size_t num_iterations = NUM_ITERATIONS; // Number of iterations
    BTreeT bt;

    // Create a leaf array with the specified size
    LeafVector leaf_array(array_size);

    // Initialize the leaf array
    initialize_leaf_array(leaf_array);

    // Maplize each leaf
    for (auto& leaf : leaf_array) {
        leaf.maplize(DBG(&bt));
    }

    // Random number generator setup
    std::mt19937 rng(seed);
    UniDistLeafT leaf_dist(0, array_size - 1);
    UniDistActionT action_dist(0.0, 1.0);

    // Time measurement variables
    size_t insert_count = 0, delete_count = 0;
    DurationT total_insert_time(0), total_delete_time(0);

    // Perform the operations for the specified number of iterations
    for (size_t i = 0; i < num_iterations; ++i) {
        // Select a random leaf
        auto& leaf = leaf_array[leaf_dist(rng)];

        // Calculate 50% and 75% of TestSlotMax
        size_t slotuse_50 = static_cast<size_t>(TestSlotMax * 0.5);
        //size_t slotuse_75 = static_cast<size_t>(TestSlotMax * 0.75);
        typename SpeedTestT::test_value_type val = SpeedTestT::generate_random_value(rng);

        // Check the slotuse of the leaf and decide the operation
        if (leaf.slotuse > slotuse_50 && leaf.slotuse < TestSlotMax) {
            // Randomly decide to insert or delete
            if (action_dist(rng) < 0.5) {
                perform_mapl_insert_operation(leaf, rng, val, total_insert_time, insert_count);
            } else {
                perform_mapl_delete_operation(leaf, rng, total_delete_time, delete_count);
            }
        } else if (leaf.slotuse <= slotuse_50) {
            perform_mapl_insert_operation(leaf, rng, val, total_insert_time, insert_count);
        } else if (leaf.slotuse == TestSlotMax) {
            perform_mapl_delete_operation(leaf, rng, total_delete_time, delete_count);
        }
    }

    // Calculate and print average times
    double avg_insert_time = (insert_count > 0) ? total_insert_time.count() / insert_count : 0.0;
    double avg_delete_time = (delete_count > 0) ? total_delete_time.count() / delete_count : 0.0;

    std::cout << "Max Slots: " << TestSlotMax
              << " Value Size: " << ValSize
              << " Slice Size: " << SliceSize
              << " Average maplize insert time: " << avg_insert_time * 1e6 << " us" << std::endl;
    std::cout << "Max Slots: " << TestSlotMax
              << " Value Size: " << ValSize
              << " Slice Size: " << SliceSize
              << " Average maplize delete time: " << avg_delete_time * 1e6 << " us" << std::endl;
}

// Function to perform the insert operation

static void perform_insert_operation(typename SpeedTestT::test_leaf_type& leaf,
                              size_t slot,
                              const typename SpeedTestT::test_value_type& value,
                              DurationT& total_insert_time,
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

static void perform_delete_operation(typename SpeedTestT::test_leaf_type& leaf,
                              size_t slot,
                              DurationT& total_delete_time,
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

static void test_insert_delete_perf() {
    constexpr size_t array_size = LEAF_ARRAY_SIZE; // Size of the leaf array
    size_t num_iterations = NUM_ITERATIONS; // Number of iterations

    // Create a leaf array with the specified size
    LeafVector leaf_array(array_size);

    // Initialize the leaf array
    initialize_leaf_array(leaf_array);

    // Random number generator setup
    std::mt19937 rng(seed);
    UniDistLeafT leaf_dist(0, array_size - 1);
    UniDistActionT action_dist(0.0, 1.0);

    // Time measurement variables
    size_t insert_count = 0, delete_count = 0;
    DurationT total_insert_time(0), total_delete_time(0);

    // Perform the operations for the specified number of iterations
    for (size_t i = 0; i < num_iterations; ++i) {
        // Select a random leaf
        auto& leaf = leaf_array[leaf_dist(rng)];

        // Calculate 50% and 100% of TestSlotMax
        size_t slotuse_50 = static_cast<size_t>(TestSlotMax * 0.5);
        size_t slotuse_100 = TestSlotMax;

        // Generate a random slot and value for insertion
        size_t slot = rng() % leaf.slotuse; // Random slot within current slotuse
        typename SpeedTestT::test_value_type val = SpeedTestT::generate_random_value(rng); //random value

        // Check the slotuse of the leaf and decide the operation
        if (leaf.slotuse > slotuse_50 && leaf.slotuse < slotuse_100) {
            // Randomly decide to insert or delete
            if (action_dist(rng) < 0.5) {
                perform_insert_operation(leaf, slot, val, total_insert_time, insert_count);
            } else {
                perform_delete_operation(leaf, slot, total_delete_time, delete_count);
            }
        } else if (leaf.slotuse == slotuse_50) {
            perform_insert_operation(leaf, slot, val, total_insert_time, insert_count);
        } else if (leaf.slotuse == slotuse_100) {
            perform_delete_operation(leaf, slot, total_delete_time, delete_count);
        }
    }

    // Calculate and print average times
    double avg_insert_time = (insert_count > 0) ? total_insert_time.count() / insert_count : 0.0;
    double avg_delete_time = (delete_count > 0) ? total_delete_time.count() / delete_count : 0.0;

    std::cout << "Max_Slots: " << TestSlotMax
              << " Value_Size: " << ValSize
              << " SliceSize: " << SliceSize
              << " Average insert time: " << avg_insert_time * 1e6 << " us" << std::endl;
    std::cout << "Max Slots: " << TestSlotMax
              << " Value Size: " << ValSize
              << " SliceSize: " << SliceSize
              << " Average delete time: " << avg_delete_time * 1e6 << " us" << std::endl;
}

// Unit test function

static void test_maplize_perf() {
    const size_t array_size = LEAF_ARRAY_SIZE;  // Size of the leaf array
    size_t num_selections = NUM_ITERATIONS;  // Number of selections

    BTreeT bt;
    // Initialize the leaf array
    LeafVector leaf_array(array_size);
    initialize_leaf_array(leaf_array);

    // Random number generator for selecting leaves
    std::mt19937 rng(seed);
    UniDistLeafT dist(0, array_size - 1);

    // Time measurements
    size_t maplize_count = 0, unmaplize_count = 0;
    DurationT total_maplize_time(0), total_unmaplize_time(0);

    // Perform random selections and maplize/unmaplize operations
    for (size_t i = 0; i < num_selections; ++i) {
        size_t index = dist(rng);
        auto& leaf = leaf_array[index];

        if (leaf.mapl) {
            auto start_time = std::chrono::high_resolution_clock::now();
            leaf.unmaplize(DBG(&bt));
            auto end_time = std::chrono::high_resolution_clock::now();
            total_unmaplize_time += end_time - start_time;
            ++unmaplize_count;
        } else {
            auto start_time = std::chrono::high_resolution_clock::now();
            leaf.maplize(DBG(&bt));
            auto end_time = std::chrono::high_resolution_clock::now();
            total_maplize_time += end_time - start_time;
            ++maplize_count;
        }
    }

    // Calculate average times
    double avg_maplize_time = (maplize_count > 0) ? total_maplize_time.count() / maplize_count : 0.0;
    double avg_unmaplize_time = (unmaplize_count > 0) ? total_unmaplize_time.count() / unmaplize_count : 0.0;

    output_result("maplize", avg_maplize_time);
    output_result("unmaplize", avg_unmaplize_time);
}

// Main performance test function for lookup

static void test_lookup_perf() {
    constexpr size_t array_size = LEAF_ARRAY_SIZE; // Size of the leaf array
    size_t num_iterations = NUM_ITERATIONS; // Number of iterations
    constexpr int key_range = MAX_KEY_RANGE;

    // Create a leaf array with the specified size
    LeafVector leaf_array(array_size);

    // Initialize the leaf array with sorted values
    initialize_leaf_array(leaf_array, true); // Pass true for sorted

    // Random number generator setup
    std::mt19937 rng(seed);
    UniDistLeafT leaf_dist(0, array_size - 1);
    UniDistKeyT key_dist(1, key_range); // Assuming key range is [1, 100]

    // Time measurement variables
    DurationT total_lookup_time(0);
    size_t total_slots = 0;

    // Perform the operations for the specified number of iterations
    for (size_t i = 0; i < num_iterations; ++i) {
        // Select a random leaf
        auto& leaf = leaf_array[leaf_dist(rng)];

        // Generate a random key
        key_type key = key_dist(rng); // Random key

        // Perform lookup using the find_lower function
        typename SpeedTestT::test_map_type ts;
        // Start time measurement
        auto start_time = std::chrono::high_resolution_clock::now();

        unsigned short slot = ts.tree_.find_lower(&leaf, key);

        // End time measurement
        auto end_time = std::chrono::high_resolution_clock::now();

        total_lookup_time += end_time - start_time;
        total_slots += slot; // Increment slot to count the accessed slot
    }

    // Calculate and print the average lookup time
    double avg_lookup_time = total_lookup_time.count() / num_iterations;

    std::cout << "Max Slots: " << TestSlotMax
              << " Value Size: " << ValSize
              << " SliceSize: " << SliceSize
              << " Average lookup time: " << avg_lookup_time * 1e6 << " us" << std::endl;
    std::cout << "Max Slots: " << TestSlotMax
              << " Value Size: " << ValSize
              << " SliceSize: " << SliceSize
              << " Total number of slots accessed: " << total_slots << std::endl;
}

// Main performance test function for lookup

static void test_maplize_lookup_perf() {
    constexpr size_t array_size = LEAF_ARRAY_SIZE; // Size of the leaf array
    size_t num_iterations = NUM_ITERATIONS; // Number of iterations
    constexpr int key_range = MAX_KEY_RANGE;

    BTreeT bt;
    // Create a leaf array with the specified size
    LeafVector leaf_array(array_size);

    // Initialize the leaf array with sorted values
    initialize_leaf_array(leaf_array, true); // Pass true for sorted

    // Maplize each leaf
    for (auto& leaf : leaf_array) {
        leaf.maplize(DBG(&bt));
    }

    // Random number generator setup
    std::mt19937 rng(seed);
    UniDistLeafT leaf_dist(0, array_size - 1);
    UniDistKeyT key_dist(1, key_range); // Assuming key range is [1, 100]

    // Time measurement variables
    DurationT total_lookup_time(0);
    size_t total_pos = 0;

    // Perform lookup using the find_lower function
    typename SpeedTestT::test_map_type ts;

    // Perform the operations for the specified number of iterations
    for (size_t i = 0; i < num_iterations; ++i) {
        // Select a random leaf
        auto& leaf = leaf_array[leaf_dist(rng)];

        // Generate a random key
        key_type key = key_dist(rng); // Random key

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

    std::cout << "Max Slots: " << TestSlotMax
              << " Value Size: " << ValSize
              << " SliceSize: " << SliceSize
              << " Average maplized lookup time: " << avg_lookup_time * 1e6 << " us" << std::endl;
    std::cout << "Max Slots: " << TestSlotMax
              << " Value Size: " << ValSize
              << " SliceSize: " << SliceSize
              << " Total number of pos accessed: " << total_pos << std::endl;
}

static void randomize_mapl_leaf_array(LeafVector& leaf_array) {
    size_t num_iterations = 2* TestSlotMax; // Number of iterations
    std::mt19937 rng(seed);

    // Time measurement variables
    size_t insert_count = 0, delete_count = 0;
    DurationT total_insert_time(0), total_delete_time(0);

    for (auto& leaf : leaf_array) {
        // Perform the operations for the specified number of iterations
        for (size_t i = 0; i < num_iterations; ++i) {
            if (i % 2 == 0) {
                // Generate a random slot and value for insertion
                typename SpeedTestT::test_value_type val = SpeedTestT::generate_random_value(rng); //random value
                perform_mapl_insert_operation(leaf, rng, val, total_insert_time, insert_count);
            } else {
                perform_mapl_delete_operation(leaf, rng, total_delete_time, delete_count);
            }
        }
    }
}

static void test_maplize_scan_perf() {
    constexpr size_t array_size = LEAF_ARRAY_SIZE; // Size of the leaf array
    size_t num_iterations = NUM_ITERATIONS; // Number of iterations

    // Create a leaf array with the specified size
    LeafVector leaf_array(array_size);
    BTreeT bt;

    // Initialize the leaf array with sorted values
    initialize_leaf_array(leaf_array, false); // Pass true for sorted

    // Maplize each leaf
    for (auto& leaf : leaf_array) {
        leaf.maplize(DBG(&bt));
    }

    randomize_mapl_leaf_array(leaf_array);

    // Random number generator setup
    std::mt19937 rng(seed);
    UniDistLeafT leaf_dist(0, array_size - 1);

    // Time measurement variables
    DurationT total_scan_time(0);

    // Perform lookup using the find_lower function
    typename SpeedTestT::test_map_type ts;

    int dummy_count = 0; // avoid compiler optimization

    // Perform the operations for the specified number of iterations
    for (size_t i = 0; i < num_iterations; ++i) {
        // Select a random leaf
        auto& leaf = leaf_array[leaf_dist(rng)];

        typename SpeedTestT::test_value_type ordered;
        typename SpeedTestT::test_btree_type::MaplKeyContext ctx;

        // Start time measurement
        auto start_time = std::chrono::high_resolution_clock::now();

        for (int j = 0; j < leaf.slotuse; j++) {
            ordered = leaf.get_overall(j, &ctx);
            dummy_count += (memchr(&ordered, 0, sizeof(ordered)) == nullptr);
        }

        // End time measurement
        auto end_time = std::chrono::high_resolution_clock::now();

        total_scan_time += end_time - start_time;
    }

    // Calculate and print the average lookup time
    double avg_scan_time = total_scan_time.count() / num_iterations;

    if (dummy_count == 123456789) {
        std::cout << "dummy_count: " << dummy_count << "\n";
    }

    std::cout << "Max Slots: " << TestSlotMax
              << " Value Size: " << ValSize
              << " SliceSize: " << SliceSize
              << " Average maplized scan time: " << avg_scan_time * 1e6 << " us" << std::endl;
}


static void test_scan_perf() {
    constexpr size_t array_size = LEAF_ARRAY_SIZE; // Size of the leaf array
    size_t num_iterations = NUM_ITERATIONS; // Number of iterations

    // Create a leaf array with the specified size
    LeafVector leaf_array(array_size);

    // Initialize the leaf array with sorted values
    initialize_leaf_array(leaf_array, false); // Pass true for sorted

    // Random number generator setup
    std::mt19937 rng(seed);
    UniDistLeafT leaf_dist(0, array_size - 1);

    // Time measurement variables
    DurationT total_scan_time(0);

    // Perform lookup using the find_lower function
    typename SpeedTestT::test_map_type ts;
    int dummy_count = 0; // avoid compiler optimization

    // Perform the operations for the specified number of iterations
    for (size_t i = 0; i < num_iterations; ++i) {
        // Select a random leaf
        auto& leaf = leaf_array[leaf_dist(rng)];

        typename SpeedTestT::test_value_type ordered;

        // Start time measurement
        auto start_time = std::chrono::high_resolution_clock::now();

        for (int j = 0; j < leaf.slotuse; j++) {
            ordered = leaf.slotdata[j];
            dummy_count += (memchr(&ordered, 0, sizeof(ordered)) == nullptr);
        }
        // End time measurement
        auto end_time = std::chrono::high_resolution_clock::now();

        total_scan_time += end_time - start_time;
    }

    // Calculate and print the average lookup time
    double avg_scan_time = total_scan_time.count() / num_iterations;

    if (dummy_count == 123456789) {
        std::cout << "dummy_count: " << dummy_count << "\n";
    }

    std::cout << "Max Slots: " << TestSlotMax
              << " Value Size: " << ValSize
              << " SliceSize: " << SliceSize
              << " Average scan time: " << avg_scan_time * 1e6 << " us" << std::endl;
}

static void test_rebalance_perf() {
    constexpr size_t array_size = LEAF_ARRAY_SIZE; // Size of the leaf array
    size_t num_iterations = NUM_ITERATIONS; // Number of iterations

    BTreeT bt;
    // Create a leaf array with the specified size
    LeafVector leaf_array(array_size);
    // Initialize the leaf array with sorted values
    initialize_leaf_array(leaf_array, false); // Pass true for sorted

    // Random number generator setup
    std::mt19937 rng(seed);
    UniDistLeafT leaf_dist(0, array_size - 1);

    // Time measurement variables
    DurationT total_rebalance_time(0);

    // Perform lookup using the find_lower function
    typename SpeedTestT::test_map_type ts;

    // Perform the operations for the specified number of iterations
    for (size_t i = 0; i < num_iterations; ++i) {
        // Select a random leaf
        auto& leaf = leaf_array[leaf_dist(rng)];
        leaf.maplize(DBG(&bt));

        // Start time measurement
        auto start_time = std::chrono::high_resolution_clock::now();

        leaf.mapl->rebalance();
        // End time measurement
        auto end_time = std::chrono::high_resolution_clock::now();

        total_rebalance_time += end_time - start_time;
    }

    // Calculate and print the average lookup time
    double avg_rebalance_time = total_rebalance_time.count() / num_iterations;

    output_result("rebalance", avg_rebalance_time);
}


static void test_maplize_structure()
{
    constexpr size_t array_size = 1; // Size of the leaf array

    BTreeT bt;
    // Create a leaf array with the specified size
    LeafVector leaf_array(array_size);

    // Initialize the leaf array with sorted values
    initialize_leaf_array(leaf_array, true); // Pass true for sorted

    // Maplize each leaf
    for (auto& leaf : leaf_array) {
        leaf.maplize(DBG(&bt));

        std::cout << "Slices Information:" << std::endl;

        for (int i = 0; i < leaf.mapl->numslices; ++i) {
            std::cout << "Slice " << i + 1 << ":" << std::endl;

            typename SpeedTestT::test_btree_type::Slice *slice = &leaf.mapl->slices[i];

            // Print slotuse
            std::cout << "  Slot Use: " << slice->slotuse
                      << " SliceSize: " << SliceSize << std::endl;

            // Print index_array
            std::cout << "  Index Array: ";
            if (slice->index_array != nullptr) {
                for (int j = 0; j < slice->slotuse; ++j) {
                    std::cout << slice->index_array[j] << " ";
                }
            } else {
                std::cout << "nullptr (empty)";
            }
            std::cout << std::endl;
        }

        // Print the slice_boundary array
        std::cout << "Slice Boundaries:" << std::endl;
        for (int i = 0; i < leaf.mapl->numslices - 1; ++i) {
            std::cout << "slice_boundary[" << i << "] = " << leaf.mapl->slice_boundary[i] << ", ";
        }
        std::cout << std::endl;
    }
}
};

#endif //TLX_BTREE_SPEEDTEST_LEAF_HEADER
