/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2003 University of Chicago, Ohio Supercomputer Center. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_gridftp.h"
#include "adioi.h"

void ADIOI_GRIDFTP_Close(ADIO_File fd, int *error_code)
{
    int err;
    static char myname[]="ADIOI_GRIDFTP_Close";

    globus_result_t result;

    MPI_Barrier(fd->comm);

    /* Destroy the ftp handle  and opattr */
    result = globus_ftp_client_operationattr_destroy(&(oattr[fd->fd_sys]));
    if (result != GLOBUS_SUCCESS )
    {
	    globus_err_handler("globus_ftp_client_operationattr_destroy",
		    myname,result);
	    *error_code = MPIO_Err_create_code(MPI_SUCESS, MPIR_ERR_RECOVERABLE,
		    myname, __LINE__, MPI_ERR_IO,
		    "**io",
		    "**io %s",globus_object_printable_to_string(result));
	    return;
    }
    result=globus_ftp_client_handle_destroy(&(gridftp_fh[fd->fd_sys]));
    if (result != GLOBUS_SUCCESS )
    {
	    globus_err_handler("globus_ftp_client_handle_destroy",
		    myname,result);
	    *error_code = MPIO_Err_create_code(MPI_SUCESS, MPIR_ERR_RECOVERABLE,
		    myname, __LINE__, MPI_ERR_IO,
		    "**io",
		    "**io %s", globus_object_printable_to_string(result));
	    return;
    }

    fd->fd_sys = -1;
    fd->fp_ind=0;
    fd->fp_sys_posn=0;
    num_gridftp_handles--;

    *error_code = MPI_SUCCESS;
}
