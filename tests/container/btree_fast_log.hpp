#pragma once

// debug routines to provide fast small in-memory log
// can only be included by one cpp file to avoid duplicated symbols

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cxxabi.h>
#include <execinfo.h>
#include <iomanip>
#include <iostream>
#include <istream>
#include <map>
#include <mutex>
#include <ostream>
#include <regex>
#include <sstream>
#include <stack>
#include <thread>

#include <tlx/timestamp.hpp>
#include <lock_type.hpp>

bool prt_lock = true;
bool prt_mem_op = prt_lock;
bool prt_retry = prt_lock;
bool prt_op = true;
bool prt_split = true;

extern set_type *g_test_set;

enum {
  STACK_START_TO_PRINT = 3,
  NUM_STACK_TO_PRINT = 4
};

// Define the struct for all thread info, aligned on 64-byte boundary
struct alignas(64) thread_info {
    std::thread::id id;
    int threadidx;
    void* cur_node;
    int op; // read, write, or upgrade
};

// Define the thread-local variable struct
struct thread_debug_info {
    thread_info* tinfo;
};

// Thread-local storage for each thread's debug information
thread_local thread_debug_info local_debug_info;

std::string format_time(
  std::chrono::time_point<std::chrono::high_resolution_clock> ts) {
    // Get the current time from system_clock
    auto now = ts;

    // Convert to time_t to get calendar time
    std::time_t time_t_now = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    // Convert time_t to tm for formatting
    std::tm tm_now = *std::localtime(&time_t_now);

    // Get the duration since the epoch
    auto duration_since_epoch = now.time_since_epoch();

    // Extract seconds and nanoseconds from the duration
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration_since_epoch);
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration_since_epoch) - seconds;

    // Format the output string
    std::stringstream ss;
    ss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setw(6) << std::setfill('0') << nanoseconds.count();

    return ss.str();
}

std::string format_current_time() {
    // Get the current time from system_clock
    auto now = std::chrono::high_resolution_clock::now();
    return format_time(now);
}

void split_sym(char *input, std::vector<std::string> *resultp) {
   std::istringstream iss(input);
   std::string word;

   auto& result = *resultp;
   result.clear();

   // Use a loop to split the string by spaces
   while (iss >> word) {
      result.push_back(word);
   }
}

std::string remove_text_between_brackets(const std::string& input) {
    std::string result;
    std::stack<char> bracket_stack;
    bool remove_text = false;

    for (char ch : input) {
        if (ch == '(' || ch == '[' || ch == '{' || ch == '<') {
            bracket_stack.push(ch);
            remove_text = true;
        } else if ((ch == ')' && !bracket_stack.empty() && bracket_stack.top() == '(') ||
                   (ch == ']' && !bracket_stack.empty() && bracket_stack.top() == '[') ||
                   (ch == '}' && !bracket_stack.empty() && bracket_stack.top() == '{') ||
                   (ch == '>' && !bracket_stack.empty() && bracket_stack.top() == '<')) {
            bracket_stack.pop();
            if (bracket_stack.empty()) {
                remove_text = false;
            }
        } else if (!remove_text) {
            result += ch;
        }
    }

    return result;
}

// Function to extract the last word from a string
std::string extract_func_name(const std::string& input) {
    auto clean_input = remove_text_between_brackets(input);
    ssize_t pos = clean_input.find_last_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
    auto res = clean_input.substr(pos + 1);
    return res;
}

std::string extractText(const std::string& input) {
    // Regular expression to match text between "::" and "("
    std::regex re(R"(::([^:(]+)\()");

    // Variable to hold the matched string
    std::smatch match;

    // Search for the pattern in the input string
    if (std::regex_search(input, match, re)) {
        // Return the matched string without "::" and with "(" included
        return match[1].str();
    }

    // Return an empty string if no match is found
    return "";
}

std::string stack_sym(void * const addrs[NUM_STACK_TO_PRINT]) {
    char **strs = backtrace_symbols(addrs, NUM_STACK_TO_PRINT);
    int status;
    std::vector<std::string> names;
    std::string s = "";

    for (int i = 0; i < NUM_STACK_TO_PRINT; ++i) {
        split_sym(strs[i], &names);
        char *demangled_name = abi::__cxa_demangle(names[3].c_str(), 0, 0, &status);
        if (demangled_name == nullptr) {
          s += "(null) ";
        } else {
          std::string func_name = extract_func_name(demangled_name);
          std::stringstream stream;
          stream << func_name << '(' << std::hex << addrs[i] << ") ";
          s += stream.str();
          free(demangled_name);
        }
    }
    return s;
}

