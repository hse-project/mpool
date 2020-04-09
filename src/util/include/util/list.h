/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_LIST_H
#define MPOOL_UTIL_LIST_H

#include <util/base.h>

/*
 * Simple doubly linked list implementation that mirrors
 * a subset of Linux kernel definitions in <linux/list.h>.
 */

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

#define LIST_HEAD_INIT(var_name) \
	{ &(var_name), &(var_name) }

#define LIST_HEAD(var_name) \
	struct list_head var_name = LIST_HEAD_INIT(var_name)

#define list_entry(ptr, type, member) \
	container_of(ptr, type, member)

#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_entry(pos, head, member)				\
	for (pos = list_first_entry(head, typeof(*pos), member);	\
	     &pos->member != (head);					\
	     pos = list_next_entry(pos, member))

#define list_for_each_entry_reverse(pos, head, member)			\
	for (pos = list_last_entry(head, typeof(*pos), member);		\
	     &pos->member != (head);					\
	     pos = list_prev_entry(pos, member))

#define list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
	     pos = n, n = pos->next)

#define list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_first_entry(head, typeof(*pos), member),	\
		n = list_next_entry(pos, member);			\
	     &pos->member != (head);					\
	     pos = n, n = list_next_entry(n, member))

#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)

#define list_last_entry(ptr, type, member) \
	list_entry((ptr)->prev, type, member)

#define list_first_entry_or_null(ptr, type, member) \
	(!list_empty(ptr) ? list_first_entry(ptr, type, member) : NULL)

#define list_next_entry(pos, member) \
	list_entry((pos)->member.next, typeof(*(pos)), member)

#define list_prev_entry(pos, member) \
	list_entry((pos)->member.prev, typeof(*(pos)), member)


static inline
void INIT_LIST_HEAD(struct list_head *head)
{
	head->next = head;
	head->prev = head;
}

/**
 * Insert @item into list after @head.
 */
static inline
void list_add(struct list_head *item,
	      struct list_head *head)
{
	struct list_head *prv = head;
	struct list_head *nxt = head->next;

	nxt->prev = item;
	item->next = nxt;
	item->prev = prv;
	prv->next = item;
}

/**
 * Insert @item into list before @head.
 */
static inline
void list_add_tail(struct list_head *item,
		   struct list_head *head)
{
	struct list_head *prv = head->prev;
	struct list_head *nxt = head;

	nxt->prev = item;
	item->next = nxt;
	item->prev = prv;
	prv->next = item;
}

static inline
void list_del(struct list_head *item)
{
	item->next->prev = item->prev;
	item->prev->next = item->next;
}

static inline
void list_del_init(struct list_head *item)
{
	item->next->prev = item->prev;
	item->prev->next = item->next;
	INIT_LIST_HEAD(item);
}

static inline
int list_empty(const struct list_head *head)
{
	return head->next == head;
}

static inline
int list_is_last(const struct list_head *item,
		 const struct list_head *head)
{
	return item->next == head;
}


static inline void __list_splice(const struct list_head *list,
				 struct list_head *prev,
				 struct list_head *next)
{
	struct list_head *first = list->next;
	struct list_head *last = list->prev;

	first->prev = prev;
	prev->next = first;

	last->next = next;
	next->prev = last;
}

/**
 * list_splice - join two lists, this is designed for stacks
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static inline void list_splice(const struct list_head *list,
				struct list_head *head)
{
	if (!list_empty(list))
		__list_splice(list, head, head->next);
}

/**
 * list_splice_tail - join two lists, each list being a queue
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static inline void list_splice_tail(struct list_head *list,
				struct list_head *head)
{
	if (!list_empty(list))
		__list_splice(list, head->prev, head);
}

/*
 * From here down is compiled for both kernel and user-space.
 */

static inline
int list_is_first(const struct list_head *item,
		  const struct list_head *head)
{
	return item->prev == head;
}

#define list_next_entry_or_null(pos, member, head) \
	((!list_is_last(&(pos)->member, head))	   \
	 ? list_next_entry(pos, member)		   \
	 : NULL)

#define list_prev_entry_or_null(pos, member, head) \
	((!list_is_first(&(pos)->member, head))	   \
	 ? list_prev_entry(pos, member)		   \
	 : NULL)

#define list_last_entry_or_null(ptr, type, member) \
	(!list_empty(ptr) ? list_last_entry(ptr, type, member) : NULL)


/**
 * list_trim() - trim the tail off a list
 * @list:   new list into which the tail will be put
 * @head:   head of the list to trim
 * @entry:  an entry within head which identies the tail
 *
 * list_trim() is similar to list_cut_position(), except that it lops
 * off the tail of the list and leave the head in place.  All entries
 * from %head starting with and including %entry are put into %list
 * with ordering preserved.  Note that %list is always clobbered.
 */
static inline
void
list_trim(
	struct list_head   *list,
	struct list_head   *head,
	struct list_head   *entry)
{
	INIT_LIST_HEAD(list);

	if (list_empty(head))
		return;

	if (entry == head) {
		list_splice(head, list);
	} else {
		struct list_head   *last = head->prev;

		list->next = entry;
		list->prev = last;

		entry->prev->next = head;
		head->prev = entry->prev;

		entry->prev = list;
		last->next = list;
	}
}
#endif
