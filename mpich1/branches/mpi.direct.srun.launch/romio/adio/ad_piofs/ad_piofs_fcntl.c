/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_piofs.h"
#include "adio_extern.h"

void ADIOI_PIOFS_Fcntl(ADIO_File fd, int flag, ADIO_Fcntl_t *fcntl_struct, int *error_code)
{
    MPI_Datatype copy_etype, copy_filetype;
    int i, ntimes, err;
    ADIO_Offset curr_fsize, alloc_size, size, len, done;
    ADIO_Status status;
    char *buf;
    piofs_change_view_t *piofs_change_view;
#ifndef PRINT_ERR_MSG
    static char myname[] = "ADIOI_PIOFS_FCNTL";
#endif

    switch(flag) {
    case ADIO_FCNTL_GET_FSIZE:
	fcntl_struct->fsize = llseek(fd->fd_sys, 0, SEEK_END);
	if (fd->fp_sys_posn != -1) 
	     llseek(fd->fd_sys, fd->fp_sys_posn, SEEK_SET);
	if (fcntl_struct->fsize == -1) {
#ifdef MPICH2
	    *error_code = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, myname, __LINE__, MPI_ERR_IO, "**io",
		"**io %s", strerror(errno));
#elif defined(PRINT_ERR_MSG)
	    *error_code =  MPI_ERR_UNKNOWN;
#else /* MPICH-1 */
	    *error_code = MPIR_Err_setmsg(MPI_ERR_IO, MPIR_ADIO_ERROR,
			      myname, "I/O Error", "%s", strerror(errno));
	    ADIOI_Error(fd, *error_code, myname);	    
#endif
	}
	else *error_code = MPI_SUCCESS;
	break;

    case ADIO_FCNTL_SET_DISKSPACE:
	ADIOI_GEN_Prealloc(fd, fcntl_struct->diskspace, error_code);
	break;

    case ADIO_FCNTL_SET_ATOMICITY:
	piofs_change_view = (piofs_change_view_t *) 
                                 ADIOI_Malloc(sizeof(piofs_change_view_t));
	piofs_change_view->Vbs = piofs_change_view->Vn = 
             piofs_change_view->Hbs = piofs_change_view->Hn = 1;
	piofs_change_view->subfile = 0;
	piofs_change_view->flags = (fcntl_struct->atomicity == 0) 
                             ? (ACTIVE | NORMAL) : (ACTIVE | CAUTIOUS);
	err = piofsioctl(fd->fd_sys, PIOFS_CHANGE_VIEW, piofs_change_view);
	ADIOI_Free(piofs_change_view);
	fd->atomicity = (fcntl_struct->atomicity == 0) ? 0 : 1;
	if (err == -1) {
#ifdef MPICH2
	    *error_code = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, myname, __LINE__, MPI_ERR_IO, "**io",
		"**io %s", strerror(errno));
#elif defined(PRINT_ERR_MSG)
	    *error_code =  MPI_ERR_UNKNOWN;
#else /* MPICH-1 */
	    *error_code = MPIR_Err_setmsg(MPI_ERR_IO, MPIR_ADIO_ERROR,
			      myname, "I/O Error", "%s", strerror(errno));
	    ADIOI_Error(fd, *error_code, myname);	    
#endif
	}
	else *error_code = MPI_SUCCESS;
	break;

    default:
	FPRINTF(stderr, "Unknown flag passed to ADIOI_PIOFS_Fcntl\n");
	MPI_Abort(MPI_COMM_WORLD, 1);
    }
}
