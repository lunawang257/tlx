/*******************************************************************************
 * tlx/container/slow_lock_btree.hpp
 *
 * Part of tlx - http://panthema.net/tlx
 *
 * Copyright (C) 2008-2017 Timo Bingmann <tb@panthema.net>
 *
 * All rights reserved. Published under the Boost Software License, Version 1.0
 ******************************************************************************/

#ifndef TLX_CONTAINER_SLOW_LOCK_BTREE_HEADER
#define TLX_CONTAINER_SLOW_LOCK_BTREE_HEADER

#include <tlx/die/core.hpp>

// *** Required Headers from the STL

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <pthread.h>
#include <istream>
#include <memory>
#include <ostream>
#include <utility>
#include <iostream>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

#include <tlx/container/ParallelTools/parallel.h>
#include <tlx/container/ParallelTools/reducer.h>
#include <tlx/container/ParallelTools/Lock.hpp>

#include <tlx/container/btree.hpp>
#include <lock_type.hpp>

//#define STD_LOCK
#define FAST_LOCK
//#define DUMMY_LOCK
//#define BUSY_SPIN_LOCK
//#define HYBRID_SPIN_LOCK
//#define BUSY_WAIT_LOCK
//#define HYBRID_LOCK

extern std::string format_current_time(void);

enum { MAX_CPU = 6 }; // 6

enum { MAX_SPIN = 200 };

// DummyLock class to do nothing, for single-threaded case
class DummyLock {
public:
    void lock() {}
    void unlock() {}
};

// DummyCondVar class to do nothing, for single-threaded case
class DummyCondVar {
public:
    // Wait function with predicate
    template <class Pred>
    void wait(std::unique_lock<DummyLock>&, Pred) {}
    void notify_one() {}
    void notify_all() {}
};

class HybridSpinLock {
public:
    HybridSpinLock() : flag_(false) {}  // Initialize the atomic flag

    void lock() {
        int i = 0;
        while (flag_.test_and_set(std::memory_order_acquire)) {  // Attempt to set the flag
            if (++i >= MAX_SPIN) {
                std::this_thread::yield();
            }
        }
    }

    void unlock() {
        flag_.clear(std::memory_order_release);  // Clear the flag to release the lock
    }

private:
    std::atomic_flag flag_;  // Atomic flag used for CAS operation
};

class HybridSpinCondVar {
public:
    // Wait function with predicate
    template <class Pred>
    void wait(std::unique_lock<HybridSpinLock>& lock, Pred pred) {
        int i = 0;
        while (true) { // Evaluate predicate
            lock.unlock();
            if (++i >= MAX_SPIN) {
                std::this_thread::yield();
            }
            lock.lock();
            if (pred()) {
                break;
            }
        }
    }
    void notify_one() {}
    void notify_all() {}
};

class BusySpinLock {
public:
    BusySpinLock() : flag_(false) {}  // Initialize the atomic flag

    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) {  // Attempt to set the flag
        }
    }

    void unlock() {
        flag_.clear(std::memory_order_release);  // Clear the flag to release the lock
    }

private:
    std::atomic_flag flag_;  // Atomic flag used for CAS operation
};

class BusySpinCondVar {
public:
    // Wait function with predicate
    template <class Pred>
    void wait(std::unique_lock<BusySpinLock>& lock, Pred pred) {
        while (true) { // Evaluate predicate
            lock.unlock();
            lock.lock();
            if (pred()) {
                break;
            }
        }
    }
    void notify_one() {}
    void notify_all() {}
};

// BusyWait lock and cond var
class BusyWaitLock {
public:
    BusyWaitLock() {
        pthread_mutex_init(&mutex_, nullptr);  // Initialize the mutex
    }

    ~BusyWaitLock() {
        pthread_mutex_destroy(&mutex_);  // Destroy the mutex
    }

    void lock() {
        while (pthread_mutex_trylock(&mutex_) != 0) {  // Attempt to lock the mutex
        }
    }

    void unlock() {
        pthread_mutex_unlock(&mutex_);  // Release the mutex
    }
private:
    pthread_mutex_t mutex_;  // Mutex object
};

class BusyWaitCondVar {
public:
    // Wait function with predicate
    template <class Pred>
    void wait(std::unique_lock<BusyWaitLock>& lock, Pred pred) {
        while (true) { // Evaluate predicate
            lock.unlock();
            lock.lock();
            if (pred()) {
                break;
            }
        }
    }
    void notify_one() {}
    void notify_all() {}
};

// HybridCondVar class is a hybrid of BusyWaitLock and FastLock
class HybridLock {
public:
    HybridLock() {
        pthread_mutex_init(&mutex_, nullptr);
    }

    ~HybridLock() {
        pthread_mutex_destroy(&mutex_);
    }

    void lock() {
        int i = 0;
        while (pthread_mutex_trylock(&mutex_) != 0) {  // Attempt to lock the mutex
            ++i;
            if (i >= MAX_SPIN) {
                pthread_mutex_lock(&mutex_);
                break;
            }
        }
    }

    void unlock() {
        pthread_mutex_unlock(&mutex_);
    }

    pthread_mutex_t* native_handle() {
        return &mutex_;
    }

private:
    pthread_mutex_t mutex_;
};

// HybridCondVar class is a hybrid of BusyWaitCondVar and FastCondVar
class HybridCondVar {
public:
    HybridCondVar() {
        pthread_cond_init(&cond_, nullptr);
    }

    ~HybridCondVar() {
        pthread_cond_destroy(&cond_);
    }

    // Wait function with predicate
    template <class Pred>
    void wait(std::unique_lock<HybridLock>& lock, Pred pred) {
        int i = 0;
        while (true) {
            lock.unlock();
            lock.lock();
            if (pred()) {
                break;
            }
            ++i;
            if (i >= MAX_SPIN) {
                while (!pred()) { // Evaluate predicate
                    pthread_cond_wait(&cond_, lock.mutex()->native_handle());
                }
                break;
            }
        }
    }

    void notify_one() {
        pthread_cond_signal(&cond_);
    }

    void notify_all() {
        pthread_cond_broadcast(&cond_);
    }

private:
    pthread_cond_t cond_;
};

// FastLock class to wrap pthread_mutex_t
class FastLock {
public:
    FastLock() {
        pthread_mutex_init(&mutex_, nullptr);
    }

    ~FastLock() {
        pthread_mutex_destroy(&mutex_);
    }

    void lock() {
        pthread_mutex_lock(&mutex_);
    }

    void unlock() {
        pthread_mutex_unlock(&mutex_);
    }

    pthread_mutex_t* native_handle() {
        return &mutex_;
    }

private:
    pthread_mutex_t mutex_;
};

// FastCondVar class to mimic std::condition_variable with pthread_cond_t
class FastCondVar {
public:
    FastCondVar() {
        pthread_cond_init(&cond_, nullptr);
    }

    ~FastCondVar() {
        pthread_cond_destroy(&cond_);
    }

    // Wait function with predicate
    template <class Pred>
    void wait(std::unique_lock<FastLock>& lock, Pred pred) {
        while (!pred()) { // Evaluate predicate
            pthread_cond_wait(&cond_, lock.mutex()->native_handle());
        }
    }

    template <typename Predicate>
    bool wait_for(std::unique_lock<FastLock>& lock,
                  std::chrono::microseconds duration,
                  Predicate pred) {
        auto now = std::chrono::system_clock::now();
        auto timeout_time = now + duration;

        timespec ts;
        ts.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(timeout_time.time_since_epoch()).count();
        ts.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout_time.time_since_epoch() % std::chrono::seconds(1)).count();

        while (!pred()) {
            int res = pthread_cond_timedwait(&cond_, lock.mutex()->native_handle(), &ts);
            if (res == ETIMEDOUT) {
                return pred();
            }
        }
        return true;
    }

    void notify_one() {
        pthread_cond_signal(&cond_);
    }

    void notify_all() {
        pthread_cond_broadcast(&cond_);
    }

private:
    pthread_cond_t cond_;
};

extern size_t cur_numthreads;

namespace tlx {

//! \addtogroup tlx_container
//! \{
//! \defgroup tlx_container_btree B+ Trees
//! B+ tree variants
//! \{

#define TAKE_LOCK(isleaf, node, func)                                   \
    do {                                                                \
        if (isleaf) {                                                   \
            LeafNode* child_leaf = static_cast<LeafNode*>(node);        \
            child_leaf->lock->func();                                   \
        } else {                                                        \
            InnerNode* child_inner = static_cast<InnerNode*>(node);     \
            child_inner->lock->func();                                  \
        }                                                               \
    } while (0);

/*!
 * Generates default traits for a B+ tree used as a set or map. It estimates
 * leaf and inner node sizes by assuming a cache line multiple of 256 bytes.
*/
template <typename Key, typename Value>
struct slbtree_default_traits {
    //! If true, the tree will self verify its invariants after each insert() or
    //! erase(). The header must have been compiled with TLX_BTREE_DEBUG
    //! defined.
    static const bool self_verify = false;

    //! If true, the tree will print out debug information and a tree dump
    //! during insert() or erase() operation. The header must have been
    //! compiled with TLX_BTREE_DEBUG defined and key_type must be std::ostream
    //! printable.
    static const bool debug = false;

    //! Number of slots in each leaf of the tree. Estimated so that each node
    //! has a size of about 256 bytes.
    static const int leaf_slots =
        TLX_BTREE_MAX(8, 256 / (sizeof(Value)));

    //! Number of slots in each inner node of the tree. Estimated so that each
    //! node has a size of about 256 bytes.
    static const int inner_slots =
        TLX_BTREE_MAX(8, 256 / (sizeof(Key) + sizeof(void*)));

    //! As of stx-btree-0.9, the code does linear search in find_lower() and
    //! find_upper() instead of binary_search, unless the node size is larger
    //! than this threshold. See notes at
    //! http://panthema.net/2013/0504-STX-B+Tree-Binary-vs-Linear-Search
    static const size_t binsearch_threshold = 256;
};

/*!
 * Basic class implementing a B+ tree data structure in memory.
 *
 * The base implementation of an in-memory B+ tree. It is based on the
 * implementation in Cormen's Introduction into Algorithms, Jan Jannink's paper
 * and other algorithm resources. Almost all STL-required function calls are
 * implemented. The asymptotic time requirements of the STL are not always
 * fulfilled in theory, however, in practice this B+ tree performs better than a
 * red-black tree and almost always uses less memory. The insertion function
 * splits the nodes on the recursion unroll. Erase is largely based on Jannink's
 * ideas.
 *
 * This class is specialized into btree_set, btree_multiset, btree_map and
 * btree_multimap using default template parameters and facade functions.
 */
template <typename Key, typename Value,
          typename KeyOfValue,
          typename Compare = std::less<Key>,
          typename Traits = slbtree_default_traits<Key, Value>,
          bool Duplicates = false,
          typename Allocator = std::allocator<Value>,
#if defined(STD_LOCK)
          typename Mutex = std::mutex,
          typename ConditionVariable = std::condition_variable,
#elif defined(FAST_LOCK)
          typename Mutex = FastLock,
          typename ConditionVariable = FastCondVar,
#elif defined(DUMMY_LOCK)
          typename Mutex = DummyLock,
          typename ConditionVariable = DummyCondVar,
#elif defined(BUSY_SPIN_LOCK)
          typename Mutex = BusySpinLock,
          typename ConditionVariable = BusySpinCondVar,
#elif defined(HYBRID_SPIN_LOCK)
          typename Mutex = HybridSpinLock,
          typename ConditionVariable = HybridSpinCondVar,
#elif defined(BUSY_WAIT_LOCK)
          typename Mutex = BusyWaitLock,
          typename ConditionVariable = BusyWaitCondVar,
#elif defined(HYBRID_LOCK)
          typename Mutex = HybridLock,
          typename ConditionVariable = HybridCondVar,
#else
#error "Must define one of STD_LOCK, FAST_LOCK, or DUMMY_LOCK"
#endif
          typename UniqueLock = std::unique_lock<Mutex>>
class SLBTree
{
public:
    //! \name Template Parameter Types
    //! \{

    //! First template parameter: The key type of the B+ tree. This is stored in
    //! inner nodes.
    typedef Key key_type;

    //! Second template parameter: Composition pair of key and data types, or
    //! just the key for set containers. This data type is stored in the leaves.
    typedef Value value_type;

    //! Third template: key extractor class to pull key_type from value_type.
    typedef KeyOfValue key_of_value;

    //! Fourth template parameter: key_type comparison function object
    typedef Compare key_compare;

    //! Fifth template parameter: Traits object used to define more parameters
    //! of the B+ tree
    typedef Traits traits;

    // TODO desc
    typedef Mutex mutex_type;

    // TODO desc
    typedef ConditionVariable cv_type;

    typedef UniqueLock lock_type;

    //! Sixth template parameter: Allow duplicate keys in the B+ tree. Used to
    //! implement multiset and multimap.
    static const bool allow_duplicates = Duplicates;

    //! Seventh template parameter: STL allocator for tree nodes
    typedef Allocator allocator_type;

    //! \}

    // The macro TLX_BTREE_FRIENDS can be used by outside class to access the B+
    // tree internals. This was added for wxBTreeDemo to be able to draw the
    // tree.
    TLX_BTREE_FRIENDS;

public:
    //! \name Constructed Types
    //! \{

    //! Typedef of our own type
    typedef SLBTree<key_type, value_type, key_of_value, key_compare,
                  traits, allow_duplicates, allocator_type, mutex_type,
                  cv_type, lock_type> Self;

    //! Size type used to count keys
    typedef size_t size_type;

    //! \}

public:
    //! \name Static Constant Options and Values of the B+ Tree
    //! \{

    //! Base B+ tree parameter: The number of key/data slots in each leaf
    static const unsigned short leaf_slotmax = traits::leaf_slots;

    //! Base B+ tree parameter: The number of key slots in each inner node,
    //! this can differ from slots in each leaf.
    static const unsigned short inner_slotmax = traits::inner_slots;

    //! Computed B+ tree parameter: The minimum number of key/data slots used
    //! in a leaf. If fewer slots are used, the leaf will be merged or slots
    //! shifted from it's siblings.
    static const unsigned short leaf_slotmin = (leaf_slotmax / 2);

    //! Computed B+ tree parameter: The minimum number of key slots used
    //! in an inner node. If fewer slots are used, the inner node will be
    //! merged or slots shifted from it's siblings.
    static const unsigned short inner_slotmin = (inner_slotmax / 2);

    //! Debug parameter: Enables expensive and thorough checking of the B+ tree
    //! invariants after each insert/erase operation.
    static const bool self_verify = traits::self_verify;

    //! Debug parameter: Prints out lots of debug information about how the
    //! algorithms change the tree. Requires the header file to be compiled
    //! with TLX_BTREE_DEBUG and the key type must be std::ostream printable.
    static const bool debug = traits::debug;

    //! \}
private:
public: // XXX
    struct node;

private:
#ifdef TLX_BTREE_DEBUG

    void verify_one_node(const node* n) const {
        if (root_->level == 0) {
            tlx_die_unless(root_->slotuse == 0);
            return;
        }

        if (n->is_leafnode())
        {
            const LeafNode* leaf = static_cast<const LeafNode*>(n);

            tlx_die_unless(root_->level <= 1 || leaf->slotuse >= leaf_slotmin - 1);
            //tlx_die_unless(leaf->slotuse > 0);

            for (unsigned short slot = 0; slot < leaf->slotuse - 1; ++slot)
            {
                tlx_die_unless(
                    key_lessequal(leaf->key(slot), leaf->key(slot + 1)));
            }
        }
        else // !n->is_leafnode()
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);

            tlx_die_unless(inner == root_ || inner->slotuse >= inner_slotmin - 1);

            for (unsigned short slot = 0; slot < inner->slotuse - 1; ++slot)
            {
                tlx_die_unless(
                    key_lessequal(inner->key(slot), inner->key(slot + 1)));
            }
        }
    }

#else
    void verify_one_node(const node* n __attribute__((unused))) const { }
#endif

