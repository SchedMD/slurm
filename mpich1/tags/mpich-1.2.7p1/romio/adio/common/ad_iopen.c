/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2002 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"

void ADIO_ImmediateOpen(ADIO_File fd, int *error_code)
{ 
	int nprocs, myrank; 
	(*(fd->fns->ADIOI_xxx_Open))(fd, error_code);
	fd->is_open = 1;

	/* DEBUG: remove following lines */
	MPI_Comm_size(fd->comm, &nprocs);
	MPI_Comm_rank(fd->comm, &myrank);
	FPRINTF(stdout, "[%d/%d] DEBUG: ADIOI_ImmediateOpen called on %s\n", 
			myrank, nprocs, fd->filename);
} 
