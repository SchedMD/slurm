/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 2003 University of Chicago, Ohio Supercomputer Center.
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_gridftp.h"
#include "adioi.h"

static globus_mutex_t lock;
static globus_cond_t cond;

static globus_bool_t file_exists,exists_done;
static void exists_cb(void *myargs, globus_ftp_client_handle_t *handle, globus_object_t *error)
{    
    if (error)
	{
	    FPRINTF(stderr, "%s\n", globus_object_printable_to_string(error));
	}
    else
	{
	    file_exists=GLOBUS_TRUE;
	}
    exists_done=GLOBUS_TRUE;
}

static globus_bool_t touch_ctl_done;
static void touch_ctl_cb(void *myargs, globus_ftp_client_handle_t *handle, globus_object_t *error)
{
    if (error)
	{
	    FPRINTF(stderr, "%s\n", globus_object_printable_to_string(error));
	}
    globus_mutex_lock(&lock);
    touch_ctl_done=GLOBUS_TRUE;
    globus_cond_signal(&cond);
    globus_mutex_unlock(&lock);
}

static void touch_data_cb(void *myargs, globus_ftp_client_handle_t *handle, globus_object_t *error,
			  globus_byte_t *buffer, globus_size_t length, globus_off_t offset,
			  globus_bool_t eof)
{
    if (error)
	FPRINTF(stderr, "%s\n", globus_object_printable_to_string(error));
    globus_ftp_client_register_read(handle,buffer,length,touch_data_cb,myargs);
    return;
}