public:
    //! \name Lock Helper Struct
    //! \{

    struct InnerLockHelper {
        // TODO desc

        // true only if write-locked, or has writer/upgrader waiting
        std::atomic_flag check_writer{false};

        mutex_type mutex;
        cv_type readcv;
        cv_type writecv;
        cv_type upgradecv;
        partitioned_counter<MAX_CPU> numreader{};
        bool haswriter = false;
        int writerswaiting = 0;
        std::atomic<int> readerswaiting = 0;
        int upgradewaiting = 0;

        SLBTree *treep;
        node *nodep;

#ifdef TLX_BTREE_DEBUG
        std::unordered_set<std::thread::id> curwrite;
        std::unordered_set<std::thread::id> curread;
        mutex_type set_mtx;

        void addtoread() {
            lock_type lock(set_mtx);
            std::thread::id cur = std::this_thread::get_id();
            TLX_BTREE_ASSERT(!curread.contains(cur));
            curread.insert(cur);
        }

        void delfromread(bool verify) {
            lock_type lock(set_mtx);
            std::thread::id cur = std::this_thread::get_id();
            TLX_BTREE_ASSERT(curread.contains(cur));
            if (verify) treep->verify_one_node(nodep);
            curread.erase(cur);
        }

        void addtowrite() {
            lock_type lock(set_mtx);
            std::thread::id cur = std::this_thread::get_id();
            TLX_BTREE_ASSERT(!curwrite.contains(cur));
            curwrite.insert(cur);
        }

        void delfromwrite(bool verify) {
            lock_type lock(set_mtx);
            std::thread::id cur = std::this_thread::get_id();
            TLX_BTREE_ASSERT(curwrite.contains(cur));
            if (verify) treep->verify_one_node(nodep);
            curwrite.erase(cur);
        }

        bool readlocked() {
            lock_type lock(set_mtx);
            return curread.contains(std::this_thread::get_id());
        }

        bool writelocked() {
            lock_type lock(set_mtx);
            return curwrite.contains(std::this_thread::get_id());
        }

#define DBGPRT() \
        if (false && debug_print && nodep->level == 0) { \
            std::lock_guard<std::mutex> printlock(printmtx); \
            std::cout << __func__ << " " << seq++ << ": node=" << nodep << " "; \
            print_node(std::cout, nodep); \
        }

#else
        void addtoread() {}
        void delfromread(bool verify __attribute__((unused))) {}
        void addtowrite() {}
        void delfromwrite(bool verify __attribute__((unused))) {}

#define DBGPRT()
#endif

        bool take_lock() {
            bool is_root = treep->root_ == nodep;

            switch (treep->lock_req) {
            case lock_all: return true;
            case lock_root_only: return is_root;
            case lock_no_root_only: return !is_root;
            case lock_none: return false;
            default: tlx_die_unless(false); return true;
            }
        }

        void readlock(bool verify __attribute__((unused)) = true) {
            if (!take_lock()) {
                numreader.add(1, local_thread_id);
                return;
            }
            log_lock(nodep, lock_type_read, -1);

            bool added_ref = false;
            addtoread();
            if (!check_writer.test(std::memory_order_acq_rel)) { // no writers
                // race: writer may come in now, must re-check check_writer later
                numreader.add(1, local_thread_id);
                added_ref = true;
            }

            // check again to avoid the race above
            if (check_writer.test(std::memory_order_acq_rel)) {
                lock_type lock(mutex);
                if (added_ref) {
                    numreader.add(-1, local_thread_id);
                    added_ref = false;
                    int64_t reader_count = numreader.get();
                    TLX_BTREE_ASSERT(reader_count >= 0);
                    if (reader_count == 0) {
                        if (upgradewaiting > 0) {
                            log_lock(nodep, lock_type_read_notify_upgrader, -1);
                            upgradecv.notify_one();
                        } else if (writerswaiting > 0) {
                            log_lock(nodep, lock_type_read_notify_writer, -1);
                            writecv.notify_one();
                        }
                    }
                }
                if (haswriter || writerswaiting > 0
                    || upgradewaiting > 0) {
                    readerswaiting++;
                    log_lock(nodep, lock_type_read_wait, -1);
                    readcv.wait(lock, [this](){
                        return !this->haswriter
                            && this->writerswaiting == 0
                            && this->upgradewaiting == 0;
                    });
                    readerswaiting--;
                }
                numreader.add(1, local_thread_id);
                added_ref = true;
            }
            if (!added_ref) {
                numreader.add(1, local_thread_id);
            }

            VERIFY_NODE(verify, treep, nodep);
            TLX_BTREE_ASSERT(numreader.get() > 0);
            log_lock(nodep, lock_type_read_got, -1);
            DBGPRT();
        }

        void upgradelock() {
            log_lock(nodep, lock_type_upgrade, -1);

            // stops future readers
            check_writer.test_and_set(std::memory_order_release);

            lock_type lock(mutex);
            delfromread(false);
            addtowrite();

            numreader.add(-1, local_thread_id);
            while (true) {
                int64_t reader_count = numreader.get();
                TLX_BTREE_ASSERT(reader_count >= 0);
                if (reader_count == 0 && !haswriter) break;
                upgradewaiting++;
                log_lock(nodep, lock_type_upgrade_wait, -1);
                upgradecv.wait_for(lock, std::chrono::microseconds(1), [this]() {
                    int64_t reader_count = numreader.get();
                    TLX_BTREE_ASSERT(reader_count >= 0);
                    return !this->haswriter && reader_count == 0;
                });
                upgradewaiting--;
            }
            haswriter = true;
            log_lock(nodep, lock_type_upgrade_got, -1);
            DBGPRT();
        }

        void writelock(bool verify __attribute__((unused)) = true) {
            log_lock(nodep, lock_type_write, -1);

            // stops future readers
            check_writer.test_and_set(std::memory_order_release);

            lock_type lock(mutex);
            addtowrite();
            while (true) {
                int64_t reader_count = numreader.get();
                TLX_BTREE_ASSERT(reader_count >= 0);
                if (!haswriter && upgradewaiting <= 0 && reader_count == 0) break;
                writerswaiting++;
                writecv.wait_for(lock, std::chrono::microseconds(1), [this](){
                    int64_t reader_count = numreader.get();
                    TLX_BTREE_ASSERT(reader_count >= 0);
                    return !this->haswriter &&
                        this->upgradewaiting == 0 &&
                        reader_count == 0;
                });
                writerswaiting--;
            }
            TLX_BTREE_ASSERT(!haswriter);
            haswriter = true;
            VERIFY_NODE(verify, treep, nodep);
            log_lock(nodep, lock_type_write_got, -1);
            DBGPRT();
        }

        void read_unlock(bool verify __attribute__((unused)) = true) {
            if (!take_lock()) {
                numreader.add(-1, local_thread_id);
                return;
            }
            log_lock(nodep, lock_type_read_unlock, -1);

            delfromread(verify);

            if (check_writer.test(std::memory_order_acquire)) {
                lock_type lock(mutex);
                int64_t reader_count = numreader.get();
                TLX_BTREE_ASSERT(reader_count >= 0);

                if (reader_count == 1 /* use 1 because self count not decremented yet */) {
                    if (upgradewaiting > 0) {
                        log_lock(nodep, lock_type_read_unlock_notify_upgrader,
                                 -1);
                        upgradecv.notify_one();
                    } else if (writerswaiting > 0) {
                        log_lock(nodep, lock_type_read_unlock_notify_writer,
                                 -1);
                        writecv.notify_one();
                    }
                }
            }

            DBGPRT();

            // can't check anything after this line because node can be deleted
            numreader.add(-1, local_thread_id);
        }

        void write_unlock(bool verify = true) {
            log_lock(nodep, lock_type_write_unlock, -1);

            lock_type lock(mutex);
            delfromwrite(verify);
            TLX_BTREE_ASSERT(haswriter);
            haswriter = false;
            if (upgradewaiting > 0) {
                log_lock(nodep, lock_type_write_unlock_notify_upgrader, -1);
                upgradecv.notify_one();
            } else if (writerswaiting > 0) {
                log_lock(nodep, lock_type_write_unlock_notify_writer, -1);
                writecv.notify_one();
            } else {
                log_lock(nodep, lock_type_write_unlock_notify_reader, -1);
                check_writer.clear();
                readcv.notify_all();
            }
            DBGPRT();
        }

        void downgrade_lock() {
            log_lock(nodep, lock_type_downgrade, -1);
            lock_type lock(mutex);
            TLX_BTREE_ASSERT(haswriter);
            addtoread();
            delfromwrite(false);
            haswriter = false;
            numreader.add(1, local_thread_id);
            if (upgradewaiting == 0 && writerswaiting == 0) {
                log_lock(nodep, lock_type_downgrade_notify_reader, -1);
                check_writer.clear();
                readcv.notify_all();
            }
            // ^ only works when theres no writers waiting
            DBGPRT();
        }

    /*private:
        int total_users() {
            int i = numreader + writerswaiting + readerswaiting
                    + upgradewaiting;

            return i + (haswriter ? 1 : 0);
        }*/
    };

    struct LockHelper {
        // TODO desc
        mutex_type mutex;
        cv_type readcv;
        cv_type writecv;
        cv_type upgradecv;
        unsigned int numreader = 0;
        bool haswriter = false;
        int writerswaiting = 0;
        int readerswaiting = 0;
        int upgradewaiting = 0;

        SLBTree *treep;
        node *nodep;

#ifdef TLX_BTREE_DEBUG
        std::unordered_set<std::thread::id> curwrite;
        std::unordered_set<std::thread::id> curread;
        mutex_type set_mtx;

        void addtoread() {
            lock_type lock(set_mtx);
            std::thread::id cur = std::this_thread::get_id();
            TLX_BTREE_ASSERT(!curread.contains(cur));
            curread.insert(cur);
        }

        void delfromread(bool verify) {
            lock_type lock(set_mtx);
            std::thread::id cur = std::this_thread::get_id();
            TLX_BTREE_ASSERT(curread.contains(cur));
            if (verify) treep->verify_one_node(nodep);
            curread.erase(cur);
        }

        void addtowrite() {
            lock_type lock(set_mtx);
            std::thread::id cur = std::this_thread::get_id();
            TLX_BTREE_ASSERT(!curwrite.contains(cur));
            curwrite.insert(cur);
        }

        void delfromwrite(bool verify) {
            lock_type lock(set_mtx);
            std::thread::id cur = std::this_thread::get_id();
            TLX_BTREE_ASSERT(curwrite.contains(cur));
            if (verify) treep->verify_one_node(nodep);
            curwrite.erase(cur);
        }

        bool readlocked() {
            lock_type lock(set_mtx);
            return curread.contains(std::this_thread::get_id());
        }

        bool writelocked() {
            lock_type lock(set_mtx);
            return curwrite.contains(std::this_thread::get_id());
        }

#define DBGPRT() \
        if (false && debug_print && nodep->level == 0) { \
            std::lock_guard<std::mutex> printlock(printmtx); \
            std::cout << __func__ << " " << seq++ << ": node=" << nodep << " "; \
            print_node(std::cout, nodep); \
        }

#else
        void addtoread() {}
        void delfromread(bool verify __attribute__((unused))) {}
        void addtowrite() {}
        void delfromwrite(bool verify __attribute__((unused))) {}

#define DBGPRT()
#endif

        bool take_lock() {
            bool is_root = treep->root_ == nodep;

            switch (treep->lock_req) {
            case lock_all: return true;
            case lock_root_only: return is_root;
            case lock_no_root_only: return !is_root;
            case lock_none: return false;
            default: tlx_die_unless(false); return true;
            }
        }

        void readlock(bool verify __attribute__((unused)) = true) {
            if (!take_lock()) {
                numreader++;
                return;
            }
            log_lock(nodep, lock_type_read, -1);
            lock_type lock(mutex);
            addtoread();
            if (haswriter || writerswaiting > 0
                    || upgradewaiting > 0) {
                readerswaiting++;
                log_lock(nodep, lock_type_read_wait, -1);
                readcv.wait(lock, [this](){
                        return !this->haswriter
                        && this->writerswaiting == 0
                        && this->upgradewaiting == 0;
                });
                readerswaiting--;
            }
            numreader++;
            VERIFY_NODE(verify, treep, nodep);
            log_lock(nodep, lock_type_read_got, -1);
            DBGPRT();
        }

        void upgradelock() {
            log_lock(nodep, lock_type_upgrade, -1);
            lock_type lock(mutex);
            delfromread(false);
            addtowrite();

            numreader--;
            if (numreader > 0 || haswriter) {
                upgradewaiting++;
                log_lock(nodep, lock_type_upgrade_wait, -1);
                upgradecv.wait(lock, [this]() {
                    return this->numreader == 0 && !this->haswriter;
                });
                upgradewaiting--;
            }
            haswriter = true;
            log_lock(nodep, lock_type_upgrade_got, -1);
            DBGPRT();
        }

        void writelock(bool verify __attribute__((unused)) = true) {
            log_lock(nodep, lock_type_write, -1);
            lock_type lock(mutex);
            addtowrite();
            if (numreader > 0 || haswriter || upgradewaiting > 0) {
                writerswaiting++;
                writecv.wait(lock, [this](){
                    return this->numreader == 0 && !this->haswriter
                            && this->upgradewaiting == 0;
                });
                writerswaiting--;
            }
            TLX_BTREE_ASSERT(!haswriter);
            haswriter = true;
            VERIFY_NODE(verify, treep, nodep);
            log_lock(nodep, lock_type_write_got, -1);
            DBGPRT();
        }

        void read_unlock(bool verify __attribute__((unused)) = true) {
            if (!take_lock()) {
                numreader--;
                return;
            }
            log_lock(nodep, lock_type_read_unlock, -1);
            lock_type lock(mutex);
            delfromread(verify);
            TLX_BTREE_ASSERT(numreader >= 1);
            numreader--;
            if (numreader == 0) {
                if (upgradewaiting > 0) upgradecv.notify_one();
                else if (writerswaiting > 0) writecv.notify_one();
                else readcv.notify_all();
            }

            DBGPRT();
        }

        void write_unlock(bool verify = true) {
            log_lock(nodep, lock_type_write_unlock, -1);
            lock_type lock(mutex);
            delfromwrite(verify);
            TLX_BTREE_ASSERT(haswriter);
            haswriter = false;
            if (upgradewaiting > 0) {
                upgradecv.notify_one();
            } else if (writerswaiting > 0) {
                writecv.notify_one();
            } else {
                readcv.notify_all();
            }
            DBGPRT();
        }

        void downgrade_lock() {
            log_lock(nodep, lock_type_downgrade, -1);
            lock_type lock(mutex);
            TLX_BTREE_ASSERT(haswriter);
            addtoread();
            delfromwrite(false);
            haswriter = false;
            numreader++;
            readcv.notify_all();
            // ^ only works when theres no writers waiting
            DBGPRT();
        }

    /*private:
        int total_users() {
            int i = numreader + writerswaiting + readerswaiting
                    + upgradewaiting;

            return i + (haswriter ? 1 : 0);
        }*/
    };

    //! \}

private:
    //! \name Node Classes for In-Memory Nodes
    //! \{

    //! The header structure of each node in-memory. This structure is extended
    //! by InnerNode or LeafNode.
public: // XXX
    struct node {
        //! Level in the b-tree, if level == 0 -> leaf node
        unsigned short level;

        //! Number of key slotuse use, so the number of valid children or data
        //! pointers
        unsigned short slotuse;

        //! Is changed every time there is a split or rebalance
        //! in order to prevent inserting into the wrong leaf
        int gen = 0;

        //! Delayed initialisation of constructed node.
        void initialize(const unsigned short l) {
            level = l;
            slotuse = 0;
        }

        //! True if this is a leaf node.
        bool is_leafnode() const {
            //TLX_BTREE_ASSERT(lock->treep->root_ != this);
            return (level == 0);
        }

        node(SLBTree *tree __attribute__((unused))) {
        }

        ~node() {
        }
    };

    //! Extended structure of a inner node in-memory. Contains only keys and no
    //! data items.
    struct InnerNode : public node {
        //! Define an related allocator for the InnerNode structs.
        typedef typename std::allocator_traits<Allocator>::template rebind_alloc<InnerNode> alloc_type;

        // TODO desc
        InnerLockHelper *lock;

        //! Keys of children or data pointers
        key_type slotkey[inner_slotmax]; // NOLINT

        //! Pointers to children
        node* childid[inner_slotmax + 1]; // NOLINT

        InnerNode(SLBTree *tree) : node(tree) {
            lock = new InnerLockHelper();
            lock->nodep = this;
            lock->treep = tree;
        }

        ~InnerNode() {
            delete lock;
        }

        //! Set variables to initial values.
        void initialize(const unsigned short l) {
            node::initialize(l);
        }

        //! Return key in slot s
        const key_type& key(size_t s) const {
            return slotkey[s];
        }

        //! True if the node's slots are full.
        bool is_full() const {
            return (node::slotuse == inner_slotmax);
        }

        //! True if few used entries, less than half full.
        bool is_few() const {
            return (node::slotuse <= inner_slotmin);
        }

        //! True if node has too few entries.
        bool is_underflow() const {
            return (node::slotuse < inner_slotmin);
        }
    };

    //! Extended structure of a leaf node in memory. Contains pairs of keys and
    //! data items. Key and data slots are kept together in value_type.
    struct LeafNode : public node {
        //! Define an related allocator for the LeafNode structs.
        typedef typename std::allocator_traits<Allocator>::template rebind_alloc<LeafNode> alloc_type;

        //! Double linked list pointers to traverse the leaves
        LeafNode* prev_leaf;

        //! Double linked list pointers to traverse the leaves
        LeafNode* next_leaf;

        //! Array of (key, data) pairs
        value_type slotdata[leaf_slotmax]; // NOLINT

        // TODO desc
        LockHelper* lock;

        LeafNode(SLBTree *tree) : node(tree) {
            lock = new LockHelper();
            lock->nodep = this;
            lock->treep = tree;
#ifdef TLX_BTREE_DEBUG
#endif
        }

        ~LeafNode() {
            delete lock;
        }

        //! Set variables to initial values
        void initialize() {
            node::initialize(0);
            prev_leaf = next_leaf = nullptr;
        }

        //! Return key in slot s.
        const key_type& key(size_t s) const {
            return key_of_value::get(slotdata[s]);
        }

        //! True if the node's slots are full.
        bool is_full() const {
            return (node::slotuse == leaf_slotmax);
        }

        //! True if few used entries, less than half full.
        bool is_few() const {
            return (node::slotuse <= leaf_slotmin);
        }

        //! True if node has too few entries.
        bool is_underflow() const {
            return (node::slotuse < leaf_slotmin);
        }

        //! Set the (key,data) pair in slot. Overloaded function used by
        //! bulk_load().
        void set_slot(unsigned short slot, const value_type& value) {
            TLX_BTREE_ASSERT(slot < node::slotuse);
            slotdata[slot] = value;
        }
    };

    //! \}

