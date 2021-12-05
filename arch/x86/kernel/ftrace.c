/*
 * Code for replacing ftrace calls with jumps.
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 *
 * Thanks goes to Ingo Molnar, for suggesting the idea.
 * Mathieu Desnoyers, for suggesting postponing the modifications.
 * Arjan van de Ven, for keeping me straight, and explaining to me
 * the dangers of modifying code on the run.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/spinlock.h>
#include <linux/hardirq.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/list.h>

#include <trace/syscall.h>

#include <asm/cacheflush.h>
#include <asm/ftrace.h>
#include <asm/nops.h>
#include <asm/nmi.h>


#ifdef CONFIG_DYNAMIC_FTRACE

/*
 * modifying_code is set to notify NMIs that they need to use
 * memory barriers when entering or exiting. But we don't want
 * to burden NMIs with unnecessary memory barriers when code
 * modification is not being done (which is most of the time).
 *
 * A mutex is already held when ftrace_arch_code_modify_prepare
 * and post_process are called. No locks need to be taken here.
 *
 * Stop machine will make sure currently running NMIs are done
 * and new NMIs will see the updated variable before we need
 * to worry about NMIs doing memory barriers.
 */
static int modifying_code __read_mostly;
static DEFINE_PER_CPU(int, save_modifying_code);

int ftrace_arch_code_modify_prepare(void)
{
	set_kernel_text_rw();		//设置内核代码段可读写
	modifying_code = 1;
	return 0;
}

int ftrace_arch_code_modify_post_process(void)
{
	modifying_code = 0;
	set_kernel_text_ro();		//设置内核代码段只读
	return 0;
}

union ftrace_code_union {
	char code[MCOUNT_INSN_SIZE];
	/*
	 * 如果不加 packed 属性，这里会占用 8 个字节，
	 * 加了之后只占用 5 个字节
	 */
	struct {
		char e8;
		int offset;
	} __attribute__((packed));
};

/* 计算ip, addr 之间偏移量 */
static int ftrace_calc_offset(long ip, long addr)
{
	return (int)(addr - ip);
}

static unsigned char *ftrace_call_replace(unsigned long ip, unsigned long addr)
{
	static union ftrace_code_union calc;

	/* 0xe8 CALL 指令，后边是四字节的地址 */
	calc.e8		= 0xe8;
	/* call+偏移地址 属于一条指令，偏移地址是想对于该指令尾的偏移地址，需要加上指令长度 */
	calc.offset	= ftrace_calc_offset(ip + MCOUNT_INSN_SIZE, addr);

	/*
	 * No locking needed, this must be called via kstop_machine
	 * which in essence is like running on a uniprocessor machine.
	 */
	/*
	 * 通过 kstop_machine 调用该函数，就像运行在单核处理器上.
	 */
	return calc.code;
}

/*
 * Modifying code must take extra care. On an SMP machine, if
 * the code being modified is also being executed on another CPU
 * that CPU will have undefined results and possibly take a GPF.
 * We use kstop_machine to stop other CPUS from exectuing code.
 * But this does not stop NMIs from happening. We still need
 * to protect against that. We separate out the modification of
 * the code to take care of this.
 *
 * Two buffers are added: An IP buffer and a "code" buffer.
 *
 * 1) Put the instruction pointer into the IP buffer
 *    and the new code into the "code" buffer.
 * 2) Wait for any running NMIs to finish and set a flag that says
 *    we are modifying code, it is done in an atomic operation.
 * 3) Write the code
 * 4) clear the flag.
 * 5) Wait for any running NMIs to finish.
 *
 * If an NMI is executed, the first thing it does is to call
 * "ftrace_nmi_enter". This will check if the flag is set to write
 * and if it is, it will write what is in the IP and "code" buffers.
 *
 * The trick is, it does not matter if everyone is writing the same
 * content to the code location. Also, if a CPU is executing code
 * it is OK to write to that code location if the contents being written
 * are the same as what exists.
 */
/*
 * 修改代码需要认真考虑，在SMP系统中，如果修改的代码正在被别的CPU执行
 * 可能会导致未定义的结果，可能会导致GPF。我们通过 kstop_machine 让其
 * 他CPU 停止执行代码，但是该操作并不会阻止NMIs，我们需要阻止这种情况。
 */

#define MOD_CODE_WRITE_FLAG (1 << 31)	/* set when NMI should do the write */
static atomic_t nmi_running = ATOMIC_INIT(0);
static int mod_code_status;		/* holds return value of text write */
					/* 保存写入代码段的返回值 */
