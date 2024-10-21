/*******************************************************************************
 * tests/container/btree_test.cpp
 *
 * Part of tlx - http://panthema.net/tlx
 *
 * Copyright (C) 2007-2017 Timo Bingmann <tb@panthema.net>
 *
 * All rights reserved. Published under the Boost Software License, Version 1.0
 ******************************************************************************/
#define TLX_BTREE_FAST_LOG
#define TLX_BTREE_DEBUG
#define TLX_IN_BTREE_TEST

#include "cpu_compatibility.hpp"

//#if defined(TLX_BTREE_FAST_LOG) && defined(TLX_BTREE_DEBUG) && !defined(NDEBUG)
//extern void before_assert(void);
//#else
//inline void before_assert(void) {}
//#endif

//#include <tlx/container/slow_lock_btree_map.hpp>
#include <tlx/container/btree_multimap.hpp>
#include <tlx/container/btree_multiset.hpp>
//#include <tlx/container/slow_lock_btree_set.hpp>
#include <tlx/container/btree_set.hpp>
#include <tlx/container/btree_map.hpp>

#include <tlx/die.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <csignal>
#include <unistd.h>
#include <iostream>
#include <set>
#include <vector>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <random>
#include <string>
#include <utility>
#include <map>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <memory>

#if TLX_MORE_TESTS
static const bool tlx_more_tests = true;
#else
static const bool tlx_more_tests = false;
#endif


#include <tlx/container/btree_set.hpp>
#include "btree_test.hpp"

const int test_slot_max = 8;

static const bool test_multi = false;
static const bool multithread = true;
static const auto seed = 1125199600; //std::random_device{}();

/******************************************************************************/
// Instantiation Tests

template class tlx::btree_set<unsigned int>;
template class tlx::btree_map<int, double>;
template class tlx::btree_multiset<int>;
template class tlx::btree_multimap<int, int>;

/******************************************************************************/
// Simple Tests

template <int Slots>
struct SimpleTest {
    template <typename KeyType>
    struct traits_nodebug : tlx::btree_default_traits<KeyType, KeyType> {
        static const bool self_verify = true;
        static const bool debug = false;

        static const int leaf_slots = Slots;
        static const int inner_slots = Slots;
    };

    static void test_empty() {
        typedef tlx::btree_set<
            unsigned int,
            std::less<unsigned int>, traits_nodebug<unsigned int> >
        btree_type;


        btree_type bt, bt2;
        bt.verify();

        die_unless(bt.erase(42) == false);

        die_unless(bt == bt2);
    }

    static void test_set_insert_erase_3200() {
       if (test_multi) {
            typedef tlx::btree_multiset<
                unsigned int,
                std::less<unsigned int>, traits_nodebug<unsigned int> >
            btree_type;

            btree_type bt;
            bt.verify();

            srand(seed);
            for (unsigned int i = 0; i < 3200; i++)
            {
                die_unless(bt.size() == i);
                bt.insert(rand() % 100);
                die_unless(bt.size() == i + 1);
            }

            srand(seed);
            for (unsigned int i = 0; i < 3200; i++)
            {
                die_unless(bt.size() == 3200 - i);
                die_unless(bt.erase_one(rand() % 100));
                die_unless(bt.size() == 3200 - i - 1);
            }

            die_unless(bt.empty());
        } else {
            // from frozenca's btree tests
            typedef tlx::btree_set<
                unsigned int,
                std::less<unsigned int>, traits_nodebug<unsigned int> >
            btree_type;

            btree_type btree;
            int n = 100;

            std::mt19937 gen(seed);

            std::vector<int> v(n);
            std::iota(v.begin(), v.end(), 0);

            unsigned long size = 0;
            // random insert
            std::ranges::shuffle(v, gen);
            for (auto num : v) {
                die_unless(btree.insert(num).second);
                die_unless(btree.size() == ++size);
            }

            btree.print(std::cout);
            btree.verify();

            // random lookup
            std::ranges::shuffle(v, gen);
            for (auto num : v) {
              die_unless(btree.exists(num));
            }

            // random erase
            std::ranges::shuffle(v, gen);
            for (auto num : v) {
                bool res = btree.erase(num);
                die_unless(res);
                die_unless(btree.size() == --size);
            }
        }
    }

    static void test_set_insert_erase_3200_descending() {
       if (test_multi) {
            typedef tlx::btree_multiset<
                unsigned int,
                std::greater<unsigned int>, traits_nodebug<unsigned int> >
            btree_type;

            btree_type bt;

            srand(seed);
            for (unsigned int i = 0; i < 3200; i++)
            {
                die_unless(bt.size() == i);
                bt.insert(rand() % 100);
                die_unless(bt.size() == i + 1);
            }

            srand(seed);
            for (unsigned int i = 0; i < 3200; i++)
            {
                die_unless(bt.size() == 3200 - i);
                die_unless(bt.erase_one(rand() % 100));
                die_unless(bt.size() == 3200 - i - 1);
            }

            die_unless(bt.empty());
        } else {
            // from frozenca's btree tests
            typedef tlx::btree_set<
                unsigned int,
                std::greater<unsigned int>, traits_nodebug<unsigned int> >
            btree_type;

            btree_type btree;
            int n = 100;

            std::mt19937 gen(seed);

            std::vector<int> v(n);
            std::iota(v.begin(), v.end(), 0);

            unsigned long size = 0;

            // random insert
            std::ranges::shuffle(v, gen);
            for (auto num : v) {
              die_unless(btree.insert(num).second);
              die_unless(btree.size() == ++size);
            }

            btree.verify();

            // random lookup
            std::ranges::shuffle(v, gen);
            for (auto num : v) {
              die_unless(btree.exists(num));
            }

            // random erase
            std::ranges::shuffle(v, gen);
            for (auto num : v) {
              die_unless(btree.erase(num));
              die_unless(btree.size() == --size);
            }
        }
    }

    static void test_map_insert_erase_3200() {
        if (test_multi) {
            typedef tlx::btree_multimap<
                    unsigned int, std::string,
                    std::less<unsigned int>, traits_nodebug<unsigned int> >
                btree_type;

            btree_type bt;

            srand(seed);
            for (unsigned int i = 0; i < 3200; i++)
            {
                die_unless(bt.size() == i);
                bt.insert2(rand() % 100, "101");
                die_unless(bt.size() == i + 1);
            }

            srand(seed);
            for (unsigned int i = 0; i < 3200; i++)
            {
                die_unless(bt.size() == 3200 - i);
                die_unless(bt.erase_one(rand() % 100));
                die_unless(bt.size() == 3200 - i - 1);
            }

            die_unless(bt.empty());
            bt.verify();
        } else {
            typedef tlx::btree_map<
                    std::string, unsigned int,
                    std::less<std::string>, traits_nodebug<unsigned int> >
                btree_type;

            btree_type btree;

            // frozenca tests
            btree["asd"] = 3;
            btree["a"] = 6;
            btree["bbb"] = 9;
            btree["asdf"] = 8;
            btree["asdf"] = 333;
            die_unless(btree["asdf"] == 333);

            btree.insert2("asdfgh", 200);
            die_unless(btree["asdfgh"] == 200);

            btree.verify();
            die_unless(btree.size() == 5);
        }
    }

    static void test_map_insert_erase_3200_descending() {
        if (test_multi) {
            typedef tlx::btree_multimap<
                    unsigned int, std::string,
                    std::greater<unsigned int>, traits_nodebug<unsigned int> >
                btree_type;

            btree_type bt;

            srand(seed);
            for (unsigned int i = 0; i < 3200; i++)
            {
                die_unless(bt.size() == i);
                bt.insert2(rand() % 100, "101");
                die_unless(bt.size() == i + 1);
            }

            srand(seed);
            for (unsigned int i = 0; i < 3200; i++)
            {
                die_unless(bt.size() == 3200 - i);
                die_unless(bt.erase_one(rand() % 100));
                die_unless(bt.size() == 3200 - i - 1);
            }

            die_unless(bt.empty());
            bt.verify();
        } else {
            typedef tlx::btree_map<
                    std::string, unsigned int,
                    std::greater<std::string>, traits_nodebug<unsigned int> >
                btree_type;

            btree_type btree;

            // frozenca tests
            btree["asd"] = 3;
            btree["a"] = 6;
            btree["bbb"] = 9;
            btree["asdf"] = 8;
            btree["asdf"] = 333;
            die_unless(btree["asdf"] == 333);

            btree.insert2("asdfgh", 200);
            die_unless(btree["asdfgh"] == 200);

            btree.verify();
            die_unless(btree.size() == 5);
        }
    }