public:
    //! \name Iterators and Reverse Iterators
    //! \{

    class iterator;
    class const_iterator;
    class reverse_iterator;
    class const_reverse_iterator;

    //! STL-like iterator object for B+ tree items. The iterator points to a
    //! specific slot number in a leaf.
    class iterator
    {
    public:
        // *** Types

        //! The key type of the btree. Returned by key().
        typedef typename SLBTree::key_type key_type;

        //! The value type of the btree. Returned by operator*().
        typedef typename SLBTree::value_type value_type;

        //! Reference to the value_type. STL required.
        typedef value_type& reference;

        //! Pointer to the value_type. STL required.
        typedef value_type* pointer;

        //! STL-magic iterator category
        typedef std::bidirectional_iterator_tag iterator_category;

        //! STL-magic
        typedef ptrdiff_t difference_type;

        //! Our own type
        typedef iterator self;

    private:
        // *** Members

        //! The currently referenced leaf node of the tree
        typename SLBTree::LeafNode* curr_leaf;

        //! Current key/data slot referenced
        unsigned short curr_slot;

        //! Friendly to the const_iterator, so it may access the two data items
        //! directly.
        friend class const_iterator;

        //! Also friendly to the reverse_iterator, so it may access the two
        //! data items directly.
        friend class reverse_iterator;

        //! Also friendly to the const_reverse_iterator, so it may access the
        //! two data items directly.
        friend class const_reverse_iterator;

        //! Also friendly to the base btree class, because erase_iter() needs
        //! to read the curr_leaf and curr_slot values directly.
        friend class SLBTree<key_type, value_type, key_of_value, key_compare,
                           traits, allow_duplicates, allocator_type>;

        // The macro TLX_BTREE_FRIENDS can be used by outside class to access
        // the B+ tree internals. This was added for wxBTreeDemo to be able to
        // draw the tree.
        TLX_BTREE_FRIENDS;

    public:
        // *** Methods

        //! Default-Constructor of a mutable iterator
        iterator()
            : curr_leaf(nullptr), curr_slot(0)
        { }

        //! Initializing-Constructor of a mutable iterator
        iterator(typename SLBTree::LeafNode* l, unsigned short s)
            : curr_leaf(l), curr_slot(s)
        { }

        //! Copy-constructor from a reverse iterator
        iterator(const reverse_iterator& it) // NOLINT
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot)
        { }

        //! Dereference the iterator.
        reference operator * () const {
            return curr_leaf->slotdata[curr_slot];
        }

        //! Dereference the iterator.
        pointer operator -> () const {
            return &curr_leaf->slotdata[curr_slot];
        }

        //! Key of the current slot.
        const key_type& key() const {
            return curr_leaf->key(curr_slot);
        }

        //! Prefix++ advance the iterator to the next slot.
        iterator& operator ++ () {
            if (curr_slot + 1u < curr_leaf->slotuse) {
                ++curr_slot;
            }
            else if (curr_leaf->next_leaf != nullptr) {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 0;
            }
            else {
                // this is end()
                curr_slot = curr_leaf->slotuse;
            }

            return *this;
        }

        //! Postfix++ advance the iterator to the next slot.
        iterator operator ++ (int) {
            iterator tmp = *this;   // copy ourselves

            if (curr_slot + 1u < curr_leaf->slotuse) {
                ++curr_slot;
            }
            else if (curr_leaf->next_leaf != nullptr) {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 0;
            }
            else {
                // this is end()
                curr_slot = curr_leaf->slotuse;
            }

            return tmp;
        }

        //! Prefix-- backstep the iterator to the last slot.
        iterator& operator -- () {
            if (curr_slot > 0) {
                --curr_slot;
            }
            else if (curr_leaf->prev_leaf != nullptr) {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse - 1;
            }
            else {
                // this is begin()
                curr_slot = 0;
            }

            return *this;
        }

        //! Postfix-- backstep the iterator to the last slot.
        iterator operator -- (int) {
            iterator tmp = *this;   // copy ourselves

            if (curr_slot > 0) {
                --curr_slot;
            }
            else if (curr_leaf->prev_leaf != nullptr) {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse - 1;
            }
            else {
                // this is begin()
                curr_slot = 0;
            }

            return tmp;
        }

        //! Equality of iterators.
        bool operator == (const iterator& x) const {
            return (x.curr_leaf == curr_leaf) && (x.curr_slot == curr_slot);
        }

        //! Inequality of iterators.
        bool operator != (const iterator& x) const {
            return (x.curr_leaf != curr_leaf) || (x.curr_slot != curr_slot);
        }
    };

    //! STL-like read-only iterator object for B+ tree items. The iterator
    //! points to a specific slot number in a leaf.
    class const_iterator
    {
    public:
        // *** Types

        //! The key type of the btree. Returned by key().
        typedef typename SLBTree::key_type key_type;

        //! The value type of the btree. Returned by operator*().
        typedef typename SLBTree::value_type value_type;

        //! Reference to the value_type. STL required.
        typedef const value_type& reference;

        //! Pointer to the value_type. STL required.
        typedef const value_type* pointer;

        //! STL-magic iterator category
        typedef std::bidirectional_iterator_tag iterator_category;

        //! STL-magic
        typedef ptrdiff_t difference_type;

        //! Our own type
        typedef const_iterator self;

    private:
        // *** Members

        //! The currently referenced leaf node of the tree
        const typename SLBTree::LeafNode* curr_leaf;

        //! Current key/data slot referenced
        unsigned short curr_slot;

        //! Friendly to the reverse_const_iterator, so it may access the two
        //! data items directly
        friend class const_reverse_iterator;

        // The macro TLX_BTREE_FRIENDS can be used by outside class to access
        // the B+ tree internals. This was added for wxBTreeDemo to be able to
        // draw the tree.
        TLX_BTREE_FRIENDS;

    public:
        // *** Methods

        //! Default-Constructor of a const iterator
        const_iterator()
            : curr_leaf(nullptr), curr_slot(0)
        { }

        //! Initializing-Constructor of a const iterator
        const_iterator(const typename SLBTree::LeafNode* l, unsigned short s)
            : curr_leaf(l), curr_slot(s)
        { }

        //! Copy-constructor from a mutable iterator
        const_iterator(const iterator& it) // NOLINT
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot)
        { }

        //! Copy-constructor from a mutable reverse iterator
        const_iterator(const reverse_iterator& it) // NOLINT
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot)
        { }

        //! Copy-constructor from a const reverse iterator
        const_iterator(const const_reverse_iterator& it) // NOLINT
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot)
        { }

        //! Dereference the iterator.
        reference operator * () const {
            return curr_leaf->slotdata[curr_slot];
        }

        //! Dereference the iterator.
        pointer operator -> () const {
            return &curr_leaf->slotdata[curr_slot];
        }

        //! Key of the current slot.
        const key_type& key() const {
            return curr_leaf->key(curr_slot);
        }

        //! Prefix++ advance the iterator to the next slot.
        const_iterator& operator ++ () {
            if (curr_slot + 1u < curr_leaf->slotuse) {
                ++curr_slot;
            }
            else if (curr_leaf->next_leaf != nullptr) {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 0;
            }
            else {
                // this is end()
                curr_slot = curr_leaf->slotuse;
            }

            return *this;
        }

        //! Postfix++ advance the iterator to the next slot.
        const_iterator operator ++ (int) {
            const_iterator tmp = *this;   // copy ourselves

            if (curr_slot + 1u < curr_leaf->slotuse) {
                ++curr_slot;
            }
            else if (curr_leaf->next_leaf != nullptr) {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 0;
            }
            else {
                // this is end()
                curr_slot = curr_leaf->slotuse;
            }

            return tmp;
        }

        //! Prefix-- backstep the iterator to the last slot.
        const_iterator& operator -- () {
            if (curr_slot > 0) {
                --curr_slot;
            }
            else if (curr_leaf->prev_leaf != nullptr) {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse - 1;
            }
            else {
                // this is begin()
                curr_slot = 0;
            }

            return *this;
        }

        //! Postfix-- backstep the iterator to the last slot.
        const_iterator operator -- (int) {
            const_iterator tmp = *this;   // copy ourselves

            if (curr_slot > 0) {
                --curr_slot;
            }
            else if (curr_leaf->prev_leaf != nullptr) {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse - 1;
            }
            else {
                // this is begin()
                curr_slot = 0;
            }

            return tmp;
        }

        //! Equality of iterators.
        bool operator == (const const_iterator& x) const {
            return (x.curr_leaf == curr_leaf) && (x.curr_slot == curr_slot);
        }

        //! Inequality of iterators.
        bool operator != (const const_iterator& x) const {
            return (x.curr_leaf != curr_leaf) || (x.curr_slot != curr_slot);
        }
    };

    //! STL-like mutable reverse iterator object for B+ tree items. The
    //! iterator points to a specific slot number in a leaf.
    class reverse_iterator
    {
    public:
        // *** Types

        //! The key type of the btree. Returned by key().
        typedef typename SLBTree::key_type key_type;

        //! The value type of the btree. Returned by operator*().
        typedef typename SLBTree::value_type value_type;

        //! Reference to the value_type. STL required.
        typedef value_type& reference;

        //! Pointer to the value_type. STL required.
        typedef value_type* pointer;

        //! STL-magic iterator category
        typedef std::bidirectional_iterator_tag iterator_category;

        //! STL-magic
        typedef ptrdiff_t difference_type;

        //! Our own type
        typedef reverse_iterator self;

    private:
        // *** Members

        //! The currently referenced leaf node of the tree
        typename SLBTree::LeafNode* curr_leaf;

        //! One slot past the current key/data slot referenced.
        unsigned short curr_slot;

        //! Friendly to the const_iterator, so it may access the two data items
        //! directly
        friend class iterator;

        //! Also friendly to the const_iterator, so it may access the two data
        //! items directly
        friend class const_iterator;

        //! Also friendly to the const_iterator, so it may access the two data
        //! items directly
        friend class const_reverse_iterator;

        // The macro TLX_BTREE_FRIENDS can be used by outside class to access
        // the B+ tree internals. This was added for wxBTreeDemo to be able to
        // draw the tree.
        TLX_BTREE_FRIENDS;

    public:
        // *** Methods

        //! Default-Constructor of a reverse iterator
        reverse_iterator()
            : curr_leaf(nullptr), curr_slot(0)
        { }

        //! Initializing-Constructor of a mutable reverse iterator
        reverse_iterator(typename SLBTree::LeafNode* l, unsigned short s)
            : curr_leaf(l), curr_slot(s)
        { }

        //! Copy-constructor from a mutable iterator
        reverse_iterator(const iterator& it) // NOLINT
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot)
        { }

        //! Dereference the iterator.
        reference operator * () const {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return curr_leaf->slotdata[curr_slot - 1];
        }

        //! Dereference the iterator.
        pointer operator -> () const {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return &curr_leaf->slotdata[curr_slot - 1];
        }

        //! Key of the current slot.
        const key_type& key() const {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return curr_leaf->key(curr_slot - 1);
        }

        //! Prefix++ advance the iterator to the next slot.
        reverse_iterator& operator ++ () {
            if (curr_slot > 1) {
                --curr_slot;
            }
            else if (curr_leaf->prev_leaf != nullptr) {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse;
            }
            else {
                // this is begin() == rend()
                curr_slot = 0;
            }

            return *this;
        }

        //! Postfix++ advance the iterator to the next slot.
        reverse_iterator operator ++ (int) {
            reverse_iterator tmp = *this;   // copy ourselves

            if (curr_slot > 1) {
                --curr_slot;
            }
            else if (curr_leaf->prev_leaf != nullptr) {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse;
            }
            else {
                // this is begin() == rend()
                curr_slot = 0;
            }

            return tmp;
        }

        //! Prefix-- backstep the iterator to the last slot.
        reverse_iterator& operator -- () {
            if (curr_slot < curr_leaf->slotuse) {
                ++curr_slot;
            }
            else if (curr_leaf->next_leaf != nullptr) {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 1;
            }
            else {
                // this is end() == rbegin()
                curr_slot = curr_leaf->slotuse;
            }

            return *this;
        }

        //! Postfix-- backstep the iterator to the last slot.
        reverse_iterator operator -- (int) {
            reverse_iterator tmp = *this;   // copy ourselves

            if (curr_slot < curr_leaf->slotuse) {
                ++curr_slot;
            }
            else if (curr_leaf->next_leaf != nullptr) {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 1;
            }
            else {
                // this is end() == rbegin()
                curr_slot = curr_leaf->slotuse;
            }

            return tmp;
        }

        //! Equality of iterators.
        bool operator == (const reverse_iterator& x) const {
            return (x.curr_leaf == curr_leaf) && (x.curr_slot == curr_slot);
        }

        //! Inequality of iterators.
        bool operator != (const reverse_iterator& x) const {
            return (x.curr_leaf != curr_leaf) || (x.curr_slot != curr_slot);
        }
    };

    //! STL-like read-only reverse iterator object for B+ tree items. The
    //! iterator points to a specific slot number in a leaf.
    class const_reverse_iterator
    {
    public:
        // *** Types

        //! The key type of the btree. Returned by key().
        typedef typename SLBTree::key_type key_type;

        //! The value type of the btree. Returned by operator*().
        typedef typename SLBTree::value_type value_type;

        //! Reference to the value_type. STL required.
        typedef const value_type& reference;

        //! Pointer to the value_type. STL required.
        typedef const value_type* pointer;

        //! STL-magic iterator category
        typedef std::bidirectional_iterator_tag iterator_category;

        //! STL-magic
        typedef ptrdiff_t difference_type;

        //! Our own type
        typedef const_reverse_iterator self;

    private:
        // *** Members

        //! The currently referenced leaf node of the tree
        const typename SLBTree::LeafNode* curr_leaf;

        //! One slot past the current key/data slot referenced.
        unsigned short curr_slot;

        //! Friendly to the const_iterator, so it may access the two data items
        //! directly.
        friend class reverse_iterator;

        // The macro TLX_BTREE_FRIENDS can be used by outside class to access
        // the B+ tree internals. This was added for wxBTreeDemo to be able to
        // draw the tree.
        TLX_BTREE_FRIENDS;

    public:
        // *** Methods

        //! Default-Constructor of a const reverse iterator.
        const_reverse_iterator()
            : curr_leaf(nullptr), curr_slot(0)
        { }

        //! Initializing-Constructor of a const reverse iterator.
        const_reverse_iterator(
            const typename SLBTree::LeafNode* l, unsigned short s)
            : curr_leaf(l), curr_slot(s)
        { }

        //! Copy-constructor from a mutable iterator.
        const_reverse_iterator(const iterator& it) // NOLINT
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot)
        { }

        //! Copy-constructor from a const iterator.
        const_reverse_iterator(const const_iterator& it) // NOLINT
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot)
        { }

        //! Copy-constructor from a mutable reverse iterator.
        const_reverse_iterator(const reverse_iterator& it) // NOLINT
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot)
        { }

        //! Dereference the iterator.
        reference operator * () const {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return curr_leaf->slotdata[curr_slot - 1];
        }

        //! Dereference the iterator.
        pointer operator -> () const {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return &curr_leaf->slotdata[curr_slot - 1];
        }

        //! Key of the current slot.
        const key_type& key() const {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return curr_leaf->key(curr_slot - 1);
        }

        //! Prefix++ advance the iterator to the previous slot.
        const_reverse_iterator& operator ++ () {
            if (curr_slot > 1) {
                --curr_slot;
            }
            else if (curr_leaf->prev_leaf != nullptr) {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse;
            }
            else {
                // this is begin() == rend()
                curr_slot = 0;
            }

            return *this;
        }

        //! Postfix++ advance the iterator to the previous slot.
        const_reverse_iterator operator ++ (int) {
            const_reverse_iterator tmp = *this;   // copy ourselves

            if (curr_slot > 1) {
                --curr_slot;
            }
            else if (curr_leaf->prev_leaf != nullptr) {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse;
            }
            else {
                // this is begin() == rend()
                curr_slot = 0;
            }

            return tmp;
        }

        //! Prefix-- backstep the iterator to the next slot.
        const_reverse_iterator& operator -- () {
            if (curr_slot < curr_leaf->slotuse) {
                ++curr_slot;
            }
            else if (curr_leaf->next_leaf != nullptr) {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 1;
            }
            else {
                // this is end() == rbegin()
                curr_slot = curr_leaf->slotuse;
            }

            return *this;
        }

        //! Postfix-- backstep the iterator to the next slot.
        const_reverse_iterator operator -- (int) {
            const_reverse_iterator tmp = *this;   // copy ourselves

            if (curr_slot < curr_leaf->slotuse) {
                ++curr_slot;
            }
            else if (curr_leaf->next_leaf != nullptr) {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 1;
            }
            else {
                // this is end() == rbegin()
                curr_slot = curr_leaf->slotuse;
            }

            return tmp;
        }

        //! Equality of iterators.
        bool operator == (const const_reverse_iterator& x) const {
            return (x.curr_leaf == curr_leaf) && (x.curr_slot == curr_slot);
        }

        //! Inequality of iterators.
        bool operator != (const const_reverse_iterator& x) const {
            return (x.curr_leaf != curr_leaf) || (x.curr_slot != curr_slot);
        }
    };

    //! \}

public:
    //! \name Small Statistics Structure
    //! \{

    /*!
     * A small struct containing basic statistics about the B+ tree. It can be
     * fetched using get_stats().
     */
    struct tree_stats {
        //! Number of items in the B+ tree
        std::atomic<size_type> size;

        //! Number of leaves in the B+ tree
        std::atomic<size_type> leaves;

        //! Number of inner nodes in the B+ tree
        std::atomic<size_type> inner_nodes;

        //! Base B+ tree parameter: The number of key/data slots in each leaf
        static const unsigned short leaf_slots = Self::leaf_slotmax;

        //! Base B+ tree parameter: The number of key slots in each inner node.
        static const unsigned short inner_slots = Self::inner_slotmax;

        //! Zero initialized
        tree_stats()
            : size(0),
              leaves(0), inner_nodes(0)
        { }

        void clear() {
            size = 0;
            leaves = 0;
            inner_nodes = 0;
        }

        void copy(const tree_stats& other) {
            size = other.size.load();
            leaves = other.leaves.load();
            inner_nodes = other.inner_nodes.load();
        }

        //! Return the total number of nodes
        size_type nodes() const {
            return inner_nodes + leaves;
        }

        //! Return the average fill of leaves
        double avgfill_leaves() const {
            return static_cast<double>(size) / (leaves * leaf_slots);
        }
    };

    //! \}

private:
    //! \name Tree Object Data Members
    //! \{

    //! Pointer to the B+ tree's root node. If there is only one leaf
    //! node in the tree, it will point to that node.
    public:
    InnerNode* root_; // TODO
    private:
    //! Pointer to first leaf in the double linked leaf chain.
    LeafNode* head_leaf_;

    //! Pointer to last leaf in the double linked leaf chain.
    LeafNode* tail_leaf_;

    //! Other small statistics about the B+ tree.
    tree_stats stats_;

    //! Key comparison object. More comparison functions are generated from
    //! this < relation.
    key_compare key_less_;

    //! Memory allocator.
    allocator_type allocator_;

    //! \}