std::mutex printmtx;
extern const size_t NUM_THREADS;
const int thread_start_idx = 2;
const bool debug_print = false;

// Global array of thread information
std::vector<thread_info> global_thread_info(NUM_THREADS);
std::atomic<int> thread_count(0);
std::map<std::thread::id, int> thread_id_map;

enum LogType {
    LOG_LOCK,
    LOG_MEM_OP,
    LOG_RETRY,
    LOG_OP,
    LOG_SPLIT,
    LOG_STRING
};

enum OpType {
    OP_INSERT,
    OP_INSERT_DONE,
    OP_ERASE,
    OP_ERASE_DONE,
    OP_FIND,
    OP_FIND_DONE,
    OP_END
};

struct LogInfo {
    LogType logtype;
    std::chrono::time_point<std::chrono::high_resolution_clock> timestamp;
    void *addrs[NUM_STACK_TO_PRINT];
    int threadidx;
    void *node;
    unsigned short min, max, split_key;

    union {
        struct { // LOG_LOCK
            void *slice;
            int gen;
            unsigned short sliceid;
            unsigned short level;
            unsigned short slotuse;
            unsigned int numreader;
            bool haswriter;
            int writerswaiting;
            int readerswaiting;
            int upgradewaiting;
            int lock_type_enum;
        };
        struct { // LOG_MEM_OP
            MemOpType mem_op_type;
            int num_inner;
            int num_leaves;
        };
        struct { // LOG_OP
            OpType op_type;
            int op_key;
            int op_res;
            int set_size;
        };
        struct { // LOG_STRING
            char str[200];
        };
    };
};

enum {
    TOTAL_DEBUG_LOG_INFO = 10000
};

inline std::string op_type_to_string(int op) {
    switch (op) {
        case OP_INSERT:
            return "insert";
        case OP_INSERT_DONE:
            return "instDn";
        case OP_ERASE:
            return "erase ";
        case OP_ERASE_DONE:
            return "erseDn";
        case OP_FIND:
            return "find  ";
        case OP_FIND_DONE:
            return "findDn";
        default:
            return "unknown_op_type";
    }
}

std::vector<LogInfo> debug_log_info(TOTAL_DEBUG_LOG_INFO);
std::atomic<size_t> cur_debug_log_info = 0;

#if defined(TLX_BTREE_FAST_LOG) && defined(TLX_BTREE_DEBUG) && !defined(NDEBUG)

void get_stack_addr(void *out_addrs[NUM_STACK_TO_PRINT]) {
    const int TOTAL_STACK =
        STACK_START_TO_PRINT + NUM_STACK_TO_PRINT;
    void *addrs[TOTAL_STACK];

    backtrace(addrs, TOTAL_STACK);
    for (int i = STACK_START_TO_PRINT; i < TOTAL_STACK; ++i) {
        out_addrs[i - STACK_START_TO_PRINT] = addrs[i];
    }
}

void log_retry(void *node) {
    auto& tinfo = local_debug_info.tinfo;
    if (debug_log_info.empty())
        return;

    size_t idx = cur_debug_log_info.fetch_add(
        1, std::memory_order_relaxed);

    LogInfo& log_info = debug_log_info[idx % TOTAL_DEBUG_LOG_INFO];
    if (tinfo) {
        log_info.threadidx = tinfo->threadidx;
    }
    log_info.node = node;
    log_info.logtype = LOG_RETRY;
    get_stack_addr(log_info.addrs);
}

void log_op(OpType op, int key, int res, int set_size, int thread_id) {
    if (debug_log_info.empty())
        return;

    size_t idx = cur_debug_log_info.fetch_add(
        1, std::memory_order_relaxed);

    LogInfo& log_info = debug_log_info[idx % TOTAL_DEBUG_LOG_INFO];
    log_info.logtype = LOG_OP;
    log_info.timestamp = std::chrono::high_resolution_clock::now();
    log_info.op_type = op;
    log_info.op_key = key;
    log_info.op_res = res;
    log_info.set_size = set_size;
    log_info.threadidx = thread_id;
}

