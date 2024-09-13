#ifndef TLX_BTREE_TEST_HEADER
#define TLX_BTREE_TEST_HEADER

#include <algorithm> // Required for std::swap
#include <tlx/container/btree_set.hpp>
#include <tlx/container/btree_map.hpp>

// Define val_type
typedef unsigned short short_val_type;

template<int ValSize>
struct long_val_type {
    int key;
    char value[ValSize - sizeof(int)];

    // define the operator< for the BigVal
    bool operator < (const long_val_type& other) const {
        return key < other.key;
    }
};

// Define test_leaf_type using the redefined test_set_type template
template<int TestSlotMax,
        typename val_type = short_val_type,
        int ValSize = 0>
class TestType {
public:

	// Redefine tlx::btree_set as a template with TestSlotMax as a parameter
    using test_set_type =
        typename tlx::btree_set<val_type,
                                std::less<val_type>,
                                struct tlx::btree_default_traits<
                                val_type, val_type,
                                TestSlotMax * (sizeof(val_type) + sizeof(void*)),
                                TestSlotMax * sizeof(val_type)>,
                                std::allocator<val_type>,
                                true /* concurrent */
                                >;

  /*   using key_compare = std::less<key_type>;  // Default comparison function
    using traits = tlx::btree_default_traits<key_type, val_type>;  // Default traits
    using allocator_type = std::allocator<std::pair<const key_type, val_type>>;  // Default allocator

    // Define test_map_type using the btree_map with the specified types
    using test_map_type = tlx::btree_map<key_type, val_type, key_compare, traits, allocator_type, true>; */

    using test_btree_type = typename test_set_type::btree_impl;
    //using test_btree_type = typename test_map_type::btree_impl;
    using test_leaf_type = typename test_btree_type::LeafNode;

    static void set_leaf_data(test_leaf_type *leaf,
                              const std::vector<val_type>& v, bool sorted = false) {
        TLX_BTREE_ASSERT(v.size() < test_btree_type::leaf_slotmax);

        // Sort the values if the sorted flag is true
        std::vector<val_type> sorted_values = v;  // Create a copy for sorting if necessary
        if (sorted) {
            std::sort(sorted_values.begin(), sorted_values.end());
        }

        // Copy the (optionally sorted) values into the leaf's slotdata
        for (size_t i = 0; i < sorted_values.size(); ++i) {
            leaf->slotdata[i] = sorted_values[i];
        }

        // Set the number of used slots
        leaf->slotuse = sorted_values.size();
    }
};

#endif