    static void test2_map_insert_erase_strings() {
        if (!test_multi) return;

        typedef tlx::btree_multimap<
                std::string, unsigned int,
                std::less<std::string>, traits_nodebug<std::string> >
            btree_type;

        std::string letters = "abcdefghijklmnopqrstuvwxyz";

        btree_type bt;

        for (unsigned int a = 0; a < letters.size(); ++a)
        {
            for (unsigned int b = 0; b < letters.size(); ++b)
            {
                bt.insert2(std::string(1, letters[a]) + letters[b],
                           static_cast<unsigned int>(a * letters.size() + b));
            }
        }

        for (unsigned int b = 0; b < letters.size(); ++b)
        {
            for (unsigned int a = 0; a < letters.size(); ++a)
            {
                std::string key = std::string(1, letters[a]) + letters[b];

                die_unless(bt.find(key)->second == a * letters.size() + b);
                die_unless(bt.erase_one(key));
            }
        }

        die_unless(bt.empty());
        bt.verify();
    }

    static void test_map_100000_uint64() {
        tlx::btree_map<std::uint64_t, std::uint8_t> bt;

        for (std::uint64_t i = 10; i < 100000; ++i)
        {
            std::uint64_t key = i % 1000;

            if (bt.find(key) == bt.end())
            {
                bt.insert(std::make_pair(key, key % 100));
            }
        }

        die_unless(bt.size() == 1000);
    }

    static void test_multiset_100000_uint32() {
        if (!test_multi) return;

        tlx::btree_multiset<std::uint32_t> bt;

        for (std::uint64_t i = 0; i < 100000; ++i)
        {
            std::uint32_t key = i % 1000;

            bt.insert(key);
        }

        die_unless(bt.size() == 100000);
    }

    SimpleTest() {
        test_empty();
        test_set_insert_erase_3200();
        test_set_insert_erase_3200_descending();
        test_map_insert_erase_3200();
        test_map_insert_erase_3200_descending();
        test2_map_insert_erase_strings();
        test_map_100000_uint64();
        test_multiset_100000_uint32();
    }
};

void test_simple() {
    // test binary search on different slot sizes
    if (tlx_more_tests) {
        SimpleTest<8>();
        SimpleTest<9>();
        SimpleTest<10>();
        SimpleTest<11>();
        SimpleTest<12>();
        SimpleTest<13>();
        SimpleTest<14>();
        SimpleTest<15>();
    }
    SimpleTest<16>();
    if (tlx_more_tests) {
        SimpleTest<17>();
        SimpleTest<19>();
        SimpleTest<20>();
        SimpleTest<21>();
        SimpleTest<23>();
        SimpleTest<24>();
        SimpleTest<32>();
        SimpleTest<48>();
        SimpleTest<63>();
        SimpleTest<64>();
        SimpleTest<65>();
        SimpleTest<101>();
        SimpleTest<203>();
    }
}

/******************************************************************************/
// Large Test

template <typename KeyType>
struct traits_nodebug : tlx::btree_default_traits<KeyType, KeyType> {
    static const bool self_verify = true;
    static const bool debug = false;

    static const int leaf_slots = 8;
    static const int inner_slots = 8;
};

void test_large_multiset(const unsigned int insnum, const unsigned int modulo) {
    if (!test_multi) return;

    typedef tlx::btree_multiset<
            unsigned int,
            std::less<unsigned int>, traits_nodebug<unsigned int> > btree_type;

    btree_type bt;

    typedef std::multiset<unsigned int> multiset_type;
    multiset_type set;

    // *** insert
    srand(seed);
    for (unsigned int i = 0; i < insnum; i++)
    {
        unsigned int k = rand() % modulo;

        die_unless(bt.size() == set.size());
        bt.insert(k);
        set.insert(k);
        die_unless(bt.count(k) == set.count(k));

        die_unless(bt.size() == set.size());
    }

    die_unless(bt.size() == insnum);

    // *** iterate
    btree_type::iterator bi = bt.begin();
    multiset_type::const_iterator si = set.begin();
    for ( ; bi != bt.end() && si != set.end(); ++bi, ++si)
    {
        die_unless(*si == bi.key());
    }
    die_unless(bi == bt.end());
    die_unless(si == set.end());

    // *** existance
    srand(seed);
    for (unsigned int i = 0; i < insnum; i++)
    {
        unsigned int k = rand() % modulo;

        die_unless(bt.exists(k));
    }

    // *** counting
    srand(seed);
    for (unsigned int i = 0; i < insnum; i++)
    {
        unsigned int k = rand() % modulo;

        die_unless(bt.count(k) == set.count(k));
    }

    // *** deletion
    srand(seed);
    for (unsigned int i = 0; i < insnum; i++)
    {
        unsigned int k = rand() % modulo;

        if (set.find(k) != set.end())
        {
            die_unless(bt.size() == set.size());

            die_unless(bt.exists(k));
            die_unless(bt.erase_one(k));
            set.erase(set.find(k));

            die_unless(bt.size() == set.size());
            die_unless(std::equal(bt.begin(), bt.end(), set.begin()));
        }
    }

    die_unless(bt.empty());
    die_unless(set.empty());
}

/*void test_large_set(const unsigned int insnum, const unsigned int modulo) {
    // from frozenca's btree tests
    typedef tlx::btree_set<
        unsigned int,
        std::greater<unsigned int>, traits_nodebug<unsigned int> >
    btree_type;

    btree_type btree;
    std::mt19937 gen(std::random_device{}());
    std::vector<int> v(insnum);
    std::iota(v.begin(), v.end(), 0);
    unsigned long size = 0;
    // random insert
    std::ranges::shuffle(v, gen);
    for (auto num : v) {
      die_unless(btree.insert(num%modulo).second);
      die_unless(btree.size() == ++size);
    }

    btree.verify();
    // random lookup
    std::ranges::shuffle(v, gen);
    for (auto num : v) {
      die_unless(btree.exists(num%modulo));
    }

    // random erase
    std::ranges::shuffle(v, gen);
    for (auto num : v) {
      die_unless(btree.erase(num));
      die_unless(btree.size() == --size);
    }
}*/

// TODO currently only multiset
void test_large() {
    test_large_multiset(320, 1000);
    test_large_multiset(320, 10000);
    test_large_multiset(3200, 10);
    test_large_multiset(3200, 100);
    test_large_multiset(3200, 1000);
    test_large_multiset(3200, 10000);
    test_large_multiset(32000, 10000);
}

void test_large_sequence() {

#if test_multi
    typedef tlx::btree_multiset<
            unsigned int,
            std::less<unsigned int>, traits_nodebug<unsigned int> > btree_type;

    typedef std::multiset<unsigned int> stdset_type;
#else
    typedef tlx::btree_set<
        unsigned int,
        std::less<unsigned int>, traits_nodebug<unsigned int> > btree_type;

    typedef std::set<unsigned int> stdset_type;
#endif

    btree_type bt;

    const unsigned int insnum = 10000;

    stdset_type set;

    // *** insert
    //srand(seed);
    for (unsigned int i = 0; i < insnum; i++)
    {
        unsigned int k = i;

        die_unless(bt.size() == set.size());
        bt.insert(k);
        set.insert(k);
        die_unless(bt.count(k) == set.count(k));

        die_unless(bt.size() == set.size());
    }

    die_unless(bt.size() == insnum);

    // *** iterate
    btree_type::iterator bi = bt.begin();
    stdset_type::const_iterator si = set.begin();
    for ( ; bi != bt.end() && si != set.end(); ++bi, ++si)
    {
        die_unless(*si == bi.key());
    }
    die_unless(bi == bt.end());
    die_unless(si == set.end());

    // *** existance
    //srand(seed);
    for (unsigned int i = 0; i < insnum; i++)
    {
        unsigned int k = i;

        die_unless(bt.exists(k));
    }

    // *** counting
    //srand(seed);
    for (unsigned int i = 0; i < insnum; i++)
    {
        unsigned int k = i;

        die_unless(bt.count(k) == set.count(k));
    }

    // *** deletion
    //srand(seed);
    for (unsigned int i = 0; i < insnum; i++)
    {
        unsigned int k = i;

        if (set.find(k) != set.end())
        {
            die_unless(bt.size() == set.size());

            die_unless(bt.exists(k));
            die_unless(bt.erase_one(k));
            set.erase(set.find(k));

            die_unless(bt.size() == set.size());
            die_unless(std::equal(bt.begin(), bt.end(), set.begin()));
        }
    }

    die_unless(bt.empty());
    die_unless(set.empty());
}

/******************************************************************************/
// Upper/Lower Bound Tests