void log_mem_op(MemOpType optype, void *node,
                int num_inner, int num_leaves) {
    auto& tinfo = local_debug_info.tinfo;

    if (debug_log_info.empty())
        return;

    size_t idx = cur_debug_log_info.fetch_add(
        1, std::memory_order_relaxed);

    LogInfo& log_info = debug_log_info[idx % TOTAL_DEBUG_LOG_INFO];
    if (tinfo) {
        tinfo->cur_node = node;
        log_info.threadidx = tinfo->threadidx;
    }
    log_info.logtype = LOG_MEM_OP;
    log_info.node = node;
    log_info.mem_op_type = optype;
    log_info.timestamp = std::chrono::high_resolution_clock::now();
    log_info.threadidx = local_debug_info.tinfo ?
        local_debug_info.tinfo->threadidx : 0;
    log_info.num_inner = num_inner;
    log_info.num_leaves = num_leaves;

    get_stack_addr(log_info.addrs);
}

void log_split(void *node, int split_key) {
    size_t idx = cur_debug_log_info.fetch_add(
        1, std::memory_order_relaxed);

    LogInfo& log_info = debug_log_info[idx % TOTAL_DEBUG_LOG_INFO];
    log_info.logtype = LOG_LOCK;
    log_info.threadidx =
        local_debug_info.tinfo ? local_debug_info.tinfo->threadidx : 0;

    set_type::btree_impl::node *nodep =
        static_cast<set_type::btree_impl::node *>(node);
    if (nodep->level == 0 && nodep->slotuse != 0) { // leaf
        set_type::btree_impl::LeafNode *leafp =
            static_cast<set_type::btree_impl::LeafNode *>(nodep);

        log_info.lock_type_enum = lock_type_leaf_split;
        log_info.min = leafp->min_key();
        log_info.max = leafp->max_key();
        log_info.split_key = leafp->slotdata[split_key];
    } else {
        set_type::btree_impl::InnerNode *innerp =
            static_cast<set_type::btree_impl::InnerNode *>(nodep);
        log_info.lock_type_enum = lock_type_inner_split;
        log_info.min = innerp->slotkey[0];
        log_info.max = innerp->slotkey[innerp->slotuse - 1];
        log_info.split_key = innerp->slotkey[split_key];
    }
}

void log_node(void *node) {
    size_t idx = cur_debug_log_info.fetch_add(
        1, std::memory_order_relaxed);

    LogInfo& log_info = debug_log_info[idx % TOTAL_DEBUG_LOG_INFO];
    log_info.logtype = LOG_LOCK;
    log_info.threadidx =
        local_debug_info.tinfo ? local_debug_info.tinfo->threadidx : 0;

    set_type::btree_impl::node *nodep =
        static_cast<set_type::btree_impl::node *>(node);
    log_info.lock_type_enum = lock_type_node;
    if (nodep->level == 0 && nodep->slotuse != 0) { // leaf
        set_type::btree_impl::LeafNode *leafp =
            static_cast<set_type::btree_impl::LeafNode *>(nodep);

        log_info.min = leafp->min_key();
        log_info.max = leafp->max_key();
    } else {
        set_type::btree_impl::InnerNode *innerp =
            static_cast<set_type::btree_impl::InnerNode *>(nodep);
        log_info.min = innerp->slotkey[0];
        log_info.max = innerp->slotkey[innerp->slotuse - 1];
    }
}

void log_str(const char *str) {
    size_t idx = cur_debug_log_info.fetch_add(
        1, std::memory_order_relaxed);

    LogInfo& log_info = debug_log_info[idx % TOTAL_DEBUG_LOG_INFO];
    log_info.logtype = LOG_STRING;
    log_info.timestamp = std::chrono::high_resolution_clock::now();
    log_info.threadidx = local_debug_info.tinfo ? local_debug_info.tinfo->threadidx : 0;
    strncpy(log_info.str, str, sizeof(log_info.str));
}

