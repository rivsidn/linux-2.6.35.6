#ifndef _LINUX_LIST_NULLS_H
#define _LINUX_LIST_NULLS_H

/*
 * Special version of lists, where end of list is not a NULL pointer,
 * but a 'nulls' marker, which can have many different values.
 * (up to 2^31 different values guaranteed on all platforms)
 *
 * In the standard hlist, termination of a list is the NULL pointer.
 * In this special 'nulls' variant, we use the fact that objects stored in
 * a list are aligned on a word (4 or 8 bytes alignment).
 * We therefore use the last significant bit of 'ptr' :
 * Set to 1 : This is a 'nulls' end-of-list marker (ptr >> 1)
 * Set to 0 : This is a pointer to some object (ptr)
 *
 * TODO:链表中的内容都是4(8)字节是所有的都是这样还是特殊情况。
 *
 * 链表中存储的对象都是4字节或8字节对齐的。
 * 所以我们用指针的最后一位表示是否到达了链表的结尾：
 * 1：链表结束标志
 * 0：该指针指向具体内容
 */

struct hlist_nulls_head {
	struct hlist_nulls_node *first;
};

struct hlist_nulls_node {
	struct hlist_nulls_node *next, **pprev;
};

//指定一个数值当作指针的指向的地址，后续操作确保不会访问到这个地址
#define INIT_HLIST_NULLS_HEAD(ptr, nulls) \
	((ptr)->first = (struct hlist_nulls_node *) (1UL | (((long)nulls) << 1)))

#define hlist_nulls_entry(ptr, type, member) container_of(ptr,type,member)
/**
 * ptr_is_a_nulls - Test if a ptr is a nulls
 * @ptr: ptr to be tested
 *
 */
static inline int is_a_nulls(const struct hlist_nulls_node *ptr)
{
	return ((unsigned long)ptr & 1);
}

/**
 * get_nulls_value - Get the 'nulls' value of the end of chain
 * @ptr: end of chain
 *
 * Should be called only if is_a_nulls(ptr);
 */
static inline unsigned long get_nulls_value(const struct hlist_nulls_node *ptr)
{
	return ((unsigned long)ptr) >> 1;
}

/* 该节点没加入到hash表中 */
static inline int hlist_nulls_unhashed(const struct hlist_nulls_node *h)
{
	return !h->pprev;
}

static inline int hlist_nulls_empty(const struct hlist_nulls_head *h)
{
	return is_a_nulls(h->first);
}

/* 头插 */
static inline void hlist_nulls_add_head(struct hlist_nulls_node *n,
					struct hlist_nulls_head *h)
{
	struct hlist_nulls_node *first = h->first;

	n->next = first;
	n->pprev = &h->first;
	h->first = n;
	if (!is_a_nulls(first))
		first->pprev = &n->next;
}

/* 删除节点 */
static inline void __hlist_nulls_del(struct hlist_nulls_node *n)
{
	struct hlist_nulls_node *next = n->next;
	struct hlist_nulls_node **pprev = n->pprev;
	*pprev = next;
	if (!is_a_nulls(next))
		next->pprev = pprev;
}

static inline void hlist_nulls_del(struct hlist_nulls_node *n)
{
	__hlist_nulls_del(n);
	/* 
	 * 表示此时的pprev是不能访问的，否则会触发一个oops，
	 * 感觉此处类似与BUG_ON()
	 */
	n->pprev = LIST_POISON2;
}

/**
 * hlist_nulls_for_each_entry	- iterate over list of given type
 * @tpos:	the type * to use as a loop cursor.
 * @pos:	the &struct hlist_node to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the hlist_node within the struct.
 *
 */
#define hlist_nulls_for_each_entry(tpos, pos, head, member)		       \
	for (pos = (head)->first;					       \
	     (!is_a_nulls(pos)) &&					       \
		({ tpos = hlist_nulls_entry(pos, typeof(*tpos), member); 1;}); \
	     pos = pos->next)

/**
 * hlist_nulls_for_each_entry_from - iterate over a hlist continuing from current point
 * @tpos:	the type * to use as a loop cursor.
 * @pos:	the &struct hlist_node to use as a loop cursor.
 * @member:	the name of the hlist_node within the struct.
 *
 */
#define hlist_nulls_for_each_entry_from(tpos, pos, member)	\
	for (; (!is_a_nulls(pos)) && 				\
		({ tpos = hlist_nulls_entry(pos, typeof(*tpos), member); 1;}); \
	     pos = pos->next)

#endif

