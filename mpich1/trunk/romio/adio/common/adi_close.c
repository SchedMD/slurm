/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef PROFILE
#include "mpe.h"
#endif


void ADIOI_GEN_Close(ADIO_File fd, int *error_code)
{
    int err, derr=0;
    static char myname[] = "ADIOI_GEN_CLOSE";

#ifdef PROFILE
    MPE_Log_event(9, 0, "start close");
#endif

    err = close(fd->fd_sys);
    if (fd->fd_direct >= 0) {
	derr = close(fd->fd_direct);
    }

#ifdef PROFILE
    MPE_Log_event(10, 0, "end close");
#endif

    fd->fd_sys    = -1;
    fd->fd_direct = -1;

    if (err == -1 || derr == -1) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", strerror(errno));
    }
    else *error_code = MPI_SUCCESS;
}
