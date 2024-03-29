/*
 * lib/debug_locks.c
 *
 * Generic place for common debugging facilities for various locks:
 * spinlocks, rwlocks, mutexes and rwsems.
 *
 * Started by Ingo Molnar:
 *
 *  Copyright (C) 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 */
#include <linux/kernel.h>
#include <linux/rwsem.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/debug_locks.h>

/*
 * We want to turn all lock-debugging facilities on/off at once,
 * via a global flag. The reason is that once a single bug has been
 * detected and reported, there might be cascade of followup bugs
 * that would just muddy the log. So we report the first one and
 * shut up after that.
 */
/*
 * 全局的锁调试开关，一旦出现问题，立即关闭所有的锁调试功能，避免
 * 后续的异常影响问题诊断
 */
int debug_locks = 1;
EXPORT_SYMBOL_GPL(debug_locks);

/*
 * The locking-testsuite uses <debug_locks_silent> to get a
 * 'silent failure': nothing is printed to the console when
 * a locking bug is detected.
 */
/*
 * 代码出现异常的时候，不输出任何提示信息
 */
int debug_locks_silent;

/*
 * Generic 'turn off all lock debugging' function:
 */
/*
 * 关闭所有的锁调试功能
 */
int debug_locks_off(void)
{
	if (__debug_locks_off()) {
		if (!debug_locks_silent) {
			oops_in_progress = 1;
			console_verbose();
			return 1;
		}
	}
	return 0;
}