static void *mod_code_ip;		/* holds the IP to write to */
					/* 保存要写入的IP 地址 */
static void *mod_code_newcode;		/* holds the text to write to the IP */
					/* 保存要写入IP 的内容 */
static unsigned nmi_wait_count;
static atomic_t nmi_update_count = ATOMIC_INIT(0);

/* 获取信息，用于debugfs 显示 */
int ftrace_arch_read_dyn_info(char *buf, int size)
{
	int r;

	r = snprintf(buf, size, "%u %u",
		     nmi_wait_count,
		     atomic_read(&nmi_update_count));
	return r;
}

/*
 * 1. nmi 中调用
 * 2. 进程上下文中调用
 * 
 * 无论是上边哪种情况，smp 情况下都可能会有其他CPU在同步运行，
 * atomic_cmpxchg() 时有三种可能:
 * 1. nmi_running 此处没动，被其他地方的nmi 设置，退出循环
 * 2. 该处的设置，推出循环
 * 3. 不停有新的nmi 进来，nmi_running 不断更新，都不会设置(如何避免这种情况)
 */
static void clear_mod_flag(void)
{
	int old = atomic_read(&nmi_running);

	for (;;) {
		int new = old & ~MOD_CODE_WRITE_FLAG;

		/* 如果没有设置 MOD_CODE_WRITE_FLAG 标识位，跳出 */
		if (old == new)
			break;

		old = atomic_cmpxchg(&nmi_running, old, new);
	}
}

static void ftrace_mod_code(void)
{
	/*
	 * Yes, more than one CPU process can be writing to mod_code_status.
	 *    (and the code itself)
	 * But if one were to fail, then they all should, and if one were
	 * to succeed, then they all should.
	 */
	/*
	 * probe_kernel_write() 参数:
	 * 地址，内容，长度
	 */
	mod_code_status = probe_kernel_write(mod_code_ip, mod_code_newcode,
					     MCOUNT_INSN_SIZE);

	/* if we fail, then kill any new writers */
	if (mod_code_status)
		clear_mod_flag();
}

/* 进入nmi 时候调用 */
void ftrace_nmi_enter(void)
{
	__get_cpu_var(save_modifying_code) = modifying_code;

	if (!__get_cpu_var(save_modifying_code))
		return;

	if (atomic_inc_return(&nmi_running) & MOD_CODE_WRITE_FLAG) {
		smp_rmb();
		ftrace_mod_code();
		atomic_inc(&nmi_update_count);
	}
	/* Must have previous changes seen before executions */
	smp_mb();
}

/* 退出nmi 时候调用 */
void ftrace_nmi_exit(void)
{
	if (!__get_cpu_var(save_modifying_code))
		return;

	/* Finish all executions before clearing nmi_running */
	smp_mb();
	atomic_dec(&nmi_running);
}

/*
 * 运行该函数之后，nmi_running 中肯定设置了 MOD_CODE_WRITE_FLAG 标识位，
 * 后边进入到 ftrace_nmi_enter() 时，如果检测到该标识位，就需要执行函数
 * 写操作.
 */
static void wait_for_nmi_and_set_mod_flag(void)
{
	/* 如果当前没有nmi 正在运行，设置MOD_CODE_WRITE_FLAG，返回 */
	if (!atomic_cmpxchg(&nmi_running, 0, MOD_CODE_WRITE_FLAG))
		return;

	/* 等待没有nmi 运行了，设置MOD_CODE_WRITE_FLAG 标识位 */
	do {
		cpu_relax();
	} while (atomic_cmpxchg(&nmi_running, 0, MOD_CODE_WRITE_FLAG));

	/* 更新统计信息 */
	nmi_wait_count++;
}

static void wait_for_nmi(void)
{
	if (!atomic_read(&nmi_running))
		return;

	/* 等待没有nmi 运行了，再返回 */
	do {
		cpu_relax();
	} while (atomic_read(&nmi_running));

	/* 更新统计信息 */
	nmi_wait_count++;
}

static inline int
within(unsigned long addr, unsigned long start, unsigned long end)
{
	return addr >= start && addr < end;
}