void ADIOI_GRIDFTP_Open(ADIO_File fd, int *error_code)
{
    static char myname[]="ADIOI_GRIDFTP_Open";
    int myrank, nprocs, keyfound;
    char hintval[MPI_MAX_INFO_VAL+1];
    globus_ftp_client_handleattr_t hattr;
    globus_result_t result;

    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);

    /* activate Globus ftp client module -- can be called multiple times, so
       it's safest to call once per file/connection */
    globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);
    fd->fd_sys = num_gridftp_handles;
    /* No shared file pointers for now */
    fd->shared_fp_fname = NULL;
    *error_code = MPI_SUCCESS;

    /* Access modes here mean something very different here than they
       would on a "real" filesystem...  As a result, the amode and hint
       processing here is intermingled and a little weird because many
       of them have to do with the connection rather than the file itself.
       The thing that sucks about this is that read and write ops will
       have to check themselves if the file is being accessed rdonly, rdwr,
       or wronly.
       */
    result=globus_ftp_client_handleattr_init(&hattr)
    if ( result != GLOBUS_SUCCESS )
	{
	    

	    globus_err_handler("globus_ftp_client_handleattr_init",
			       myname,result);
	    fd->fd_sys = -1;
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
		    myname, __LINE__, MPI_ERR_IO,
		    "**io",
		    "**io %s", globus_object_printable_to_string(result));
	    return;
	}
    result = globus_ftp_client_operationattr_init(&(oattr[fd->fd_sys]));
    if ( result != GLOBUS_SUCCESS )
	{
	    globus_err_handler("globus_ftp_client_operationattr_init",
			       myname,result);
	    fd->fd_sys = -1;
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
		    myname, __LINE__, MPI_ERR_IO,
		    "**io",
		    "**io %s", globus_object_printable_to_string(result));
	    return;
	}


    /* Always use connection caching unless told otherwise */
    result=globus_ftp_client_handleattr_set_cache_all(&hattr,GLOBUS_TRUE)
    if ( result !=GLOBUS_SUCCESS )
	globus_err_handler("globus_ftp_client_handleattr_set_cache_all",myname,result);

    /* Assume that it's safe to cache a file if it's read-only */
    if ( (fd->access_mode&MPI_MODE_RDONLY) &&
	 (result=globus_ftp_client_handleattr_add_cached_url(&hattr,fd->filename))!=GLOBUS_SUCCESS )
	globus_err_handler("globus_ftp_client_handleattr_add_cached_url",myname,result);

    /* Since we're (almost by definition) doing things that FTP S (stream)
       control mode can't handle, default to E (extended block) control mode
       for gsiftp:// URLs.  ftp:// URLs use standard stream control mode 
       by default.  This behavior can be overridden by the ftp_control_mode
       hint. */

    /*
    if ( !strncmp(fd->filename,"gsiftp:",7) && 
	 (result=globus_ftp_client_operationattr_set_mode(&(oattr[fd->fd_sys]),GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK))!=GLOBUS_SUCCESS )
	globus_err_handler("globus_ftp_client_operationattr_set_mode",myname,result);
    else if ( !strncmp(fd->filename,"ftp:",4) && 
	      (result=globus_ftp_client_operationattr_set_mode(&(oattr[fd->fd_sys]),GLOBUS_FTP_CONTROL_MODE_STREAM))!=GLOBUS_SUCCESS )
	globus_err_handler("globus_ftp_client_operationattr_set_mode",myname,result);
    */

    /* Set append mode if necessary */
    if ( (fd->access_mode&MPI_MODE_APPEND) && 
	 ((result=globus_ftp_client_operationattr_set_append(&(oattr[fd->fd_sys]),GLOBUS_TRUE))!=GLOBUS_SUCCESS) )
	globus_err_handler("globus_ftp_client_operationattr_set_append",myname,result);

    /* Other hint and amode processing that would affect hattr and/or 
       oattr[] (eg. parallelism, striping, etc.) goes here */
    if ( fd->info!=MPI_INFO_NULL )
	{
	    MPI_Info_get(fd->info,"ftp_control_mode",MPI_MAX_INFO_VAL,hintval,&keyfound);
	    if ( keyfound )
		{
		    if ( ( !strcmp(hintval,"extended") || !strcmp(hintval,"extended_block") ) && 
			 (result=globus_ftp_client_operationattr_set_mode(&(oattr[fd->fd_sys]),GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK))!=GLOBUS_SUCCESS )
			globus_err_handler("globus_ftp_client_operationattr_set_mode",myname,result);
		    else if ( !strcmp(hintval,"block") && 
			      (result=globus_ftp_client_operationattr_set_mode(&(oattr[fd->fd_sys]),GLOBUS_FTP_CONTROL_MODE_BLOCK))!=GLOBUS_SUCCESS )
			globus_err_handler("globus_ftp_client_operationattr_set_mode",myname,result);
		    else if ( !strcmp(hintval,"compressed") && 
			      (result=globus_ftp_client_operationattr_set_mode(&(oattr[fd->fd_sys]),GLOBUS_FTP_CONTROL_MODE_COMPRESSED))!=GLOBUS_SUCCESS )
			globus_err_handler("globus_ftp_client_operationattr_set_mode",myname,result);
		    else if ( !strcmp(hintval,"stream") && 
			      (result=globus_ftp_client_operationattr_set_mode(&(oattr[fd->fd_sys]),GLOBUS_FTP_CONTROL_MODE_STREAM))!=GLOBUS_SUCCESS )
			globus_err_handler("globus_ftp_client_operationattr_set_mode",myname,result);
		}

	    MPI_Info_get(fd->info,"parallelism",MPI_MAX_INFO_VAL,hintval,&keyfound);
	    if ( keyfound )
		{
		    int nftpthreads;
		    
		    if ( sscanf(hintval,"%d",&nftpthreads)==1 )
			{
			    globus_ftp_control_parallelism_t parallelism;

			    parallelism.mode = GLOBUS_FTP_CONTROL_PARALLELISM_FIXED;
			    parallelism.fixed.size = nftpthreads;
			    if ( (result=globus_ftp_client_operationattr_set_parallelism(&(oattr[fd->fd_sys]),
											 &parallelism))!=GLOBUS_SUCCESS )
				globus_err_handler("globus_ftp_client_operationattr_set_parallelism",myname,result);
			}
		}

	    MPI_Info_get(fd->info,"striped_ftp",MPI_MAX_INFO_VAL,hintval,&keyfound);
	    if ( keyfound )
		{
		    /* if set to "true" or "enable", set up round-robin block layout */
		    if ( !strncmp("true",hintval,4) || !strncmp("TRUE",hintval,4) ||
			 !strncmp("enable",hintval,4) || !strncmp("ENABLE",hintval,4) )
			{
			    MPI_Info_get(fd->info,"striping_factor",MPI_MAX_INFO_VAL,hintval,&keyfound);
			    if ( keyfound )
				{
				    int striping_factor;

				    if ( sscanf(hintval,"%d",&striping_factor)==1 )
					{
					    globus_ftp_control_layout_t layout;

					    layout.mode = GLOBUS_FTP_CONTROL_STRIPING_BLOCKED_ROUND_ROBIN;
					    layout.round_robin.block_size = striping_factor;
					    if ( (result=globus_ftp_client_operationattr_set_layout(&(oattr[fd->fd_sys]),
												    &layout))!=GLOBUS_SUCCESS  )
						globus_err_handler("globus_ftp_client_operationattr_set_layout",
								   myname,result);
					}
				}
			}
		}

	    MPI_Info_get(fd->info,"tcp_buffer",MPI_MAX_INFO_VAL,hintval,&keyfound);
	    if ( keyfound )
		{
		    /* set tcp buffer size */
		    int buffer_size;
		    if ( sscanf(hintval,"%d",&buffer_size)==1 )
			{
			    globus_ftp_control_tcpbuffer_t tcpbuf;

			    tcpbuf.mode = GLOBUS_FTP_CONTROL_TCPBUFFER_FIXED;
			    tcpbuf.fixed.size = buffer_size;
			    if ( (result=globus_ftp_client_operationattr_set_tcp_buffer(&(oattr[fd->fd_sys]),
											&tcpbuf))!=GLOBUS_SUCCESS )
				globus_err_handler("globus_ftp_client_operationattr_set_tcp_buffer",myname,result);
			}
		}

	    MPI_Info_get(fd->info,"transfer_type",MPI_MAX_INFO_VAL,hintval,&keyfound);
	    if ( keyfound )
		{
		    globus_ftp_control_type_t filetype;
		    /* set transfer type (i.e. ASCII or binary) */
		    if ( !strcmp("ascii",hintval) || !strcmp("ASCII",hintval) )
			{
			    filetype=GLOBUS_FTP_CONTROL_TYPE_ASCII;
			}
		    else
			{
			    filetype=GLOBUS_FTP_CONTROL_TYPE_IMAGE;
			}
		    if ( (result=globus_ftp_client_operationattr_set_type(&(oattr[fd->fd_sys]),filetype))!=GLOBUS_SUCCESS )
			globus_err_handler("globus_ftp_client_operationattr_set_type",myname,result);
		}
	}
    else
	FPRINTF(stderr,"no MPI_Info object associated with %s\n",fd->filename);

    /* Create the ftp handle */
    result=globus_ftp_client_handle_init(&(gridftp_fh[fd->fd_sys]),&hattr)
    if ( result != GLOBUS_SUCCESS )
	{
	    globus_err_handler("globus_ftp_client_handle_init",myname,result);
	    fd->fd_sys = -1;
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
		    myname, __LINE__, MPI_ERR_IO,
		    "**io",
		    "**io %s", globus_object_printable_to_string(result));
	    return;
	}

    /* Check for existence of the file */
    globus_mutex_init(&lock, GLOBUS_NULL);
    globus_cond_init(&cond, GLOBUS_NULL);
    file_exists=GLOBUS_FALSE;
    exists_done=GLOBUS_FALSE;
    if ( myrank==0 )
	{
	    if ( (result=globus_ftp_client_exists(&(gridftp_fh[fd->fd_sys]),
						  fd->filename,
						  &(oattr[fd->fd_sys]),
						  exists_cb,
						  GLOBUS_NULL))!=GLOBUS_SUCCESS )
		{
		    globus_err_handler("globus_ftp_client_exists",myname,result);
		    fd->fd_sys = -1; 
		    *error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
				    myname, __LINE__, MPI_ERR_IO,
				    "**io", "**io %s", 
				    globus_object_printable_to_string(result));
		    return;
		}
	    /* wait till the callback completes */
	    globus_mutex_lock(&lock);
	    while ( exists_done!=GLOBUS_TRUE )
		globus_cond_wait(&cond,&lock);
	    globus_mutex_unlock(&lock);
	}
    MPI_Barrier(fd->comm);
    MPI_Bcast(&file_exists,1,MPI_INT,0,fd->comm);

    /* It turns out that this is handled by MPI_File_open() directly */
    if ( (file_exists!=GLOBUS_TRUE) && (fd->access_mode&MPI_MODE_CREATE) &&
	 !(fd->access_mode&MPI_MODE_EXCL) && !(fd->access_mode&MPI_MODE_RDONLY) )
	{
	    if ( myrank==0 )
		{
		    /* if the file doesn't exist, write a single NULL to it */
		    globus_byte_t touchbuf=(globus_byte_t)'\0';
		    touch_ctl_done=GLOBUS_FALSE;
		    if ( (result=globus_ftp_client_put(&(gridftp_fh[fd->fd_sys]),
						       fd->filename,
						       &(oattr[fd->fd_sys]),
						       GLOBUS_NULL,
						       touch_ctl_cb,
						       GLOBUS_NULL))!=GLOBUS_SUCCESS )
			{
			    globus_err_handler("globus_ftp_client_put",myname,result);
			    fd->fd_sys = -1;
			    *error_code = MPIO_Err_create_code(MPI_SUCCESS, 
				MPIR_ERR_RECOVERABLE,
				myname, __LINE__, MPI_ERR_IO,
				"**io", "**io %s", 
				globus_object_printable_to_string(result));
			    return;
			}
		    result=globus_ftp_client_register_write(&(gridftp_fh[fd->fd_sys]),
				  (globus_byte_t *)&touchbuf, 0,
				  (globus_off_t)0, GLOBUS_TRUE,
				  touch_data_cb, GLOBUS_NULL);

		    if ( result != GLOBUS_SUCCESS )
			{
			    globus_err_handler("globus_ftp_client_register_write",myname,result);
			    *error_code = MPIO_Err_create_code(MPI_SUCCESS, 
				MPIR_ERR_RECOVERABLE,
				myname, __LINE__, MPI_ERR_IO,
				"**io", "**io %s", 
				globus_object_printable_to_string(result));
			    return;
			}
		    globus_mutex_lock(&lock);
		    while ( touch_ctl_done!=GLOBUS_TRUE )
			globus_cond_wait(&cond,&lock);
		    globus_mutex_unlock(&lock);
		}
	    MPI_Barrier(fd->comm);
	}
    else if ( (fd->access_mode&MPI_MODE_EXCL) && (file_exists==GLOBUS_TRUE) )
	{
	    fd->fd_sys = -1;
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
			    myname, __LINE__, MPI_ERR_IO, 
			    "**io", 0);
	    return;
	}
    else if ( (fd->access_mode&MPI_MODE_RDONLY) && (file_exists!=GLOBUS_TRUE) )
	{
	    if ( myrank==0 )
		{
		    FPRINTF(stderr,"WARNING:  read-only file %s does not exist!\n",fd->filename);
		}
	}
    num_gridftp_handles++;
    
