#include <random>
#include <iostream>
#include "btree_test.hpp"

// Unit test function
template<int TestSlotMax>
void test_maplize_perf() {
    const size_t array_size = 1000000;  // Size of the leaf array
    const size_t num_selections = 1000000;  // Number of selections

    // Initialize the leaf array
    std::vector<typename TestType<TestSlotMax>::test_leaf_type> leaf_array(array_size);
    for (auto& leaf : leaf_array) {
        TestType<TestSlotMax>::set_leaf_data(&leaf, {10, 20, 30, 40, 50, 60});
    }

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
    std::cout << "Average maplize time: " << avg_maplize_time << " seconds" << std::endl;
    std::cout << "Average unmaplize time: " << avg_unmaplize_time << " seconds" << std::endl;
}

int main() {
    // test_maplize_perf for TestSlotMax = 64, 128, 256, and 512 respectively
    test_maplize_perf<64>();
}
