# qemu + GDB 调试 linux 内核(一)



在qemu中添加网卡，跟踪网卡识别流程。

* 在qemu中添加网卡
* 构建一个小的linux系统，能够有ping，ifconfig，ls，cat 等基本命令



## 编译busybox

```bash
$ wget http://busybox.net/downloads/busybox-1.26.2.tar.bz2
$ tar -jxvf busybox-1.26.2.tar.bz2
$ make O=/home/rivsidn/codes/linux-2.6.35.6/Discovery/qemu_gdb/busybox-x86 defconfig
$ make O=/home/rivsidn/codes/linux-2.6.35.6/Discovery/qemu_gdb/busybox-x86 menuconfig
$ cd /home/rivsidn/codes/linux-2.6.35.6/Discovery/qemu_gdb/busybox-x86
$ make
$ make install
```



## 创建根文件系统

```bash
$ mkdir -pv initramfs/x86-busybox
$ cd initramfs/x86-busybox/
$ mkdir -pv {bin,sbin,etc,proc,sys,usr/{bin,sbin}}
$ cp -av ../../busybox-x86/_install/* ./
```



在initramfs/x86-busybox 中创建 init 文件并添加可执行权限，内容如下：

```bash
#!/bin/sh
 
mount -t proc none /proc
mount -t sysfs none /sys

echo -e "\nBoot took $(cut -d' ' -f1 /proc/uptime) seconds\n"
mount -t devtmpfs none /dev 
exec /bin/sh
```



制作rootfs

```bash
$ find . -print0 \
    | cpio --null -ov --format=newc \
    | gzip -9 > ./initramfs-busybox-x86.cpio.gz
```



## 启动调试(测试一)

```bash
$ qemu-system-x86_64 -kernel ./arch/x86/boot/bzImage -initrd /home/rivsidn/codes/linux-2.6.35.6/Discovery/qemu_gdb/initramfs/x86-busybox/initramfs-busybox-x86.cpio.gz -smp 2 -s -S
```



```bash
$ gdb vmlinux

(gdb) target remote:1234
(gdb) b start_kernel
(gdb) c
```



## qemu添加网卡

```bash
# 将网卡驱动添加到rootfs中（否则会因为没有网卡驱动识别不了网卡）
$ make modules_install INSTALL_MOD_PATH=/home/rivsidn/codes/linux-2.6.35.6/Discovery/qemu_gdb/initramfs/x86-busybox
$ cd Discovery/qemu_gdb/initramfs/x86-busybox/
# 也可以直接将驱动编译到内核中，这样就能直接识别，之后就用这种方式
CONFIG_E1000=y

# 否则生成的ramfs文件太大，导致qemu不能正常启动
$ find ./ -name "*.ko" -exec rm {} \;
$ cd /home/rivsidn/codes/linux-2.6.35.6/
$ cp drivers/net/e1000/e1000.ko Discovery/qemu_gdb/initramfs/x86-busybox/lib/modules/2.6.35.6+/kernel/drivers/net/e1000
$ find . -print0 | cpio --null -ov --format=newc | gzip -9 > ./initramfs-busybox-x86.cpio.gz
```



```bash
$ qemu-system-x86_64 -kernel ./arch/x86/boot/bzImage -initrd /home/rivsidn/codes/linux-2.6.35.6/Discovery/qemu_gdb/initramfs/x86-busybox/initramfs-busybox-x86.cpio.gz -net nic,model=e1000 -smp 2 -s -S

# modprobe e1000 
# ifconfig -a 
```



```bash
$ gdb vmlinux

(gdb) target remote:1234
(gdb) b start_kernel
(gdb) c
```



## 参考资料

* [从0开始构建linux系统](http://blog.chinaunix.net/uid-192452-id-5763135.html)
* [qemu虚拟机与外部网络的通信](http://www.blog.chinaunix.net/uid-26061689-id-2981914.html)