void log_lock(void* node __attribute__((unused)),
              int lock_type __attribute__((unused)),
              unsigned short sliceid = MAPL_NONE) {
    auto& tinfo = local_debug_info.tinfo;
    size_t idx = cur_debug_log_info.fetch_add(
        1, std::memory_order_relaxed);

    LogInfo& log_info = debug_log_info[idx % TOTAL_DEBUG_LOG_INFO];
    if (tinfo) {
        tinfo->cur_node = node;
        tinfo->op = lock_type;
        log_info.threadidx = tinfo->threadidx;
    }
    log_info.logtype = LOG_LOCK;
    log_info.timestamp = std::chrono::high_resolution_clock::now();
    log_info.node = node;

    set_type::btree_impl::node *nodep =
        static_cast<set_type::btree_impl::node *>(node);
    log_info.numreader = 0;
    log_info.haswriter = 0;
    log_info.readerswaiting = 0;
    log_info.writerswaiting = 0;
    log_info.upgradewaiting = 0;

    log_info.level = nodep ? nodep->level : -1;
    log_info.slotuse = nodep ? nodep->slotuse : -1;

    if (nodep) {
        if (nodep->level == 0 && nodep->slotuse != 0) { // leaf
            set_type::btree_impl::LeafNode *leafp =
                static_cast<set_type::btree_impl::LeafNode *>(nodep);
            auto mapl = leafp->mapl;
            auto lockp = &leafp->mutex_;

            log_info.sliceid = sliceid;

            log_info.min = log_info.max = 0;
            if (mapl) {
                log_info.slice = mapl->slices + sliceid;
                if (sliceid == MAPL_FREE_LIST_MTX) {
                    lockp = &mapl->free_slot_mtx;
                }
                else if (sliceid != MAPL_NONE) {
                    lockp = &mapl->slices[sliceid].lock;
                }
            } else {
                log_info.slice = nullptr;
            }
            // get min/max only if leaf is locked
            if ((leafp->mutex_.self_read_locked() && !leafp->mapl) ||
                leafp->mutex_.self_write_locked()) {
                log_info.min = leafp->min_key();
                log_info.max = leafp->max_key();
            }

            log_info.numreader = lockp->numreader;
            log_info.haswriter = lockp->haswriter;
            log_info.readerswaiting = lockp->readerswaiting;
            log_info.writerswaiting = lockp->writerswaiting;
            log_info.upgradewaiting = lockp->upgradewaiting;
        } else {
            set_type::btree_impl::InnerNode *innerp =
                static_cast<set_type::btree_impl::InnerNode *>(nodep);
            log_info.min = innerp->slotkey[0];
            log_info.max = innerp->slotkey[innerp->slotuse - 1];
            log_info.numreader = innerp->mutex_.numreader;
            log_info.haswriter = innerp->mutex_.haswriter;
            log_info.sliceid = MAPL_NONE;
            log_info.readerswaiting = innerp->mutex_.readerswaiting;
            log_info.writerswaiting = innerp->mutex_.writerswaiting;
            log_info.upgradewaiting = innerp->mutex_.upgradewaiting;
        }
    }
    log_info.lock_type_enum = lock_type;

    get_stack_addr(log_info.addrs);
}

const char *MemOpName[] = {
    "alloc inner",
    "alloc leaf",
    "free inner",
    "free leaf"
};

bool print_log_record(const LogInfo& info, int n) {
    std::lock_guard<std::mutex> printlock(printmtx);
    switch (info.logtype) {
    case LOG_LOCK:
        if (!prt_lock) {
            return true;
        }
        if (info.lock_type_enum == 0) {
            return false; // empty record stop printing
        }
        std::cout << std::setw(5) << std::setfill(' ') << n << " "
                  << format_time(info.timestamp)
                  << " thread " << info.threadidx
                  << " node " << info.node
                  << "[" << info.min << "," << info.max << "]";
        if (info.sliceid == MAPL_FREE_LIST_MTX) {
            std::cout << " free_hdr";
        }
        else if (info.sliceid != MAPL_NONE) {
            std::cout << " slice[" << info.sliceid << "]("
                      << info.slice << ")";
        }
        std::cout << " (g" << info.gen
                  << " c" << info.slotuse
                  << " L" << info.level
                  << ") (r" << info.numreader
                  << "|w" << info.haswriter
                  << " waiter:r" << info.readerswaiting
                  << "|w" << info.writerswaiting
                  << "|u" << info.upgradewaiting
                  << ") "
                  << lock_type_to_string(info.lock_type_enum)
                  << " "
                  << stack_sym(info.addrs)
                  << std::endl;
        break;
    case LOG_SPLIT:
        if (!prt_split) {
            return true;
        }
        std::cout << format_time(info.timestamp)
                  << " thread " << info.threadidx
                  << " node " << info.node
                  << " split "
                  << "[" << info.min << "," << info.max << "]"
                  << " split_key=" << info.split_key
                  << " "
                  << stack_sym(info.addrs)
                  << std::endl;
        break;
    case LOG_MEM_OP:
        if (!prt_mem_op) {
            return true;
        }
        std::cout << format_time(info.timestamp)
                  << " thread " << info.threadidx
                  << " " << MemOpName[info.mem_op_type]
                  << " " << info.node
                  << " #inner=" << info.num_inner
                  << " #leaves=" << info.num_leaves
                  << " "
                  << stack_sym(info.addrs)
                  << std::endl;
        break;
    case LOG_RETRY:
        if (!prt_retry) {
            return true;
        }
        std::cout << format_time(info.timestamp)
                  << " thread " << info.threadidx
                  << " retry "
                  << " node " << info.node
                  << " "
                  << stack_sym(info.addrs)
                  << std::endl;
        break;
    case LOG_OP:
        if (!prt_op) {
            return true;
        }
        std::cout << format_time(info.timestamp)
                  << " thread " << info.threadidx
                  << " " << op_type_to_string(info.op_type)
                  << "\tkey=" << std::setw(2) << info.op_key
                  << "\tres=" << info.op_res
                  << "\tset_size=" << info.set_size
                  << std::endl;
        break;
    case LOG_STRING:
        if (!prt_op) {
            return true;
        }
        std::cout << format_time(info.timestamp)
                  << " thread " << info.threadidx
                  << " " << info.str
                  << std::endl;
        break;
    default:
        return false;
    }
    return true;
}

