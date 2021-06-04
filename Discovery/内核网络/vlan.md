## 文件列表

| 文件名          | 用途                                    |
| --------------- | --------------------------------------- |
| vlan_core.c     | 编译到内核中                            |
|                 |                                         |
| vlan.c          |                                         |
| vlan_dev.c      |                                         |
| vlan_netflink.c | `rtnl_link_register()` 这个函数的用法？ |
|                 |                                         |
| vlan_gvrp.c     | 暂时不关注                              |
| vlanproc.c      | 暂时不关注                              |
|                 |                                         |











## 附录

### 配置案例(一)

```bash
# 创建vlan
ifconfig eth0 up
vconfig eth0 10
ifconfig eth0.10 up
# 创建Bridge
brctl addbr brvlan10
brctl addif brvlan10 eth0.10
# 添加网卡
ifconfig eth1 up
brctl addif brvlan10 eth1
ifconfig eth2 up
brctl addif brvlan10 eth2
```

配置之后，`eth1`、`eth2` 就 `VLAN 10`的access 口，`eth0` 就是`VLAN 10` 的trunk 口。





## TODO

* GRO 功能是做什么用的？