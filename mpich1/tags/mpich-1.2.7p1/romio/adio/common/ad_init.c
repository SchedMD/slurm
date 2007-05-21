/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"

ADIOI_Flatlist_node *ADIOI_Flatlist = NULL;
ADIOI_Async_node *ADIOI_Async_list_head = NULL, *ADIOI_Async_list_tail = NULL;
    /* list of outstanding asynchronous requests */
ADIOI_Async_node *ADIOI_Async_avail_head = NULL,
    *ADIOI_Async_avail_tail = NULL;
    /* list of available (already malloced) nodes for above async list */
ADIOI_Malloc_async *ADIOI_Malloc_async_head = NULL,
    *ADIOI_Malloc_async_tail = NULL;
  /* list of malloced areas for async_list, which must be freed in ADIO_End */

ADIOI_Req_node *ADIOI_Req_avail_head = NULL, *ADIOI_Req_avail_tail = NULL;
    /* list of available (already malloced) request objects */
ADIOI_Malloc_req *ADIOI_Malloc_req_head = NULL, *ADIOI_Malloc_req_tail = NULL;
    /* list of malloced areas for requests, which must be freed in ADIO_End */

ADIOI_Datarep *ADIOI_Datarep_head = NULL;
    /* list of datareps registered by the user */

/* for f2c and c2f conversion */
ADIO_File *ADIOI_Ftable = NULL;
int ADIOI_Ftable_ptr = 0, ADIOI_Ftable_max = 0;
ADIO_Request *ADIOI_Reqtable = NULL;
int ADIOI_Reqtable_ptr = 0, ADIOI_Reqtable_max = 0;
#ifndef HAVE_MPI_INFO
MPI_Info *MPIR_Infotable = NULL;
int MPIR_Infotable_ptr = 0, MPIR_Infotable_max = 0;
#endif

#ifdef ROMIO_XFS
int ADIOI_Direct_read = 0, ADIOI_Direct_write = 0;
#endif

int ADIO_Init_keyval=MPI_KEYVAL_INVALID;

MPI_Errhandler ADIOI_DFLT_ERR_HANDLER = MPI_ERRORS_RETURN;

void ADIO_Init(int *argc, char ***argv, int *error_code)
{
#ifdef ROMIO_XFS
    char *c;
#endif

    ADIOI_UNREFERENCED_ARG(argc);
    ADIOI_UNREFERENCED_ARG(argv);

/* initialize the linked list containing flattened datatypes */
    ADIOI_Flatlist = (ADIOI_Flatlist_node *) ADIOI_Malloc(sizeof(ADIOI_Flatlist_node));
    ADIOI_Flatlist->type = MPI_DATATYPE_NULL;
    ADIOI_Flatlist->next = NULL;
    ADIOI_Flatlist->blocklens = NULL;
    ADIOI_Flatlist->indices = NULL;

#ifdef ROMIO_XFS
    c = getenv("MPIO_DIRECT_READ");
    if (c && (!strcmp(c, "true") || !strcmp(c, "TRUE"))) 
	ADIOI_Direct_read = 1;
    else ADIOI_Direct_read = 0;
    c = getenv("MPIO_DIRECT_WRITE");
    if (c && (!strcmp(c, "true") || !strcmp(c, "TRUE"))) 
	ADIOI_Direct_write = 1;
    else ADIOI_Direct_write = 0;
#endif

    *error_code = MPI_SUCCESS;
}
