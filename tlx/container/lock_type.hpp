// lock_type.hpp
#ifndef LOCK_TYPE_HPP
#define LOCK_TYPE_HPP

enum lock_type_enum {
    lock_type_read = 1,
    lock_type_try_read,
    lock_type_read_notify_upgrader,
    lock_type_read_notify_writer,
    lock_type_read_wait,
    lock_type_read_got,
    lock_type_write,
    lock_type_write_got,
    lock_type_upgrade,
    lock_type_upgrade_wait,
    lock_type_upgrade_got,
    lock_type_downgrade,
    lock_type_downgrade_notify_reader,
    lock_type_read_unlock,
    lock_type_read_unlock_notify_upgrader,
    lock_type_read_unlock_notify_writer,
    lock_type_try_upgrade_release_on_fail,
    lock_type_try_upgrade_failed,
    lock_type_try_upgrade_got,
    lock_type_write_unlock,
    lock_type_write_unlock_notify_upgrader,
    lock_type_write_unlock_notify_writer,
    lock_type_write_unlock_notify_reader,
    lock_type_inner_split,
    lock_type_leaf_split,
    lock_type_node
};

// Function to convert enum to string
inline std::string lock_type_to_string(int lt) {
    switch (lt) {
        case lock_type_read:
            return "read_lock";
        case lock_type_try_read:
            return "try_read_lock";
        case lock_type_read_notify_upgrader:
            return "read_lock_t_upgrader";
        case lock_type_read_notify_writer:
            return "read_lock_t_writer";
        case lock_type_read_wait:
            return "read_lock_wait";
        case lock_type_read_got:
            return "read_lock_got";
        case lock_type_write:
            return "write_lock";
        case lock_type_write_got:
            return "write_lock_got";
        case lock_type_upgrade:
            return "upgrade_lock";
        case lock_type_upgrade_wait:
            return "upgrade_lock_wait";
        case lock_type_upgrade_got:
            return "upgrade_lock_got";
        case lock_type_downgrade:
            return "downgrade_lock";
        case lock_type_downgrade_notify_reader:
            return "downgrade_t_reader";
        case lock_type_read_unlock:
            return "read_unlock";
        case lock_type_read_unlock_notify_upgrader:
            return "read_unlock_t_upgrader";
        case lock_type_read_unlock_notify_writer:
            return "read_unlock_t_writer";
        case lock_type_try_upgrade_release_on_fail:
            return "lock_type_try_upgrade_release_on_fail";
        case lock_type_try_upgrade_failed:
            return "lock_type_try_upgrade_failed";
        case lock_type_try_upgrade_got:
            return "lock_type_try_upgrade_got";
        case lock_type_write_unlock:
            return "write_unlock";
        case lock_type_write_unlock_notify_upgrader:
            return "write_unlock_t_upgrader";
        case lock_type_write_unlock_notify_writer:
            return "write_unlock_t_writer";
        case lock_type_write_unlock_notify_reader:
            return "write_unlock_t_reader";
        case lock_type_inner_split:
            return "inner_split";
        case lock_type_leaf_split:
            return "leaf_split";
        case lock_type_node:
            return "node";
        default:
            return "unknown_lock_type";
    }
}

enum lock_requirement {
    lock_all,
    lock_root_only,
    lock_no_root_only,
    lock_none
};

enum MemOpType {
  ALLOC_INNER,
  ALLOC_LEAF,
  FREE_INNER,
  FREE_LEAF,
};

extern std::mutex printmtx;
extern const bool debug_print;
extern int seq; // TODO delete
thread_local int local_thread_id;

#if defined(TLX_BTREE_FAST_LOG) && defined(TLX_BTREE_DEBUG) && !defined(NDEBUG)

extern void log_lock(void *node, int lock_type_enum, unsigned short sliceid);
extern void log_split(void *nodep, int split_key);
extern void log_mem_op(MemOpType optype,void *node, int num_inner, int num_leaves);
extern void log_retry(void *node);
extern void log_str(const char *str);

#define LOG_STR(s)                              \
    {                                           \
        std::stringstream _ss;                   \
        _ss << s;                                \
        log_str(_ss.str().c_str());              \
    }

#define VERIFY_NODE(verify, treep, nodep)       \
    if (verify) treep->verify_one_node(nodep);

#else

#define log_lock(node, lock_type, sliceid)
#define log_split(nodep, split_key);
#define log_mem_op(optype, node, num_inner, num_leaves)
#define log_op(op, key, res, set_size, thread_id)
#define log_retry(node)
#define LOG_STR(s)
#define before_assert()
#define VERIFY_NODE(verify, treep, nodep)

#endif



#endif // LOCK_TYPE_HPP
