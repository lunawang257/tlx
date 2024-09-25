#pragma once

#include <algorithm>
#include <atomic>
#include <inttypes.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __cilksan__
#ifdef __cplusplus
extern "C" {
#endif
void __csan_default_libhook(uint64_t call_id, uint64_t func_id, unsigned count);
void __csan_llvm_x86_sse2_pause(uint64_t call_id, uint64_t func_id,
                                unsigned count) {
  __csan_default_libhook(call_id, func_id, count);
}
#ifdef __cplusplus
}
#endif
#endif

#define num_tries 3
class Lock {
  std::atomic<bool> flag;

public:
  bool try_lock() {
    bool value = false;
    return flag.compare_exchange_strong(value, true);
  }
  void lock() {
    int tries = 0;
    bool success = false;

    while (!success) {
      tries = 0;
      while (tries < num_tries) {
        bool value = false;
        success = flag.compare_exchange_weak(value, true);

        if (success) {
          return;
        } else {
          tries++;
        }
      }
      if (!success) {
        sched_yield();
      }
    }
  }
  void unlock() { flag = false; }
};

template <int num_counters = 8> class partitioned_counter {

#ifdef __cpp_lib_hardware_interference_size
  static constexpr std::size_t hardware_constructive_interference_size =
    std::hardware_constructive_interference_size;
  static constexpr std::size_t hardware_destructive_interference_size =
    std::hardware_destructive_interference_size;
#else
  // 64 bytes on x86-64 │ L1_CACHE_BYTES │ L1_CACHE_SHIFT │ __cacheline_aligned
  // │
  // ...
  static constexpr std::size_t hardware_constructive_interference_size = 64;
  static constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

  class local_counter {
  public:
    alignas(
        hardware_destructive_interference_size) std::atomic<int64_t> counter{0};
  };

  local_counter *local_counters;

public:
  partitioned_counter() {
    local_counters = new local_counter[num_counters];
    return;
  }

  int64_t get() const {
    int64_t total = 0;
    for (uint32_t i = 0; i < num_counters; i++) {
      int64_t c = local_counters[i].counter.load();
      total += c;
    }
    return total;
  }

  void add(int64_t count, uint8_t counter_id) {
    counter_id = counter_id % num_counters;
    local_counters[counter_id].counter += count;
  }

  ~partitioned_counter() { delete[] local_counters; }
};

template<int NumCounters>
struct StatsCount {
    std::atomic<int> counter[NumCounters];
};

template<int RolloverThreshold, int NumCounters, int BufferSize>
struct StatsTracker {
    static const int size = BufferSize;
    static const int num_counters = NumCounters;
    static const int rollover_threshold = RolloverThreshold;

    StatsCount<num_counters> buffer[size];
    std::atomic<int> first = 0;
    int total[num_counters]; // total except buffer[first]
    int all_total = 0; // all total except all data in buffer[first]

private:
    void rollover() {
        int my_first = (first + 1) % size;
        for (int i = 0; i < num_counters; i++) {
            int oldVal = buffer[my_first].counter[i].exchange(0);
            total[i] -= oldVal;
            all_total -= oldVal;
        }

        first = my_first;
    }

public:
    StatsTracker() {
        for (int c = 0; c < num_counters; c++) {
            for (int i = 0; i < size; i++) {
                buffer[i].counter[c] = 0;
            }
            total[c] = 0;
        }
        all_total = 0;
    }

    int percent(int counter) {
        int first_total = 0;
        for (int c = 0; c < num_counters; c++) {
            first_total += buffer[first].counter[c];
        }

        return buffer[first].counter[counter] * 100 / (all_total + first_total);
    }

    void track(int counter) {
        if ( ++buffer[first].counter[counter] >= rollover_threshold) {
            rollover();
        }
    }
};

struct ContentionTracker {
    StatsTracker<100, 2, 4> tracker;
    int percent_waited() {
        return tracker.percent(0);
    }

    void track_wait() {
        tracker.track(0);
    }

    void track_no_wait() {
        tracker.track(1);
    }
};

enum {
    LEAF_OP_FIND,
    LEAF_OP_UPDATE,
    LEAF_OP_SCAN,
};
struct LeafOpTracker {
    StatsTracker<10, 3, 4> tracker;
    int get(int op) {
        return tracker.total[op] + tracker.buffer[tracker.first].counter[op];
    }

    void track(int op) {
        tracker.track(op);
    }
};

struct ThreadLocalLockStat {
    std::chrono::duration<uint64_t, std::nano> total_leaf_read_lock_ns;
    std::chrono::duration<uint64_t, std::nano> total_leaf_write_lock_ns;

    uint64_t total_leaf_read_lock_ct = 0;
    uint64_t total_leaf_write_lock_ct = 0;

    std::chrono::duration<uint64_t, std::nano> total_inner_read_lock_ns;
    std::chrono::duration<uint64_t, std::nano> total_inner_write_lock_ns;

    uint64_t total_inner_read_lock_ct = 0;
    uint64_t total_inner_write_lock_ct = 0;
};
thread_local ThreadLocalLockStat localLockStat;

class ReaderWriterLock {

public:
  ReaderWriterLock() : writer(0) {}

