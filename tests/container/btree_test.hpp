#ifndef TLX_BTREE_TEST_HEADER
#define TLX_BTREE_TEST_HEADER

#include <tlx/container/btree_set.hpp>

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
template<int TestSlotMax, typename val_type = short_val_type>
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
    using test_btree_type = typename test_set_type::btree_impl;
    using test_leaf_type = typename test_btree_type::LeafNode;

    static void set_leaf_data(test_leaf_type *leaf,
                              const std::vector<val_type>& v) {
        TLX_BTREE_ASSERT(v.size() < test_btree_type::leaf_slotmax);
        for (size_t i = 0; i < v.size(); ++i) {
            leaf->slotdata[i] = v[i];
        }
        leaf->slotuse = v.size();
    }
};

#endif