public:
    //! \name Constructors and Destructor
    //! \{

    //! Default constructor initializing an empty B+ tree with the standard key
    //! comparison function.
    explicit SLBTree(const allocator_type& alloc = allocator_type())
        : head_leaf_(nullptr), tail_leaf_(nullptr),
          allocator_(alloc)
    {
        root_ = allocate_inner(0);
    }

    //! Constructor initializing an empty B+ tree with a special key
    //! comparison object.
    explicit SLBTree(const key_compare& kcf,
                   const allocator_type& alloc = allocator_type())
        : head_leaf_(nullptr), tail_leaf_(nullptr),
          key_less_(kcf), allocator_(alloc)
    {
        root_ = allocate_inner(0);
    }

    //! Constructor initializing a B+ tree with the range [first,last). The
    //! range need not be sorted. To create a B+ tree from a sorted range, use
    //! bulk_load().
    template <class InputIterator>
    SLBTree(InputIterator first, InputIterator last,
          const allocator_type& alloc = allocator_type())
        : head_leaf_(nullptr), tail_leaf_(nullptr),
          allocator_(alloc) {
        insert(first, last);
        root_ = allocate_inner(0);
    }

    //! Constructor initializing a B+ tree with the range [first,last) and a
    //! special key comparison object.  The range need not be sorted. To create
    //! a B+ tree from a sorted range, use bulk_load().
    template <class InputIterator>
    SLBTree(InputIterator first, InputIterator last, const key_compare& kcf,
          const allocator_type& alloc = allocator_type())
        : head_leaf_(nullptr), tail_leaf_(nullptr),
          key_less_(kcf), allocator_(alloc) {
        insert(first, last);
        root_ = allocate_inner(0);
    }

    //! Frees up all used B+ tree memory pages
    ~SLBTree() {
        clear();
    }

    //! Fast swapping of two identical B+ tree objects.
    void swap(SLBTree& from) {
        std::swap(root_, from.root_);
        std::swap(head_leaf_, from.head_leaf_);
        std::swap(tail_leaf_, from.tail_leaf_);
        std::swap(stats_, from.stats_);
        std::swap(key_less_, from.key_less_);
        std::swap(allocator_, from.allocator_);
    }

    //! \}

public:
    //! \name Key and Value Comparison Function Objects
    //! \{

    //! Function class to compare value_type objects. Required by the STL
    class value_compare
    {
    protected:
        //! Key comparison function from the template parameter
        key_compare key_comp;

        //! Constructor called from SLBTree::value_comp()
        explicit value_compare(key_compare kc)
            : key_comp(kc)
        { }

        //! Friendly to the btree class so it may call the constructor
        friend class SLBTree<key_type, value_type, key_of_value, key_compare,
                           traits, allow_duplicates, allocator_type>;

    public:
        //! Function call "less"-operator resulting in true if x < y.
        bool operator () (const value_type& x, const value_type& y) const {
            return key_comp(x.first, y.first);
        }
    };

    //! Constant access to the key comparison object sorting the B+ tree.
    key_compare key_comp() const {
        return key_less_;
    }

    //! Constant access to a constructed value_type comparison object. Required
    //! by the STL.
    value_compare value_comp() const {
        return value_compare(key_less_);
    }

    //! \}

private:
    //! \name Convenient Key Comparison Functions Generated From key_less
    //! \{

    //! True if a < b ? "constructed" from key_less_()
    bool key_less(const key_type& a, const key_type& b) const {
        return key_less_(a, b);
    }

    //! True if a <= b ? constructed from key_less()
    bool key_lessequal(const key_type& a, const key_type& b) const {
        return !key_less_(b, a);
    }

    //! True if a > b ? constructed from key_less()
    bool key_greater(const key_type& a, const key_type& b) const {
        return key_less_(b, a);
    }

    //! True if a >= b ? constructed from key_less()
    bool key_greaterequal(const key_type& a, const key_type& b) const {
        return !key_less_(a, b);
    }

    //! True if a == b ? constructed from key_less(). This requires the <
    //! relation to be a total order, otherwise the B+ tree cannot be sorted.
    bool key_equal(const key_type& a, const key_type& b) const {
        return !key_less_(a, b) && !key_less_(b, a);
    }

    //! \}

public:
    //! \name Allocators
    //! \{

    //! Return the base node allocator provided during construction.
    allocator_type get_allocator() const {
        return allocator_;
    }

    //! \}

private:
    //! \name Node Object Allocation and Deallocation Functions
    //! \{

    //! Return an allocator for LeafNode objects.
    typename LeafNode::alloc_type leaf_node_allocator() {
        return typename LeafNode::alloc_type(allocator_);
    }

    //! Return an allocator for InnerNode objects.
    typename InnerNode::alloc_type inner_node_allocator() {
        return typename InnerNode::alloc_type(allocator_);
    }

    //! Allocate and initialize a leaf node
    LeafNode * allocate_leaf() {
        LeafNode* n = new (leaf_node_allocator().allocate(1)) LeafNode(this);
        n->initialize();
        stats_.leaves++;
        log_mem_op(ALLOC_LEAF, n, stats_.inner_nodes,
                      stats_.leaves);
        return n;
    }

    //! Allocate and initialize an inner node
    InnerNode * allocate_inner(unsigned short level) {
        InnerNode* n = new (inner_node_allocator().allocate(1)) InnerNode(this);
        n->initialize(level);
        stats_.inner_nodes++;
        log_mem_op(ALLOC_INNER, n, stats_.inner_nodes,
                      stats_.leaves);
        return n;
    }

    //! Correctly free either inner or leaf node, destructs all contained key
    //! and value objects.
    void free_node(node* n) {
        if (n->is_leafnode()) {
            LeafNode* ln = static_cast<LeafNode*>(n);
            TLX_BTREE_ASSERT(ln->lock->readerswaiting == 0 && ln->lock->writerswaiting == 0
                             && ln->lock->upgradewaiting == 0);
            typename LeafNode::alloc_type a(leaf_node_allocator());
            std::allocator_traits<typename LeafNode::alloc_type>::destroy(a, ln);
            std::allocator_traits<typename LeafNode::alloc_type>::deallocate(a, ln, 1);
            stats_.leaves--;
            log_mem_op(FREE_LEAF, n, stats_.inner_nodes,
                          stats_.leaves);
        }
        else {
            InnerNode* in = static_cast<InnerNode*>(n);
            TLX_BTREE_ASSERT(in->lock->readerswaiting == 0 && in->lock->writerswaiting == 0
                             && in->lock->upgradewaiting == 0);
            typename InnerNode::alloc_type a(inner_node_allocator());
            std::allocator_traits<typename InnerNode::alloc_type>::destroy(a, in);
            std::allocator_traits<typename InnerNode::alloc_type>::deallocate(a, in, 1);
            stats_.inner_nodes--;
            log_mem_op(FREE_INNER, n, stats_.inner_nodes,
                          stats_.leaves);
        }
    }

    //! \}

public:
    //! \name Fast Destruction of the B+ Tree
    //! \{

    //! Frees all key/data pairs and all nodes of the tree.
    void clear() {
        if (root_->level > 0)
        {
            clear_recursive(root_);

            root_->slotuse = 0;
            root_->level = 0;
            head_leaf_ = tail_leaf_ = nullptr;

            // stats_ = tree_stats();
            stats_.clear();
        }

        TLX_BTREE_ASSERT(stats_.size == 0);
    }

private:
    //! Recursively free up nodes.
    // TODO locking
    void clear_recursive(node* n) {
        if (n->is_leafnode())
        {
            LeafNode* leafnode = static_cast<LeafNode*>(n);

            for (unsigned short slot = 0; slot < leafnode->slotuse; ++slot)
            {
                // data objects are deleted by LeafNode's destructor
            }
        }
        else
        {
            InnerNode* innernode = static_cast<InnerNode*>(n);

            for (unsigned short slot = 0; slot < innernode->slotuse + 1; ++slot)
            {
                clear_recursive(innernode->childid[slot]);
                free_node(innernode->childid[slot]);
            }
        }
    }

    //! \}

public:
    //! \name STL Iterator Construction Functions
    //! \{

    //! Constructs a read/data-write iterator that points to the first slot in
    //! the first leaf of the B+ tree.
    iterator begin() {
        return iterator(head_leaf_, 0);
    }

    //! Constructs a read/data-write iterator that points to the first invalid
    //! slot in the last leaf of the B+ tree.
    iterator end() {
        return iterator(tail_leaf_, tail_leaf_ ? tail_leaf_->slotuse : 0);
    }

    //! Constructs a read-only constant iterator that points to the first slot
    //! in the first leaf of the B+ tree.
    const_iterator begin() const {
        return const_iterator(head_leaf_, 0);
    }

    //! Constructs a read-only constant iterator that points to the first
    //! invalid slot in the last leaf of the B+ tree.
    const_iterator end() const {
        return const_iterator(tail_leaf_, tail_leaf_ ? tail_leaf_->slotuse : 0);
    }

    //! Constructs a read/data-write reverse iterator that points to the first
    //! invalid slot in the last leaf of the B+ tree. Uses STL magic.
    reverse_iterator rbegin() {
        return reverse_iterator(end());
    }

    //! Constructs a read/data-write reverse iterator that points to the first
    //! slot in the first leaf of the B+ tree. Uses STL magic.
    reverse_iterator rend() {
        return reverse_iterator(begin());
    }

    //! Constructs a read-only reverse iterator that points to the first
    //! invalid slot in the last leaf of the B+ tree. Uses STL magic.
    const_reverse_iterator rbegin() const {
        return const_reverse_iterator(end());
    }

    //! Constructs a read-only reverse iterator that points to the first slot
    //! in the first leaf of the B+ tree. Uses STL magic.
    const_reverse_iterator rend() const {
        return const_reverse_iterator(begin());
    }

    //! \}

private:
    //! \name B+ Tree Node Binary Search Functions
    //! \{

    //! Searches for the first key in the node n greater or equal to key. Uses
    //! binary search with an optional linear self-verification. This is a
    //! template function, because the slotkey array is located at different
    //! places in LeafNode and InnerNode.
    template <typename node_type>
    unsigned short find_lower(const node_type* n, const key_type& key) const {
        if (sizeof(*n) > traits::binsearch_threshold)
        {
            if (n->slotuse == 0) return 0;

            unsigned short lo = 0, hi = n->slotuse;

            while (lo < hi)
            {
                unsigned short mid = (lo + hi) >> 1;

                if (key_lessequal(key, n->key(mid))) {
                    hi = mid; // key <= mid
                }
                else {
                    lo = mid + 1; // key > mid
                }
            }

            TLX_BTREE_PRINT("SLBTree::find_lower: on " << n <<
                            " key " << key << " -> " << lo << " / " << hi);

            // verify result using simple linear search
            if (self_verify)
            {
                unsigned short i = 0;
                while (i < n->slotuse && key_less(n->key(i), key)) ++i;

                TLX_BTREE_PRINT("SLBTree::find_lower: testfind: " << i);
                TLX_BTREE_ASSERT(i == lo);
            }

            return lo;
        }
        else // for nodes <= binsearch_threshold do linear search.
        {
            unsigned short lo = 0;
            while (lo < n->slotuse && key_less(n->key(lo), key)) ++lo;
            return lo;
        }
    }

    //! Searches for the first key in the node n greater than key. Uses binary
    //! search with an optional linear self-verification. This is a template
    //! function, because the slotkey array is located at different places in
    //! LeafNode and InnerNode.
    template <typename node_type>
    unsigned short find_upper(const node_type* n, const key_type& key) const {
        if (sizeof(*n) > traits::binsearch_threshold)
        {
            if (n->slotuse == 0) return 0;

            unsigned short lo = 0, hi = n->slotuse;

            while (lo < hi)
            {
                unsigned short mid = (lo + hi) >> 1;

                if (key_less(key, n->key(mid))) {
                    hi = mid; // key < mid
                }
                else {
                    lo = mid + 1; // key >= mid
                }
            }

            TLX_BTREE_PRINT("SLBTree::find_upper: on " << n <<
                            " key " << key << " -> " << lo << " / " << hi);

            // verify result using simple linear search
            if (self_verify)
            {
                unsigned short i = 0;
                while (i < n->slotuse && key_lessequal(n->key(i), key)) ++i;

                TLX_BTREE_PRINT("SLBTree::find_upper testfind: " << i);
                TLX_BTREE_ASSERT(i == hi);
            }

            return lo;
        }
        else // for nodes <= binsearch_threshold do linear search.
        {
            unsigned short lo = 0;
            while (lo < n->slotuse && key_lessequal(n->key(lo), key)) ++lo;
            return lo;
        }
    }

    //! \}

public:
    //! \name Access Functions to the Item Count
    //! \{

    //! Return the number of key/data pairs in the B+ tree
    size_type size() const {
        return stats_.size;
    }

    //! Returns true if there is at least one key/data pair in the B+ tree
    bool empty() const {
        return (size() == size_type(0));
    }

    //! Returns the largest possible size of the B+ Tree. This is just a
    //! function required by the STL standard, the B+ Tree can hold more items.
    size_type max_size() const {
        return size_type(-1);
    }

    //! Return a const reference to the current statistics.
    const struct tree_stats& get_stats() const {
        return stats_;
    }

    //! \}

    void get_root_info(unsigned short *level, unsigned short *slotuse) const {
        *level = root_->level;
        *slotuse = root_->slotuse;
    }

    void set_lock_requirement(lock_requirement req) {
        lock_req = req;
    }

    lock_requirement lock_req = lock_all;

public:
    //! \name STL Access Functions Querying the Tree by Descending to a Leaf
    //! \{

    //! Non-STL function checking whether a key is in the B+ tree. The same as
    //! (find(k) != end()) or (count() != 0).
    bool exists(const key_type& key) const {
        root_->lock->readlock();

        if (root_->level == 0) {
            root_->lock->read_unlock();
            return false;
        }

        node* n = root_;

        while (!n->is_leafnode())
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            unsigned short slot = find_lower(inner, key);

            bool childisleaf = inner->level == 1;
            TAKE_LOCK(childisleaf, inner->childid[slot], readlock);

            n = inner->childid[slot];
            inner->lock->read_unlock();
        }

        const LeafNode* leaf = static_cast<const LeafNode*>(n);

        unsigned short slot = find_lower(leaf, key);

        bool res = (slot < leaf->slotuse && key_equal(key, leaf->key(slot)));
        leaf->lock->read_unlock();
        return res;
    }

    //! Tries to locate a key in the B+ tree and returns an iterator to the
    //! key/data slot if found. If unsuccessful it returns end().
    iterator find(const key_type& key) {
        root_->lock->readlock();
        if (root_->level == 0) {
            root_->lock->read_unlock();
            return end();
        }

        node* n = root_;

        while (!n->is_leafnode())
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            unsigned short slot = find_lower(inner, key);

            bool childisleaf = inner->level == 1;
            TAKE_LOCK(childisleaf, inner->childid[slot], readlock);

            inner->lock->read_unlock();

            n = inner->childid[slot];
        }

        LeafNode* leaf = static_cast<LeafNode*>(n);

        unsigned short slot = find_lower(leaf, key);

        auto res = (slot < leaf->slotuse && key_equal(key, leaf->key(slot)))
               ? iterator(leaf, slot) : end();
        leaf->lock->read_unlock();
        return res;
    }

    //! Tries to locate a key in the B+ tree and returns an constant iterator to
    //! the key/data slot if found. If unsuccessful it returns end().
    const_iterator find(const key_type& key) const {
        const node* n = root_;
        if (!n) return end();

        while (!n->is_leafnode())
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            unsigned short slot = find_lower(inner, key);

            n = inner->childid[slot];
        }

        const LeafNode* leaf = static_cast<const LeafNode*>(n);

        unsigned short slot = find_lower(leaf, key);
        return (slot < leaf->slotuse && key_equal(key, leaf->key(slot)))
               ? const_iterator(leaf, slot) : end();
    }

    //! Tries to locate a key in the B+ tree and returns the number of identical
    //! key entries found.
    size_type count(const key_type& key) const {
        const node* n = root_;
        if (!n) return 0;

        while (!n->is_leafnode())
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            unsigned short slot = find_lower(inner, key);

            n = inner->childid[slot];
        }

        const LeafNode* leaf = static_cast<const LeafNode*>(n);

        unsigned short slot = find_lower(leaf, key);
        size_type num = 0;

        while (leaf && slot < leaf->slotuse && key_equal(key, leaf->key(slot)))
        {
            ++num;
            if (++slot >= leaf->slotuse)
            {
                leaf = leaf->next_leaf;
                slot = 0;
            }
        }

        return num;
    }

    //! Searches the B+ tree and returns an iterator to the first pair equal to
    //! or greater than key, or end() if all keys are smaller.
    // TODO lb and ub not very important
    iterator lower_bound(const key_type& key) {
        node* n = root_;
        if (!n) return end();

        while (!n->is_leafnode())
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            unsigned short slot = find_lower(inner, key);

            n = inner->childid[slot];
        }

        LeafNode* leaf = static_cast<LeafNode*>(n);

        unsigned short slot = find_lower(leaf, key);
        return iterator(leaf, slot);
    }

    //! Searches the B+ tree and returns a constant iterator to the first pair
    //! equal to or greater than key, or end() if all keys are smaller.
    const_iterator lower_bound(const key_type& key) const {
        const node* n = root_;
        if (!n) return end();

        while (!n->is_leafnode())
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            unsigned short slot = find_lower(inner, key);

            n = inner->childid[slot];
        }

        const LeafNode* leaf = static_cast<const LeafNode*>(n);

        unsigned short slot = find_lower(leaf, key);
        return const_iterator(leaf, slot);
    }

    //! Searches the B+ tree and returns an iterator to the first pair greater
    //! than key, or end() if all keys are smaller or equal.
    iterator upper_bound(const key_type& key) {
        node* n = root_;
        if (!n) return end();

        while (!n->is_leafnode())
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            unsigned short slot = find_upper(inner, key);

            n = inner->childid[slot];
        }

        LeafNode* leaf = static_cast<LeafNode*>(n);

        unsigned short slot = find_upper(leaf, key);
        return iterator(leaf, slot);
    }

    //! Searches the B+ tree and returns a constant iterator to the first pair
    //! greater than key, or end() if all keys are smaller or equal.
    const_iterator upper_bound(const key_type& key) const {
        const node* n = root_;
        if (!n) return end();

        while (!n->is_leafnode())
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            unsigned short slot = find_upper(inner, key);

            n = inner->childid[slot];
        }

        const LeafNode* leaf = static_cast<const LeafNode*>(n);

        unsigned short slot = find_upper(leaf, key);
        return const_iterator(leaf, slot);
    }

    //! Searches the B+ tree and returns both lower_bound() and upper_bound().
    std::pair<iterator, iterator> equal_range(const key_type& key) {
        return std::pair<iterator, iterator>(
            lower_bound(key), upper_bound(key));
    }

    //! Searches the B+ tree and returns both lower_bound() and upper_bound().
    std::pair<const_iterator, const_iterator>
    equal_range(const key_type& key) const {
        return std::pair<const_iterator, const_iterator>(
            lower_bound(key), upper_bound(key));
    }

    //! \}

