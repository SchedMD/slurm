/*
 * Functions to maintain a list of numeric node ranges. Depending upon the
 * parameter "sorted" used when adding elements, this list may be a strictly-
 * ordered, singly-linked list to represent disjoint node ranges of the type
 * 'a' (single node) or 'a-b' (range, with a < b).
 *
 * For example, '1,7-8,20,33-29'
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#include "basil_alps.h"
#define CRAY_MAX_DIGITS	5	/* nid%05d format */

/* internal constructor */
static struct nodespec *_ns_new(uint32_t start, uint32_t end)
{
	struct nodespec *new = xmalloc(sizeof(*new));

	new->start = start;
	new->end   = end;
	return new;
}

/**
 * _ns_add_range  -  Insert/merge new range into existing nodespec list.
 * @head:       head of the ordered list
 * @new_start:  start value of node range to insert
 * @new_end:    end value of node range to insert
 * @sorted:	if set, then maintain @head as duplicate-free list, ordered
 *		in ascending order of node-specifier intervals, with a gap of
 *		at least 2 between adjacent entries. Otherwise maintain @head
 *		as a list of elements in the order added
 *
 * Maintains @head as list
 * Returns 0 if ok, -1 on failure.
 */
static int _ns_add_range(struct nodespec **head,
			 uint32_t new_start, uint32_t new_end, bool sorted)
{
	struct nodespec *cur = *head, *next;

	assert(new_start <= new_end);

	if (!sorted) {
		if (cur) {
			while (cur->next)	/* find end of list */
				cur = cur->next;
			if (new_start == (cur->end + 1))
				cur->end = new_end;
			else
				cur->next = _ns_new(new_start, new_end);
		} else {
			*head = _ns_new(new_start, new_end);
		}
		return 0;
	}

	if (cur == NULL || new_end + 1 < cur->start) {
		*head = _ns_new(new_start, new_end);
		(*head)->next = cur;
		return 0;
	}

	for (next = cur->next;
	     new_start > cur->end + 1;
	     cur = next, next = cur->next)
		if (next == NULL || new_end + 1 < next->start) {
			next = _ns_new(new_start, new_end);
			next->next = cur->next;
			cur->next  = next;
			return 0;
		}

	/* new_start <= cur->end + 1 */
	if (new_start < cur->start)
		cur->start = new_start;

	if (new_end <= cur->end)
		return 0;
	cur->end = new_end;

	while ((next = cur->next) && next->start <= new_end + 1) {
		if (next->end > new_end)
			cur->end = next->end;
		cur->next = next->next;
		xfree(next);
	}
	/* next == NULL || next->start > new_end + 1 */

	return 0;
}

/** Add a single node (1-element range) */
extern int ns_add_node(struct nodespec **head, uint32_t node_id, bool sorted)
{
	return _ns_add_range(head, node_id, node_id, sorted);
}

/* count the number of nodes starting at @head */
static int ns_count_nodes(const struct nodespec *head)
{
	const struct nodespec *cur;
	uint32_t node_count = 0;

	for (cur = head; cur; cur = cur->next)
		node_count += cur->end - cur->start + 1;

	return node_count;
}

/**
 * ns_ranged_string - Write compressed node specification to buffer.
 * @head:   start of nodespec list
 * @buf:    buffer to write to
 * @buflen: size of @buf
 * Returns number of characters written if successful, -1 on overflow.
 */
static ssize_t ns_ranged_string(const struct nodespec *head,
				char *buf, size_t buflen)
{
	const struct nodespec *cur;
	ssize_t n, len = 0;

	for (cur = head; cur; cur = cur->next) {
		if (cur != head) {
			n = snprintf(buf + len, buflen - len, ",");
			if (n < 0 || (len += n) >= buflen)
				return -1;
		}

		n = snprintf(buf + len, buflen - len, "%u", cur->start);
		if (n < 0 || (len += n) >= buflen)
			return -1;

		if (cur->start != cur->end) {
			n = snprintf(buf + len, buflen - len, "-%u", cur->end);
			if (n < 0 || (len += n) >= buflen)
				return -1;
		}
	}
	return len;
}

/* Compress @head into nodestring. Result must be xfree()d. */
char *ns_to_string(const struct nodespec *head)
{
	char *buf = NULL;
	size_t size = ns_count_nodes(head);

	if (size) {
		/* Over-estimation: using all digits, plus either '-' or '\0' */
		size *= CRAY_MAX_DIGITS + 1;

		buf = xmalloc(size);
		if (ns_ranged_string(head, buf, size) < 0)
			fatal("can not expand nodelist expression");
	}
	return buf;
}
