/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"
#include "adio_extern.h"

struct ADIOI_RequestD *ADIOI_Malloc_request(void)
{
/* returns a pointer to a new request object.
   To reduce the number of system calls, mallocs NUM requests at a time
   and maintains list of available requests. Supplies an object from this
   list if available, else mallocs a new set of NUM and provides one
   from that set. Is NUM=100 a good number? */

#define NUM 100

    ADIOI_Req_node *curr, *ptr;
    int i;

    if (!ADIOI_Req_avail_head) {
	ADIOI_Req_avail_head = (ADIOI_Req_node *) 
	              ADIOI_Malloc(NUM*sizeof(ADIOI_Req_node));  
	curr = ADIOI_Req_avail_head;
	for (i=1; i<NUM; i++) {
	    curr->next = ADIOI_Req_avail_head+i;
	    curr = curr->next;
	}
	curr->next = NULL;
	ADIOI_Req_avail_tail = curr;

	/* keep track of malloced area that needs to be freed later */
	if (!ADIOI_Malloc_req_tail) {
	    ADIOI_Malloc_req_tail = (ADIOI_Malloc_req *)
		ADIOI_Malloc(sizeof(ADIOI_Malloc_req)); 
	    ADIOI_Malloc_req_head = ADIOI_Malloc_req_tail;
	    ADIOI_Malloc_req_head->ptr = ADIOI_Req_avail_head;
	    ADIOI_Malloc_req_head->next = NULL;
	}
	else {
	    ADIOI_Malloc_req_tail->next = (ADIOI_Malloc_req *)
		ADIOI_Malloc(sizeof(ADIOI_Malloc_req));
	    ADIOI_Malloc_req_tail = ADIOI_Malloc_req_tail->next;
	    ADIOI_Malloc_req_tail->ptr = ADIOI_Req_avail_head;
	    ADIOI_Malloc_req_tail->next = NULL;
	}
    }

    ptr = ADIOI_Req_avail_head;
    ADIOI_Req_avail_head = ADIOI_Req_avail_head->next;
    if (!ADIOI_Req_avail_head) ADIOI_Req_avail_tail = NULL;
    
    (ptr->reqd).cookie = ADIOI_REQ_COOKIE;
    return &(ptr->reqd);
}


void ADIOI_Free_request(ADIOI_Req_node *node)
{
/* This function could be called as ADIOI_Free_request(ADIO_Request request), 
   because request would be a pointer to the first element of ADIOI_Req_node.*/

/* moves this node to available pool. does not actually free it. */

    (node->reqd).cookie = 0;

    if (!ADIOI_Req_avail_tail)
	ADIOI_Req_avail_head = ADIOI_Req_avail_tail = node;
    else {
	ADIOI_Req_avail_tail->next = node;
	ADIOI_Req_avail_tail = node;
    }
    node->next = NULL;
}