public:
    //! \name B+ Tree Object Comparison Functions
    //! \{

    //! Equality relation of B+ trees of the same type. B+ trees of the same
    //! size and equal elements (both key and data) are considered equal. Beware
    //! of the random ordering of duplicate keys.
    bool operator == (const SLBTree& other) const {
        return (size() == other.size()) &&
               std::equal(begin(), end(), other.begin());
    }

    //! Inequality relation. Based on operator==.
    bool operator != (const SLBTree& other) const {
        return !(*this == other);
    }

    //! Total ordering relation of B+ trees of the same type. It uses
    //! std::lexicographical_compare() for the actual comparison of elements.
    // TODO lock help!!!
    bool operator < (const SLBTree& other) const {
        return std::lexicographical_compare(
            begin(), end(), other.begin(), other.end());
    }

    //! Greater relation. Based on operator<.
    bool operator > (const SLBTree& other) const {
        return other < *this;
    }

    //! Less-equal relation. Based on operator<.
    bool operator <= (const SLBTree& other) const {
        return !(other < *this);
    }

    //! Greater-equal relation. Based on operator<.
    bool operator >= (const SLBTree& other) const {
        return !(*this < other);
    }

    //! \}

public: // TODO skipping these two sections
    //! \name Fast Copy: Assign Operator and Copy Constructors
    //! \{

    //! Assignment operator. All the key/data pairs are copied.
    SLBTree& operator = (const SLBTree& other) {
        if (this != &other)
        {
            clear();

            key_less_ = other.key_comp();
            allocator_ = other.get_allocator();

            if (other.size() != 0)
            {
                stats_.leaves = stats_.inner_nodes = 0;
                if (other.root_) {
                    root_ = static_cast<InnerNode*>(copy_recursive(other.root_));
                }
                stats_.copy(other.stats_);
            }

            if (self_verify) verify();
        }
        return *this;
    }

    //! Copy constructor. The newly initialized B+ tree object will contain a
    //! copy of all key/data pairs.
    SLBTree(const SLBTree& other)
        : head_leaf_(nullptr), tail_leaf_(nullptr),
          key_less_(other.key_comp()),
          allocator_(other.get_allocator()) {

        stats_.copy(other.stats_);
        if (size() > 0)
        {
            stats_.leaves = stats_.inner_nodes = 0;
            if (other.root_) {
                root_ = static_cast<InnerNode*>(copy_recursive(other.root_));
            }
            if (self_verify) verify();
        }
    }

private:
    //! Recursively copy nodes from another B+ tree object
    struct node * copy_recursive(const node* n) {
        if (n->is_leafnode())
        {
            const LeafNode* leaf = static_cast<const LeafNode*>(n);
            LeafNode* newleaf = allocate_leaf();

            newleaf->slotuse = leaf->slotuse;
            std::copy(leaf->slotdata, leaf->slotdata + leaf->slotuse,
                      newleaf->slotdata);

            if (head_leaf_ == nullptr)
            {
                head_leaf_ = tail_leaf_ = newleaf;
                newleaf->prev_leaf = newleaf->next_leaf = nullptr;
            }
            else
            {
                newleaf->prev_leaf = tail_leaf_;
                tail_leaf_->next_leaf = newleaf;
                tail_leaf_ = newleaf;
            }

            return newleaf;
        }
        else
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            InnerNode* newinner = allocate_inner(inner->level);

            newinner->slotuse = inner->slotuse;
            std::copy(inner->slotkey, inner->slotkey + inner->slotuse,
                      newinner->slotkey);

            for (unsigned short slot = 0; slot <= inner->slotuse; ++slot)
            {
                newinner->childid[slot] = copy_recursive(inner->childid[slot]);
            }

            return newinner;
        }
    }

    //! \}

public:
    //! \name Public Insertion Functions
    //! \{

    //! Attempt to insert a key/data pair into the B+ tree. If the tree does not
    //! allow duplicate keys, then the insert may fail if it is already present.
    std::pair<iterator, bool> insert(const value_type& x) {
        insert_res res;
        int tries __attribute__((unused)) = 0; // TODO delete
        do {
            tries++;
            res = insert_start(key_of_value::get(x), x);
            if (res.retry) {
                log_retry(nullptr);
            }
        } while (res.retry);
        return std::make_pair(res.it, res.inserted);
    }

    //! Attempt to insert a key/data pair into the B+ tree. The iterator hint is
    //! currently ignored by the B+ tree insertion routine.
    iterator insert(iterator /* hint */, const value_type& x) {
        insert_res res;
        do {
            res = insert_start(key_of_value::get(x), x);
            if (res.retry) {
                log_retry(nullptr);
            }
        } while (res.retry);
        return res.it;
    }

    //! Attempt to insert the range [first,last) of value_type pairs into the B+
    //! tree. Each key/data pair is inserted individually; to bulk load the
    //! tree, use a constructor with range.
    template <typename InputIterator>
    void insert(InputIterator first, InputIterator last) {
        InputIterator iter = first;
        while (iter != last)
        {
            insert(*iter);
            ++iter;
        }
    }

    //! \}
private:
    //! \name Result Types for Insertion Functions
    //! \{

    // TODO desc
    struct insert_res {
        iterator it;
        bool inserted;
        bool retry;

        insert_res(iterator i, bool in):
            it(i), inserted(in), retry(false)
        {}

        insert_res():
            it(nullptr, 0), inserted(false), retry(true)
        {}
    };

    enum check_split_res {
        no_split = 0,
        did_split = 1,
        retry = 2,
    };

    //! \}

private:
    //! \name Private Insertion Functions
    //! \{

    //! Start the insertion descent at the current root and handle root splits.
    //! Returns true if the item was inserted
    insert_res
    insert_start(const key_type& key, const value_type& value) {
        //TLX_BTREE_PRINT("insert start");
        root_->lock->readlock();

        if (root_->level == 0) {
            TLX_BTREE_ASSERT(root_->slotuse == 0);
            //TLX_BTREE_ASSERT(stats_.size == 0);

            int oldgen = root_->gen;
            root_->lock->upgradelock();

            if (root_->gen != oldgen) {
                root_->lock->write_unlock();
                return insert_res();
            }

            TLX_BTREE_ASSERT(root_->level == 0);
            TLX_BTREE_ASSERT(root_->slotuse == 0);
            //TLX_BTREE_ASSERT(stats_.size == 0);

            root_->childid[0] = head_leaf_ = tail_leaf_ = allocate_leaf();

            root_->level++;
            root_->gen++;
            root_->lock->downgrade_lock();
        }

        if (root_->is_full()) {
            root_->lock->upgradelock();

            if (!root_->is_full()) {
                root_->lock->write_unlock();
                return insert_res();
            }

            InnerNode* leftchild = allocate_inner(root_->level);
            leftchild->lock->writelock(false);

            // copy root into leftchild
            std::copy(root_->slotkey, root_->slotkey + root_->slotuse,
                    leftchild->slotkey);
            std::copy(root_->childid, root_->childid + root_->slotuse + 1,
                    leftchild->childid);
            leftchild->slotuse = root_->slotuse;

            key_type newkey = key_type();
            InnerNode* rightchild = nullptr;
            split_inner_node(leftchild, &newkey, &rightchild); // TODO first split then copy

            leftchild->lock->downgrade_lock();

            root_->slotkey[0] = newkey;
            root_->childid[0] = leftchild;
            root_->childid[1] = rightchild;

            root_->slotuse = 1;
            root_->level++;
            root_->gen++;

            root_->lock->downgrade_lock();
            leftchild->lock->read_unlock();
            rightchild->lock->read_unlock();
        }

        insert_res r =
            insert_descend(root_, key, value);

        if (r.retry) return r;

        // increment size if the item was inserted
        if (r.inserted) ++stats_.size;

#ifdef TLX_BTREE_DEBUG
        if (debug) print(std::cout);
#endif

        if (self_verify) {
            verify();
            TLX_BTREE_ASSERT(exists(key));
        }

        return r;
    }

    /*!
     * Insert an item into the B+ tree.
     *
     * Descend down the nodes to a leaf, insert the key/data pair in a free
     * slot. If the node overflows, then it must be split and the new split node
     * inserted into the parent. Unroll / this splitting up to the root.
    */
    insert_res insert_descend(
        node* n, const key_type& key, const value_type& value) {

        if (!n->is_leafnode())
        {
            InnerNode* inner = static_cast<InnerNode*>(n);
            TLX_BTREE_ASSERT(inner->lock->readlocked());
            unsigned short slot = find_lower(inner, key);

            bool childisleaf = inner->level == 1;
            if (childisleaf) {
                LeafNode* child_leaf = static_cast<LeafNode*>(inner->childid[slot]);
                child_leaf->lock->writelock();
            } else {
                InnerNode* child_inner = static_cast<InnerNode*>(inner->childid[slot]);
                child_inner->lock->readlock();
            }

            check_split_res res = check_split_child(inner, inner->childid[slot], slot);
            if (res == retry) return insert_res(); // unlocked in check_split_child

            // unlocking the irrelevant child
            if (res == did_split) {
                // if the key ended up moving
                if (key_less(inner->key(slot), key)){
                    if (childisleaf) {
                        LeafNode* child_leaf = static_cast<LeafNode*>(inner->childid[slot]);
                        child_leaf->lock->write_unlock();
                    } else {
                        InnerNode* child_inner = static_cast<InnerNode*>(inner->childid[slot]);
                        child_inner->lock->read_unlock();
                    }
                    slot++;
                } else {
                    if (childisleaf) {
                        LeafNode* child_leaf = static_cast<LeafNode*>(inner->childid[slot + 1]);
                        child_leaf->lock->write_unlock();
                    } else {
                        InnerNode* child_inner = static_cast<InnerNode*>(inner->childid[slot + 1]);
                        child_inner->lock->read_unlock();
                    }
                }
            }

            //TLX_BTREE_PRINT(
                //"SLBTree::insert_descend into " << inner->childid[slot]);

            n = inner->childid[slot];
            inner->lock->read_unlock();

            insert_res r =
                insert_descend(n, key, value);

            return r;
        }
        else // n->is_leafnode() == true
        {
            LeafNode* leaf = static_cast<LeafNode*>(n);

            unsigned short slot = find_lower(leaf, key);

            if (!allow_duplicates &&
                slot < leaf->slotuse && key_equal(key, leaf->key(slot))) {

                leaf->lock->write_unlock();
                return insert_res(iterator(leaf, slot), false);
            }

            // move items and put data item into correct data slot
            TLX_BTREE_ASSERT(slot >= 0 && slot <= leaf->slotuse);
            // leaf will not overflow
            TLX_BTREE_ASSERT(leaf->slotuse + 1 <= leaf_slotmax);

            std::copy_backward(
                leaf->slotdata + slot, leaf->slotdata + leaf->slotuse,
                leaf->slotdata + leaf->slotuse + 1);
            leaf->slotdata[slot] = value;
            leaf->slotuse++;

            insert_res ret_val = insert_res(iterator(leaf, slot), true);
            leaf->lock->write_unlock();
            return ret_val;
        }
    }

    //! Check if the node's child need to be split,
    //! in which case it splits it. Returns whether or now the child was split
    check_split_res check_split_child(InnerNode* parent, node* c, unsigned short slot) {
        TLX_BTREE_ASSERT(parent->childid[slot] == c);
        TLX_BTREE_ASSERT(parent->lock->readlocked());

        if (c->is_leafnode())
        {
            LeafNode* child = static_cast<LeafNode*>(c);
            TLX_BTREE_ASSERT(child->lock->writelocked());
            if (child->is_full())
            {
                int oldgen = parent->gen;
                child->lock->write_unlock();
                parent->lock->upgradelock();

                if (parent->gen != oldgen) {
                    parent->lock->write_unlock();
                    return retry;
                }

                child->lock->writelock();

                if (parent->childid[slot] != child
                        || !child->is_full()) {
                    parent->lock->write_unlock();
                    child->lock->write_unlock();
                    return retry;
                }

                key_type newkey = key_type();
                LeafNode* newleaf = nullptr;

                split_leaf_node(child, &newkey, &newleaf);

                child->gen++;
                parent->gen++;

                std::copy_backward(
                    parent->slotkey + slot, parent->slotkey + parent->slotuse,
                    parent->slotkey + parent->slotuse + 1);
                std::copy_backward(
                    parent->childid + slot, parent->childid + parent->slotuse + 1,
                    parent->childid + parent->slotuse + 2);

                parent->slotkey[slot] = newkey;
                parent->childid[slot + 1] = newleaf;
                parent->slotuse++;

                parent->lock->downgrade_lock();
                return did_split;
            }
        }
        else
        {
            InnerNode* child = static_cast<InnerNode*>(c);
            TLX_BTREE_ASSERT(child->lock->readlocked());
            if (child->is_full()) {
                int oldgen = parent->gen;
                child->lock->read_unlock();
                parent->lock->upgradelock();

                if (parent->gen != oldgen) {
                    parent->lock->write_unlock();
                    return retry;
                }

                child->lock->writelock();

               if (parent->childid[slot] != child
                        || !child->is_full()) {
                    parent->lock->write_unlock();
                    child->lock->write_unlock();
                    return retry;
                }

                key_type newkey = key_type();
                InnerNode* newnode = nullptr;

                split_inner_node(child, &newkey, &newnode);
                child->gen++;
                child->lock->downgrade_lock();

                std::copy_backward(
                    parent->slotkey + slot, parent->slotkey + parent->slotuse,
                    parent->slotkey + parent->slotuse + 1);
                std::copy_backward(
                    parent->childid + slot, parent->childid + parent->slotuse + 1,
                    parent->childid + parent->slotuse + 2);

                parent->slotkey[slot] = newkey;
                parent->childid[slot + 1] = newnode;
                parent->slotuse++;
                parent->gen++;

                parent->lock->downgrade_lock();
                return did_split;
            }
        }
        return no_split;
    }

    //! Split up a leaf node into two equally-filled sibling leaves. Returns the
    //! new nodes and its insertion key in the two parameters. The new
    //! node is write locked.
    void split_leaf_node(LeafNode* leaf,
                         key_type* out_newkey, LeafNode** out_newleaf) {

        TLX_BTREE_ASSERT(leaf->is_full());
        TLX_BTREE_ASSERT(leaf->lock->writelocked());

        unsigned short mid = (leaf->slotuse >> 1);

        //TLX_BTREE_PRINT("SLBTree::split_leaf_node on " << leaf);

        LeafNode* newleaf = allocate_leaf();
        newleaf->lock->writelock(false);

        newleaf->slotuse = leaf->slotuse - mid;

        newleaf->next_leaf = leaf->next_leaf;
        if (newleaf->next_leaf == nullptr) {
            TLX_BTREE_ASSERT(leaf == tail_leaf_);
            tail_leaf_ = newleaf;
        }
        else {
            newleaf->next_leaf->prev_leaf = newleaf;
        }

        std::copy(leaf->slotdata + mid, leaf->slotdata + leaf->slotuse,
                  newleaf->slotdata);

        leaf->slotuse = mid;
        leaf->next_leaf = newleaf;
        newleaf->prev_leaf = leaf;

        *out_newkey = leaf->key(leaf->slotuse - 1);
        *out_newleaf = newleaf;
    }

    //! Split up an inner node into two equally-filled sibling nodes. Returns
    //! the new nodes and its insertion key in the two parameters. The new
    //! node is read locked.
    void split_inner_node(InnerNode* inner, key_type* out_newkey,
                          InnerNode** out_newinner) {
        // TODO changed type of out_newinner from node to InnerNode, contribute to original
        // also split leaf node too
        TLX_BTREE_ASSERT(inner->is_full());
        TLX_BTREE_ASSERT(inner->lock->writelocked());

        unsigned short mid = (inner->slotuse >> 1);

        //TLX_BTREE_PRINT("SLBTree::split_inner: mid " << mid);

        //TLX_BTREE_PRINT("SLBTree::split_inner_node on " << inner <<
                        //" into two nodes " << mid << " and " <<
                        //inner->slotuse - (mid + 1) << " sized");

        InnerNode* newinner = allocate_inner(inner->level);
        newinner->lock->writelock(false);

        newinner->slotuse = inner->slotuse - (mid + 1);
        newinner->lock->downgrade_lock();

        std::copy(inner->slotkey + mid + 1, inner->slotkey + inner->slotuse,
                  newinner->slotkey);
        std::copy(inner->childid + mid + 1, inner->childid + inner->slotuse + 1,
                  newinner->childid);

        inner->slotuse = mid;

        *out_newkey = inner->key(mid);
        *out_newinner = newinner;
    }

    //! \}