void test_bounds_multimap(const unsigned int insnum, const unsigned int modulo) {
    if (!test_multi) return;

    typedef tlx::btree_multimap<
            unsigned int, unsigned int,
            std::less<unsigned int>, traits_nodebug<unsigned int> > btree_type;
    btree_type bt;

    typedef std::multiset<unsigned int> multiset_type;
    multiset_type set;

    // *** insert
    srand(seed);
    for (unsigned int i = 0; i < insnum; i++)
    {
        unsigned int k = rand() % modulo;
        unsigned int v = 234;

        die_unless(bt.size() == set.size());
        bt.insert2(k, v);
        set.insert(k);
        die_unless(bt.count(k) == set.count(k));

        die_unless(bt.size() == set.size());
    }

    die_unless(bt.size() == insnum);

    // *** iterate
    {
        btree_type::iterator bi = bt.begin();
        multiset_type::const_iterator si = set.begin();
        for ( ; bi != bt.end() && si != set.end(); ++bi, ++si)
        {
            die_unless(*si == bi.key());
        }
        die_unless(bi == bt.end());
        die_unless(si == set.end());
    }

    // *** existance
    srand(seed);
    for (unsigned int i = 0; i < insnum; i++)
    {
        unsigned int k = rand() % modulo;

        die_unless(bt.exists(k));
    }

    // *** counting
    srand(seed);
    for (unsigned int i = 0; i < insnum; i++)
    {
        unsigned int k = rand() % modulo;

        die_unless(bt.count(k) == set.count(k));
    }

    // *** lower_bound
    for (unsigned int k = 0; k < modulo + 100; k++)
    {
        multiset_type::const_iterator si = set.lower_bound(k);
        btree_type::const_iterator bi = bt.lower_bound(k);

        if (bi == bt.end())
            die_unless(si == set.end());
        else if (si == set.end())
            die_unless(bi == bt.end());
        else
            die_unless(*si == bi.key());
    }

    // *** upper_bound
    for (unsigned int k = 0; k < modulo + 100; k++)
    {
        multiset_type::const_iterator si = set.upper_bound(k);
        btree_type::const_iterator bi = bt.upper_bound(k);

        if (bi == bt.end())
            die_unless(si == set.end());
        else if (si == set.end())
            die_unless(bi == bt.end());
        else
            die_unless(*si == bi.key());
    }

    // *** equal_range
    for (unsigned int k = 0; k < modulo + 100; k++)
    {
        std::pair<multiset_type::const_iterator, multiset_type::const_iterator> si = set.equal_range(k);
        std::pair<btree_type::const_iterator, btree_type::const_iterator> bi = bt.equal_range(k);

        if (bi.first == bt.end())
            die_unless(si.first == set.end());
        else if (si.first == set.end())
            die_unless(bi.first == bt.end());
        else
            die_unless(*si.first == bi.first.key());

        if (bi.second == bt.end())
            die_unless(si.second == set.end());
        else if (si.second == set.end())
            die_unless(bi.second == bt.end());
        else
            die_unless(*si.second == bi.second.key());
    }

    // *** deletion
    srand(seed);
    for (unsigned int i = 0; i < insnum; i++)
    {
        unsigned int k = rand() % modulo;

        if (set.find(k) != set.end())
        {
            die_unless(bt.size() == set.size());

            die_unless(bt.exists(k));
            die_unless(bt.erase_one(k));
            set.erase(set.find(k));

            die_unless(bt.size() == set.size());
        }
    }

    die_unless(bt.empty());
    die_unless(set.empty());
}

void test_bounds() {
    test_bounds_multimap(3200, 10);
    test_bounds_multimap(320, 1000);
}

/******************************************************************************/
// Test Iterators

void test_iterator1() {
    if (!test_multi) return;

    typedef tlx::btree_multiset<
            unsigned int,
            std::less<unsigned int>, traits_nodebug<unsigned int> > btree_type;

    std::vector<unsigned int> vector;

    srand(seed);
    for (unsigned int i = 0; i < 3200; i++)
    {
        vector.push_back(rand() % 1000);
    }

    die_unless(vector.size() == 3200);

    // test construction and insert(iter, iter) function
    btree_type bt(vector.begin(), vector.end());

    die_unless(bt.size() == 3200);

    // copy for later use
    btree_type bt2 = bt;

    // empty out the first bt
    srand(seed);
    for (unsigned int i = 0; i < 3200; i++)
    {
        die_unless(bt.size() == 3200 - i);
        die_unless(bt.erase_one(rand() % 1000));
        die_unless(bt.size() == 3200 - i - 1);
    }

    die_unless(bt.empty());

    // copy btree values back to a vector

    std::vector<unsigned int> vector2;
    vector2.assign(bt2.begin(), bt2.end());

    // afer sorting the vector, the two must be the same
    std::sort(vector.begin(), vector.end());

    die_unless(vector == vector2);

    // test reverse iterator
    vector2.clear();
    vector2.assign(bt2.rbegin(), bt2.rend());

    std::reverse(vector.begin(), vector.end());

    btree_type::reverse_iterator ri = bt2.rbegin();
    for (unsigned int i = 0; i < vector2.size(); ++i)
    {
        die_unless(vector[i] == vector2[i]);
        die_unless(vector[i] == *ri);

        ri++;
    }

    die_unless(ri == bt2.rend());
}

void test_iterator2() {
    if (!test_multi) return;

    typedef tlx::btree_multimap<
            unsigned int, unsigned int,
            std::less<unsigned int>, traits_nodebug<unsigned int> > btree_type;

    std::vector<btree_type::value_type> vector;

    srand(seed);
    for (unsigned int i = 0; i < 3200; i++)
    {
        vector.push_back(btree_type::value_type(rand() % 1000, 0));
    }

    die_unless(vector.size() == 3200);

    // test construction and insert(iter, iter) function
    btree_type bt(vector.begin(), vector.end());

    die_unless(bt.size() == 3200);

    // copy for later use
    btree_type bt2 = bt;

    // empty out the first bt
    srand(seed);
    for (unsigned int i = 0; i < 3200; i++)
    {
        die_unless(bt.size() == 3200 - i);
        die_unless(bt.erase_one(rand() % 1000));
        die_unless(bt.size() == 3200 - i - 1);
    }

    die_unless(bt.empty());

    // copy btree values back to a vector

    std::vector<btree_type::value_type> vector2;
    vector2.assign(bt2.begin(), bt2.end());

    // afer sorting the vector, the two must be the same
    std::sort(vector.begin(), vector.end());

    die_unless(vector == vector2);

    // test reverse iterator
    vector2.clear();
    vector2.assign(bt2.rbegin(), bt2.rend());

    std::reverse(vector.begin(), vector.end());

    btree_type::reverse_iterator ri = bt2.rbegin();
    for (unsigned int i = 0; i < vector2.size(); ++i, ++ri)
    {
        die_unless(vector[i].first == vector2[i].first);
        die_unless(vector[i].first == ri->first);
        die_unless(vector[i].second == ri->second);
    }

    die_unless(ri == bt2.rend());
}

