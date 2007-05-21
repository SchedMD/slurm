/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2003 University of Chicago, Ohio Supercomputer Center. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <fcntl.h>
#include "adio.h"
#include <globus_ftp_client.h>

/* Here are the canonical definitions of the extern's referenced by
   ad_gridftp.h */
int num_gridftp_handles=0;
#ifndef ADIO_GRIDFTP_HANDLES_MAX
#define ADIO_GRIDFTP_HANDLES_MAX 200
#endif /* ! ADIO_GRIDFTP_HANDLES_MAX */
/* having to keep not one but two big global tables sucks... */
globus_ftp_client_handle_t gridftp_fh[ADIO_GRIDFTP_HANDLES_MAX];
globus_ftp_client_operationattr_t oattr[ADIO_GRIDFTP_HANDLES_MAX];

void globus_err_handler(const char *routine, const char *caller,
			globus_result_t result)
{
  int myrank,nprocs;
  globus_object_t *err;

  MPI_Comm_rank(MPI_COMM_WORLD,&myrank);
  MPI_Comm_size(MPI_COMM_WORLD,&nprocs);
  err = globus_error_get(result);
  FPRINTF(stderr, "[%d/%d] %s error \"%s\", called from %s\n",
	  myrank,nprocs,routine,globus_object_printable_to_string(err),caller);
}