public:
#if 0
    //! \name Bulk Loader - Construct Tree from Sorted Sequence
    //! \{

    //! Bulk load a sorted range. Loads items into leaves and constructs a
    //! B-tree above them. The tree must be empty when calling this function.
    template <typename Iterator>
    void bulk_load(Iterator ibegin, Iterator iend) {
        TLX_BTREE_ASSERT(empty());

        stats_.size = iend - ibegin;

        // calculate number of leaves needed, round up.
        size_t num_items = iend - ibegin;
        size_t num_leaves = (num_items + leaf_slotmax - 1) / leaf_slotmax;

        /*TLX_BTREE_PRINT("SLBTree::bulk_load, level 0: " << stats_.size <<
                        " items into " << num_leaves <<
                        " leaves with up to " <<
                        ((iend - ibegin + num_leaves - 1) / num_leaves) <<
                        " items per leaf.");*/

        Iterator it = ibegin;
        for (size_t i = 0; i < num_leaves; ++i)
        {
            // allocate new leaf node
            LeafNode* leaf = allocate_leaf();

            // copy keys or (key,value) pairs into leaf nodes, uses template
            // switch leaf->set_slot().
            leaf->slotuse = static_cast<int>(num_items / (num_leaves - i));
            for (size_t s = 0; s < leaf->slotuse; ++s, ++it)
                leaf->set_slot(s, *it);

            if (tail_leaf_ != nullptr) {
                tail_leaf_->next_leaf = leaf;
                leaf->prev_leaf = tail_leaf_;
            }
            else {
                head_leaf_ = leaf;
            }
            tail_leaf_ = leaf;

            num_items -= leaf->slotuse;
        }

        TLX_BTREE_ASSERT(it == iend && num_items == 0);

        // if the btree is so small to fit into one leaf, then we're done.
        if (head_leaf_ == tail_leaf_) {
            root_ = head_leaf_;
            return;
        }

        TLX_BTREE_ASSERT(stats_.leaves == num_leaves);

        // create first level of inner nodes, pointing to the leaves.
        size_t num_parents =
            (num_leaves + (inner_slotmax + 1) - 1) / (inner_slotmax + 1);

        /*TLX_BTREE_PRINT("SLBTree::bulk_load, level 1: " <<
                        num_leaves << " leaves in " <<
                        num_parents << " inner nodes with up to " <<
                        ((num_leaves + num_parents - 1) / num_parents) <<
                        " leaves per inner node.");*/

        // save inner nodes and maxkey for next level.
        typedef std::pair<InnerNode*, const key_type*> nextlevel_type;
        nextlevel_type* nextlevel = new nextlevel_type[num_parents];

        LeafNode* leaf = head_leaf_;
        for (size_t i = 0; i < num_parents; ++i)
        {
            // allocate new inner node at level 1
            InnerNode* n = allocate_inner(1);

            n->slotuse = static_cast<int>(num_leaves / (num_parents - i));
            TLX_BTREE_ASSERT(n->slotuse > 0);
            // this counts keys, but an inner node has keys+1 children.
            --n->slotuse;

            // copy last key from each leaf and set child
            for (unsigned short s = 0; s < n->slotuse; ++s)
            {
                n->slotkey[s] = leaf->key(leaf->slotuse - 1);
                n->childid[s] = leaf;
                leaf = leaf->next_leaf;
            }
            n->childid[n->slotuse] = leaf;

            // track max key of any descendant.
            nextlevel[i].first = n;
            nextlevel[i].second = &leaf->key(leaf->slotuse - 1);

            leaf = leaf->next_leaf;
            num_leaves -= n->slotuse + 1;
        }

        TLX_BTREE_ASSERT(leaf == nullptr && num_leaves == 0);

        // recursively build inner nodes pointing to inner nodes.
        for (int level = 2; num_parents != 1; ++level)
        {
            size_t num_children = num_parents;
            num_parents =
                (num_children + (inner_slotmax + 1) - 1) / (inner_slotmax + 1);

            /*TLX_BTREE_PRINT(
                "SLBTree::bulk_load, level " << level <<
                    ": " << num_children << " children in " <<
                    num_parents << " inner nodes with up to " <<
                    ((num_children + num_parents - 1) / num_parents) <<
                    " children per inner node.");*/

            size_t inner_index = 0;
            for (size_t i = 0; i < num_parents; ++i)
            {
                // allocate new inner node at level
                InnerNode* n = allocate_inner(level);

                n->slotuse = static_cast<int>(num_children / (num_parents - i));
                TLX_BTREE_ASSERT(n->slotuse > 0);
                // this counts keys, but an inner node has keys+1 children.
                --n->slotuse;

                // copy children and maxkeys from nextlevel
                for (unsigned short s = 0; s < n->slotuse; ++s)
                {
                    n->slotkey[s] = *nextlevel[inner_index].second;
                    n->childid[s] = nextlevel[inner_index].first;
                    ++inner_index;
                }
                n->childid[n->slotuse] = nextlevel[inner_index].first;

                // reuse nextlevel array for parents, because we can overwrite
                // slots we've already consumed.
                nextlevel[i].first = n;
                nextlevel[i].second = nextlevel[inner_index].second;

                ++inner_index;
                num_children -= n->slotuse + 1;
            }

            TLX_BTREE_ASSERT(num_children == 0);
        }

        root_ = nextlevel[0].first;
        delete[] nextlevel;

        if (self_verify) verify();
    }

    //! \}
#else
    // bulk_load not implemented
    template <typename Iterator>
    void bulk_load(Iterator ibegin, Iterator iend) {
        (void)ibegin;
        (void)iend;
        abort();
    }
#endif

private:
    //! \name Support Class Encapsulating Deletion Results
    //! \{

    //! Result flags of recursive deletion.
    enum result_flags_t {
        //! Deletion successful and no fix-ups necessary.
        btree_ok = 0,

        //! Deletion not successful because key was not found.
        btree_not_found = 1,

        //! Deletion successful, the last key was updated so parent slotkeys
        //! need updates.
        btree_update_lastkey = 2,

        //! Deletion successful, children nodes were merged and the parent needs
        //! to remove the empty node.
        btree_fixmerge = 4,

        // TODO yes this is stupid i know
        restart = 5
    };

    //! B+ tree recursive deletion has much information which is needs to be
    //! passed upward.
    struct result_t {
        //! Merged result flags
        result_flags_t flags;

        //! The key to be updated at the parent's slot
        key_type lastkey;

        //! Constructor of a result with a specific flag, this can also be used
        //! as for implicit conversion.
        result_t(result_flags_t f = btree_ok) // NOLINT
            : flags(f), lastkey()
        { }

        //! Constructor with a lastkey value.
        result_t(result_flags_t f, const key_type& k)
            : flags(f), lastkey(k)
        { }

        //! Test if this result object has a given flag set.
        bool has(result_flags_t f) const {
            return (flags & f) != 0;
        }

        //! Merge two results OR-ing the result flags and overwriting lastkeys.
        result_t& operator |= (const result_t& other) {
            flags = result_flags_t(flags | other.flags);

            // we overwrite existing lastkeys on purpose
            if (other.has(btree_update_lastkey))
                lastkey = other.lastkey;

            return *this;
        }
    };
    //! \}

public:
    //! \name Public Erase Functions
    //! \{

    //! Erases one (the first) of the key/data pairs associated with the given
    //! key.
    bool erase_one(const key_type key) {
        result_flags_t res;
        do {
            res = erase_one_start(key);
            if (res == restart) {
                log_retry(nullptr);
            }
        } while (res == restart);

        return res == btree_ok;
    }

    //! Erases all the key/data pairs associated with the given key. This is
    //! implemented using erase_one().
    size_type erase(const key_type& key) {
        size_type c = 0;

        while (erase_one(key))
        {
            ++c;
            if (!allow_duplicates) break;
        }

        return c;
    }

    //! Erase the key/data pair referenced by the iterator.
    void erase(iterator iter) {
        TLX_BTREE_PRINT("SLBTree::erase_iter(" << iter.curr_leaf <<
                        "," << iter.curr_slot << ") on btree size " << size());

        if (self_verify) verify();

        if (!root_) return;

        bool successful;
        if (!allow_duplicates) {
            successful = erase_one(iter.key());
        } else {
            /*result_t result = erase_iter_descend(
                iter, root_, nullptr, nullptr, nullptr, nullptr, nullptr, 0);
            successful = !result.has(btree_not_found);*/
            // not implemented because I changed the structure of root
            abort();
        }

        if (successful)
            --stats_.size;

#ifdef TLX_BTREE_DEBUG
        if (debug) print(std::cout);
#endif
        if (self_verify) verify();
    }

#ifdef BTREE_TODO
    //! Erase all key/data pairs in the range [first,last). This function is
    //! currently not implemented by the B+ Tree.
    void erase(iterator /* first */, iterator /* last */) {
        abort();
    }
#endif

    //! \}

private:
    //! \name Private Erase Functions
    //! \{

    result_flags_t erase_one_start(const key_type& key) {
        TLX_BTREE_PRINT("SLBTree::erase_one(" << key <<
                        ") on btree size " << size());

        if (self_verify) verify();
        root_->lock->readlock();

        if (root_->level == 0) {
            root_->lock->read_unlock();
            return btree_not_found;
        }

        // checking edge case (must do this when root_ is locked)
        if (root_->slotuse == 0) {
            LeafNode* child = static_cast<LeafNode*>(root_->childid[0]);
            child->lock->readlock();

            if (child->slotuse == 1) {
                if (key_equal(child->key(0), key)) {
                    child->lock->read_unlock();

                    int oldgen = root_->gen;
                    root_->lock->upgradelock();
                    if (root_->gen != oldgen) {
                        root_->lock->write_unlock();
                        return restart;
                    }
                    child->lock->writelock();

                    if (child->slotuse != 1) {
                        child->lock->write_unlock();
                        root_->lock->write_unlock();
                        return restart;
                    }

                    free_node(child);
                    root_->gen++;
                    root_->level = 0;
                    stats_.size--;

                    root_->lock->write_unlock();
                    return btree_ok;
                } else {
                    root_->lock->read_unlock();
                    child->lock->read_unlock();
                    return btree_not_found;
                }
            } else {
                child->lock->read_unlock();
            }
        }

        // if the root only has one child,
        // root_ will only point to one node, and therefore should
        // be skipped in erase_one_descend
        node* to_descendin = root_;
        bool childisleaf = root_->level == 1;

        if (root_->slotuse == 1) {
            TAKE_LOCK(childisleaf, root_->childid[0], readlock);
            TAKE_LOCK(childisleaf, root_->childid[1], readlock);

            node* leftchild = root_->childid[0];
            node* rightchild = root_->childid[1];

            auto slotmax =
                    leftchild->is_leafnode() ? leaf_slotmax : inner_slotmax;
            if (leftchild->slotuse + rightchild->slotuse < slotmax)
            {
                // case: they can merge
                int oldgen = root_->gen;

                TAKE_LOCK(childisleaf, leftchild, read_unlock);
                TAKE_LOCK(childisleaf, rightchild, read_unlock);

                root_->lock->upgradelock();

                if (root_->gen != oldgen) {
                    root_->lock->write_unlock();
                    return restart;
                }

                leftchild = root_->childid[0];
                rightchild = root_->childid[1];

                TAKE_LOCK(childisleaf, leftchild, writelock);
                TAKE_LOCK(childisleaf, rightchild, writelock);

                if (leftchild->slotuse + rightchild->slotuse >= slotmax) {
                    root_->lock->write_unlock();
                    TAKE_LOCK(childisleaf, leftchild, write_unlock);
                    TAKE_LOCK(childisleaf, rightchild, write_unlock);
                    return restart;
                }

                if (leftchild->is_leafnode()) {
                    LeafNode* leftleaf = static_cast<LeafNode*>(leftchild);
                    LeafNode* rightleaf = static_cast<LeafNode*>(rightchild);

                    merge_leaves(leftleaf, rightleaf, root_);
                    free_node(rightleaf);

                    root_->childid[0] = leftleaf;
                    root_->slotuse = 0;

                    root_->gen++;
                    leftleaf->gen++;

                    root_->lock->downgrade_lock();
                    leftleaf->lock->write_unlock();
                } else {
                    InnerNode* leftinner = static_cast<InnerNode*>(leftchild);
                    InnerNode* rightinner = static_cast<InnerNode*>(rightchild);

                    merge_inner(leftinner, rightinner, root_, 0);

                    std::copy(leftinner->slotkey, leftinner->slotkey + leftinner->slotuse,
                            root_->slotkey);
                    std::copy(leftinner->childid, leftinner->childid + leftinner->slotuse + 1,
                            root_->childid);

                    root_->slotuse = leftinner->slotuse;

                    free_node(leftinner);
                    free_node(rightinner);

                    root_->gen++;
                    root_->level--;
                    root_->lock->downgrade_lock();
                }
            } else {
                TAKE_LOCK(childisleaf, leftchild, read_unlock);
                TAKE_LOCK(childisleaf, rightchild, read_unlock);
            }
        }

        if (root_->slotuse == 0) {
            if (root_->level > 1) static_cast<InnerNode*>(root_->childid[0])->lock->readlock();
            else static_cast<LeafNode*>(root_->childid[0])->lock->writelock();
            root_->lock->read_unlock();
            to_descendin = root_->childid[0];
        }
        //if (self_verify) verify();

        result_flags_t result = erase_one_descend(
            key, to_descendin);

        if (result == restart) return restart;

        if (result == btree_ok)
            --stats_.size;

#ifdef TLX_BTREE_DEBUG
        if (debug) print(std::cout);
#endif
        if (self_verify) verify();

        return result;
    }

    /*!
     * Erase one (the first) key/data pair in the B+ tree matching key.
     *
     * Descends down the tree in search of key. During the descent the parent,
     * left and right siblings and their parents are computed and passed
     * down. Once the key/data pair is found, it is removed from the leaf. If
     * the leaf underflows 6 different cases are handled. These cases resolve
     * the underflow by shifting key/data pairs from adjacent sibling nodes,
     * merging two sibling nodes or trimming the tree.
     */
    result_flags_t erase_one_descend(const key_type& key, node* curr) {
        TLX_BTREE_PRINT("erase one descend(" << key << "," << curr <<
                ") on btree size " << size());

        //if (self_verify) verify();

        if (curr->is_leafnode())
        {
            LeafNode* leaf = static_cast<LeafNode*>(curr);
            TLX_BTREE_ASSERT(leaf->lock->writelocked());

            TLX_BTREE_PRINT("erase one descend LeafNode" << leaf);

            if (stats_.size > leaf->slotuse && leaf->is_underflow()) {
                TLX_BTREE_ASSERT(cur_numthreads > 1);
                leaf->lock->write_unlock();
                return restart;
            }

            unsigned short slot = find_lower(leaf, key);

            if (slot >= leaf->slotuse || !key_equal(key, leaf->key(slot)))
            {
                TLX_BTREE_PRINT("Could not find key " << key << " to erase.");

                leaf->lock->write_unlock();
                return btree_not_found;
            }

            TLX_BTREE_PRINT(
                "Found key in leaf " << curr << " at slot " << slot);

            std::copy(leaf->slotdata + slot + 1, leaf->slotdata + leaf->slotuse,
                      leaf->slotdata + slot);

            leaf->slotuse--;
            leaf->lock->write_unlock();
            return btree_ok;
        }
        else // !curr->is_leafnode()
        {
            InnerNode* parent = static_cast<InnerNode*>(curr);
            TLX_BTREE_ASSERT(parent->lock->readlocked());

            TLX_BTREE_PRINT("erase one descend InnerNode" << parent);
            unsigned short slot = find_lower(parent, key);

            if (parent->level == 1) {
                LeafNode* child = static_cast<LeafNode*>(parent->childid[slot]);
                child->lock->writelock();

                if (child->is_underflow()) {
                    int oldgen = parent->gen;
                    child->lock->write_unlock();
                    parent->lock->upgradelock();

                    if (parent->gen != oldgen) {
                        parent->lock->write_unlock();
                        return restart;
                    }

                    TLX_BTREE_ASSERT(parent->childid[slot] == child);
                    child->lock->writelock();

                    if (!child->is_underflow()) {
                        parent->lock->write_unlock();
                        child->lock->write_unlock();
                        return restart;
                    }

                    LeafNode* other;

                    // whether the child is the parent's last child
                    bool lastslot = slot == parent->slotuse;
                    if (lastslot) {
                        other = static_cast<LeafNode*>
                                (parent->childid[slot - 1]);
                        other->lock->writelock();
                    } else {
                        other = static_cast<LeafNode*>
                                (parent->childid[slot + 1]);
                        other->lock->writelock();
                    }
                    TLX_BTREE_PRINT("erase one descend: leaf redistribute");
                    if (lastslot)
                        redistribute_leaf_children(parent, other, &child, slot - 1);
                    else
                        redistribute_leaf_children(parent, child, &other, slot);

                    bool slot_moved = false;
                    if (other != nullptr && child != nullptr) {
                        // assuming merge didn't happen
                        if (key_greater(key, parent->key(slot)) &&
                                !lastslot) {
                            slot++;
                            slot_moved = true;
                        } else if (key_greaterequal(parent->key(slot - 1), key) &&
                                lastslot) {
                            slot--;
                            slot_moved = true;
                        }
                    } else if (lastslot) { // && rightchild == nullptr
                        slot--;
                        slot_moved = true;
                    }

                    if (slot_moved) {
                        if (child) child->lock->write_unlock();
                    } else if (other) {
                        other->lock->write_unlock();
                    }

                    TLX_BTREE_ASSERT(find_lower(parent, key) == slot);
                }
            } else {
                InnerNode* child = static_cast<InnerNode*>(parent->childid[slot]);
                child->lock->readlock();

                if (child->is_underflow()) {
                    int oldgen = parent->gen;
                    child->lock->read_unlock();
                    parent->lock->upgradelock();

                    if (parent->gen != oldgen) {
                        parent->lock->write_unlock();
                        return restart;
                    }

                    TLX_BTREE_ASSERT(parent->childid[slot] == child);
                    child->lock->writelock();

                    if (!child->is_underflow()) {
                        parent->lock->write_unlock();
                        child->lock->write_unlock();
                        return restart;
                    }

                    InnerNode* other;

                    // whether the child is the parent's last child
                    bool lastslot = slot == parent->slotuse;
                    if (lastslot) {
                        other = static_cast<InnerNode*>
                                (parent->childid[slot - 1]);
                        other->lock->writelock();
                    } else {
                        other = static_cast<InnerNode*>
                                (parent->childid[slot + 1]);
                        other->lock->writelock();
                    }
                    TLX_BTREE_PRINT("erase one descend: inner redistribute");
                    result_flags_t res;
                    if (lastslot)
                        res = redistribute_inner_children(parent, other, &child, slot - 1);
                    else
                        res = redistribute_inner_children(parent, child, &other, slot);

                    if (res) return restart;

                    bool slot_moved = false;
                    if (other != nullptr && child != nullptr) {
                        // assuming merge didn't happen
                        if (key_greater(key, parent->key(slot)) &&
                                !lastslot) {
                            slot++;
                            slot_moved = true;
                        } else if (key_greaterequal(parent->key(slot - 1), key) &&
                                lastslot) {
                            slot--;
                            slot_moved = true;
                        }
                    } else if (lastslot) { // && rightchild == nullptr
                        slot--;
                        slot_moved = true;
                    }

                    if (slot_moved) {
                        if (child) child->lock->read_unlock();
                    } else if (other) {
                        other->lock->read_unlock();
                    }

                    TLX_BTREE_ASSERT(find_lower(parent, key) == slot);
                }
            }

            //if (self_verify) verify();

            curr = parent->childid[slot];
            parent->lock->read_unlock();
            return erase_one_descend(key, curr);
        }
    }

    // Redistributes/merges the data of two adjacent leaf children of the parent.
    // It is assumed that the children need rebalancing/merging.
    void redistribute_leaf_children(InnerNode* parent, LeafNode* leftchild,
                                    LeafNode** rightchildp, unsigned int parentslot) {
        LeafNode* rightchild = *rightchildp;
        // TODO: this is a random place to put this todo, but uncomment the prints
        TLX_BTREE_PRINT("redistribute_leaf_children(" << parent << "," << leftchild
                 << "," << rightchild << "," << parentslot << ")");
        TLX_BTREE_ASSERT(leftchild->is_underflow() || rightchild->is_underflow());
        TLX_BTREE_ASSERT(parentslot < parent->slotuse);
        TLX_BTREE_ASSERT(parent->childid[parentslot] == leftchild);
        TLX_BTREE_ASSERT(parent->childid[parentslot + 1] == rightchild);
        TLX_BTREE_ASSERT(parent->level > 0);

        TLX_BTREE_ASSERT(parent->lock->writelocked());
        TLX_BTREE_ASSERT(leftchild->lock->writelocked());
        TLX_BTREE_ASSERT(rightchild->lock->writelocked());

        // case: merging is necessary
        if ( (leftchild->is_few() && rightchild->is_underflow())
            || (leftchild->is_underflow() && rightchild->is_few()))
        {
            merge_leaves(leftchild, rightchild, parent);

            TLX_BTREE_ASSERT(rightchild->slotuse == 0);

            free_node(rightchild);
            *rightchildp = nullptr;

            // move parent slots/children to not point to rightchild
            std::copy(
                parent->slotkey + parentslot + 1,
                parent->slotkey + parent->slotuse,
                parent->slotkey + parentslot);
            std::copy(
                parent->childid + parentslot + 2,
                parent->childid + parent->slotuse + 1,
                parent->childid + parentslot + 1);

            parent->slotuse--;

            if (parentslot == 0) {
                if (parent->slotuse > 0)
                    TLX_BTREE_ASSERT(key_greaterequal(parent->slotkey[parentslot],
                            leftchild->key(leftchild->slotuse - 1)));
            } else {
                // this looks weird but its because leftchild is
                // now the right child of (parentslot - 1) (because parentslot was deleted)
                // because of the merge
                TLX_BTREE_ASSERT(key_lessequal(parent->slotkey[parentslot - 1],
                    leftchild->key(0)));
            }
        }
         // case: the left takes from the right
        else if (leftchild->is_underflow())
        {
            TLX_BTREE_ASSERT(rightchild->slotuse > leftchild->slotuse);
            result_t res __attribute__((unused)) =
                    shift_left_leaf(leftchild, rightchild, parent, parentslot);
            TLX_BTREE_ASSERT(!res.has(btree_update_lastkey));
        }
        // case: the right takes from the left
        else
        {
            TLX_BTREE_ASSERT(rightchild->slotuse < leftchild->slotuse);
            TLX_BTREE_ASSERT(rightchild->is_few());
            shift_right_leaf(leftchild, rightchild, parent, parentslot);
        }

        leftchild->gen++;
        if (*rightchildp) rightchild->gen++;
        parent->gen++;
        parent->lock->downgrade_lock();
    }

    // Redistributes/merges the keys of two adjacent inner children of the parent.
    // It is assumed that the children need rebalancing/merging.
    result_flags_t redistribute_inner_children(InnerNode* parent, InnerNode* leftchild,
                                    InnerNode** rightchildp, unsigned int parentslot) {
        InnerNode* rightchild = *rightchildp;
        TLX_BTREE_PRINT("redistribute_inner_children(" << parent << "," << leftchild
                 << "," << rightchild << "," << parentslot << ")");
        TLX_BTREE_ASSERT(leftchild->is_underflow() || rightchild->is_underflow());
        TLX_BTREE_ASSERT(parentslot < parent->slotuse);
        TLX_BTREE_ASSERT(parent->childid[parentslot] == leftchild);
        TLX_BTREE_ASSERT(parent->childid[parentslot + 1] == rightchild);

        TLX_BTREE_ASSERT(parent->lock->writelocked());
        TLX_BTREE_ASSERT(leftchild->lock->writelocked());
        TLX_BTREE_ASSERT(rightchild->lock->writelocked());

        // case: merging is necessary.
        if ( (leftchild->is_few() && rightchild->is_underflow())
            || (leftchild->is_underflow() && rightchild->is_few()))
        {
            if (parent == root_ && parent->slotuse <= 1) {
                TLX_BTREE_ASSERT(parent->slotuse == 1);
                parent->lock->write_unlock();
                leftchild->lock->write_unlock();
                rightchild->lock->write_unlock();
                return restart;
            }

            merge_inner(leftchild, rightchild, parent, parentslot);

            TLX_BTREE_ASSERT(rightchild->slotuse == 0);

            free_node(rightchild);
            *rightchildp = nullptr;

            std::copy(
                parent->slotkey + parentslot + 1,
                parent->slotkey + parent->slotuse,
                parent->slotkey + parentslot);
            std::copy(
                parent->childid + parentslot + 2,
                parent->childid + parent->slotuse + 1,
                parent->childid + parentslot + 1);

            parent->slotuse--;

            if (parentslot == 0) {
                TLX_BTREE_ASSERT(key_greaterequal(parent->slotkey[parentslot],
                    leftchild->key(leftchild->slotuse - 1)));
            } else {
            // this looks weird but its because leftchild is
            // now the right child of (parentslot - 1) (because parentslot was deleted)
            // because of the merge
            TLX_BTREE_ASSERT(key_lessequal(parent->slotkey[parentslot - 1],
                    leftchild->key(0)));
            }
        }
         // case: the left takes from the right
        else if (leftchild->is_underflow())
        {
            TLX_BTREE_ASSERT(rightchild->slotuse > leftchild->slotuse);
            shift_left_inner(leftchild, rightchild, parent, parentslot);
        }
        // case: the right takes from the left
        else
        {
            TLX_BTREE_ASSERT(rightchild->slotuse < leftchild->slotuse);
            TLX_BTREE_ASSERT(rightchild->is_few());
            shift_right_inner(leftchild, rightchild, parent, parentslot);
        }

        parent->gen++;
        leftchild->gen++;
        parent->lock->downgrade_lock();
        leftchild->lock->downgrade_lock();
        if (*rightchildp != nullptr) {
            rightchild->gen++;
            rightchild->lock->downgrade_lock();
        }
        return btree_ok;
    }

