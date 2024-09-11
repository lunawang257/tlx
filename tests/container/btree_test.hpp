#ifndef TLX_BTREE_TEST_HEADER
#define TLX_BTREE_TEST_HEADER

#include <tlx/container/btree_set.hpp>

// Define val_type
typedef unsigned short val_type;

// Redefine tlx::btree_set as a template with TestSlotMax as a parameter
template<int TestSlotMax>
using test_btree_type = tlx::btree_set<
    val_type,
    std::less<val_type>,
    struct tlx::btree_default_traits<
        val_type, val_type,
        TestSlotMax * (sizeof(val_type) + sizeof(void*)),
        TestSlotMax * sizeof(val_type)>,
    std::allocator<val_type>,
    true /* concurrent */
>;

// Define test_leaf_type using the redefined test_set_type template
template<int TestSlotMax>
using test_leaf_type = typename test_btree_type<TestSlotMax>::btree_impl::LeafNode;

template<int TestSlotMax>
void set_leaf_data(test_leaf_type<TestSlotMax> *leaf,
                   const std::vector<val_type>& v) {
    TLX_BTREE_ASSERT(v.size() < test_btree_type<TestSlotMax>::btree_impl::leaf_slotmax);
    for (size_t i = 0; i < v.size(); ++i) {
        leaf->slotdata[i] = v[i];
    }
    leaf->slotuse = v.size();
}

#endif