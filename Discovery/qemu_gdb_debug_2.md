# qemu + GDB 调试 linux 内核(二)

* 实现虚拟机跟外部通信



## 理论知识

虚拟机的网络设备连接在qemu虚拟的VLAN中。每个qemu的运行实例是宿主机中的一个进程，而每个这样的进程中可以虚拟一些VLAN，虚拟机网络设备接入这些VLAN中。当某个VLAN上连接的网络设备发送数据帧，与它在同一个VLAN中的其它网路设备都能接收到数据帧。如果加入的虚拟网卡没有指定其连接的VLAN号，那么qemu默认会将该网卡连入vlan0。



```
-net nic,model=e1000,vlan=1
```

表示添加一块e1000的网卡到虚拟机中， 该网卡位于vlan1。



qemu提供了4中跟外界通信的方式：

* User mode stack

* socket

* TAP

  宿主机中创建并配置一个TAP设备，qemu进程将该TAP设备添加到VLAN中；

  宿主机创建一个网桥，并将宿主机的网路接口和qemu的TAP设备添加到该网桥中；

* VDE





## 操作步骤

### User mode

将simple.script 放到 /usr/share/udhcpc/ 下，改名为default.script

```bash
$ cat init
#!/bin/sh

mount -t proc none /proc
mount -t sysfs none /sys

echo -e "\nBoot took $(cut -d' ' -f1 /proc/uptime) seconds\n"
mount -t devtmpfs none /dev 

ifconfig eth0 up		# 新增
udhcpc -i eth0			# 新增

exec /bin/sh
```

启动qemu虚拟机

```bash
$ sudo qemu-system-x86_64 -kernel ./arch/x86/boot/bzImage -initrd /home/rivsidn/codes/linux-2.6.35.6/Discovery/qemu_gdb/initramfs/initramfs-busybox-x86.cpio.gz -net nic,model=e1000 -net user,net=10.0.2.15 -smp 1
```



配置好之后，能够通过 http，ftp，ssh，telnet 访问（tftp不可以）。















## 参考资料

* [qemu documentation](https://wiki.qemu.org/Documentation)
* [qemu虚拟机与外部网络的通信](http://www.blog.chinaunix.net/uid-26061689-id-2981914.html)
* [qemu安装及与宿主机通信](https://blog.csdn.net/qiusi0225/article/details/80447710)
* [QEMU 简单使用](https://blog.csdn.net/StandMyGround/article/details/52576934)
* [qemu网络配置的slirp模式](https://blog.csdn.net/simonzhao0536/article/details/9194591)

