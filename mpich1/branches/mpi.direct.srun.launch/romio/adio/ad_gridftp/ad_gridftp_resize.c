/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2003 University of Chicago, Ohio Supercomputer Center. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_gridftp.h"
#include "adioi.h"

static globus_mutex_t resize_lock;
static globus_cond_t resize_cond;
static globus_bool_t resize_done;
static globus_bool_t resize_success;

void resize_cb(void *myargs, globus_ftp_client_handle_t *handle,
		  globus_object_t *error)
{
    if (error)
	{
	    FPRINTF(stderr, "%s\n", globus_object_printable_to_string(error));
	    globus_mutex_lock(&resize_lock);
	    resize_success=GLOBUS_FALSE;
	    globus_mutex_unlock(&resize_lock);
	}
    else
	{
	    globus_mutex_lock(&resize_lock);
	    resize_success=GLOBUS_TRUE;
	    globus_mutex_unlock(&resize_lock);
	}
    globus_mutex_lock(&resize_lock);
    resize_done=GLOBUS_TRUE;
    globus_cond_signal(&resize_cond);
    globus_mutex_unlock(&resize_lock);
}


static void resize_wrdata_cb(void *myargs, globus_ftp_client_handle_t *handle, globus_object_t *error,
			     globus_byte_t *buffer, globus_size_t length, globus_off_t offset,
			     globus_bool_t eof)
{
    if (error)
	FPRINTF(stderr, "%s\n", globus_object_printable_to_string(error));
    if (!eof)
	globus_ftp_client_register_read(handle,
					buffer,
					length,
					resize_wrdata_cb,
					myargs);
    return;
}