#if 0
    /* Debugging info for testing PASV mode behind firewalls */
    if ( myrank==0 )
	{
	    globus_bool_t striped;
	    globus_ftp_control_mode_t mode;
	    globus_ftp_control_type_t filetype;
	    globus_ftp_control_parallelism_t parallelism;

	    FPRINTF(stderr,"--gridftp details for %s--\n",
		    fd->filename);

	    /* 
	    FPRINTF(stderr,"Connection caching: ");
	    globus_ftp_client_handleattr_get_cache_all(&hattr,&cached);
	    if ( cached==GLOBUS_TRUE )
		FPRINTF(stderr,"Y\n");
	    else
		FPRINTF(stderr,"N\n");
	    */

	    FPRINTF(stderr,"Control mode:  ");
	    globus_ftp_client_operationattr_get_mode(&(oattr[fd->fd_sys]),&mode);
	    if ( mode==GLOBUS_FTP_CONTROL_MODE_BLOCK )
		FPRINTF(stderr,"block\n");
	    else if ( mode==GLOBUS_FTP_CONTROL_MODE_COMPRESSED )
		FPRINTF(stderr,"compressed\n");
	    else if ( mode==GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK )
		FPRINTF(stderr,"extended block\n");
	    else if ( mode==GLOBUS_FTP_CONTROL_MODE_STREAM )
		FPRINTF(stderr,"stream\n");
	    else
		FPRINTF(stderr,"unknown\n");

	    FPRINTF(stderr,"File type:  ");
	    globus_ftp_client_operationattr_get_type(&(oattr[fd->fd_sys]),&filetype);
	    if ( filetype==GLOBUS_FTP_CONTROL_TYPE_ASCII )
		FPRINTF(stderr,"ASCII\n");
	    else if ( filetype==GLOBUS_FTP_CONTROL_TYPE_IMAGE )
		FPRINTF(stderr,"binary\n");
	    else if ( filetype==GLOBUS_FTP_CONTROL_TYPE_EBCDIC )
		FPRINTF(stderr,"EBCDIC\n");
	    else
		FPRINTF(stderr,"unknown\n");

	    FPRINTF(stderr,"Parallelism:  ");
	    globus_ftp_client_operationattr_get_parallelism(&(oattr[fd->fd_sys]),&parallelism);
	    if ( parallelism.mode==GLOBUS_FTP_CONTROL_PARALLELISM_NONE )
		FPRINTF(stderr,"none\n");
	    else if ( parallelism.mode==GLOBUS_FTP_CONTROL_PARALLELISM_FIXED )
		FPRINTF(stderr,"fixed with %d streams\n",parallelism.fixed.size);
	    else
		FPRINTF(stderr,"unknown\n");

	    FPRINTF(stderr,"Striping:  ");
	    globus_ftp_client_operationattr_get_striped(&(oattr[fd->fd_sys]),&striped);
	    if ( striped==GLOBUS_TRUE )
		{
		    globus_ftp_control_layout_t layout;

		    FPRINTF(stderr,"Y\nLayout:  ");
		    globus_ftp_client_operationattr_get_layout(&(oattr[fd->fd_sys]),
									       &layout);
		    if ( layout.mode==GLOBUS_FTP_CONTROL_STRIPING_NONE )
			FPRINTF(stderr,"none\n");
		    else if ( layout.mode==GLOBUS_FTP_CONTROL_STRIPING_PARTITIONED )
			FPRINTF(stderr,"partitioned, size=%d\n",layout.partitioned.size);
		    else if ( layout.mode==GLOBUS_FTP_CONTROL_STRIPING_BLOCKED_ROUND_ROBIN )
			FPRINTF(stderr,"round-robin, block size=%d\n",layout.round_robin.block_size);
		    else
			FPRINTF(stderr,"unknown\n");
		}
	    else
		FPRINTF(stderr,"N\n");

	    fflush(stderr);
	}
#endif

}
