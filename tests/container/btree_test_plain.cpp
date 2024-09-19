#include <sys/time.h>

#include <algorithm>
#include <chrono>
#include <container/btree_set.hpp>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include "ParallelTools/reducer.h"
#include "container/btree_map.hpp"
#include "cxxopts.hpp"
#include "test_suite.h"
#include "timers.hpp"

#define NUM_THREADS 1  // TODO: pass this via command line?

static long get_usecs() {
  struct timeval st;
  gettimeofday(&st, NULL);
  return st.tv_sec * 1000000 + st.tv_usec;
}

struct ThreadArgs {
    std::function<void(int, int)> func;
    int start;
    int end;
};


void* threadFunction(void* arg) {
    ThreadArgs* args = static_cast<ThreadArgs*>(arg);
    args->func(args->start, args->end);
    pthread_exit(NULL);
}

template <typename F> inline void parallel_for(size_t start, size_t end, int threads_num, F f) {
    //const int numThreads = NUM_THREADS;
    const int numThreads = threads_num;
    pthread_t threads[numThreads];
    ThreadArgs threadArgs[numThreads];
    int per_thread = (end - start)/numThreads;

    // Create the threads and start executing the lambda function
    for (int i = 0; i < numThreads; i++) {
        threadArgs[i].func = [&f](int arg1, int arg2) {
            for (int k = arg1 ; k < arg2; k++) {
                f(k);
            }
        };

        threadArgs[i].start = start + (i * per_thread);
        if (i == numThreads - 1) {
          threadArgs[i].end = end;
        } else {
          threadArgs[i].end = start + ((i+1) * per_thread);
        }
        int result = pthread_create(&threads[i], NULL, threadFunction, &threadArgs[i]);

        if (result != 0) {
            std::cerr << "Failed to create thread " << i << std::endl;
            exit(-1);
        }

        /* cpu_set_t cpuset;
        CPU_ZERO (&cpuset);
        CPU_SET (i % 32, &cpuset);
        int rc = pthread_setaffinity_np (threads[i], sizeof (cpu_set_t),
                                             &cpuset);
        if (rc != 0) {
            std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
            exit(-1);
        } */
    }

    // Wait for the threads to finish
    for (int i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }
}

template <class T>
std::vector<T> create_random_data(size_t n, size_t max_val,
                                  std::seed_seq &seed) {

  std::mt19937_64 eng(seed); // a source of random data

  std::uniform_int_distribution<T> dist(0, max_val);
  std::vector<T> v(n);

  generate(begin(v), end(v), bind(dist, eng));
  return v;
}

template <class T>
std::vector<T> create_random_data_in_parallel(size_t n, size_t max_val,
                                                   uint64_t seed_add = 0) {

  std::vector<T> v(n);

  uint64_t per_worker = (n / NUM_THREADS) + 1;
  parallel_for(0, NUM_THREADS, NUM_THREADS, [&](const uint32_t &i) {
    uint64_t start = i * per_worker;
    uint64_t end = (i + 1) * per_worker;
    if (end > n) {
      end = n;
    }
    if (static_cast<int>(i) == NUM_THREADS - 1) {
      end = n;
    }
    std::random_device rd;
    std::mt19937_64 eng(i + seed_add); // a source of random data

    std::uniform_int_distribution<uint64_t> dist(0, max_val);
    for (size_t j = start; j < end; j++) {
      v[j] = dist(eng);
    }
  });
  return v;
}

template <class T>
std::vector<T> create_zipf_data(size_t n, double theta = 0.99) {
    std::vector<T> zipfian_key_list{};
    zipfian_key_list.reserve(n);
    // Initialize it with time() as the random seed
    Zipfian zipf{n, theta, static_cast<uint64_t>(time(NULL))};

    // Populate the array with random numbers
    for (size_t i = 0; i < n; ++i) {
        zipfian_key_list.push_back(zipf.Get());
    }

    return zipfian_key_list;
}

/*** TEST DRIVER AFTER HERE ***/

template <class T, uint32_t internal_bytes, uint32_t leaf_bytes>
bool test_random_inserts(uint64_t max_size, std::seed_seq &seed, int trials, size_t threads_num) {
    printf("*** testing random inserts ***\n");
    uint64_t start_time, end_time;
    std::vector<uint64_t> insert_times;

    for (int cur_trial = 0; cur_trial <= trials; cur_trial++) {
        std::vector<T> data = create_random_data<T>(
            max_size, std::numeric_limits<T>::max(), seed);
        printf("trials %d, inserts %lu; data.size=%lu; threads_num=%lu\n", cur_trial, max_size,
               data.size(), threads_num);
        tlx::btree_map<
            T, T, std::less<T>,
            tlx::btree_default_traits<T, T, internal_bytes, leaf_bytes>,
            std::allocator<T>, true>
            concurrent_map;
        // TIME INSERTS

        start_time = get_usecs();
        parallel_for(0, max_size, threads_num,[&](const uint32_t &i) {
            concurrent_map.insert({data[i], data[i]});
        });
        end_time = get_usecs();
        if (cur_trial > 0) {
            insert_times.push_back(end_time - start_time);
        }
        printf("\tDone inserting %lu elts in %lu. map size=%lu\n", max_size,
               end_time - start_time, concurrent_map.size());

        auto concurrent_sum = concurrent_map.psum();
        printf("concurrent sum = %lu\n", concurrent_sum);
    }
    std::sort(insert_times.begin(), insert_times.end());
    printf("median insert time = %lu\n", insert_times[trials / 2]);
    std::cout << "num_thread=" << threads_num << "; throughput="
              << (static_cast<double>(max_size) / insert_times[trials / 2]) << " Mops/s"
              << std::endl;
    std::cout << "[moti] "<<threads_num<<","<<(static_cast<double>(max_size) / insert_times[trials / 2]) << std::endl;

    return true;
}

