/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_piofs.h"
#ifdef PROFILE
#include "mpe.h"
#endif

void ADIOI_PIOFS_ReadContig(ADIO_File fd, void *buf, int count, 
                     MPI_Datatype datatype, int file_ptr_type,
		     ADIO_Offset offset, ADIO_Status *status, int *error_code)
{
    int err=-1, datatype_size, len;
#ifndef PRINT_ERR_MSG
    static char myname[] = "ADIOI_PIOFS_READCONTIG";
#endif

    MPI_Type_size(datatype, &datatype_size);
    len = datatype_size * count;

    if (file_ptr_type == ADIO_EXPLICIT_OFFSET) {
	if (fd->fp_sys_posn != offset) {
#ifdef PROFILE
            MPE_Log_event(11, 0, "start seek");
#endif
	    llseek(fd->fd_sys, offset, SEEK_SET);
#ifdef PROFILE
            MPE_Log_event(12, 0, "end seek");
#endif
	}
#ifdef PROFILE
        MPE_Log_event(3, 0, "start read");
#endif
	err = read(fd->fd_sys, buf, len);
#ifdef PROFILE
        MPE_Log_event(4, 0, "end read");
#endif
	fd->fp_sys_posn = offset + err;
         /* individual file pointer not updated */        
    }
    else {  /* read from curr. location of ind. file pointer */
	if (fd->fp_sys_posn != fd->fp_ind) {
#ifdef PROFILE
            MPE_Log_event(11, 0, "start seek");
#endif
	    llseek(fd->fd_sys, fd->fp_ind, SEEK_SET);
#ifdef PROFILE
            MPE_Log_event(12, 0, "end seek");
#endif
	}
#ifdef PROFILE
        MPE_Log_event(3, 0, "start read");
#endif
	err = read(fd->fd_sys, buf, len);
#ifdef PROFILE
        MPE_Log_event(4, 0, "end read");
#endif
	fd->fp_ind += err; 
	fd->fp_sys_posn = fd->fp_ind;
    }         

#ifdef HAVE_STATUS_SET_BYTES
    if (err != -1) MPIR_Status_set_bytes(status, datatype, err);
#endif

    if (err == -1) {
#ifdef MPICH2
	*error_code = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, myname, __LINE__, MPI_ERR_IO, "**io",
	    "**io %s", strerror(errno));
#elif defined(PRINT_ERR_MSG)
	*error_code =  MPI_ERR_UNKNOWN;
#else
	*error_code = MPIR_Err_setmsg(MPI_ERR_IO, MPIR_ADIO_ERROR,
			      myname, "I/O Error", "%s", strerror(errno));
	ADIOI_Error(fd, *error_code, myname);	    
#endif
    }
    else *error_code = MPI_SUCCESS;
}