void print_all_lock_records() {
  size_t i;
  size_t n = 0;
  size_t cur_index = cur_debug_log_info % TOTAL_DEBUG_LOG_INFO;
  for (i = cur_index; i < debug_log_info.size(); ++i) {
      if (!print_log_record(debug_log_info[i], n++)) {
          break;
      }
  }

  for (i = 0; i < cur_index; ++i) {
      print_log_record(debug_log_info[i], n++);
  }
}

void print_threads_states(void)
{
    g_test_set->print(std::cout);
#if 0
    for (size_t i = 0; i < NUM_THREADS; ++i) {
        std::cout << "Thread " << i + thread_start_idx << " id: " << global_thread_info[i].id
            << " - Node: " << global_thread_info[i].cur_node
            << ", Operation: " << lock_type_to_string(global_thread_info[i].op)
            << std::endl;
        if (global_thread_info[i].cur_node) {
            set_type::btree_impl::node *nodep = static_cast<set_type::btree_impl::node *>(global_thread_info[i].cur_node);
            bool isleaf = nodep->level == 0 && nodep->slotuse != 0;
            auto leaf_lock = static_cast<set_type::btree_impl::LeafNode *>(nodep)->lock;
            auto inner_lock = static_cast<set_type::btree_impl::InnerNode *>(nodep)->lock;
            if ((isleaf && leaf_lock == nullptr) ||
                (!isleaf && inner_lock == nullptr)) {
                std::cout << "  lock=null\n";
                continue;
            }
            std::cout << "  curread: ";
            std::set<int> ids; // print all ids in order
            if (isleaf) {
                for (auto id: leaf_lock->curread) {
                    ids.insert(thread_id_map[id]);
                }
            } else {
                for (auto id: inner_lock->curread) {
                    ids.insert(thread_id_map[id]);
                }
            }
            for (auto id: ids) {
                std::cout << id << ' ';
            }
            ids.clear();
            std::cout << std::endl;
            std::cout << "  curwrite: ";
            if (isleaf) {
                for (auto id: leaf_lock->curwrite) {
                    ids.insert(thread_id_map[id]);
                }
            } else {
                for (auto id: inner_lock->curwrite) {
                    ids.insert(thread_id_map[id]);
                }
            }
            for (auto id: ids) {
                std::cout << id << ' ';
            }
            std::cout << std::endl;
        }
    }
#endif
}

void before_assert(void)
{
    static bool tree_printed = false;
    if (!tree_printed) { // only print once
        tree_printed = true;
        print_all_lock_records();
        std::lock_guard<std::mutex> l(printmtx);
        std::cout << "======= print thread state before assert =======\n";
        print_threads_states();
        std::cout << std::endl;
        return;
    } else {
        sleep(3600);
    }
}

#else

inline void print_all_lock_records() {}
inline void print_threads_states() {}

#endif

// Signal handler for SIGUSR1
void signal_handler(int signum) {
    if (signum == SIGUSR1) {
        std::cout << "Received SIGUSR1. Current thread states:\n";
        print_all_lock_records();
        print_threads_states();
    } else {
        std::cout << "Received signal " << signum << std::endl;
    }
}

// Function to initialize thread debug info
void initialize_thread_info(int index) {
    std::lock_guard<std::mutex> lock(printmtx);
    local_debug_info.tinfo = &global_thread_info[index];
    local_debug_info.tinfo->id = std::this_thread::get_id();
    local_debug_info.tinfo->threadidx = index + thread_start_idx;
    local_debug_info.tinfo->cur_node = nullptr;
    local_debug_info.tinfo->op = 0;
    thread_id_map[std::this_thread::get_id()] = index + thread_start_idx;
}

// Function to cleanup thread debug info
void cleanup_thread_info() {
    local_debug_info.tinfo->cur_node = nullptr;
    local_debug_info.tinfo->op = 0;
}