void test_iterator3() {
    typedef tlx::btree_map<
            unsigned int, unsigned int,
            std::less<unsigned int>, traits_nodebug<unsigned int> > btree_type;

    btree_type map;

    unsigned int maxnum = 1000;

    for (unsigned int i = 0; i < maxnum; ++i)
    {
        map.insert(std::make_pair(i, i * 3));
    }

    {
        // test iterator prefix++
        unsigned int nownum = 0;

        for (btree_type::iterator i = map.begin();
             i != map.end(); ++i)
        {
            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);

            nownum++;
        }

        die_unless(nownum == maxnum);
    }

    {
        // test iterator prefix--
        unsigned int nownum = maxnum;

        btree_type::iterator i;
        for (i = --map.end(); i != map.begin(); --i)
        {
            nownum--;

            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);
        }

        nownum--;

        die_unless(nownum == i->first);
        die_unless(nownum * 3 == i->second);

        die_unless(nownum == 0);
    }

    {
        // test const_iterator prefix++
        unsigned int nownum = 0;

        for (btree_type::const_iterator i = map.begin();
             i != map.end(); ++i)
        {
            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);

            nownum++;
        }

        die_unless(nownum == maxnum);
    }

    {
        // test const_iterator prefix--
        unsigned int nownum = maxnum;

        btree_type::const_iterator i;
        for (i = --map.end(); i != map.begin(); --i)
        {
            nownum--;

            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);
        }

        nownum--;

        die_unless(nownum == i->first);
        die_unless(nownum * 3 == i->second);

        die_unless(nownum == 0);
    }

    {
        // test reverse_iterator prefix++
        unsigned int nownum = maxnum;

        for (btree_type::reverse_iterator i = map.rbegin();
             i != map.rend(); ++i)
        {
            nownum--;

            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);
        }

        die_unless(nownum == 0);
    }

    {
        // test reverse_iterator prefix--
        unsigned int nownum = 0;

        btree_type::reverse_iterator i;
        for (i = --map.rend(); i != map.rbegin(); --i)
        {
            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);

            nownum++;
        }

        die_unless(nownum == i->first);
        die_unless(nownum * 3 == i->second);

        nownum++;

        die_unless(nownum == maxnum);
    }

    {
        // test const_reverse_iterator prefix++
        unsigned int nownum = maxnum;

        for (btree_type::const_reverse_iterator i = map.rbegin();
             i != map.rend(); ++i)
        {
            nownum--;

            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);
        }

        die_unless(nownum == 0);
    }

    {
        // test const_reverse_iterator prefix--
        unsigned int nownum = 0;

        btree_type::const_reverse_iterator i;
        for (i = --map.rend(); i != map.rbegin(); --i)
        {
            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);

            nownum++;
        }

        die_unless(nownum == i->first);
        die_unless(nownum * 3 == i->second);

        nownum++;

        die_unless(nownum == maxnum);
    }

    // postfix

    {
        // test iterator postfix++
        unsigned int nownum = 0;

        for (btree_type::iterator i = map.begin();
             i != map.end(); i++)
        {
            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);

            nownum++;
        }

        die_unless(nownum == maxnum);
    }

    {
        // test iterator postfix--
        unsigned int nownum = maxnum;

        btree_type::iterator i;
        for (i = --map.end(); i != map.begin(); i--)
        {
            nownum--;

            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);
        }

        nownum--;

        die_unless(nownum == i->first);
        die_unless(nownum * 3 == i->second);

        die_unless(nownum == 0);
    }

    {
        // test const_iterator postfix++
        unsigned int nownum = 0;

        for (btree_type::const_iterator i = map.begin();
             i != map.end(); i++)
        {
            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);

            nownum++;
        }

        die_unless(nownum == maxnum);
    }

    {
        // test const_iterator postfix--
        unsigned int nownum = maxnum;

        btree_type::const_iterator i;
        for (i = --map.end(); i != map.begin(); i--)
        {
            nownum--;

            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);
        }

        nownum--;

        die_unless(nownum == i->first);
        die_unless(nownum * 3 == i->second);

        die_unless(nownum == 0);
    }

    {
        // test reverse_iterator postfix++
        unsigned int nownum = maxnum;

        for (btree_type::reverse_iterator i = map.rbegin();
             i != map.rend(); i++)
        {
            nownum--;

            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);
        }

        die_unless(nownum == 0);
    }

    {
        // test reverse_iterator postfix--
        unsigned int nownum = 0;

        btree_type::reverse_iterator i;
        for (i = --map.rend(); i != map.rbegin(); i--)
        {
            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);

            nownum++;
        }

        die_unless(nownum == i->first);
        die_unless(nownum * 3 == i->second);

        nownum++;

        die_unless(nownum == maxnum);
    }

    {
        // test const_reverse_iterator postfix++
        unsigned int nownum = maxnum;

        for (btree_type::const_reverse_iterator i = map.rbegin();
             i != map.rend(); i++)
        {
            nownum--;

            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);
        }

        die_unless(nownum == 0);
    }

    {
        // test const_reverse_iterator postfix--
        unsigned int nownum = 0;

        btree_type::const_reverse_iterator i;
        for (i = --map.rend(); i != map.rbegin(); i--)
        {
            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);

            nownum++;
        }

        die_unless(nownum == i->first);
        die_unless(nownum * 3 == i->second);

        nownum++;

        die_unless(nownum == maxnum);
    }
}

void test_iterator4() {
    typedef tlx::btree_set<
            unsigned int,
            std::less<unsigned int>, traits_nodebug<unsigned int> > btree_type;

    btree_type set;

    unsigned int maxnum = 1000;

    for (unsigned int i = 0; i < maxnum; ++i)
    {
        set.insert(i);
    }

    {
        // test iterator prefix++
        unsigned int nownum = 0;

        for (btree_type::iterator i = set.begin();
             i != set.end(); ++i)
        {
            die_unless(nownum == *i);
            nownum++;
        }

        die_unless(nownum == maxnum);
    }

    {
        // test iterator prefix--
        unsigned int nownum = maxnum;

        btree_type::iterator i;
        for (i = --set.end(); i != set.begin(); --i)
        {
            die_unless(--nownum == *i);
        }

        die_unless(--nownum == *i);

        die_unless(nownum == 0);
    }

    {
        // test const_iterator prefix++
        unsigned int nownum = 0;

        for (btree_type::const_iterator i = set.begin();
             i != set.end(); ++i)
        {
            die_unless(nownum++ == *i);
        }

        die_unless(nownum == maxnum);
    }

    {
        // test const_iterator prefix--
        unsigned int nownum = maxnum;

        btree_type::const_iterator i;
        for (i = --set.end(); i != set.begin(); --i)
        {
            die_unless(--nownum == *i);
        }

        die_unless(--nownum == *i);

        die_unless(nownum == 0);
    }

    {
        // test reverse_iterator prefix++
        unsigned int nownum = maxnum;

        for (btree_type::reverse_iterator i = set.rbegin();
             i != set.rend(); ++i)
        {
            die_unless(--nownum == *i);
        }

        die_unless(nownum == 0);
    }

    {
        // test reverse_iterator prefix--
        unsigned int nownum = 0;

        btree_type::reverse_iterator i;
        for (i = --set.rend(); i != set.rbegin(); --i)
        {
            die_unless(nownum++ == *i);
        }

        die_unless(nownum++ == *i);

        die_unless(nownum == maxnum);
    }

    {
        // test const_reverse_iterator prefix++
        unsigned int nownum = maxnum;

        for (btree_type::const_reverse_iterator i = set.rbegin();
             i != set.rend(); ++i)
        {
            die_unless(--nownum == *i);
        }

        die_unless(nownum == 0);
    }

    {
        // test const_reverse_iterator prefix--
        unsigned int nownum = 0;

        btree_type::const_reverse_iterator i;
        for (i = --set.rend(); i != set.rbegin(); --i)
        {
            die_unless(nownum++ == *i);
        }

        die_unless(nownum++ == *i);

        die_unless(nownum == maxnum);
    }

    // postfix

    {
        // test iterator postfix++
        unsigned int nownum = 0;

        for (btree_type::iterator i = set.begin();
             i != set.end(); i++)
        {
            die_unless(nownum++ == *i);
        }

        die_unless(nownum == maxnum);
    }

    {
        // test iterator postfix--
        unsigned int nownum = maxnum;

        btree_type::iterator i;
        for (i = --set.end(); i != set.begin(); i--)
        {

            die_unless(--nownum == *i);
        }

        die_unless(--nownum == *i);

        die_unless(nownum == 0);
    }

    {
        // test const_iterator postfix++
        unsigned int nownum = 0;

        for (btree_type::const_iterator i = set.begin();
             i != set.end(); i++)
        {
            die_unless(nownum++ == *i);
        }

        die_unless(nownum == maxnum);
    }

    {
        // test const_iterator postfix--
        unsigned int nownum = maxnum;

        btree_type::const_iterator i;
        for (i = --set.end(); i != set.begin(); i--)
        {
            die_unless(--nownum == *i);
        }

        die_unless(--nownum == *i);

        die_unless(nownum == 0);
    }

    {
        // test reverse_iterator postfix++
        unsigned int nownum = maxnum;

        for (btree_type::reverse_iterator i = set.rbegin();
             i != set.rend(); i++)
        {
            die_unless(--nownum == *i);
        }

        die_unless(nownum == 0);
    }

    {
        // test reverse_iterator postfix--
        unsigned int nownum = 0;

        btree_type::reverse_iterator i;
        for (i = --set.rend(); i != set.rbegin(); i--)
        {
            die_unless(nownum++ == *i);
        }

        die_unless(nownum++ == *i);

        die_unless(nownum == maxnum);
    }

    {
        // test const_reverse_iterator postfix++
        unsigned int nownum = maxnum;

        for (btree_type::const_reverse_iterator i = set.rbegin();
             i != set.rend(); i++)
        {
            die_unless(--nownum == *i);
        }

        die_unless(nownum == 0);
    }

    {
        // test const_reverse_iterator postfix--
        unsigned int nownum = 0;

        btree_type::const_reverse_iterator i;
        for (i = --set.rend(); i != set.rbegin(); i--)
        {
            die_unless(nownum++ == *i);
        }

        die_unless(nownum++ == *i);

        die_unless(nownum == maxnum);
    }
}