void ADIOI_GRIDFTP_Resize(ADIO_File fd, ADIO_Offset size, int *error_code)
{
    int myrank, nprocs;
    char myname[]="ADIOI_GRIDFTP_Resize";
    globus_off_t fsize;
    globus_result_t result;

    *error_code = MPI_SUCCESS;

    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);

    /* Sanity check */
    if ( fd->access_mode&MPI_MODE_RDONLY )
	{
	    FPRINTF(stderr,"%s:  attempt to resize read-only file %s!\n",
		    myname,fd->filename);
	    *error_code = MPIO_Err_create_code(MPI_SUCESS, MPIR_ERR_RECOVERABLE,
		    myname, __LINE__, MPI_ERR_IO,
		    "**io", 0);
	    return;
	}

    /* This routine is supposed to do the moral equivalent of truncate(),
       but there's not an equivalent operation in the globus_ftp_client API. */
    globus_mutex_init(&resize_lock,GLOBUS_NULL);
    globus_cond_init(&resize_cond,GLOBUS_NULL);
    resize_done=GLOBUS_FALSE;
    if ( (result=globus_ftp_client_size(&(gridftp_fh[fd->fd_sys]),
					fd->filename,
					&(oattr[fd->fd_sys]),
					&(fsize),
					resize_cb,
					GLOBUS_NULL))!=GLOBUS_SUCCESS )
	{
	    globus_err_handler("globus_ftp_client_size",myname,result);
	    *error_code = MPIO_Err_create_code(MPI_SUCESS, MPIR_ERR_RECOVERABLE,
		    myname, __LINE__, MPI_ERR_IO,
		    "**io",
		    "**io %s", globus_object_printable_to_string(result));
	    return;
	}
    globus_mutex_lock(&resize_lock);
    while ( resize_done!=GLOBUS_TRUE )
	globus_cond_wait(&resize_lock,&resize_cond);
    if ( fsize < (globus_off_t)size )
	{
	    /* The file is smaller than the requested size, so
	       do a zero-byte write to where the new EOF should be. */
	    globus_byte_t touchbuf=(globus_byte_t)'\0';
	    resize_done=GLOBUS_FALSE;
	    if ( (result=globus_ftp_client_partial_put(&(gridftp_fh[fd->fd_sys]),
						       fd->filename,
						       &(oattr[fd->fd_sys]),
						       GLOBUS_NULL,
						       (globus_off_t)size,
						       (globus_off_t)size,
						       resize_cb,
						       GLOBUS_NULL))!=GLOBUS_SUCCESS )
		{
		    globus_err_handler("globus_ftp_client_partial_put",myname,result);
		    *error_code = MPIO_Err_create_code(MPI_SUCESS, 
				    MPIR_ERR_RECOVERABLE, myname, __LINE__, 
				    MPI_ERR_IO, "**io", "**io %s", 
				    globus_object_printable_to_string(result));
		    return;
		}

	    if ( (result=globus_ftp_client_register_write(&(gridftp_fh[fd->fd_sys]),
							  (globus_byte_t *)&touchbuf,
							  0,
							  (globus_off_t)0,
							  GLOBUS_TRUE,
							  resize_wrdata_cb,
							  GLOBUS_NULL))!=GLOBUS_SUCCESS )
		{
		    globus_err_handler("globus_ftp_client_register_write",myname,result);
		    *error_code = MPIO_Err_create_code(MPI_SUCESS, 
				    MPIR_ERR_RECOVERABLE, myname, __LINE__, 
				    MPI_ERR_IO, "**io", "**io %s", 
				    globus_object_printable_to_string(result));
		    return;
		}
	    globus_mutex_lock(&resize_lock);
	    while ( resize_done!=GLOBUS_TRUE )
		globus_cond_wait(&resize_cond,&resize_lock);
	    globus_mutex_unlock(&resize_lock);
	}
    else if ( fsize > (globus_off_t)size )
	{
	    /* The file is bigger than the requested size, so
	       we'll abuse globus_ftp_client_third_party_partial_put()
	       into truncating it for us. */
	    char *urlold;
	    size_t urllen;

	    urllen=strlen(fd->filename);
	    urlold=(char *)ADIOI_Malloc(urllen+5);
	    ADIOI_Snprintf(urlold,urllen+5,"%s.old",fd->filename);
	    resize_done=GLOBUS_FALSE;
	    resize_success=GLOBUS_FALSE;
	    if ( (result=globus_ftp_client_move(&(gridftp_fh[fd->fd_sys]),
						fd->filename,
						urlold,
						&(oattr[fd->fd_sys]),
						resize_cb,
						GLOBUS_NULL))!=GLOBUS_SUCCESS )
		{
		    globus_err_handler("globus_ftp_client_move",myname,result);
		    *error_code = MPIO_Err_create_code(MPI_SUCESS, 
				    MPIR_ERR_RECOVERABLE, myname, __LINE__, 
				    MPI_ERR_IO, "**io", "**io %s", 
				    globus_object_printable_to_string(result));
		    return;
		}
	    globus_mutex_lock(&resize_lock);
	    while ( resize_done!=GLOBUS_TRUE )
		globus_cond_wait(&resize_cond,&resize_lock);
	    globus_mutex_unlock(&resize_lock);
	    if ( resize_success!=GLOBUS_TRUE )
		{
		    *error_code = MPI_ERR_IO;
		    return;
		}
	    resize_done=GLOBUS_FALSE;
	    if ( (result=globus_ftp_client_partial_third_party_transfer(&(gridftp_fh[fd->fd_sys]),
						urlold,
						&(oattr[fd->fd_sys]),
						fd->filename,
						&(oattr[fd->fd_sys]),
						GLOBUS_NULL,
						0,
						(globus_off_t)size,
						resize_cb,
						GLOBUS_NULL))!=GLOBUS_SUCCESS )
		{
		    globus_err_handler("globus_ftp_client_partial_third_party_transfer",myname,result);
		    *error_code = MPIO_Err_create_code(MPI_SUCESS, 
				    MPIR_ERR_RECOVERABLE, myname, __LINE__, 
				    MPI_ERR_IO, "**io", "**io %s", 
				    globus_object_printable_to_string(result));
		    return;
		}
	    globus_mutex_lock(&resize_lock);
	    while ( resize_done!=GLOBUS_TRUE )
		globus_cond_wait(&resize_cond,&resize_lock);
	    globus_mutex_unlock(&resize_lock);
	    if ( resize_success!=GLOBUS_TRUE )
		{
		    *error_code = MPI_ERR_IO;
		    ADIOI_Error(fd,*error_code,myname);
		    return;
		}
	    resize_done=GLOBUS_FALSE;
	    if ( (result=globus_ftp_client_delete(&(gridftp_fh[fd->fd_sys]),
						  urlold,
						  &(oattr[fd->fd_sys]),
						  resize_cb,
						  GLOBUS_NULL))!=GLOBUS_SUCCESS )
		{
		    globus_err_handler("globus_ftp_client_delete",myname,result);
		    *error_code = MPIO_Err_create_code(MPI_SUCESS, 
				    MPIR_ERR_RECOVERABLE, myname, __LINE__, 
				    MPI_ERR_IO, "**io", "**io %s", 
				    globus_object_printable_to_string(result));
		    return;
		}
	    globus_mutex_lock(&resize_lock);
	    while ( resize_done!=GLOBUS_TRUE )
		globus_cond_wait(&resize_cond,&resize_lock);
	    globus_mutex_unlock(&resize_lock);
	    if ( resize_success!=GLOBUS_TRUE )
		{
		    *error_code = MPI_ERR_IO;
		    ADIOI_Error(fd,*error_code,myname);
		    return;
		}
	    ADIOI_Free(urlold);
	}
    globus_mutex_destroy(&resize_lock);
    globus_cond_destroy(&resize_cond);
}





