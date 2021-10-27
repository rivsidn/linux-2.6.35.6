/*
 * Lockdep states,
 *
 * please update XXX_LOCK_USAGE_STATES in include/linux/lockdep.h whenever
 * you add one, or come up with a nice dynamic solution.
 */
/*
 * lockdep_internals.h 文件中引用这些定义
 */
LOCKDEP_STATE(HARDIRQ)
LOCKDEP_STATE(SOFTIRQ)
LOCKDEP_STATE(RECLAIM_FS)
