# qemu + GDB 调试 linux 内核(零)

ubuntu16.04中通过qemu，gdb 调试 linux 内核，内核版本2.6.35.6。



## 工具安装

### 安装qemu

```
sudo apt-get install qemu
```



### 安装gdb 8.3

```bash
# 安装gcc-9
sudo apt install software-properties-common
sudo apt-get update
sudo apt install gcc-9 g++-9

# 如果不是最新版本需要重建软链接
gcc -v
```

```bash
# 下载gdb
wget https://mirror.bjtu.edu.cn/gnu/gdb/gdb-8.3.tar.xz
tar -xvf gdb-8.3.tar.xz
```

修改 gdb/remote.c 文件

```c
    /* Further sanity checks, with knowledge of the architecture.  */
    // if (buf_len > 2 * rsa->sizeof_g_packet)
    //   error (_("Remote 'g' packet reply is too long (expected %ld bytes, got %d "
    //      "bytes): %s"),
    //    rsa->sizeof_g_packet, buf_len / 2,
    //    rs->buf.data ());
  if (buf_len > 2 * rsa->sizeof_g_packet) {
    rsa->sizeof_g_packet = buf_len;
    for (i = 0; i < gdbarch_num_regs (gdbarch); i++){
            if (rsa->regs[i].pnum == -1)
                continue;
            if (rsa->regs[i].offset >= rsa->sizeof_g_packet)
                rsa->regs[i].in_g_packet = 0;
            else
                rsa->regs[i].in_g_packet = 1;
        }
    }
```

```bash
#编译gdb
./configure
./make -j8
sudo ./make install

#查看是否为最新版本
gdb
```



## 内核编译

1. 更新gcc为 gcc-4.9 
2. 编译，遇到问题参见参考资料



## 运行

### 制作根文件系统

```bash
cat main.c
```

```c
#include <stdio>
int main()
{
    printf("hello world!");
    printf("hello world!");
    printf("hello world!");
    printf("hello world!");
    fflush(stdout);
    while(1);
    return 0;
}
```

```bash
gcc --static -o helloworld main.c
echo helloworld | cpio -o --format=newc > rootfs
```



### 调试运行

```bash
# 启动qemu
qemu-system-x86_64  \
 -kernel ./arch/x86/boot/bzImage  \
 -initrd ./rootfs  \
 -append "root=/dev/ram rdinit=/helloworld" \
 -smp 2  \
 -s -S
```
```bash
# 启动gdb
gdb ./vmLinux

#以下进行调试
target remote:1234
b start_kernel
c
```



## 参考资料

[手把手教你利用VS Code+Qemu+GDB调试Linux内核](https://zhuanlan.zhihu.com/p/105069730)

[编译内核错误：Can't use 'defined(@array)](https://www.cnblogs.com/tid-think/p/10929435.html)

[Compile an old Linux kernel on Ubuntu 16.04 LTS](https://stackoverflow.com/questions/37802473/compile-an-old-linux-kernel-on-ubuntu-16-04-lts)

[Linux kernel compile error elf_x86_64 missing](https://stackoverflow.com/questions/22662906/linux-kernel-compile-error-elf-x86-64-missing)