void test_iterator5() {
    typedef tlx::btree_set<
            unsigned int,
            std::less<unsigned int>, traits_nodebug<unsigned int> > btree_type;

    btree_type set;

    unsigned int maxnum = 100;

    for (unsigned int i = 0; i < maxnum; ++i)
    {
        set.insert(i);
    }

    {
        btree_type::iterator it;

        it = set.begin();
        it--;
        die_unless(it == set.begin());

        it = set.begin();
        --it;
        die_unless(it == set.begin());

        it = set.end();
        it++;
        die_unless(it == set.end());

        it = set.end();
        ++it;
        die_unless(it == set.end());
    }

    {
        btree_type::const_iterator it;

        it = set.begin();
        it--;
        die_unless(it == set.begin());

        it = set.begin();
        --it;
        die_unless(it == set.begin());

        it = set.end();
        it++;
        die_unless(it == set.end());

        it = set.end();
        ++it;
        die_unless(it == set.end());
    }

    {
        btree_type::reverse_iterator it;

        it = set.rbegin();
        it--;
        die_unless(it == set.rbegin());

        it = set.rbegin();
        --it;
        die_unless(it == set.rbegin());

        it = set.rend();
        it++;
        die_unless(it == set.rend());

        it = set.rend();
        ++it;
        die_unless(it == set.rend());
    }

    {
        btree_type::const_reverse_iterator it;

        it = set.rbegin();
        it--;
        die_unless(it == set.rbegin());

        it = set.rbegin();
        --it;
        die_unless(it == set.rbegin());

        it = set.rend();
        it++;
        die_unless(it == set.rend());

        it = set.rend();
        ++it;
        die_unless(it == set.rend());
    }
}

// TODO test_multi
void test_erase_iterator1() {
    if (!test_multi) return;

    typedef tlx::btree_multimap<
            int, int,
            std::less<int>, traits_nodebug<int> > btree_type;

    btree_type map;

    const int size1 = 32;
    const int size2 = 256;

    for (int i = 0; i < size1; ++i)
    {
        for (int j = 0; j < size2; ++j)
        {
            map.insert2(i, j);
        }
    }

    die_unless(map.size() == size1 * size2);

    // erase in reverse order. that should be the worst case for
    // erase_iter()

    for (int i = size1 - 1; i >= 0; --i)
    {
        for (int j = size2 - 1; j >= 0; --j)
        {
            // find iterator
            btree_type::iterator it = map.find(i);

            while (it != map.end() && it->first == i && it->second != j)
                ++it;

            die_unless(it->first == i);
            die_unless(it->second == j);

            size_t mapsize = map.size();
            map.erase(it);
            die_unless(map.size() == mapsize - 1);
        }
    }

    die_unless(map.size() == 0);
}

void test_iterators() {
    test_iterator1();
    test_iterator2();
    test_iterator3();
    test_iterator4();
    test_iterator5();
    test_erase_iterator1();
}

/******************************************************************************/
// Test with Structs

struct TestData {
    unsigned int a, b;

    // required by the btree
    TestData()
        : a(0), b(0)
    { }

    // also used as implicit conversion constructor
    inline TestData(unsigned int _a)
        : a(_a), b(0)
    { }
};

std::ostream& operator<<(std::ostream& os, const TestData& d)
{
    os << '(' << d.a << ',' << d.b << ')';
    return os;
}

struct TestCompare {
    unsigned int somevalue;

    inline TestCompare(unsigned int sv = 0)
        : somevalue(sv)
    { }

    bool operator () (const struct TestData& a, const struct TestData& b) const {
        return a.a > b.a;
    }
};

void test_struct() {
    if (test_multi) {
        typedef tlx::btree_multiset<struct TestData, struct TestCompare,
                                    struct traits_nodebug<struct TestData> > btree_type;

        btree_type bt(TestCompare(42));

        srand(seed);
        for (unsigned int i = 0; i < 320; i++)
        {
            die_unless(bt.size() == i);
            bt.insert(rand() % 100);
            die_unless(bt.size() == i + 1);
        }

        srand(seed);
        for (unsigned int i = 0; i < 320; i++)
        {
            die_unless(bt.size() == 320 - i);
            die_unless(bt.erase_one(rand() % 100));
            die_unless(bt.size() == 320 - i - 1);
        }
    } else {
        // from frozenca's btree tests
        typedef tlx::btree_set<struct TestData, struct TestCompare,
                                    struct traits_nodebug<struct TestData> >
        btree_type;

        btree_type btree(TestCompare(42));

        int n = 320;
        std::mt19937 gen(seed);
        std::vector<int> v(n);

        std::iota(v.begin(), v.end(), 0);
        unsigned long size = 0;

        // random insert
        std::ranges::shuffle(v, gen);
        for (auto num : v) {
            die_unless(btree.insert(num).second);
            die_unless(btree.size() == ++size);
        }

        btree.verify();

        // random lookup
        std::ranges::shuffle(v, gen);
        for (auto num : v) {
          die_unless(btree.exists(num));
        }

        // random erase
        std::ranges::shuffle(v, gen);
        for (auto num : v) {
            bool res = btree.erase(num);
            die_unless(res);
            die_unless(btree.size() == --size);
        }
    }
}

/******************************************************************************/
// Test Relations

void test_relations() {
#if test_multi
    typedef tlx::btree_multiset<
            unsigned int,
            std::less<unsigned int>, traits_nodebug<unsigned int> > btree_type;
#else
    typedef tlx::btree_set<
        unsigned int,
        std::less<unsigned int>, traits_nodebug<unsigned int> > btree_type;
#endif

    btree_type bt1, bt2;

    if (test_multi) {
        srand(seed);
        for (unsigned int i = 0; i < 320; i++)
        {
            unsigned int key = rand() % 1000;

            bt1.insert(key);
            bt2.insert(key);
        }
    } else {
        // from frozenca's btree tests
        btree_type btree;
        int n = 320;

        std::mt19937 gen(seed);
        std::vector<int> v(n);
        std::iota(v.begin(), v.end(), 0);
        std::ranges::shuffle(v, gen);

        for (auto num : v) {
            bt1.insert(num);
            bt2.insert(num);
        }
    }


    die_unless(bt1 == bt2);

    bt1.insert(499);
    bt2.insert(500);

    die_unless(bt1 != bt2);
    die_unless(bt1 < bt2);
    die_unless(!(bt1 > bt2));

    bt1.insert(500);
    bt2.insert(499);

    die_unless(bt1 == bt2);
    die_unless(bt1 <= bt2);

    // test assignment operator
    btree_type bt3;

    bt3 = bt1;
    die_unless(bt1 == bt3);
    die_unless(bt1 >= bt3);

    // test copy constructor
    btree_type bt4 = bt3;

    die_unless(bt1 == bt4);
}

/******************************************************************************/
// Test Bulk Load

void test_bulkload_set_instance(size_t numkeys, unsigned int mod) {
    // TODO this isn't very important rn but implement later please
    if (!test_multi) return;

    typedef tlx::btree_multiset<
            unsigned int,
            std::less<unsigned int>, traits_nodebug<unsigned int> > btree_type;

    std::vector<unsigned int> keys(numkeys);

    srand(seed);
    for (unsigned int i = 0; i < numkeys; i++)
    {
        keys[i] = rand() % mod;
    }

    std::sort(keys.begin(), keys.end());

    btree_type bt;
    bt.bulk_load(keys.begin(), keys.end());

    unsigned int i = 0;
    for (btree_type::iterator it = bt.begin();
         it != bt.end(); ++it, ++i)
    {
        die_unless(*it == keys[i]);
    }
}

void test_bulkload_map_instance(size_t numkeys, unsigned int mod) {
    // TODO see above todo
    if (!test_multi) return;

    typedef tlx::btree_multimap<
            int, std::string,
            std::less<int>, traits_nodebug<int> > btree_type;

    std::vector<std::pair<int, std::string> > pairs(numkeys);

    srand(seed);
    for (unsigned int i = 0; i < numkeys; i++)
    {
        pairs[i].first = rand() % mod;
        pairs[i].second = "key";
    }

    std::sort(pairs.begin(), pairs.end());

    btree_type bt;
    bt.bulk_load(pairs.begin(), pairs.end());

    unsigned int i = 0;
    for (btree_type::iterator it = bt.begin();
         it != bt.end(); ++it, ++i)
    {
        die_unless(*it == pairs[i]);
    }
}

void test_bulkload() {
    for (size_t n = 6; n < 3200; ++n)
        test_bulkload_set_instance(n, 1000);

    test_bulkload_set_instance(31996, 10000);
    test_bulkload_set_instance(32000, 10000);
    test_bulkload_set_instance(117649, 100000);

    for (size_t n = 6; n < 3200; ++n)
        test_bulkload_map_instance(n, 1000);

    test_bulkload_map_instance(31996, 10000);
    test_bulkload_map_instance(32000, 10000);
    test_bulkload_map_instance(117649, 100000);
}

/******************************************************************************/
// Test Multithreading
#if 0
const int Slots = 8;
typedef tlx::btree_set<
    unsigned int,
    std::less<unsigned int>,
    struct tlx::btree_default_traits<
        size_t, size_t,
        Slots * (sizeof(size_t) + sizeof(void*)),
        Slots * sizeof(size_t)>,
    std::allocator<size_t> /* Allocator */,
    true /* concurrent */ > set_type;
#endif

typedef TestType<test_slot_max>::test_set_type set_type;

