/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_hfs.h"

void ADIOI_HFS_ReadContig(ADIO_File fd, void *buf, int count, 
                     MPI_Datatype datatype, int file_ptr_type,
		     ADIO_Offset offset, ADIO_Status *status, int *error_code)
{
    int err=-1, datatype_size, len;
#ifndef PRINT_ERR_MSG
    static char myname[] = "ADIOI_HFS_READCONTIG";
#endif

    MPI_Type_size(datatype, &datatype_size);
    len = datatype_size * count;

#ifdef SPPUX
    fd->fp_sys_posn = -1; /* set it to null, since we are using pread */

    if (file_ptr_type == ADIO_EXPLICIT_OFFSET) 
	err = pread64(fd->fd_sys, buf, len, offset);
    else {    /* read from curr. location of ind. file pointer */
	err = pread64(fd->fd_sys, buf, len, fd->fp_ind);
	fd->fp_ind += err;
    }
#endif

#ifdef HPUX
    if (file_ptr_type == ADIO_EXPLICIT_OFFSET) {
	if (fd->fp_sys_posn != offset)
	    lseek64(fd->fd_sys, offset, SEEK_SET);
	err = read(fd->fd_sys, buf, len);
	fd->fp_sys_posn = offset + err;
	/* individual file pointer not updated */        
    }
    else {  /* read from curr. location of ind. file pointer */
	if (fd->fp_sys_posn != fd->fp_ind)
	    lseek64(fd->fd_sys, fd->fp_ind, SEEK_SET);
	err = read(fd->fd_sys, buf, len);
	fd->fp_ind += err; 
	fd->fp_sys_posn = fd->fp_ind;
    }         
#endif

#ifdef HAVE_STATUS_SET_BYTES
    if (err != -1) MPIR_Status_set_bytes(status, datatype, err);
#endif

	if (err == -1 ) {
#ifdef MPICH2
	    *error_code = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, myname, __LINE__, MPI_ERR_IO, "**io",
		"**io %s", strerror(errno));
#elif defined(PRINT_ERR_MSG)
	    *error_code = (err == -1) ? MPI_ERR_UNKNOWN : MPI_SUCCESS;
#else /* MPICH-1 */
	*error_code = MPIR_Err_setmsg(MPI_ERR_IO, MPIR_ADIO_ERROR,
			      myname, "I/O Error", "%s", strerror(errno));
	ADIOI_Error(fd, *error_code, myname);	    
#endif
    }
    else *error_code = MPI_SUCCESS;
}