#if 0
    /*!
     * Erase one key/data pair referenced by an iterator in the B+ tree.
     *
     * Descends down the tree in search of an iterator. During the descent the
     * parent, left and right siblings and their parents are computed and passed
     * down. The difficulty is that the iterator contains only a pointer to a
     * LeafNode, which means that this function must do a recursive depth first
     * search for that leaf node in the subtree containing all pairs of the same
     * key. This subtree can be very large, even the whole tree, though in
     * practice it would not make sense to have so many duplicate keys.
     *
     * Once the referenced key/data pair is found, it is removed from the leaf
     * and the same underflow cases are handled as in erase_one_descend.
     */
    result_t erase_iter_descend(const iterator& iter,
                                node* curr,
                                node* left, node* right,
                                InnerNode* left_parent, InnerNode* right_parent,
                                InnerNode* parent, unsigned int parentslot) {

        if (curr->is_leafnode())
        {
            LeafNode* leaf = static_cast<LeafNode*>(curr);
            LeafNode* left_leaf = static_cast<LeafNode*>(left);
            LeafNode* right_leaf = static_cast<LeafNode*>(right);

            // if this is not the correct leaf, get next step in recursive
            // search
            if (leaf != iter.curr_leaf)
            {
                return btree_not_found;
            }

            if (iter.curr_slot >= leaf->slotuse)
            {
                TLX_BTREE_PRINT("Could not find iterator (" <<
                                iter.curr_leaf << "," << iter.curr_slot <<
                                ") to erase. Invalid leaf node?");

                return btree_not_found;
            }

            unsigned short slot = iter.curr_slot;

            TLX_BTREE_PRINT("Found iterator in leaf " <<
                            curr << " at slot " << slot);

            std::copy(leaf->slotdata + slot + 1, leaf->slotdata + leaf->slotuse,
                      leaf->slotdata + slot);

            leaf->slotuse--;

            result_t myres = btree_ok;

            // if the last key of the leaf was changed, the parent is notified
            // and updates the key of this leaf
            if (slot == leaf->slotuse)
            {
                if (parent && parentslot < parent->slotuse)
                {
                    TLX_BTREE_ASSERT(parent->childid[parentslot] == curr);
                    parent->slotkey[parentslot] = leaf->key(leaf->slotuse - 1);
                }
                else
                {
                    if (leaf->slotuse >= 1)
                    {
                        /*TLX_BTREE_PRINT("Scheduling lastkeyupdate: key " <<
                                        leaf->key(leaf->slotuse - 1));*/
                        myres |= result_t(
                            btree_update_lastkey, leaf->key(leaf->slotuse - 1));
                    }
                    else
                    {
                        TLX_BTREE_ASSERT(leaf == root_);
                    }
                }
            }

            if (leaf->is_underflow() && !(leaf == root_ && leaf->slotuse >= 1))
            {
                // determine what to do about the underflow

                // case : if this empty leaf is the root, then delete all nodes
                // and set root to nullptr.
                if (left_leaf == nullptr && right_leaf == nullptr)
                {
                    TLX_BTREE_ASSERT(leaf == root_);
                    TLX_BTREE_ASSERT(leaf->slotuse == 0);

                    free_node(root_);

                    root_ = leaf = nullptr;
                    head_leaf_ = tail_leaf_ = nullptr;

                    // will be decremented soon by insert_start()
                    TLX_BTREE_ASSERT(stats_.size == 1);
                    TLX_BTREE_ASSERT(stats_.leaves == 0);
                    TLX_BTREE_ASSERT(stats_.inner_nodes == 0);

                    return btree_ok;
                }
                // case : if both left and right leaves would underflow in case
                // of a shift, then merging is necessary. choose the more local
                // merger with our parent
                else if ((left_leaf == nullptr || left_leaf->is_few()) &&
                         (right_leaf == nullptr || right_leaf->is_few()))
                {
                    if (left_parent == parent)
                        myres |= merge_leaves(left_leaf, leaf, left_parent);
                    else
                        myres |= merge_leaves(leaf, right_leaf, right_parent);
                }
                // case : the right leaf has extra data, so balance right with
                // current
                else if ((left_leaf != nullptr && left_leaf->is_few()) &&
                         (right_leaf != nullptr && !right_leaf->is_few()))
                {
                    if (right_parent == parent) {
                        myres |= shift_left_leaf(
                            leaf, right_leaf, right_parent, parentslot);
                    }
                    else {
                        myres |= merge_leaves(left_leaf, leaf, left_parent);
                    }
                }
                // case : the left leaf has extra data, so balance left with
                // current
                else if ((left_leaf != nullptr && !left_leaf->is_few()) &&
                         (right_leaf != nullptr && right_leaf->is_few()))
                {
                    if (left_parent == parent) {
                        shift_right_leaf(
                            left_leaf, leaf, left_parent, parentslot - 1);
                    }
                    else {
                        myres |= merge_leaves(leaf, right_leaf, right_parent);
                    }
                }
                // case : both the leaf and right leaves have extra data and our
                // parent, choose the leaf with more data
                else if (left_parent == right_parent)
                {
                    if (left_leaf->slotuse <= right_leaf->slotuse) {
                        myres |= shift_left_leaf(
                            leaf, right_leaf, right_parent, parentslot);
                    }
                    else {
                        shift_right_leaf(
                            left_leaf, leaf, left_parent, parentslot - 1);
                    }
                }
                else
                {
                    if (left_parent == parent) {
                        shift_right_leaf(
                            left_leaf, leaf, left_parent, parentslot - 1);
                    }
                    else {
                        myres |= shift_left_leaf(
                            leaf, right_leaf, right_parent, parentslot);
                    }
                }
            }

            return myres;
        }
        else // !curr->is_leafnode()
        {
            InnerNode* inner = static_cast<InnerNode*>(curr);
            InnerNode* left_inner = static_cast<InnerNode*>(left);
            InnerNode* right_inner = static_cast<InnerNode*>(right);

            // find first slot below which the searched iterator might be
            // located.

            result_t result;
            unsigned short slot = find_lower(inner, iter.key());

            while (slot <= inner->slotuse)
            {
                node* myleft, * myright;
                InnerNode* myleft_parent, * myright_parent;

                if (slot == 0) {
                    myleft = (left == nullptr) ? nullptr
                             : static_cast<InnerNode*>(left)->childid[
                        left->slotuse - 1];
                    myleft_parent = left_parent;
                }
                else {
                    myleft = inner->childid[slot - 1];
                    myleft_parent = inner;
                }

                if (slot == inner->slotuse) {
                    myright = (right == nullptr) ? nullptr
                              : static_cast<InnerNode*>(right)->childid[0];
                    myright_parent = right_parent;
                }
                else {
                    myright = inner->childid[slot + 1];
                    myright_parent = inner;
                }

                TLX_BTREE_PRINT("erase_iter_descend into " <<
                                inner->childid[slot]);

                result = erase_iter_descend(iter,
                                            inner->childid[slot],
                                            myleft, myright,
                                            myleft_parent, myright_parent,
                                            inner, slot);

                if (!result.has(btree_not_found))
                    break;

                // continue recursive search for leaf on next slot

                if (slot < inner->slotuse &&
                    key_less(inner->slotkey[slot], iter.key()))
                    return btree_not_found;

                ++slot;
            }

            if (slot > inner->slotuse)
                return btree_not_found;

            result_t myres = btree_ok;

            if (result.has(btree_update_lastkey))
            {
                if (parent && parentslot < parent->slotuse)
                {
                    TLX_BTREE_PRINT("Fixing lastkeyupdate: key " <<
                                    result.lastkey << " into parent " <<
                                    parent << " at parentslot " << parentslot);

                    TLX_BTREE_ASSERT(parent->childid[parentslot] == curr);
                    parent->slotkey[parentslot] = result.lastkey;
                }
                else
                {
                    TLX_BTREE_PRINT(
                        "Forwarding lastkeyupdate: key " << result.lastkey);
                    myres |= result_t(btree_update_lastkey, result.lastkey);
                }
            }

            if (result.has(btree_fixmerge))
            {
                // either the current node or the next is empty and should be
                // removed
                if (inner->childid[slot]->slotuse != 0)
                    slot++;

                // this is the child slot invalidated by the merge
                TLX_BTREE_ASSERT(inner->childid[slot]->slotuse == 0);

                free_node(inner->childid[slot]);

                std::copy(
                    inner->slotkey + slot, inner->slotkey + inner->slotuse,
                    inner->slotkey + slot - 1);
                std::copy(
                    inner->childid + slot + 1,
                    inner->childid + inner->slotuse + 1,
                    inner->childid + slot);

                inner->slotuse--;

                if (inner->level == 1)
                {
                    // fix split key for children leaves
                    slot--;
                    LeafNode* child =
                        static_cast<LeafNode*>(inner->childid[slot]);
                    inner->slotkey[slot] = child->key(child->slotuse - 1);
                }
            }

            if (inner->is_underflow() &&
                !(inner == root_ && inner->slotuse >= 1))
            {
                // case: the inner node is the root and has just one
                // child. that child becomes the new root
                if (left_inner == nullptr && right_inner == nullptr)
                {
                    TLX_BTREE_ASSERT(inner == root_);
                    TLX_BTREE_ASSERT(inner->slotuse == 0);

                    root_ = inner->childid[0];

                    inner->slotuse = 0;
                    free_node(inner);

                    return btree_ok;
                }
                // case : if both left and right leaves would underflow in case
                // of a shift, then merging is necessary. choose the more local
                // merger with our parent
                else if ((left_inner == nullptr || left_inner->is_few()) &&
                         (right_inner == nullptr || right_inner->is_few()))
                {
                    if (left_parent == parent) {
                        myres |= merge_inner(
                            left_inner, inner, left_parent, parentslot - 1);
                    }
                    else {
                        myres |= merge_inner(
                            inner, right_inner, right_parent, parentslot);
                    }
                }
                // case : the right leaf has extra data, so balance right with
                // current
                else if ((left_inner != nullptr && left_inner->is_few()) &&
                         (right_inner != nullptr && !right_inner->is_few()))
                {
                    if (right_parent == parent) {
                        shift_left_inner(
                            inner, right_inner, right_parent, parentslot);
                    }
                    else {
                        myres |= merge_inner(
                            left_inner, inner, left_parent, parentslot - 1);
                    }
                }
                // case : the left leaf has extra data, so balance left with
                // current
                else if ((left_inner != nullptr && !left_inner->is_few()) &&
                         (right_inner != nullptr && right_inner->is_few()))
                {
                    if (left_parent == parent) {
                        shift_right_inner(
                            left_inner, inner, left_parent, parentslot - 1);
                    }
                    else {
                        myres |= merge_inner(
                            inner, right_inner, right_parent, parentslot);
                    }
                }
                // case : both the leaf and right leaves have extra data and our
                // parent, choose the leaf with more data
                else if (left_parent == right_parent)
                {
                    if (left_inner->slotuse <= right_inner->slotuse) {
                        shift_left_inner(
                            inner, right_inner, right_parent, parentslot);
                    }
                    else {
                        shift_right_inner(
                            left_inner, inner, left_parent, parentslot - 1);
                    }
                }
                else
                {
                    if (left_parent == parent) {
                        shift_right_inner(
                            left_inner, inner, left_parent, parentslot - 1);
                    }
                    else {
                        shift_left_inner(
                            inner, right_inner, right_parent, parentslot);
                    }
                }
            }

            return myres;
        }
    }
