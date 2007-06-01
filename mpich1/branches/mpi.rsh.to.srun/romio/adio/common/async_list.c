/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"
#include "adio_extern.h"

ADIOI_Async_node *ADIOI_Malloc_async_node(void)
{
/* returns a pointer to a new node that can be added to ADIOI_Async_list.
   To reduce the number of system calls, mallocs NUM nodes at a time
   and maintains list of available nodes. Supplies a node from this
   list if available, else mallocs a new set of NUM and provides one
   from that set. Is NUM=100 a good number? */

#define NUM 100

    ADIOI_Async_node *curr, *ptr;
    int i;

    if (!ADIOI_Async_avail_head) {
	ADIOI_Async_avail_head = (ADIOI_Async_node *)
	              ADIOI_Malloc(NUM*sizeof(ADIOI_Async_node));  
	curr = ADIOI_Async_avail_head;
	for (i=1; i<NUM; i++) {
	    curr->next = ADIOI_Async_avail_head+i;
	    curr = curr->next;
	}
	curr->next = NULL;
	ADIOI_Async_avail_tail = curr;

	/* keep track of malloced area that needs to be freed later */
	if (!ADIOI_Malloc_async_tail) {
	    ADIOI_Malloc_async_tail = (ADIOI_Malloc_async *)
		ADIOI_Malloc(sizeof(ADIOI_Malloc_async)); 
	    ADIOI_Malloc_async_head = ADIOI_Malloc_async_tail;
	    ADIOI_Malloc_async_head->ptr = ADIOI_Async_avail_head;
	    ADIOI_Malloc_async_head->next = NULL;
	}
	else {
	    ADIOI_Malloc_async_tail->next = (ADIOI_Malloc_async *)
		ADIOI_Malloc(sizeof(ADIOI_Malloc_async));
	    ADIOI_Malloc_async_tail = ADIOI_Malloc_async_tail->next;
	    ADIOI_Malloc_async_tail->ptr = ADIOI_Async_avail_head;
	    ADIOI_Malloc_async_tail->next = NULL;
	}
    }

    ptr = ADIOI_Async_avail_head;
    ADIOI_Async_avail_head = ADIOI_Async_avail_head->next;
    if (!ADIOI_Async_avail_head) ADIOI_Async_avail_tail = NULL;

    return ptr;
}


void ADIOI_Free_async_node(ADIOI_Async_node *node)
{
/* moves this node to available pool. does not actually free it. */

    if (!ADIOI_Async_avail_tail)
	ADIOI_Async_avail_head = ADIOI_Async_avail_tail = node;
    else {
	ADIOI_Async_avail_tail->next = node;
	ADIOI_Async_avail_tail = node;
    }
    node->next = NULL;
}


void ADIOI_Add_req_to_list(ADIO_Request *request)
{
/* add request to list of outstanding requests */

    ADIOI_Async_node *curr;

    if (!ADIOI_Async_list_head) {
	ADIOI_Async_list_head = ADIOI_Malloc_async_node();
	ADIOI_Async_list_head->request = request;
	ADIOI_Async_list_head->prev = ADIOI_Async_list_head->next = NULL;
	ADIOI_Async_list_tail = ADIOI_Async_list_head;
	(*request)->ptr_in_async_list = ADIOI_Async_list_head;
    }
    else {
	curr = ADIOI_Async_list_tail;
	curr->next = ADIOI_Malloc_async_node();
	ADIOI_Async_list_tail = curr->next;
	ADIOI_Async_list_tail->request = request;
	ADIOI_Async_list_tail->prev = curr;
	ADIOI_Async_list_tail->next = NULL;
	(*request)->ptr_in_async_list = ADIOI_Async_list_tail;
    }
}
	
/* Sets error_code to MPI_SUCCESS on success, creates an error code on
 * failure.
 */
void ADIOI_Complete_async(int *error_code)
{
/* complete all outstanding async I/O operations so that new ones can be
   initiated. Remove them all from async_list. */

    ADIO_Status status;
    ADIO_Request *request;
    ADIOI_Async_node *tmp;
    static char myname[] = "ADIOI_Complete_async";

    *error_code = MPI_SUCCESS;

    while (ADIOI_Async_list_head) {
	request = ADIOI_Async_list_head->request;
	(*request)->queued = -1; /* ugly internal hack that prevents
                  ADIOI_xxxComplete from freeing the request object. 
                  This is required, because the user will call MPI_Wait
                  later, which would require status to be filled. */
	switch ((*request)->optype) {
	case ADIOI_READ:
/*	    (*((*request)->fd->fns->ADIOI_xxx_ReadComplete))(request,
						    &status,error_code);*/
	    ADIO_ReadComplete(request, &status, error_code);
	    break;
	case ADIOI_WRITE:
/*	    (*((*request)->fd->fns->ADIOI_xxx_WriteComplete))(request,
						     &status, error_code);*/
	    ADIO_WriteComplete(request, &status, error_code);
	    break;
	default:
	    /* --BEGIN ERROR HANDLING-- */
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE,
					       myname, __LINE__,
					       MPI_ERR_INTERN,
					       "Unknown request optype", 0);
	    return;
	    /* --END ERROR HANDLING-- */
	}
	(*request)->queued = 0;  /* dequeued, but request object not
				    freed */

	tmp = ADIOI_Async_list_head;
	ADIOI_Async_list_head = ADIOI_Async_list_head->next;
	ADIOI_Free_async_node(tmp);
    }
    ADIOI_Async_list_tail = NULL;
}


void ADIOI_Del_req_from_list(ADIO_Request *request)
{
/* Delete a request that has already been completed from the async
   list and move it to the list of available nodes. Typically called
   from within an ADIO_Test/ADIO_Wait. */ 

    ADIOI_Async_node *curr, *prev, *next;

    curr = (*request)->ptr_in_async_list;
    prev = curr->prev;

    if (prev) prev->next = curr->next;
    else ADIOI_Async_list_head = curr->next;

    next = curr->next;
    if (next) next->prev = prev;
    else ADIOI_Async_list_tail = prev;

    ADIOI_Free_async_node(curr);
}