static int
do_ftrace_mod_code(unsigned long ip, void *new_code)
{
	/*
	 * On x86_64, kernel text mappings are mapped read-only with
	 * CONFIG_DEBUG_RODATA. So we use the kernel identity mapping instead
	 * of the kernel text mapping to modify the kernel text.
	 *
	 * For 32bit kernels, these mappings are same and we can use
	 * kernel identity mapping to modify code.
	 */
	if (within(ip, (unsigned long)_text, (unsigned long)_etext))
		ip = (unsigned long)__va(__pa(ip));

	mod_code_ip = (void *)ip;
	mod_code_newcode = new_code;

	/* The buffers need to be visible before we let NMIs write them */
	smp_mb();

	wait_for_nmi_and_set_mod_flag();

	/* Make sure all running NMIs have finished before we write the code */
	smp_mb();

	ftrace_mod_code();

	/* Make sure the write happens before clearing the bit */
	smp_mb();

	clear_mod_flag();
	wait_for_nmi();

	return mod_code_status;
}

static unsigned char ftrace_nop[MCOUNT_INSN_SIZE];

static unsigned char *ftrace_nop_replace(void)
{
	return ftrace_nop;
}

static int
ftrace_modify_code(unsigned long ip, unsigned char *old_code,
		   unsigned char *new_code)
{
	unsigned char replaced[MCOUNT_INSN_SIZE];

	/*
	 * Note: Due to modules and __init, code can
	 *  disappear and change, we need to protect against faulting
	 *  as well as code changing. We do this by using the
	 *  probe_kernel_* functions.
	 *
	 * No real locking needed, this code is run through
	 * kstop_machine, or before SMP starts.
	 */

	/* read the text we want to modify */
	if (probe_kernel_read(replaced, (void *)ip, MCOUNT_INSN_SIZE))
		return -EFAULT;

	/* Make sure it is what we expect it to be */
	if (memcmp(replaced, old_code, MCOUNT_INSN_SIZE) != 0)
		return -EINVAL;

	/* replace the text with the new text */
	if (do_ftrace_mod_code(ip, new_code))
		return -EPERM;

	sync_core();

	return 0;
}

int ftrace_make_nop(struct module *mod,
		    struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned char *new, *old;
	unsigned long ip = rec->ip;

	old = ftrace_call_replace(ip, addr);
	new = ftrace_nop_replace();

	return ftrace_modify_code(rec->ip, old, new);
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned char *new, *old;
	unsigned long ip = rec->ip;

	old = ftrace_nop_replace();
	new = ftrace_call_replace(ip, addr);

	return ftrace_modify_code(rec->ip, old, new);
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long ip = (unsigned long)(&ftrace_call);
	unsigned char old[MCOUNT_INSN_SIZE], *new;
	int ret;

	memcpy(old, &ftrace_call, MCOUNT_INSN_SIZE);
	new = ftrace_call_replace(ip, (unsigned long)func);
	ret = ftrace_modify_code(ip, old, new);

	return ret;
}

int __init ftrace_dyn_arch_init(void *data)
{
	extern const unsigned char ftrace_test_p6nop[];
	extern const unsigned char ftrace_test_nop5[];
	extern const unsigned char ftrace_test_jmp[];
	int faulted = 0;

	/*
	 * There is no good nop for all x86 archs.
	 * We will default to using the P6_NOP5, but first we
	 * will test to make sure that the nop will actually
	 * work on this CPU. If it faults, we will then
	 * go to a lesser efficient 5 byte nop. If that fails
	 * we then just use a jmp as our nop. This isn't the most
	 * efficient nop, but we can not use a multi part nop
	 * since we would then risk being preempted in the middle
	 * of that nop, and if we enabled tracing then, it might
	 * cause a system crash.
	 *
	 * TODO: check the cpuid to determine the best nop.
	 */
	/*
	 * TODO: 理解这里的汇编代码
	 */
	asm volatile (
		"ftrace_test_jmp:"
		"jmp ftrace_test_p6nop\n"
		"nop\n"
		"nop\n"
		"nop\n"  /* 2 byte jmp + 3 bytes */
		"ftrace_test_p6nop:"
		P6_NOP5
		"jmp 1f\n"
		"ftrace_test_nop5:"
		".byte 0x66,0x66,0x66,0x66,0x90\n"
		"1:"
		".section .fixup, \"ax\"\n"
		"2:	movl $1, %0\n"
		"	jmp ftrace_test_nop5\n"
		"3:	movl $2, %0\n"
		"	jmp 1b\n"
		".previous\n"
		_ASM_EXTABLE(ftrace_test_p6nop, 2b)
		_ASM_EXTABLE(ftrace_test_nop5, 3b)
		: "=r"(faulted) : "0" (faulted));

	/* 通过返回值判断改用那个作为 nop 操作 */
	switch (faulted) {
	case 0:
		pr_info("converting mcount calls to 0f 1f 44 00 00\n");
		memcpy(ftrace_nop, ftrace_test_p6nop, MCOUNT_INSN_SIZE);
		break;
	case 1:
		pr_info("converting mcount calls to 66 66 66 66 90\n");
		memcpy(ftrace_nop, ftrace_test_nop5, MCOUNT_INSN_SIZE);
		break;
	case 2:
		pr_info("converting mcount calls to jmp . + 5\n");
		memcpy(ftrace_nop, ftrace_test_jmp, MCOUNT_INSN_SIZE);
		break;
	}

	/* The return code is retured via data */
	*(unsigned long *)data = 0;

	return 0;
}
#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

