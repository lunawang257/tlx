// lock_type.hpp
#ifndef LOCK_TYPE_HPP
#define LOCK_TYPE_HPP

enum lock_type_enum {
    lock_type_read = 1,
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
};

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

#if defined(TLX_BTREE_TEST) && defined(TLX_BTREE_DEBUG) && !defined(NDEBUG)

extern void log_lock(void *node, int lock_type_enum);
extern void log_split(void *nodep, int split_key);
extern void log_mem_op(MemOpType optype,void *node, int num_inner, int num_leaves);
extern void log_retry(void *node);

#define VERIFY_NODE(verify, treep, nodep)       \
    if (verify) treep->verify_one_node(nodep);

#else

#define log_lock(node, lock_type)
#define log_split(nodep, split_key);
#define log_mem_op(optype, node, num_inner, num_leaves)
#define log_op(op, key, res, set_size, thread_id)
#define log_retry(node)
#define before_assert()
#define VERIFY_NODE(verify, treep, nodep)

#endif



#endif // LOCK_TYPE_HPP