template <class T, uint32_t internal_bytes, uint32_t leaf_bytes>
bool test_zipfian_inserts(uint64_t max_size, int trials, size_t threads_num, double theta=0.99) {
    printf("*** testing zipfian inserts ***\n");
    uint64_t start_time, end_time;
    std::vector<uint64_t> insert_times;

    for (int cur_trial = 0; cur_trial <= trials; cur_trial++) {
        std::vector<T> data = create_zipf_data<T>(max_size, theta);
        printf("trials %d, inserts %lu; data.size=%lu; threads_num=%lu, theta=%.3f\n", cur_trial,
               max_size, data.size(), threads_num, theta);
        tlx::btree_map<
            T, T, std::less<T>,
            tlx::btree_default_traits<T, T, internal_bytes, leaf_bytes>,
            std::allocator<T>, true>
            concurrent_map;

        // TIME INSERTS
        start_time = get_usecs();
        parallel_for(0, max_size, threads_num, [&](const uint32_t &i) {
            concurrent_map.insert({data[i], data[i]});
        });
        end_time = get_usecs();
        if (cur_trial > 0) {
            insert_times.push_back(end_time - start_time);
        }
        printf("\tDone inserting %lu elts in %lu. map size=%lu\n", max_size,
               end_time - start_time, concurrent_map.size());

        auto concurrent_sum = concurrent_map.psum();
        printf("concurrent sum = %lu\n", concurrent_sum);
        //size,leaves,inner_nodes,leaf_slots,inner_slots,avgfill_leaves
        std::cout << "[Tree states] size=" << concurrent_map.get_stats().size
        << ", inner_nodes=" << concurrent_map.get_stats().inner_nodes
        << ", leaves=" << concurrent_map.get_stats().leaves
        <<", inner_slots=" << concurrent_map.get_stats().inner_slots
        <<", leaf_slots=" << concurrent_map.get_stats().leaf_slots
        <<", avgfill_leaves=" << concurrent_map.get_stats().avgfill_leaves()
        <<", threads_num=" << threads_num << ", insert_num=" << max_size
        <<std::endl;
    }
    std::sort(insert_times.begin(), insert_times.end());
    printf("median insert time = %lu\n", insert_times[trials / 2]);
    std::cout << "num_thread=" << threads_num << "; throughput="
              << (static_cast<double>(max_size) / insert_times[trials / 2]) << " Mops/s"
              << std::endl;
    std::cout << "[moti] "<<threads_num<<","<<(static_cast<double>(max_size) / insert_times[trials / 2]) << std::endl;

    return true;
}


int main(int argc, char *argv[]) {

  cxxopts::Options options("BtreeTester",
                           "allows testing different attributes of the btree");

  options.positional_help("Help Text");

  // clang-format off
  options.add_options()
    ("trials", "how many values to insert", cxxopts::value<int>()->default_value( "5"))
    ("theta", "zipfian constant", cxxopts::value<double>()->default_value( "0.99"))
    ("threads_num", "number of threads", cxxopts::value<int>()->default_value( "8"))
    ("num_inserts", "number of values to insert", cxxopts::value<int>()->default_value( "100000000"))
    ("num_queries", "number of queries for query tests", cxxopts::value<int>()->default_value( "1000000"))
    ("num_chunks", "number of chunks for merge tests", cxxopts::value<int>()->default_value( "480"))
    ("query_size", "query size for cache test", cxxopts::value<int>()->default_value( "10000"))
    ("write_csv", "whether to write timings to disk")
    ("random_inserts", "run parallel inserts where each threads inserts random elements")
    ("zipf_inserts", "run parallel inserts where each threads inserts random elements with zipfian distribution");

  std::seed_seq seed{static_cast<uint64_t>(time(NULL))};
  auto result = options.parse(argc, argv);
  uint32_t trials = result["trials"].as<int>();
  double theta = result["theta"].as<double>();
  size_t threads_num = result["threads_num"].as<int>();
  uint32_t num_inserts = result["num_inserts"].as<int>();
  uint32_t num_queries = result["num_queries"].as<int>();
  uint32_t num_chunks = result["num_chunks"].as<int>();
  uint32_t query_size = result["query_size"].as<int>();
  uint32_t write_csv = result["write_csv"].as<bool>();

  std::cout << "Info: traials=" << trials << ", num_inserts=" << num_inserts << ", num_queries="
  << num_queries << ", num_chunks=" << num_chunks << ", query_size=" << query_size << ", write_csv=" <<
  write_csv << ", threads_num=" << threads_num << ", theta=" << theta << std::endl;

  std::ofstream outfile;
  outfile.open("insert_finds.csv", std::ios_base::app);
  outfile << "tree_type, internal bytes, leaf bytes, num_inserted, insert_time, num_finds, find_time, \n";
  outfile.close();
  outfile.open("range_queries.csv", std::ios_base::app);
  outfile << "tree_type, internal bytes, leaf bytes, num_inserted,num_range_queries, max_query_size,  unsorted_query_time, sorted_query_time, \n";
  outfile.close();

  if (result["random_inserts"].as<bool>()) {
    return test_random_inserts<unsigned long, 1024, 1024>(num_inserts, seed, trials, threads_num);
  }
  if (result["zipf_inserts"].as<bool>()) {
    return test_zipfian_inserts<unsigned long, 1024, 1024>(num_inserts, trials, threads_num, theta);
  }
  return 0;
}
