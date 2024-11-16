#ifndef TLX_BTREE_FAST_LOG_HEADER
#define TLX_BTREE_FAST_LOG_HEADER

#include <tlx/container/btree_set.hpp>

// Define val_type
typedef unsigned short short_val_type;

// Define test_leaf_type using the redefined test_set_type template
template<int LeafSlotMax = 16>
class TestType {
public:
    using test_set_type =
        typename tlx::btree_set<short_val_type,
                                std::less<short_val_type>,
                                struct tlx::btree_default_traits<
                                short_val_type, short_val_type,
                                LeafSlotMax * (sizeof(short_val_type) + sizeof(void*)),
                                LeafSlotMax * sizeof(short_val_type)>,
                                std::allocator<short_val_type>,
                                true // concurrent
                                >;

    using test_btree_type = typename test_set_type::btree_impl;
    using test_leaf_type = typename test_btree_type::LeafNode;
    using test_alloc_type = typename test_set_type::allocator_type;
};

#endif //#define TLX_BTREE_FAST_LOG_HEADER
