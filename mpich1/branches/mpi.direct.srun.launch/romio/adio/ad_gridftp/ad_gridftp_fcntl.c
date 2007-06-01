/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2003 University of Chicago, Ohio Supercomputer Center. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_gridftp.h"
#include "adioi.h"
#include "adio_extern.h"

globus_mutex_t fcntl_size_lock;
globus_cond_t fcntl_size_cond;
globus_bool_t fcntl_size_done;

void fcntl_size_cb(void *myargs, globus_ftp_client_handle_t *handle,
		  globus_object_t *error)
{
    if (error)
	{
	    FPRINTF(stderr, "%s\n", globus_object_printable_to_string(error));
	}
    globus_mutex_lock(&fcntl_size_lock);
    fcntl_size_done=GLOBUS_TRUE;
    globus_cond_signal(&fcntl_size_cond);
    globus_mutex_unlock(&fcntl_size_lock);
}

void ADIOI_GRIDFTP_Fcntl(ADIO_File fd, int flag, ADIO_Fcntl_t *fcntl_struct, 
			int *error_code)
{
    MPI_Datatype copy_etype, copy_filetype;
    int combiner, i, j, k, filetype_is_contig, err;
    ADIOI_Flatlist_node *flat_file;
    char myname[]="ADIOI_GRIDFTP_Fcntl";

    int myrank, nprocs;

    *error_code = MPI_SUCCESS;

    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);

    switch(flag) {
    case ADIO_FCNTL_GET_FSIZE:
	{
	    globus_result_t result;
	    globus_off_t fsize=0;
	    
	    globus_mutex_init(&fcntl_size_lock,GLOBUS_NULL);
	    globus_cond_init(&fcntl_size_cond,GLOBUS_NULL);
	    fcntl_size_done=GLOBUS_FALSE;
	    if ( (result=globus_ftp_client_size(&(gridftp_fh[fd->fd_sys]),
						fd->filename,
						&(oattr[fd->fd_sys]),
						&(fsize),
						fcntl_size_cb,
						GLOBUS_NULL))!=GLOBUS_SUCCESS )
		{
		    globus_err_handler("globus_ftp_client_size",myname,result);
		    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
				    MPIR_ERR_RECOVERABLE,
                                    myname, __LINE__, MPI_ERR_IO,
				    "**io", "**io %s", 
				    globus_object_printable_to_string(result));
		    return;
		}
	    globus_mutex_lock(&fcntl_size_lock);
	    while ( fcntl_size_done!=GLOBUS_TRUE )
		globus_cond_wait(&fcntl_size_lock,&fcntl_size_cond);
	    globus_mutex_unlock(&fcntl_size_lock);
	    globus_mutex_destroy(&fcntl_size_lock);
	    globus_cond_destroy(&fcntl_size_cond);
	    fcntl_struct->fsize=fsize;
	}
	*error_code = MPI_SUCCESS;
	break;

    case ADIO_FCNTL_SET_DISKSPACE:
	ADIOI_GEN_Prealloc(fd, fcntl_struct->diskspace, error_code);
	break;

    case ADIO_FCNTL_SET_ATOMICITY:
    default:
	*error_code = MPIO_Err_create_code(MPI_SUCCESS,
			MPIR_ERR_RECOVERABLE,
			myname, __LINE__,
			MPI_ERR_ARG,
			"**flag", "**flag %d", flag);
    }
}
