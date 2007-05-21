/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2005 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

void ADIOI_GEN_Fcntl(ADIO_File fd, int flag, ADIO_Fcntl_t *fcntl_struct,
		     int *error_code)
{
    static char myname[] = "ADIOI_GEN_FCNTL";

    switch(flag) {
    case ADIO_FCNTL_GET_FSIZE:
	fcntl_struct->fsize = lseek(fd->fd_sys, 0, SEEK_END);
	if (fd->fp_sys_posn != -1) 
	     lseek(fd->fd_sys, fd->fp_sys_posn, SEEK_SET);
	if (fcntl_struct->fsize == -1) {
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_IO, "**io",
					       "**io %s", strerror(errno));
	}
	else *error_code = MPI_SUCCESS;
	break;

    case ADIO_FCNTL_SET_DISKSPACE:
	ADIOI_GEN_Prealloc(fd, fcntl_struct->diskspace, error_code);
	break;

    case ADIO_FCNTL_SET_ATOMICITY:
	fd->atomicity = (fcntl_struct->atomicity == 0) ? 0 : 1;
	*error_code = MPI_SUCCESS;
	break;

	/* --BEGIN ERROR HANDLING-- */
    default:
	*error_code = MPIO_Err_create_code(MPI_SUCCESS,
					   MPIR_ERR_RECOVERABLE,
					   myname, __LINE__,
					   MPI_ERR_ARG,
					   "**flag", "**flag %d", flag);
	/* --END ERROR HANDLING-- */
    }
}
