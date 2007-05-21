/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"
#include "adio_extern.h"
#ifdef ROMIO_INSIDE_MPICH2
#include "mpiimpl.h"
#endif

void ADIO_End(int *error_code)
{
    ADIOI_Flatlist_node *curr, *next;
    ADIOI_Malloc_async *tmp;
    ADIOI_Malloc_req *tmp1;
    ADIOI_Datarep *datarep, *datarep_next;
    static char myname[] = "ADIO_END";
    
/*    FPRINTF(stderr, "reached end\n"); */

/* delete the flattened datatype list */
    curr = ADIOI_Flatlist;
    while (curr) {
	if (curr->blocklens) ADIOI_Free(curr->blocklens);
	if (curr->indices) ADIOI_Free(curr->indices);
	next = curr->next;
	ADIOI_Free(curr);
	curr = next;
    }
    ADIOI_Flatlist = NULL;

    /* --BEGIN ERROR HANDLING-- */
    if (ADIOI_Async_list_head) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS,
					   MPIR_ERR_RECOVERABLE,
					   myname, __LINE__,
					   MPI_ERR_IO,
					   "Error: outstanding nonblocking I/O operations", 0);
	return;
    }
    /* --END ERROR HANDLING-- */

/* free list of available ADIOI_Async_nodes. */
    while (ADIOI_Malloc_async_head) {
	ADIOI_Free(ADIOI_Malloc_async_head->ptr);
	tmp = ADIOI_Malloc_async_head;
	ADIOI_Malloc_async_head = ADIOI_Malloc_async_head->next;
	ADIOI_Free(tmp);
    }
    ADIOI_Async_avail_head = ADIOI_Async_avail_tail = NULL;
    ADIOI_Malloc_async_head = ADIOI_Malloc_async_tail = NULL;

/* free all available request objects */
    while (ADIOI_Malloc_req_head) {
	ADIOI_Free(ADIOI_Malloc_req_head->ptr);
	tmp1 = ADIOI_Malloc_req_head;
	ADIOI_Malloc_req_head = ADIOI_Malloc_req_head->next;
	ADIOI_Free(tmp1);
    }
    ADIOI_Malloc_req_head = ADIOI_Malloc_req_tail = NULL;

/* free file, request, and info tables used for Fortran interface */
    if (ADIOI_Ftable) ADIOI_Free(ADIOI_Ftable);
    if (ADIOI_Reqtable) ADIOI_Free(ADIOI_Reqtable);
#ifndef HAVE_MPI_INFO
    if (MPIR_Infotable) ADIOI_Free(MPIR_Infotable);
#endif


/* free the memory allocated for a new data representation, if any */
    datarep = ADIOI_Datarep_head;
    while (datarep) {
        datarep_next = datarep->next;
#ifdef MPICH2
        MPIU_Free(datarep->name);
#else
        ADIOI_Free(datarep->name);
#endif
        ADIOI_Free(datarep);
        datarep = datarep_next;
    }

    *error_code = MPI_SUCCESS;
}



/* This is the delete callback function associated with
   ADIO_Init_keyval when MPI_COMM_WORLD is freed */

int ADIOI_End_call(MPI_Comm comm, int keyval, void *attribute_val, void
		  *extra_state)
{
    int error_code;

    ADIOI_UNREFERENCED_ARG(comm);
    ADIOI_UNREFERENCED_ARG(keyval);
    ADIOI_UNREFERENCED_ARG(attribute_val);
    ADIOI_UNREFERENCED_ARG(extra_state);

    ADIO_End(&error_code);
    return error_code;
}