#endif

    //! Merge two leaf nodes. The function moves all key/data pairs from right
    //! to left and sets right's slotuse to zero. The right slot is then removed
    //! by the calling parent node.
    result_t merge_leaves(LeafNode* left, LeafNode* right,
                          const InnerNode* parent) {
        TLX_BTREE_PRINT("Merge leaf nodes " << left << " and " << right <<
                        " with common parent " << parent << ".");
        (void)parent;

        TLX_BTREE_ASSERT(left->is_leafnode() && right->is_leafnode());
        TLX_BTREE_ASSERT(parent->level == 1);

        TLX_BTREE_ASSERT(left->slotuse + right->slotuse < leaf_slotmax);

        std::copy(right->slotdata, right->slotdata + right->slotuse,
                  left->slotdata + left->slotuse);

        left->slotuse += right->slotuse;

        left->next_leaf = right->next_leaf;
        if (left->next_leaf)
            left->next_leaf->prev_leaf = left;
        else
            tail_leaf_ = left;

        right->slotuse = 0;

        return btree_fixmerge;
    }

    //! Merge two inner nodes. The function moves all key/childid pairs from
    //! right to left and sets right's slotuse to zero. The right slot is then
    //! removed by the calling parent node.
    static result_t merge_inner(InnerNode* left, InnerNode* right,
                                const InnerNode* parent, unsigned int parentslot) {
        TLX_BTREE_PRINT("Merge inner nodes " << left << " and " << right <<
                        " with common parent " << parent << ".");

        TLX_BTREE_ASSERT(left->level == right->level);
        TLX_BTREE_ASSERT(parent->level == left->level + 1);

        TLX_BTREE_ASSERT(parent->childid[parentslot] == left);

        TLX_BTREE_ASSERT(left->slotuse + right->slotuse < inner_slotmax);

        if (self_verify)
        {
            // find the left node's slot in the parent's children
            unsigned int leftslot = 0;
            while (leftslot <= parent->slotuse &&
                   parent->childid[leftslot] != left)
                ++leftslot;

            TLX_BTREE_ASSERT(leftslot < parent->slotuse);
            TLX_BTREE_ASSERT(parent->childid[leftslot] == left);
            TLX_BTREE_ASSERT(parent->childid[leftslot + 1] == right);

            TLX_BTREE_ASSERT(parentslot == leftslot);
        }

        // retrieve the decision key from parent
        left->slotkey[left->slotuse] = parent->slotkey[parentslot];
        left->slotuse++;

        // copy over keys and children from right
        std::copy(right->slotkey, right->slotkey + right->slotuse,
                  left->slotkey + left->slotuse);
        std::copy(right->childid, right->childid + right->slotuse + 1,
                  left->childid + left->slotuse);

        left->slotuse += right->slotuse;
        right->slotuse = 0;

        return btree_fixmerge;
    }

    //! Balance two leaf nodes. The function moves key/data pairs from right to
    //! left so that both nodes are equally filled. The parent node is updated
    //! if possible.
    static result_t shift_left_leaf(
        LeafNode* left, LeafNode* right,
        InnerNode* parent, unsigned int parentslot) {

        TLX_BTREE_ASSERT(left->is_leafnode() && right->is_leafnode());
        TLX_BTREE_ASSERT(parent->level == 1);

        TLX_BTREE_ASSERT(left->next_leaf == right);
        TLX_BTREE_ASSERT(left == right->prev_leaf);

        TLX_BTREE_ASSERT(left->slotuse < right->slotuse);
        TLX_BTREE_ASSERT(parent->childid[parentslot] == left);

        unsigned int shiftnum = (right->slotuse - left->slotuse) >> 1;

        TLX_BTREE_PRINT("Shifting (leaf) " << shiftnum << " entries to left " <<
                        left << " from right " << right <<
                        " with common parent " << parent << ".");

        TLX_BTREE_ASSERT(left->slotuse + shiftnum < leaf_slotmax);

        // copy the first items from the right node to the last slot in the left
        // node.

        std::copy(right->slotdata, right->slotdata + shiftnum,
                  left->slotdata + left->slotuse);

        left->slotuse += shiftnum;

        // shift all slots in the right node to the left

        std::copy(right->slotdata + shiftnum, right->slotdata + right->slotuse,
                  right->slotdata);

        right->slotuse -= shiftnum;

        // fixup parent
        if (parentslot < parent->slotuse) {
            parent->slotkey[parentslot] = left->key(left->slotuse - 1);
            return btree_ok;
        }
        else {  // the update is further up the tree
            return result_t(btree_update_lastkey, left->key(left->slotuse - 1));
        }
    }

    //! Balance two inner nodes. The function moves key/data pairs from right to
    //! left so that both nodes are equally filled. The parent node is updated
    //! if possible.
    static void shift_left_inner(InnerNode* left, InnerNode* right,
                                 InnerNode* parent, unsigned int parentslot) {
        TLX_BTREE_ASSERT(left->level == right->level);
        TLX_BTREE_ASSERT(parent->level == left->level + 1);

        TLX_BTREE_ASSERT(left->slotuse < right->slotuse);
        TLX_BTREE_ASSERT(parent->childid[parentslot] == left);

        unsigned int shiftnum = (right->slotuse - left->slotuse) >> 1;

        TLX_BTREE_PRINT("Shifting (inner) " << shiftnum <<
                        " entries to left " << left <<
                        " from right " << right <<
                        " with common parent " << parent << ".");

        TLX_BTREE_ASSERT(left->slotuse + shiftnum < inner_slotmax);

        if (self_verify)
        {
            // find the left node's slot in the parent's children and compare to
            // parentslot

            unsigned int leftslot = 0;
            while (leftslot <= parent->slotuse &&
                   parent->childid[leftslot] != left)
                ++leftslot;

            TLX_BTREE_ASSERT(leftslot < parent->slotuse);
            TLX_BTREE_ASSERT(parent->childid[leftslot] == left);
            TLX_BTREE_ASSERT(parent->childid[leftslot + 1] == right);

            TLX_BTREE_ASSERT(leftslot == parentslot);
        }

        // copy the parent's decision slotkey and childid to the first new key
        // on the left
        left->slotkey[left->slotuse] = parent->slotkey[parentslot];
        left->slotuse++;

        // copy the other items from the right node to the last slots in the
        // left node.
        std::copy(right->slotkey, right->slotkey + shiftnum - 1,
                  left->slotkey + left->slotuse);
        std::copy(right->childid, right->childid + shiftnum,
                  left->childid + left->slotuse);

        left->slotuse += shiftnum - 1;

        // fixup parent
        parent->slotkey[parentslot] = right->slotkey[shiftnum - 1];

        // shift all slots in the right node
        std::copy(
            right->slotkey + shiftnum, right->slotkey + right->slotuse,
            right->slotkey);
        std::copy(
            right->childid + shiftnum, right->childid + right->slotuse + 1,
            right->childid);

        right->slotuse -= shiftnum;
    }

    //! Balance two leaf nodes. The function moves key/data pairs from left to
    //! right so that both nodes are equally filled. The parent node is updated
    //! if possible.
    static void shift_right_leaf(LeafNode* left, LeafNode* right,
                                 InnerNode* parent, unsigned int parentslot) {
        TLX_BTREE_ASSERT(left->is_leafnode() && right->is_leafnode());
        TLX_BTREE_ASSERT(parent->level == 1);

        TLX_BTREE_ASSERT(left->next_leaf == right);
        TLX_BTREE_ASSERT(left == right->prev_leaf);
        TLX_BTREE_ASSERT(parent->childid[parentslot] == left);

        TLX_BTREE_ASSERT(left->slotuse > right->slotuse);

        unsigned int shiftnum = (left->slotuse - right->slotuse) >> 1;

        TLX_BTREE_PRINT("Shifting (leaf) " << shiftnum <<
                        " entries to right " << right <<
                        " from left " << left <<
                        " with common parent " << parent << ".");

        if (self_verify)
        {
            // find the left node's slot in the parent's children
            unsigned int leftslot = 0;
            while (leftslot <= parent->slotuse &&
                   parent->childid[leftslot] != left)
                ++leftslot;

            TLX_BTREE_ASSERT(leftslot < parent->slotuse);
            TLX_BTREE_ASSERT(parent->childid[leftslot] == left);
            TLX_BTREE_ASSERT(parent->childid[leftslot + 1] == right);

            TLX_BTREE_ASSERT(leftslot == parentslot);
        }

        // shift all slots in the right node

        TLX_BTREE_ASSERT(right->slotuse + shiftnum < leaf_slotmax);

        std::copy_backward(right->slotdata, right->slotdata + right->slotuse,
                           right->slotdata + right->slotuse + shiftnum);

        right->slotuse += shiftnum;

        // copy the last items from the left node to the first slot in the right
        // node.
        std::copy(left->slotdata + left->slotuse - shiftnum,
                  left->slotdata + left->slotuse,
                  right->slotdata);

        left->slotuse -= shiftnum;

        parent->slotkey[parentslot] = left->key(left->slotuse - 1);
    }

    //! Balance two inner nodes. The function moves key/data pairs from left to
    //! right so that both nodes are equally filled. The parent node is updated
    //! if possible.
    static void shift_right_inner(InnerNode* left, InnerNode* right,
                                  InnerNode* parent, unsigned int parentslot) {
        TLX_BTREE_ASSERT(left->level == right->level);
        TLX_BTREE_ASSERT(parent->level == left->level + 1);

        TLX_BTREE_ASSERT(left->slotuse > right->slotuse);
        TLX_BTREE_ASSERT(parent->childid[parentslot] == left);

        unsigned int shiftnum = (left->slotuse - right->slotuse) >> 1;
        if (shiftnum == 0) shiftnum = 1;

        TLX_BTREE_PRINT("Shifting (leaf) " << shiftnum <<
                        " entries to right " << right <<
                        " from left " << left <<
                        " with common parent " << parent << ".");

        if (self_verify)
        {
            // find the left node's slot in the parent's children
            unsigned int leftslot = 0;
            while (leftslot <= parent->slotuse &&
                   parent->childid[leftslot] != left)
                ++leftslot;

            TLX_BTREE_ASSERT(leftslot < parent->slotuse);
            TLX_BTREE_ASSERT(parent->childid[leftslot] == left);
            TLX_BTREE_ASSERT(parent->childid[leftslot + 1] == right);

            TLX_BTREE_ASSERT(leftslot == parentslot);
        }

        // shift all slots in the right node

        TLX_BTREE_ASSERT(right->slotuse + shiftnum < inner_slotmax);

        std::copy_backward(
            right->slotkey, right->slotkey + right->slotuse,
            right->slotkey + right->slotuse + shiftnum);
        std::copy_backward(
            right->childid, right->childid + right->slotuse + 1,
            right->childid + right->slotuse + 1 + shiftnum);

        right->slotuse += shiftnum;

        // copy the parent's decision slotkey and childid to the last new key on
        // the right
        right->slotkey[shiftnum - 1] = parent->slotkey[parentslot];

        // copy the remaining last items from the left node to the first slot in
        // the right node.
        std::copy(left->slotkey + left->slotuse - shiftnum + 1,
                  left->slotkey + left->slotuse,
                  right->slotkey);
        std::copy(left->childid + left->slotuse - shiftnum + 1,
                  left->childid + left->slotuse + 1,
                  right->childid);

        // copy the first to-be-removed key from the left node to the parent's
        // decision slot
        parent->slotkey[parentslot] = left->slotkey[left->slotuse - shiftnum];

        left->slotuse -= shiftnum;
    }

    //! \}

#ifdef TLX_BTREE_DEBUG

public:
    //! \name Debug Printing
    //! \{

    //! Print out the B+ tree structure with keys onto the given ostream. This
    //! function requires that the header is compiled with TLX_BTREE_DEBUG and
    //! that key_type is printable via std::ostream.
    void print(std::ostream& os) const {
        if (root_->level > 0) {
            print_node(os, root_, 0, true);
        }
    }

    //! Print out only the leaves via the double linked list.
    void print_leaves(std::ostream& os) const {
        os << "leaves:" << std::endl;

        const LeafNode* n = head_leaf_;

        while (n)
        {
            os << "  " << n << std::endl;

            n = n->next_leaf;
        }
    }

private:
    friend LockHelper;

    //! Recursively descend down the tree and print out nodes.
    static void print_node(std::ostream& os, const node* node,
                           unsigned int depth = 0, bool recursive = false) {
        if (node->level == 0 && node->slotuse == 0) {
            os << "empty tree" << std::endl;
            return;
        }

        for (unsigned int i = 0; i < depth; i++) os << "  ";

        os << "node " << node << " level " << node->level <<
            " slotuse " << node->slotuse << std::endl;

        if (node->is_leafnode())
        {
            const LeafNode* leafnode = static_cast<const LeafNode*>(node);

            for (unsigned int i = 0; i < depth; i++) os << "  ";
            os << "  leaf prev " << leafnode->prev_leaf <<
                " next " << leafnode->next_leaf << std::endl;

            for (unsigned int i = 0; i < depth; i++) os << "  ";

            for (unsigned short slot = 0; slot < leafnode->slotuse; ++slot)
            {
                // os << leafnode->key(slot) << " "
                //    << "(data: " << leafnode->slotdata[slot] << ") ";
                os << leafnode->key(slot) << "  ";
            }
            os << std::endl;
        }
        else
        {
            const InnerNode* innernode = static_cast<const InnerNode*>(node);

            for (unsigned int i = 0; i < depth; i++) os << "  ";

            for (unsigned short slot = 0; slot < innernode->slotuse; ++slot)
            {
                os << "(" << innernode->childid[slot] << ") "
                   << innernode->slotkey[slot] << " ";
            }
            os << "(" << innernode->childid[innernode->slotuse] << ")"
               << std::endl;

            if (recursive)
            {
                for (unsigned short slot = 0;
                     slot < innernode->slotuse + 1; ++slot)
                {
                    print_node(
                        os, innernode->childid[slot], depth + 1, recursive);
                }
            }
        }
    }

    //! \}
#endif

public:
    //! \name Verification of B+ Tree Invariants
    //! \{

    //! Run a thorough verification of all B+ tree invariants. The program
    //! aborts via tlx_die_unless() if something is wrong.
    void verify() const {
        key_type minkey, maxkey;
        tree_stats vstats;

        if (root_ && root_->level > 0)
        {
            verify_node(root_, &minkey, &maxkey, vstats);

            tlx_die_unless(vstats.size == stats_.size);
            tlx_die_unless(vstats.leaves == stats_.leaves);
            tlx_die_unless(vstats.inner_nodes == stats_.inner_nodes);

            verify_leaflinks();
        }
    }

private:
    //! Recursively descend down the tree and verify each node
    void verify_node(const node* n, key_type* minkey, key_type* maxkey,
                     tree_stats& vstats) const {
        //TLX_BTREE_PRINT("verifynode " << n);

        if (n->is_leafnode())
        {
            const LeafNode* leaf = static_cast<const LeafNode*>(n);

            tlx_die_unless(root_->level <= 1 || leaf->slotuse >= leaf_slotmin - 1);
            tlx_die_unless(leaf->slotuse > 0);
#ifdef TLX_BTREE_DEBUG
            tlx_die_unless(!leaf->lock->readlocked());
            tlx_die_unless(!leaf->lock->writelocked());
#endif

            for (unsigned short slot = 0; slot < leaf->slotuse - 1; ++slot)
            {
                tlx_die_unless(
                    key_lessequal(leaf->key(slot), leaf->key(slot + 1)));
            }

            *minkey = leaf->key(0);
            *maxkey = leaf->key(leaf->slotuse - 1);

            vstats.leaves++;
            vstats.size += leaf->slotuse;
        }
        else // !n->is_leafnode()
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            vstats.inner_nodes++;

            tlx_die_unless(inner == root_ || inner->slotuse >= inner_slotmin - 1);
#ifdef TLX_BTREE_DEBUG
            tlx_die_unless(!inner->lock->readlocked());
            tlx_die_unless(!inner->lock->writelocked());
#endif

            for (unsigned short slot = 0; slot < inner->slotuse - 1; ++slot)
            {
                tlx_die_unless(
                    key_lessequal(inner->key(slot), inner->key(slot + 1)));
            }

            for (unsigned short slot = 0; slot <= inner->slotuse; ++slot)
            {
                const node* subnode = inner->childid[slot];
                key_type subminkey = key_type();
                key_type submaxkey = key_type();

                tlx_die_unless(subnode->level + 1 == inner->level);
                verify_node(subnode, &subminkey, &submaxkey, vstats);

                /*TLX_BTREE_PRINT("verify subnode " << subnode <<
                                ": " << subminkey <<
                                " - " << submaxkey);*/

                if (slot == 0)
                    *minkey = subminkey;
                else
                    tlx_die_unless(
                        key_greaterequal(subminkey, inner->key(slot - 1)));

                if (slot == inner->slotuse)
                    *maxkey = submaxkey;
                else
                    tlx_die_unless(key_greaterequal(inner->key(slot), submaxkey));

                if (inner->level == 1 && slot < inner->slotuse)
                {
                    // children are leaves and must be linked together in the
                    // correct order
                    const LeafNode* leafa = static_cast<const LeafNode*>(
                        inner->childid[slot]);
                    const LeafNode* leafb = static_cast<const LeafNode*>(
                        inner->childid[slot + 1]);

                    tlx_die_unless(leafa->next_leaf == leafb);
                    tlx_die_unless(leafa == leafb->prev_leaf);
                }
                if (inner->level == 2 && slot < inner->slotuse)
                {
                    // verify leaf links between the adjacent inner nodes
                    const InnerNode* parenta = static_cast<const InnerNode*>(
                        inner->childid[slot]);
                    const InnerNode* parentb = static_cast<const InnerNode*>(
                        inner->childid[slot + 1]);

                    const LeafNode* leafa = static_cast<const LeafNode*>(
                        parenta->childid[parenta->slotuse]);
                    const LeafNode* leafb = static_cast<const LeafNode*>(
                        parentb->childid[0]);

                    tlx_die_unless(leafa->next_leaf == leafb);
                    tlx_die_unless(leafa == leafb->prev_leaf);
                }
            }
        }
    }

    //! Verify the double linked list of leaves.
    void verify_leaflinks() const {
        const LeafNode* n = head_leaf_;

        tlx_die_unless(n->level == 0);
        tlx_die_unless(!n || n->prev_leaf == nullptr);

        unsigned int testcount = 0;

        while (n)
        {
            tlx_die_unless(n->level == 0);
            tlx_die_unless(n->slotuse > 0);

            for (unsigned short slot = 0; slot < n->slotuse - 1; ++slot)
            {
                tlx_die_unless(key_lessequal(n->key(slot), n->key(slot + 1)));
            }

            testcount += n->slotuse;

            if (n->next_leaf)
            {
                tlx_die_unless(key_lessequal(n->key(n->slotuse - 1),
                                             n->next_leaf->key(0)));

                tlx_die_unless(n == n->next_leaf->prev_leaf);
            }
            else
            {
                tlx_die_unless(tail_leaf_ == n);
            }

            n = n->next_leaf;
        }

        tlx_die_unless(testcount == size());
    }

    //! \}
};

//! \}
//! \}

} // namespace tlx

#endif // !TLX_CONTAINER_SLOW_LOCK_BTREE_HEADER

/******************************************************************************/