set_type* g_test_set = nullptr;

#include <tests/container/btree_fast_log.hpp>

int MULTI_THREAD_PASSES = 1000;

size_t g_initial_size = 50;
int g_max_key = 100;
int g_num_operations = 150;

size_t g_big_initial_size = 1000;
int g_big_max_key = 2000;
int g_big_num_operations = 1000;

const size_t NUM_THREADS = 4;
size_t cur_numthreads = NUM_THREADS;

struct scan_stat {
    uint64_t num_total_next_leaf;
    uint64_t num_no_wait_next_leaf;
};
std::vector<scan_stat> scan_stats(NUM_THREADS);

struct Entry {
    std::mutex mtx;
    bool in_set = false;
};

std::vector<Entry> truth_source(g_big_max_key);
bool in_multi_test = false;

void print(const char* op, int val, int id) {
    static int seqnum = 0;
    if (!debug_print) return;
    std::lock_guard<std::mutex> l(printmtx);
    std::cout << seqnum++ << ": thread " << id << " doing " << op
        << " value: " << val << std::endl;
}

void thread_func(int max_key, int num_operations, set_type* my_set,
                 int insert_prop, int lookup_prop, int erase_prob,
                 int scan_length, int id) {
    // TODO std::mt19937 gen(seed + id);
    // std::mt19937 gen(std::random_device{}());
    std::mt19937 gen(seed + id);
    std::uniform_int_distribution<> dist(0, 99);
    std::uniform_int_distribution<> key_dist(0, max_key - 1);
    uint16_t num_total_next_leaf = 0;
    uint16_t num_no_wait_next_leaf = 0;

    initialize_thread_info(id);
    //usleep(10 * 1000 * 1000ull); // sleep for debugging

    for (int i = 0; i < num_operations; ++i) {
        int key = key_dist(gen);
        int operation = dist(gen);

        if (operation < insert_prop)
        {
            std::lock_guard<std::mutex> lock(truth_source[key].mtx);
            print("insert", key, id);
            log_op(OP_INSERT, key, false, my_set->size(), id + thread_start_idx);
            bool succeeded = my_set->insert(key).second;
            log_op(OP_INSERT_DONE, key, succeeded, my_set->size(), id + thread_start_idx);
            die_unless(succeeded != truth_source[key].in_set);
            truth_source[key].in_set = true;

            log_op(OP_FIND, key, false, my_set->size(), id + thread_start_idx);
            bool found = my_set->exists(key);
            log_op(OP_FIND_DONE, key, found, my_set->size(), id + thread_start_idx);
            if (found != truth_source[key].in_set) {
                before_assert();
                std::cout << "Cannot find just inserted key " << key << "\n" << std::flush;
                my_set->print(std::cout);
                exit(1);
            }
        }
        else if (operation < insert_prop + lookup_prop)
        {
            std::lock_guard<std::mutex> lock(truth_source[key].mtx);
            print("find", key, id);
            // using exists because this currently doesn't support iterators
            log_op(OP_FIND, key, false, my_set->size(), id + thread_start_idx);
            bool found = my_set->exists(key);
            log_op(OP_FIND_DONE, key, found, my_set->size(), id + thread_start_idx);
            die_unless(found == truth_source[key].in_set);
        }
        else if (operation < insert_prop + lookup_prop + erase_prob)
        {
            std::lock_guard<std::mutex> lock(truth_source[key].mtx);
            print("erase", key, id);
            log_op(OP_ERASE, key, false, my_set->size(), id + thread_start_idx);
            bool erased = my_set->erase(key);
            log_op(OP_ERASE_DONE, key, erased, my_set->size(), id + thread_start_idx);
            die_unless(erased == truth_source[key].in_set);
            truth_source[key].in_set = false;

            log_op(OP_FIND, key, false, my_set->size(), id + thread_start_idx);
            bool found = my_set->exists(key);
            log_op(OP_FIND_DONE, key, found, my_set->size(), id + thread_start_idx);
            if (found != truth_source[key].in_set) {
                before_assert();
                std::cout << "Found just deleted key " << key << "\n" << std::flush;
                my_set->print(std::cout);
                exit(1);
            }
        }
        else // scan
        {
            int num_set = 0;
            int locked_range_end = key;
            int prev = key - 1;
            int j;

            LOG_STR("scan key=" << key << " thread=" << id);
            for (j = key; j < max_key && num_set < scan_length; ++j) {
                truth_source[j].mtx.lock();
                if (truth_source[j].in_set) {
                    LOG_STR("truth[" << j << "] is in set");
                    ++num_set;
                }
            }
            locked_range_end = j;
            my_set->map_range_length_safe(
                key, scan_length,
                &num_total_next_leaf, &num_no_wait_next_leaf,
                [&prev, &num_set]
                (const set_type::value_type* kv) noexcept {
                    int cur = *kv;
                    for (int k = prev + 1; k < cur; ++k) {
                        die_unless(!truth_source[k].in_set);
                    }
                    die_unless(truth_source[cur].in_set);
                    --num_set;
                    prev = cur;
                });

            die_unless(num_set == 0);
            for (j = key; j < locked_range_end; ++j) {
                truth_source[j].mtx.unlock();
            }
        }
        //std::cout << "After iteration " << i << "\n";
        //my_set->print(std::cout);
        //usleep(10 * 1000 * 1000ull); // sleep for debugging
    }
    scan_stats[id].num_total_next_leaf += num_total_next_leaf;
    scan_stats[id].num_no_wait_next_leaf += num_no_wait_next_leaf;

    cleanup_thread_info();
}

void test_multithread(set_type* my_multi_thread_set,
                      int max_key, int num_operations,
                      size_t initial_size, int num_threads,
                      scan_stat *total_st) {
    in_multi_test = true;
    // Probability out of 100
    int insert_prop = 33;
    int lookup_prop = 0;
    int erase_prob = 33;
    int scan_length = 16;

    std::mt19937 gen(seed);
    std::uniform_int_distribution<> key(0, max_key - 1);

    // Register signal handler for SIGUSR1
    std::signal(SIGUSR1, signal_handler);

    cur_numthreads = num_threads; // for debug printing TODO

    // reset from previous runs
    my_multi_thread_set->clear();
    TLX_BTREE_ASSERT(max_key <= static_cast<int>(truth_source.size()));
    for (int i = 0; i < max_key; ++i) {
         truth_source[i].in_set = false;
    }

    // prepare the set to start with random items
    while (my_multi_thread_set->size() < initial_size) {
        auto k = key(gen) % max_key;
        bool inserted = my_multi_thread_set->insert(k).second;
        die_unless(inserted != truth_source[k].in_set);
        truth_source[k].in_set = true;
    }

    std::vector<std::thread> threads;
    scan_stats.resize(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        scan_stats[i].num_total_next_leaf = 0;
        scan_stats[i].num_no_wait_next_leaf = 0;
        threads.emplace_back(
            thread_func, max_key, num_operations,
            my_multi_thread_set,
            insert_prop, lookup_prop, erase_prob,
            scan_length, i);
        //&scan_stats[i].num_total_next_leaf,
        //   &scan_stats[i].num_no_wait_next_leaf);
    }
    for (auto& th : threads) {
        th.join();
    }

    for (const auto& st: scan_stats) {
        total_st->num_total_next_leaf += st.num_total_next_leaf;
        total_st->num_no_wait_next_leaf += st.num_no_wait_next_leaf;
    }
}

#ifdef NDEBUG

void test_mapl() {}

#else

// Function to trim leading/trailing spaces and empty lines from a string
std::string trim(const std::string& input) {
    std::stringstream ss(input);
    std::string line, result;

    // Process each line
    while (std::getline(ss, line)) {
        // Find the first non-space character (leading space trim)
        size_t start = line.find_first_not_of(' ');

        // Find the last non-space character (trailing space trim)
        size_t end = line.find_last_not_of(' ');

        if (start != std::string::npos && end != std::string::npos) {
            // Append the trimmed line to result
            result += line.substr(start, end - start + 1) + '\n';
        }
    }

    return result;
}

template<int LeafSlotMax>
void verify_mapl(const char *testname,
                 const typename TestType<LeafSlotMax>::test_leaf_type& leaf,
                 const char *expected_c) {
    std::stringstream ss;
    leaf.print_mapl(ss);
    std::string actual = trim(ss.str());
    std::string expected = trim(expected_c);
    if (actual != expected) {
        std::cerr << "Mapl leaf content wrong\nExpected:\n"
                  << expected
                  << "\nActual:\n"
                  << actual;
        TLX_BTREE_ASSERT(false);
    }
    std::cout << "[PASS] " << testname << "\n" << std::flush;
}

#define VERIFY_EQ(a, b)                             \
    {                                               \
        auto res_a = (a);                               \
        if (res_a != (b)) {                             \
            std::cerr << ""#a << "=" << res_a << "\n"   \
                      << "expected: " << (b) << "\n";   \
            TLX_BTREE_ASSERT(false);                    \
        }                                               \
    }