#ifdef CONFIG_DYNAMIC_FTRACE
extern void ftrace_graph_call(void);

static int ftrace_mod_jmp(unsigned long ip,
			  int old_offset, int new_offset)
{
	unsigned char code[MCOUNT_INSN_SIZE];

	if (probe_kernel_read(code, (void *)ip, MCOUNT_INSN_SIZE))
		return -EFAULT;

	if (code[0] != 0xe9 || old_offset != *(int *)(&code[1]))
		return -EINVAL;

	*(int *)(&code[1]) = new_offset;

	if (do_ftrace_mod_code(ip, &code))
		return -EPERM;

	return 0;
}

int ftrace_enable_ftrace_graph_caller(void)
{
	unsigned long ip = (unsigned long)(&ftrace_graph_call);
	int old_offset, new_offset;

	/* 使能function grapth时，设置地址为 ftrace_graph_caller */
	old_offset = (unsigned long)(&ftrace_stub) - (ip + MCOUNT_INSN_SIZE);
	new_offset = (unsigned long)(&ftrace_graph_caller) - (ip + MCOUNT_INSN_SIZE);

	return ftrace_mod_jmp(ip, old_offset, new_offset);
}

int ftrace_disable_ftrace_graph_caller(void)
{
	unsigned long ip = (unsigned long)(&ftrace_graph_call);
	int old_offset, new_offset;

	old_offset = (unsigned long)(&ftrace_graph_caller) - (ip + MCOUNT_INSN_SIZE);
	new_offset = (unsigned long)(&ftrace_stub) - (ip + MCOUNT_INSN_SIZE);

	return ftrace_mod_jmp(ip, old_offset, new_offset);
}

#endif /* !CONFIG_DYNAMIC_FTRACE */

/*
 * Hook the return address and push it in the stack of return addrs
 * in current thread info.
 */
/*
 * 挂载钩子函数挂载到返回地址中
 */
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr,
			   unsigned long frame_pointer)
{
	unsigned long old;
	int faulted;
	struct ftrace_graph_ent trace;
	unsigned long return_hooker = (unsigned long)&return_to_handler;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	/*
	 * Protect against fault, even if it shouldn't
	 * happen. This tool is too much intrusive to
	 * ignore such a protection.
	 */
	/*
	 * 设置返回地址
	 */
	asm volatile(
		"1: " _ASM_MOV " (%[parent]), %[old]\n"
		"2: " _ASM_MOV " %[return_hooker], (%[parent])\n"
		"   movl $0, %[faulted]\n"
		"3:\n"

		".section .fixup, \"ax\"\n"
		"4: movl $1, %[faulted]\n"
		"   jmp 3b\n"
		".previous\n"

		_ASM_EXTABLE(1b, 4b)
		_ASM_EXTABLE(2b, 4b)

		: [old] "=&r" (old), [faulted] "=r" (faulted)
		: [parent] "r" (parent), [return_hooker] "r" (return_hooker)
		: "memory"
	);

	/* 如果出现异常，停止追踪并告警 */
	if (unlikely(faulted)) {
		ftrace_graph_stop();
		WARN_ON(1);
		return;
	}

	if (ftrace_push_return_trace(old, self_addr, &trace.depth, frame_pointer) == -EBUSY) {
		*parent = old;
		return;
	}

	trace.func = self_addr;

	/* Only trace if the calling function expects to */
	if (!ftrace_graph_entry(&trace)) {
		current->curr_ret_stack--;
		*parent = old;
	}
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
