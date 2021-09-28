#ifndef _NF_CONNTRACK_TUPLE_COMMON_H
#define _NF_CONNTRACK_TUPLE_COMMON_H

enum ip_conntrack_dir {
	IP_CT_DIR_ORIGINAL,		//0
	IP_CT_DIR_REPLY,		//1
	IP_CT_DIR_MAX
};

#define CTINFO2DIR(ctinfo) ((ctinfo) >= IP_CT_IS_REPLY ? IP_CT_DIR_REPLY : IP_CT_DIR_ORIGINAL)

#endif /* _NF_CONNTRACK_TUPLE_COMMON_H */
