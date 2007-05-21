/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2003 University of Chicago, Ohio Supercomputer Center. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_gridftp.h"
#include "adioi.h"

static globus_mutex_t lock;
static globus_cond_t cond;
static globus_bool_t delete_done, delete_success;
static void delete_cb(void *myarg, globus_ftp_client_handle_t *handle, globus_object_t *error)
{
    
    if (error)
	{
	    FPRINTF(stderr, "%s\n", globus_object_printable_to_string(error));
	}
    else
	{
	    delete_success=GLOBUS_TRUE;
	}
    delete_done=GLOBUS_TRUE;
}

void ADIOI_GRIDFTP_Delete(char *filename, int *error_code)
{
    char myname[]="ADIOI_GRIDFTP_Delete";
    int myrank, nprocs;
    globus_ftp_client_handle_t handle;
    globus_result_t result;

    *error_code = MPI_SUCCESS;

    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &myrank);

    globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);
    result=globus_ftp_client_handle_init(&handle,GLOBUS_NULL);
   
    if (result != GLOBUS_SUCCESS )
    {
	    globus_err_handler("globus_ftp_client_handle_init",myname,result);
	    *error_code= MPIO_Err_create_code(MPI_SUCCESS,
			    MPIR_ERR_RECOVERABLE,
			    myname, __LINE__,
			    MPI_ERR_IO,
			    "**io", "**io %s", 
			    globus_object_printable_to_string(result));
	    return; 
    }
    
    delete_done=GLOBUS_FALSE;
    delete_success=GLOBUS_FALSE;
    result=globus_ftp_client_delete(&handle,filename,GLOBUS_NULL,delete_cb,GLOBUS_NULL);
    if (result != GLOBUS_SUCCESS )
	{
	    globus_err_handler("globus_ftp_client_delete",myname,result);
	    *error_code= MPIO_Err_create_code(MPI_SUCCESS,
			    MPIR_ERR_RECOVERABLE,
			    myname, __LINE__,
			    MPI_ERR_IO,
			    "**io", "**io %s",
			    globus_object_printable_to_string(result));
	    return;
	}
    globus_mutex_lock(&lock);
    while ( delete_done!=GLOBUS_TRUE )
	globus_cond_wait(&cond,&lock);
    globus_mutex_unlock(&lock);
    result=globus_ftp_client_handle_destroy(&handle);
    if (result != GLOBUS_SUCCESS )
	{
	    globus_err_handler("globus_ftp_client_handle_destroy",myname,result);
	    *error_code= MPIO_Err_create_code(MPI_SUCCESS,
			    MPIR_ERR_RECOVERABLE,
			    myname, __LINE__,
			    MPI_ERR_IO,
			    "**io", "**io %s", 
			    globus_object_printable_to_string(result));
	    return;
	}

    if ( delete_success!=GLOBUS_TRUE )
	{
	    *error_code= MPIO_Err_create_code(MPI_SUCCESS,
			    MPIR_ERR_RECOVERABLE,
			    myname, __LINE__,
			    MPI_ERR_IO,
			    "**io", "**io %s", 
			    globus_object_printable_to_string(result));
	}
}
