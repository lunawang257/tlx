#ifndef TLX_BTREE_SPEEDTEST_CONTROLLER_HEADER
#define TLX_BTREE_SPEEDTEST_CONTROLLER_HEADER

#include <tlx/container/btree_map.hpp>

// Define an enum to represent test options
enum TestOption {
    UPDATE,
    LOOKUP,
    MAPLIZE,
    SCAN,
    BTREEMIX,
    REBALANCE,
    ZIPF,
    UNIFORM,
    INVALID
};

const int MAX_KEY_RANGE = 10000;
const int LEAF_ARRAY_SIZE = 1000;
int NUM_ITERATIONS = 100000;

// define key_type
typedef uint64_t key_type;

template<int ValSize>
struct long_val_type {
    char value[ValSize];
    long_val_type() {
        memset(value, 123, ValSize);
    }
};

// Define test_leaf_type using the redefined test_set_type template
template<int TestSlotMax = 64,
         int ValSize = 8,
         unsigned short SliceSize = 8,
         unsigned short SliceSizeMax = 16>
struct SpeedTestType {
    static const int val_size = ValSize;
    static const int slot_max = TestSlotMax;
    static const unsigned short slice_size = SliceSize;
    static const unsigned short slice_size_max = SliceSizeMax;

    using UniDistKeyT = std::uniform_int_distribution<key_type>;

    using data_type = long_val_type<ValSize>;
    using val_type = std::pair<key_type, data_type>;
    using key_compare = std::less<key_type>;  // Default comparison function
    using traits = tlx::btree_default_traits<key_type,
                        val_type,
                        (sizeof(key_type) + sizeof(void*))*TestSlotMax,
                        sizeof(val_type)*TestSlotMax,
                        SliceSize,
                        SliceSizeMax>;  // Default traits
    using allocator_type = std::allocator<val_type>;  // Default allocator

    // Define test_map_type using the btree_map with the specified types
    using test_map_type = tlx::btree_map<key_type,
                                        data_type,
                                        key_compare,
                                        traits,
                                        allocator_type, true>;

    using test_value_type = typename test_map_type::value_type;
    using test_btree_type = typename test_map_type::btree_impl;
    using test_leaf_type = typename test_btree_type::LeafNode;
    using test_mapl_type = typename test_btree_type::Mapl;
    using test_slice_type = typename test_btree_type::Slice;

    struct ValueComparator {
        bool operator()(const test_value_type &a, const test_value_type &b) const {
            key_compare key_less;
            return key_less(test_btree_type::key_of_value::get(a), test_btree_type::key_of_value::get(b));
        }
    };

    // Function to generate a single random value of type val_type
    static test_value_type generate_random_value(std::mt19937& rng, int key_range = MAX_KEY_RANGE) {
        UniDistKeyT key_dist(1, key_range); // Random keys in the range [1..100]
        key_type key = key_dist(rng); // Random key

        // Generate random value for long_val_type
        return generate_random_value(rng, key_dist, key);
    }

    // Function to generate a single random value of type val_type
    static test_value_type generate_random_value(std::mt19937& rng,
                                                UniDistKeyT& key_dist,
                                                const key_type key) {
        // Generate random value for long_val_type
        data_type data;

        // Fill the value array with random characters
        for (int i = 0; i < ValSize; ++i) {
            data.value[i] = static_cast<char>(key_dist(rng));
        }

        return test_value_type(key, data);
    }
};

#endif //TLX_BTREE_SPEEDTEST_CONTROLLER_HEADER
