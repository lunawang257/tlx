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

  int64_t get() {
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

struct ContentionCount {
      std::atomic<int> no_wait = 0;
      std::atomic<int> wait = 0;
  };

struct ContentionTracker {
    static const int size = 4;
    static const int rollover_threshold = 100;
private:
    ContentionCount buffer[size];
    std::atomic<int> first = 0;

public:
    void rollover() {
        int my_first = (first + 1)%size;
        buffer[my_first].wait = 0;
        buffer[my_first].no_wait = 0;
        first = my_first;
    }

    int percent_waited() {
        int wait_count = 0;
        int no_wait_count = 0;
        for (int i = 0; i < size; i++) {
            wait_count += buffer[i].wait;
            no_wait_count += buffer[i].no_wait;
        }

        return (wait_count * 100) / (wait_count + no_wait_count);
    }

    void track_no_wait() {
        if ( ++buffer[first].no_wait >= rollover_threshold) {
            rollover();
        }
    }

    void track_wait() {
        if ( ++buffer[first].wait >= rollover_threshold) {
            rollover();
        }
    }
};

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

    readers.add(1, cpuid);

    while (writer.test(std::memory_order_relaxed)) {
      readers.add(-1, cpuid);
      writer.wait(true, std::memory_order_relaxed);
      readers.add(1, cpuid);
    }
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
    // acquire write lock.
    while (writer.test_and_set(std::memory_order_acq_rel)) {
      writer.wait(true, std::memory_order_acq_rel);
    }
    // wait for readers to finish
    while (readers.get()) {
    }
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

    // acquire write lock.
    while (writer.test_and_set(std::memory_order_acq_rel)) {
      waited = true;
      writer.wait(true, std::memory_order_acq_rel);
    }

    if (readers > 0) waited = true;
    // wait for readers to finish
    while (readers > 0) {
    }

    if (waited) con_tracker.track_wait();
    else con_tracker.track_no_wait();
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