  /**
   * Try to acquire a read lock and return failure if needs to wait
   */
  bool try_read_lock(int cpuid = -1) {

    readers.add(1, cpuid);

    if (writer.test(std::memory_order_relaxed)) {
      readers.add(-1, cpuid);
      return false;
    }
    return true;
  }

  /**
   * Try to acquire a lock and spin until the lock is available.
   */
  void read_lock(int cpuid = -1) {

    localLockStat.total_inner_read_lock_ct++;
    auto start = std::chrono::high_resolution_clock::now();

    readers.add(1, cpuid);

    while (writer.test(std::memory_order_relaxed)) {
      readers.add(-1, cpuid);
      writer.wait(true, std::memory_order_relaxed);
      readers.add(1, cpuid);
    }

    localLockStat.total_inner_read_lock_ns += std::chrono::high_resolution_clock::now() - start;
  }

  void read_unlock(int cpuid) {
    readers.add(-1, cpuid);
    return;
  }

  /**
   * Try to acquire a write lock and spin until the lock is available.
   * Then wait till reader count is 0.
   */
  void write_lock() {
    localLockStat.total_inner_write_lock_ct++;
    auto start = std::chrono::high_resolution_clock::now();

    // acquire write lock.
    while (writer.test_and_set(std::memory_order_acq_rel)) {
      writer.wait(true, std::memory_order_acq_rel);
    }
    // wait for readers to finish
    while (readers.get()) {
    }

    localLockStat.total_inner_write_lock_ns += std::chrono::high_resolution_clock::now() - start;
  }

  bool try_upgrade_release_on_fail(int cpuid) {
    // acquire write lock.

    if (writer.test_and_set()) {
      readers.add(-1, cpuid);
      return false;
    }

    readers.add(-1, cpuid);

    // wait for readers to finish
    while (readers.get()) {
    }

    return true;
  }

  void write_unlock(void) {
    writer.clear(std::memory_order_release);
    writer.notify_all();
    return;
  }

private:
  std::atomic_flag writer{false};
  partitioned_counter<48> readers{};
};

class ReaderWriterLock2 {
public:
  ReaderWriterLock2() : writer(0), readers(0) {}
  ContentionTracker con_tracker;

  /**
   * Try to acquire a read lock and return failure if needs to wait
   */
bool try_read_lock(int cpuid __attribute__((unused)) = -1) {
    readers++;

    if (writer.test(std::memory_order_relaxed)) {
      readers--;
      return false;
    }

    con_tracker.track_no_wait();
    return true;
  }

  /**
   * Try to acquire a lock and spin until the lock is available.
   */
  void read_lock(int cpuid __attribute__((unused)) = -1) {

    localLockStat.total_leaf_read_lock_ct++;

    auto start = std::chrono::high_resolution_clock::now();

    readers++;

    bool waited = false;
    while (writer.test(std::memory_order_relaxed)) {
      waited = true;
      readers--;
      writer.wait(true, std::memory_order_relaxed);
      readers++;
    }

    if (waited) con_tracker.track_wait();
    else con_tracker.track_no_wait();

    localLockStat.total_leaf_read_lock_ns += std::chrono::high_resolution_clock::now() - start;
  }

  void read_unlock(int cpuid __attribute__((unused)) = -1) {
    readers--;
    return;
  }

  bool read_locked() {
      return readers > 0;
  }

  /**
   * Try to acquire a write lock and spin until the lock is available.
   * Then wait till reader count is 0.
   */
  void write_lock() {
    bool waited = false;

    localLockStat.total_leaf_write_lock_ct++;

    auto start = std::chrono::high_resolution_clock::now();

    // acquire write lock.
    while (writer.test_and_set(std::memory_order_acq_rel)) {
      waited = true;
      writer.wait(true, std::memory_order_acquire);
    }

    if (readers > 0) waited = true;
    // wait for readers to finish
    while (readers > 0) {
    }

    if (waited) con_tracker.track_wait();
    else con_tracker.track_no_wait();

    localLockStat.total_leaf_write_lock_ns += std::chrono::high_resolution_clock::now() - start;
  }

  bool write_locked() {
    return writer.test(std::memory_order_relaxed);
  }

  bool try_upgrade_release_on_fail(int cpuid __attribute__((unused))) {
    // acquire write lock.

    if (writer.test_and_set()) {
      readers--;
      con_tracker.track_wait();
      return false;
    }

    readers--;

    // wait for readers to finish
    while (readers > 0) {
    }

    con_tracker.track_no_wait();
    return true;
  }

  void write_unlock(void) {
    writer.clear(std::memory_order_release);
    writer.notify_all();
    return;
  }

private:
  std::atomic_flag writer{false};
  std::atomic<int> readers{};
};

class DummyReaderWriterLock {
public:

  bool try_read_lock(int cpuid __attribute__((unused)) = -1) {
    return true;
  }

  void read_lock(int cpuid __attribute__((unused)) = -1) {}

  void read_unlock(int cpuid __attribute__((unused)) = -1) {}

  bool read_locked() { return true; }

  void write_lock() {}

  bool write_locked() { return true; }

  bool try_upgrade_release_on_fail(int cpuid __attribute__((unused))) {
    return true;
  }

  void write_unlock(void) {}
};
