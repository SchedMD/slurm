/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pfs.h"
#include "adio_extern.h"

void ADIOI_PFS_Fcntl(ADIO_File fd, int flag, ADIO_Fcntl_t *fcntl_struct,
		     int *error_code)
{
    int i, err;
    int iomod, np_total, np_comm;
    static char myname[] = "ADIOI_PFS_FCNTL";

    switch(flag) {
    case ADIO_FCNTL_GET_FSIZE:
	if (!(fd->atomicity)) {
          /* in M_ASYNC mode, all processes are not aware of changes 
             in file size (although the manual says otherwise). Therefore, 
             temporarily change to M_UNIX and then change 
             back to M_ASYNC.*/ 
	    MPI_Comm_size(MPI_COMM_WORLD, &np_total);
	    MPI_Comm_size(fd->comm, &np_comm);
	    if (np_total == np_comm) {
		err = _setiomode(fd->fd_sys, M_UNIX);
		err = _setiomode(fd->fd_sys, M_ASYNC);
	    }
            /* else it is M_UNIX anyway, so no problem */
	}
	fcntl_struct->fsize = lseek(fd->fd_sys, 0, SEEK_END);
	if (fd->fp_sys_posn != -1) 
	    lseek(fd->fd_sys, fd->fp_sys_posn, SEEK_SET);
	*error_code = MPI_SUCCESS;
	break;

    case ADIO_FCNTL_SET_DISKSPACE:
	err = _lsize(fd->fd_sys, fcntl_struct->diskspace, SEEK_SET);
	if (err == -1) {
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_IO, "**io",
					       "**io %s", strerror(errno));
	}
	else *error_code = MPI_SUCCESS;
	break;

    case ADIO_FCNTL_SET_ATOMICITY:
	MPI_Comm_size(MPI_COMM_WORLD, &np_total);
	MPI_Comm_size(fd->comm, &np_comm);
	if (np_total == np_comm) {
	    iomod = (fcntl_struct->atomicity == 0) ? M_ASYNC : M_UNIX;
	    err = _setiomode(fd->fd_sys, iomod);
	}
        /* else can't do anything because setiomode is global. but
           the file will have been opened with M_UNIX anyway, because
           gopen is also global. */

	fd->atomicity = (fcntl_struct->atomicity == 0) ? 0 : 1;
	if (err == -1) {
	    /* --BEGIN ERROR HANDLING-- */
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_IO, "**io",
					       "**io %s", strerror(errno));
	    /* --END ERROR HANDLING-- */
	}
	else *error_code = MPI_SUCCESS;
	break;

    default:
	/* --BEGIN ERROR HANDLING-- */
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPI_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_ARG,
					   "**flag", "**flag %d", flag);
	return;
	/* --END ERROR HANDLING-- */
    }
}
