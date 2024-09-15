#ifndef TLX_BTREE_SPEEDTEST_HEADER
#define TLX_BTREE_SPEEDTEST_HEADER

#include <tlx/container/btree_map.hpp>

// define key_type
typedef long key_type;

template<int ValSize>
struct long_val_type {
    char value[ValSize];
};

// Define test_leaf_type using the redefined test_set_type template
template<int TestSlotMax = 64,
         int ValSize = 8,
         unsigned short SliceSize = 8>
class SpeedTestType {
public:
// Redefine tlx::btree_set as a template with TestSlotMax as a parameter
    using key_compare = std::less<key_type>;  // Default comparison function

    using traits = tlx::btree_default_traits<key_type,
                        long_val_type<ValSize>,
                        (sizeof(key_type) + sizeof(void*))*TestSlotMax,
                        sizeof(long_val_type<ValSize>)*TestSlotMax,
                        SliceSize>;  // Default traits

    using allocator_type = std::allocator<std::pair<const key_type, long_val_type<ValSize>>>;  // Default allocator

    // Define test_map_type using the btree_map with the specified types
    using test_map_type = tlx::btree_map<key_type, long_val_type<ValSize>, key_compare, traits, allocator_type, true>;

    using test_value_type = typename test_map_type::value_type;
    using test_btree_type = typename test_map_type::btree_impl;
    using test_leaf_type = typename test_btree_type::LeafNode;

    struct ValueComparator {
        bool operator()(const test_value_type &a, const test_value_type &b) const {
            key_compare key_less;
            return key_less(test_btree_type::key_of_value::get(a), test_btree_type::key_of_value::get(b));
        }
    };
};

#endif //TLX_BTREE_SPEEDTEST_HEADER
