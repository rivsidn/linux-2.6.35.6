## 使用内核Tracepoints

该文档介绍了Linux内核Tracepoints 和它的使用方法，提供了向内核中插入tracepoints并关联探测函数以及探测函数的示例。



### tracepoints目的

tracepoints 在代码中给probe函数提供了一个挂载点，代码运行时，可以提供probe函数。

tracepoints 当挂载了probe函数时为“启动”状态，否则是“关闭”状态。

当tracepoints 为关闭状态时，除了少量时间、空间上的损耗外，没有任何影响。

当tracepoints 关联了probe 函数时，当执行到tracepoints 的时候就会执行我们提供的probe函数，函数执行结束之后，返回到调用者继续向下执行。

你可以在重要代码的位置放置tracepoints，他们是轻量的钩子函数并且可以传递任意数量的参数，函数原型在头文件中tracepoint 声明时描述。

tracepoints 可以用于跟踪和性能统计。



### 用法

tracepoints 需要两个元素：

* tracepoint 的定义，在头文件中
* tracepoint 的声明，在 C 代码中

使用tracepoints 时，需要包含头文件linux/tracepoint.h 。

include/trace/subsys.h :

```c
#include <linux/tracepoint.h>

DECLARE_TRACE(subsys_eventname,
	TP_PROTO(int firstarg, struct task_struct *p),
	TP_ARGS(firstarg, p));
```

subsys/file.c :

```c
DEFINE_TRACE(subsys_eventname);

void somefct(void)
{
	...
	trace_subsys_eventname(arg, task);
	...
}
```

其中：

* subsys\_eventname 是事件的描述符，该描述符是唯一的
	* subsys 是子系统的名称
	* eventname 是跟踪事件的名称
* TP\_PROTO(int firstarg, struct task\_struct \*p) 是tracepoint 调用函数的函数原型
* TP\_ARGS(firstarg, p) 是参数名称，根函数原型中一致


将probe 函数连接到tracepoint 是通过register\_trace\_subsys\_eventname() 给特定的 tracepoint 点提供一个钩子函数来实现的；移除是通过unregister\_trace\_subsys\_eventname()函数来实现的。

必须要在模块退出之前调用tracepoint\_synchronize\_unregister()函数，确保没有调用者在用该钩子函数。当调用probe函数的时候，是禁止抢占的，确保探测函数移除和模块写在是安全的。查看下边的“探测示例”中的probe模块示例。

tracepoint 机制支持同一个tracepoint 中插入多个实例，但是内核中只能有一个定义，否则可能会出现类型冲突。tracepoint 通过函数原型来确保类型是正确的，probe 函数在注册时编译器会确保类型是正确的。tracepoint 可以放在内连函数中，内联静态函数中，展开的循环中，和正常的函数中。

为了降低冲突，我们建议都采用 “subsys_event” 的命名机制，tracepoint 在内核中是全局唯一的，无论是内核镜像还是模块中。

如果 tracepoint 需要用在内核模块中，需要通过`EXPORT_TRACEPOINT_SYMBOL_GPL()` `EXPORT_TRACEPOINT_SYMBOL()` 将模块导出。



### Probe/tracepoint 示例

查看在 samples/tracepoints 中提供的示例。

在你的内核中编译他们，CONFIG_SAMPLE_TRACEPOINTS=m 时通过 make 编译。

运行：

```bash
//加载的顺序并不重要
modprobe tracepoint-sample
modprobe tracepoint-probe-sample

//返回期望的错误
cat /proc/tracepoint-sample
rmmod tracepoint-sample tracepoint-probe-sample

dmesg
```



## 总结

使用 tracepoint 分两部分，内核景象或者模块中定义了tracepoint的挂载点和调用位置，probe函数在模块中定义，允许动态插入。

同一个tracepoint 可以存在多个调用位置，但是只能定义一次，防止出现类型冲突。



**TDOO： 能不能重复定义多次？？？**






