template<int LeafSlotMax>
void set_leaf_data(typename TestType<LeafSlotMax>::test_leaf_type *leaf,
                   const std::vector<short_val_type>& v,
                   bool sorted = false) {
    TLX_BTREE_ASSERT(v.size() < TestType<LeafSlotMax>::test_btree_type::leaf_slotmax);

    // Sort the values if the sorted flag is true
    std::vector<short_val_type> sorted_values = v;  // Create a copy for sorting if necessary
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

template<int LeafSlotMax>
void slice_insert(typename TestType<LeafSlotMax>::test_leaf_type *leaf,
                  short_val_type val) {
    typename TestType<LeafSlotMax>::test_set_type ts;

    const auto& key = TestType<LeafSlotMax>::test_set_type::btree_impl::key_of_value::get(val);
    int slicenum = leaf->mapl->get_slicenum(key);
    auto* slice = leaf->mapl->slices + slicenum;
    int pos = ts.tree_.find_lower(slice, key);
    leaf->mapl->slices[slicenum].lock.write_lock();
    leaf->mapl->slice_insert(slicenum, pos, val);
    leaf->mapl->slices[slicenum].lock.write_unlock();
}

template<int LeafSlotMax>
void slice_erase(typename TestType<LeafSlotMax>::test_leaf_type *leaf,
                 typename TestType<LeafSlotMax>::test_btree_type::key_type key) {
    typename TestType<LeafSlotMax>::test_set_type ts;

    int slicenum = leaf->mapl->get_slicenum(key);
    auto* slice = leaf->mapl->slices + slicenum;
    int pos = ts.tree_.find_lower(slice, key);
    leaf->mapl->slices[slicenum].lock.write_lock();
    leaf->mapl->slice_erase(slicenum, pos);
    leaf->mapl->slices[slicenum].lock.write_unlock();
}

template<int LeafSlotMax>
bool mapl_has_extra() {
    typename TestType<LeafSlotMax>::test_leaf_type leaf(nullptr);
    set_leaf_data<LeafSlotMax>(&leaf, {10, 20, 30, 40, 50, 60});
    typename TestType<LeafSlotMax>::test_set_type my_set;
    leaf.maplize(&my_set.tree_);
    std::cout << "sizeof(leaf.mapl->extra)=" << sizeof(leaf.mapl->extra) << "\n" << std::flush;
    verify_mapl<LeafSlotMax>("maplize", leaf, R"(
    #slices=3
    slice[0]: 0:10 1:20
    slice[1]: 2:30 3:40
    slice[2]: 4:50 5:60
    Boundaries: 0:20 1:40
    Free list: 6 7
    )");

    return false; //sizeof(leaf.mapl->extra) != 0; XXX idk why this doesn't work the way is should
}

template<int LeafSlotMax>
void test_mapl_with_extra() {

    typename TestType<LeafSlotMax>::test_set_type my_set;
    {
        typename TestType<LeafSlotMax>::test_leaf_type leaf(nullptr);
        set_leaf_data<LeafSlotMax>(&leaf, {10, 20, 30, 40, 50, 60});

        leaf.maplize(&my_set.tree_);

        verify_mapl<LeafSlotMax>("aligned", leaf, R"(
#slices=2
slice[0]: 0:10 1:20 2:30
slice[1]: 3:40 4:50 5:60
Boundaries: 0:30
Free list: 6 7 8 9 10 11
)");

        VERIFY_EQ(leaf.mapl->get_slicenum(60), 1);

        // insert key
        slice_insert<LeafSlotMax>(&leaf, 15);
        verify_mapl<LeafSlotMax>("insert 15", leaf, R"(
#slices=2
slice[0]: 0:10 6:15 1:20 2:30
slice[1]: 3:40 4:50 5:60
Boundaries: 0:30
Free list: 7 8 9 10 11
)");

        slice_insert<LeafSlotMax>(&leaf, 35);
        verify_mapl<LeafSlotMax>("insert 35", leaf, R"(
#slices=2
slice[0]: 0:10 6:15 1:20 2:30
slice[1]: 7:35 3:40 4:50 5:60
Boundaries: 0:30
Free list: 8 9 10 11
)");

        slice_insert<LeafSlotMax>(&leaf, 33);
        verify_mapl<LeafSlotMax>("insert 33", leaf, R"(
#slices=2
slice[0]: 0:10 6:15 1:20 2:30
slice[1]: 8:33 7:35 3:40 4:50 5:60
Boundaries: 0:30
Free list: 9 10 11
)");

        slice_insert<LeafSlotMax>(&leaf, 31);
        verify_mapl<LeafSlotMax>("insert 31", leaf, R"(
#slices=2
slice[0]: 0:10 6:15 1:20 2:30
slice[1]: 9:31 8:33 7:35 3:40 4:50 5:60
Boundaries: 0:30
Free list: 10 11
)");
    }

    {
        typename TestType<LeafSlotMax>::test_leaf_type leaf(nullptr);
        set_leaf_data<LeafSlotMax>(&leaf, {10, 20, 30, 40, 50});
        leaf.maplize(&my_set.tree_);

        verify_mapl<LeafSlotMax>("unaligned", leaf, R"(
#slices=2
slice[0]: 0:10 1:20 2:30
slice[1]: 3:40 4:50
Boundaries: 0:30
Free list: 5 6 7 8 9 10 11
)");

        VERIFY_EQ(leaf.mapl->get_slicenum(5), 0);
        VERIFY_EQ(leaf.mapl->get_slicenum(10), 0);
        VERIFY_EQ(leaf.mapl->get_slicenum(30), 0);
        VERIFY_EQ(leaf.mapl->get_slicenum(35), 1);
        VERIFY_EQ(leaf.mapl->get_slicenum(40), 1);
        VERIFY_EQ(leaf.mapl->get_slicenum(80), 1);
    }

    {
        typename TestType<LeafSlotMax>::test_leaf_type leaf(nullptr);
        set_leaf_data<LeafSlotMax>(&leaf, {10, 20, 30, 40, 50, 60});
        leaf.maplize(&my_set.tree_);

        // bc i messed up writing the tests
        slice_insert<LeafSlotMax>(&leaf, 15);
        //leaf.print_mapl(std::cout);

        // erase key
        slice_erase<LeafSlotMax>(&leaf, 40);
        verify_mapl<LeafSlotMax>("erase 40", leaf, R"(
#slices=2
slice[0]: 0:10 6:15 1:20 2:30
slice[1]: 4:50 5:60
Boundaries: 0:30
Free list: 3 7 8 9 10 11
)");

        slice_erase<LeafSlotMax>(&leaf, 15);
        verify_mapl<LeafSlotMax>("erase 15", leaf, R"(
#slices=2
slice[0]: 0:10 1:20 2:30
slice[1]: 4:50 5:60
Boundaries: 0:30
Free list: 6 3 7 8 9 10 11
)");

        slice_erase<LeafSlotMax>(&leaf, 60);
        verify_mapl<LeafSlotMax>("erase 60", leaf, R"(
#slices=2
slice[0]: 0:10 1:20 2:30
slice[1]: 4:50
Boundaries: 0:30
Free list: 5 6 3 7 8 9 10 11
)");

        slice_erase<LeafSlotMax>(&leaf, 50);
        verify_mapl<LeafSlotMax>("erase 50", leaf, R"(
#slices=2
slice[0]: 0:10 1:20 2:30
slice[1]:
Boundaries: 0:30
Free list: 4 5 6 3 7 8 9 10 11
)");
    }

    std::cout << "[PASS] " << __func__ << "()\n";
}

template<int LeafSlotMax>
void test_mapl_without_extra() { // array 'extra' is empty

    typename TestType<LeafSlotMax>::test_set_type my_set;
    {
        typename TestType<LeafSlotMax>::test_leaf_type leaf(nullptr);
        set_leaf_data<LeafSlotMax>(&leaf, {10, 20, 30, 40, 50, 60});
        leaf.maplize(&my_set.tree_);

        verify_mapl<LeafSlotMax>("maplize", leaf, R"(
#slices=3
slice[0]: 0:10 1:20
slice[1]: 2:30 3:40
slice[2]: 4:50 5:60
Boundaries: 0:20 1:40
Free list: 6 7
)");

        VERIFY_EQ(leaf.mapl->get_slicenum(60), 2);

        // insert key
        slice_insert<LeafSlotMax>(&leaf, 15);
        verify_mapl<LeafSlotMax>("insert 15", leaf, R"(
#slices=3
slice[0]: 0:10 6:15 1:20
slice[1]: 2:30 3:40
slice[2]: 4:50 5:60
Boundaries: 0:20 1:40
Free list: 7
)");

        slice_insert<LeafSlotMax>(&leaf, 35);
        verify_mapl<LeafSlotMax>("insert 35", leaf, R"(
#slices=3
slice[0]: 0:10 6:15 1:20
slice[1]: 2:30 7:35 3:40
slice[2]: 4:50 5:60
Boundaries: 0:20 1:40
Free list:
)");
    }

    {
        typename TestType<LeafSlotMax>::test_leaf_type leaf(nullptr);
        set_leaf_data<LeafSlotMax>(&leaf, {10, 20, 30, 40, 50});
        leaf.maplize(&my_set.tree_);

        verify_mapl<LeafSlotMax>("maplize", leaf, R"(
#slices=3
slice[0]: 0:10 1:20
slice[1]: 2:30 3:40
slice[2]: 4:50
Boundaries: 0:20 1:40
Free list: 5 6 7
)");

        VERIFY_EQ(leaf.mapl->get_slicenum(5), 0);
        VERIFY_EQ(leaf.mapl->get_slicenum(10), 0);
        VERIFY_EQ(leaf.mapl->get_slicenum(30), 1);
        VERIFY_EQ(leaf.mapl->get_slicenum(35), 1);
        VERIFY_EQ(leaf.mapl->get_slicenum(40), 1);
        VERIFY_EQ(leaf.mapl->get_slicenum(80), 2);
    }

    {
        typename TestType<LeafSlotMax>::test_leaf_type leaf(nullptr);
        set_leaf_data<LeafSlotMax>(&leaf, {10, 20, 30, 40, 50, 60});
        leaf.maplize(&my_set.tree_);
        verify_mapl<LeafSlotMax>("maplize", leaf, R"(
#slices=3
slice[0]: 0:10 1:20
slice[1]: 2:30 3:40
slice[2]: 4:50 5:60
Boundaries: 0:20 1:40
Free list: 6 7
)");

        // bc i messed up writing the tests
        slice_insert<LeafSlotMax>(&leaf, 15);
        verify_mapl<LeafSlotMax>("insert 15", leaf, R"(
#slices=3
slice[0]: 0:10 6:15 1:20
slice[1]: 2:30 3:40
slice[2]: 4:50 5:60
Boundaries: 0:20 1:40
Free list: 7
)");
        //leaf.print_mapl(std::cout);

        // erase key
        slice_erase<LeafSlotMax>(&leaf, 40);
        verify_mapl<LeafSlotMax>("erase 40", leaf, R"(
#slices=3
slice[0]: 0:10 6:15 1:20
slice[1]: 2:30
slice[2]: 4:50 5:60
Boundaries: 0:20 1:40
Free list: 3 7
)");

        slice_erase<LeafSlotMax>(&leaf, 15);
        verify_mapl<LeafSlotMax>("erase 15", leaf, R"(
#slices=3
slice[0]: 0:10 1:20
slice[1]: 2:30
slice[2]: 4:50 5:60
Boundaries: 0:20 1:40
Free list: 6 3 7
)");

        slice_erase<LeafSlotMax>(&leaf, 60);
        verify_mapl<LeafSlotMax>("erase 60", leaf, R"(
#slices=3
slice[0]: 0:10 1:20
slice[1]: 2:30
slice[2]: 4:50
Boundaries: 0:20 1:40
Free list: 5 6 3 7
)");

        slice_erase<LeafSlotMax>(&leaf, 50);
        verify_mapl<LeafSlotMax>("erase 50", leaf, R"(
#slices=3
slice[0]: 0:10 1:20
slice[1]: 2:30
slice[2]:
Boundaries: 0:20 1:40
Free list: 4 5 6 3 7
)");
    }

    {
        typename TestType<LeafSlotMax>::test_leaf_type leaf(nullptr);
        set_leaf_data<LeafSlotMax>(&leaf, {10, 20, 30, 40, 50});
        leaf.maplize(&my_set.tree_);
        leaf.mutex_.write_lock();
        leaf.mapl->rebalance();

        verify_mapl<LeafSlotMax>("rebalance on balanced", leaf, R"(
#slices=3
slice[0]: 0:10 1:20
slice[1]: 2:30 3:40
slice[2]: 4:50
Boundaries: 0:20 1:40
Free list: 5 6 7
)");

        slice_insert<LeafSlotMax>(&leaf, 35);
        leaf.mapl->rebalance();

        verify_mapl<LeafSlotMax>("rebalance unbalanced", leaf, R"(
#slices=3
slice[0]: 0:10 1:20
slice[1]: 2:30 5:35
slice[2]: 3:40 4:50
Boundaries: 0:20 1:35
Free list: 6 7
)");

        slice_insert<LeafSlotMax>(&leaf, 41);
        slice_insert<LeafSlotMax>(&leaf, 5);
        leaf.mapl->rebalance();

        verify_mapl<LeafSlotMax>("rebalance unbalanced", leaf, R"(
#slices=3
slice[0]: 7:5 0:10 1:20
slice[1]: 2:30 5:35 3:40
slice[2]: 6:41 4:50
Boundaries: 0:20 1:40
Free list:
)");
    }

    std::cout << "[PASS] " << __func__ << "()\n";
}

void test_mapl() {
    if (mapl_has_extra<test_slot_max>()) {
        test_mapl_with_extra<test_slot_max>();
    }
    else {
        test_mapl_without_extra<test_slot_max>();
    }
}

#endif

int main() {
    std::cout << "seed: " << seed << std::endl;
    std::cout << "pid: " << getpid() << std::endl;

    /*
    test_simple();
    if (tlx_more_tests) {
        test_large();
        // TODO test_large_sequence();
        test_bounds();
        test_iterators();
        test_struct();
        test_relations();
        test_bulkload();
    }
    // */

    test_mapl();

    if (multithread) {
        int total_passes = MULTI_THREAD_PASSES;
        double ts_start = tlx::timestamp();
        bool one_line = false;
        int prt_interval = 50;
        size_t initial_size;
        int num_threads;
        int max_key;
        int num_operations;
        scan_stat total_scan_stat = scan_stat();

        // always test some single thread cases first as sanity test
        int single_thread_passes =
            std::min(1000, std::max(1, total_passes / 100));

        for (int i = 0; i < total_passes; i++) {
            set_type* my_multi_thread_set = new set_type;
            g_test_set = my_multi_thread_set;

            switch (i % 3) {
            case 0: // test empty tree
                initial_size = 0;
                max_key = g_max_key;
                num_operations = g_num_operations;
                break;
            case 1: // test tall tree
                initial_size = g_initial_size;
                max_key = g_max_key;
                num_operations = g_num_operations;
                break;
            case 2: // test very big tree
                initial_size = g_big_initial_size;
                max_key = g_big_max_key;
                num_operations = g_big_num_operations;
                break;
            }
            num_threads = (i < single_thread_passes) ? 1 : NUM_THREADS;
            test_multithread(my_multi_thread_set, max_key, num_operations,
                             initial_size, num_threads,
                             &total_scan_stat);
            debug_log_info.resize(0);
            debug_log_info.resize(TOTAL_DEBUG_LOG_INFO);

            if (i != 0 && i % prt_interval == 0) {
                double ts_now = tlx::timestamp();
                double prop = 100.0 * i / total_passes;
                int sec = int(ts_now - ts_start + 0.5);
                int sec_left = sec / (prop / 100.0) - sec;
                if (one_line) {
                   std::cout << "\r";
                }
                auto stats = my_multi_thread_set->get_stats();
                std::cout << std::setfill('0') << std::setw(2) << sec / 60 << ':'
                          << std::setfill('0') << std::setw(2) << sec % 60
                          <<" " << i << "/" << total_passes
                          << "  " << int(prop + 0.5)
                          << "%  time left: "
                          << std::setfill('0') << std::setw(2) << sec_left / 60 << ':'
                          << std::setfill('0') << std::setw(2) << sec_left % 60
                          << " mapl-leaf: " << stats->mapl_leaves << "-" << stats->leaves
                          << " write mapl-write leaf: " << stats->write_mapl.get() << "-"
                          << stats->write_leaf.get()
                          << " read mapl-read leaf: " << stats->read_mapl.get() << "-"
                          << stats->read_leaf.get();

                if (one_line) {
                   std::cout << std::flush;
                } else {
                   std::cout << std::endl;
                }
            }
            delete my_multi_thread_set;
        }
        std::cout << std::endl;

        uint64_t total = total_scan_stat.num_total_next_leaf;
        uint64_t no_wait = total_scan_stat.num_no_wait_next_leaf;
        double prop = (total - no_wait) * 100.0 / total;
        std::cout << "total new leaf scanned: " << total
                  << " percentage of waiting: " << std::setprecision(3)
                  << prop << "%\n";
    }
    std::cout << "test successful!" << std::endl;
    return 0;
}

/******************************************************************************/
