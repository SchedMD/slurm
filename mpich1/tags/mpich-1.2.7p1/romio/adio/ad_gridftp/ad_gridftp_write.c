/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2003 University of Chicago, Ohio Supercomputer Center. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_gridftp.h"
#include "adioi.h"
#include "adio_extern.h"

static globus_mutex_t writecontig_ctl_lock;
static globus_cond_t writecontig_ctl_cond;
static globus_bool_t writecontig_ctl_done;
static void writecontig_ctl_cb(void *myargs, globus_ftp_client_handle_t *handle, globus_object_t *error)
{
    if (error)
	{
	    FPRINTF(stderr, "%s\n", globus_object_printable_to_string(error));
	}
    globus_mutex_lock(&writecontig_ctl_lock);
    if ( writecontig_ctl_done!=GLOBUS_TRUE )
	writecontig_ctl_done=GLOBUS_TRUE;
    globus_cond_signal(&writecontig_ctl_cond);
    globus_mutex_unlock(&writecontig_ctl_lock);
#ifdef PRINT_ERR_MSG
    FPRINTF(stderr,"finished with contig write transaction\n");
#endif /* PRINT_ERR_MSG */
    return;
}

static void writecontig_data_cb(void *myargs, globus_ftp_client_handle_t *handle, globus_object_t *error,
			       globus_byte_t *buffer, globus_size_t length, globus_off_t offset,
			       globus_bool_t eof)
{
   globus_size_t *bytes_written;

    bytes_written=(globus_size_t *)myargs;
    if (error)
	{
	    FPRINTF(stderr, "%s\n", globus_object_printable_to_string(error));
	}
    *bytes_written+=length;
    /* I don't understand why the data callback has to keep recalling register_write,
       but everything I've done and all the examples I've seen seem to require
       that behavior to work... */
    if ( !eof )
	{
	    globus_ftp_client_register_write(handle,
					     buffer,
					     length,
					     offset,
					     GLOBUS_TRUE,
					     writecontig_data_cb,
					     (void *)(bytes_written));
	}
#ifdef PRINT_ERR_MSG
    FPRINTF(stderr,"wrote %Ld bytes...",(long long)length);
#endif /* PRINT_ERR_MSG */
    return;
}


static globus_mutex_t writediscontig_ctl_lock;
static globus_cond_t writediscontig_ctl_cond;
static globus_bool_t writediscontig_ctl_done;
static void writediscontig_ctl_cb(void *myargs, globus_ftp_client_handle_t *handle, globus_object_t *error)
{
    if (error)
	{
	    FPRINTF(stderr, "%s\n", globus_object_printable_to_string(error));
	}
    globus_mutex_lock(&writediscontig_ctl_lock);
    if ( writediscontig_ctl_done!=GLOBUS_TRUE )
	writediscontig_ctl_done=GLOBUS_TRUE;
    globus_cond_signal(&writediscontig_ctl_cond);
    globus_mutex_unlock(&writediscontig_ctl_lock);
    return;
}

static void writediscontig_data_cb(void *myargs, globus_ftp_client_handle_t *handle, globus_object_t *error,
			       globus_byte_t *buffer, globus_size_t length, globus_off_t offset,
			       globus_bool_t eof)
{
   globus_size_t *bytes_written;

    bytes_written=(globus_size_t *)myargs;
    if (error)
	{
	    FPRINTF(stderr, "%s\n", globus_object_printable_to_string(error));
	}
    *bytes_written+=length;
    /* I don't understand why the data callback has to keep recalling register_read,
       but everything I've done and all the examples I've seen seem to require
       that behavior to work... */
    if ( !eof )
	globus_ftp_client_register_write(handle,
					 buffer,
					 length,
					 offset,
					 eof,
					 writediscontig_data_cb,
					 (void *)(bytes_written));
    FPRINTF(stderr,"wrote %Ld bytes...",(long long)length); 
    return;
}


void ADIOI_GRIDFTP_WriteContig(ADIO_File fd, void *buf, int count, 
			     MPI_Datatype datatype, int file_ptr_type,
			     ADIO_Offset offset, ADIO_Status *status, int
			     *error_code)
{
    char myname[]="ADIOI_GRIDFTP_WriteContig";
    int myrank, nprocs, datatype_size;
    globus_size_t len,bytes_written=0;
    globus_off_t goff;
    globus_result_t result;

    if ( fd->access_mode&MPI_MODE_RDONLY )
	{
	    *error_code=MPI_ERR_AMODE;
	    return;
	}

    *error_code = MPI_SUCCESS;

    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);
    MPI_Type_size(datatype, &datatype_size);

    if (file_ptr_type != ADIO_EXPLICIT_OFFSET)
    {
	offset = fd->fp_ind;
    }

    /* Do the gridftp I/O transfer */
    goff = (globus_off_t)offset;
    len = ((globus_size_t)datatype_size)*((globus_size_t)count);

    globus_mutex_init(&writecontig_ctl_lock, GLOBUS_NULL);
    globus_cond_init(&writecontig_ctl_cond, GLOBUS_NULL);
    writecontig_ctl_done=GLOBUS_FALSE;
    if ( (result=globus_ftp_client_partial_put(&(gridftp_fh[fd->fd_sys]),
					       fd->filename,
					       &(oattr[fd->fd_sys]),
					       GLOBUS_NULL,
					       goff,
					       goff+(globus_off_t)len,
					       writecontig_ctl_cb,
					       GLOBUS_NULL))!=GLOBUS_SUCCESS )
	{
	    globus_err_handler("globus_ftp_client_partial_put",myname,result);
	    *error_code = MPIO_Err_create_code(MPI_SUCESS, MPIR_ERR_RECOVERABLE,
		    myname, __LINE__, MPI_ERR_IO,
		    "**io",
		    "**io %s", globus_object_printable_to_string(result));
	    return;
	}
    if ( (result=globus_ftp_client_register_write(&(gridftp_fh[fd->fd_sys]),
						  (globus_byte_t *)buf,
						  len,
						  goff,
						  GLOBUS_TRUE,
						  writecontig_data_cb,
						  (void *)(&bytes_written)))!=GLOBUS_SUCCESS )
	{
	    globus_err_handler("globus_ftp_client_register_write",myname,result);
	    *error_code = MPIO_Err_create_code(MPI_SUCESS, MPIR_ERR_RECOVERABLE,
		    myname, __LINE__, MPI_ERR_IO,
		    "**io",
		    "**io %s", globus_object_printable_to_string(result));
	    return;
	}


    /* The ctl callback won't start till the data callbacks complete, so it's
       safe to wait on just the ctl callback */
    globus_mutex_lock(&writecontig_ctl_lock);
    while ( writecontig_ctl_done!=GLOBUS_TRUE )
	globus_cond_wait(&writecontig_ctl_cond,&writecontig_ctl_lock);
    globus_mutex_unlock(&writecontig_ctl_lock);

    globus_mutex_destroy(&writecontig_ctl_lock);
    globus_cond_destroy(&writecontig_ctl_cond);

#ifdef HAVE_STATUS_SET_BYTES
    MPIR_Status_set_bytes(status, datatype, bytes_written);
#endif
    if (file_ptr_type != ADIO_EXPLICIT_OFFSET)
    {
	offset = fd->fp_ind;
	fd->fp_ind += bytes_written;
	fd->fp_sys_posn = fd->fp_ind;
    }
    else {
	fd->fp_sys_posn = offset + bytes_written;
    }
}


void ADIOI_GRIDFTP_WriteDiscontig(ADIO_File fd, void *buf, int count,
				 MPI_Datatype datatype, int file_ptr_type,
				 ADIO_Offset offset, ADIO_Status *status, int
				 *error_code)
{
    char myname[]="ADIOI_GRIDFTP_WriteDiscontig";
    int myrank,nprocs;
    MPI_Aint btype_size,btype_extent;
    MPI_Aint ftype_size,ftype_extent;
    MPI_Aint etype_size;
    MPI_Aint extent;
    ADIOI_Flatlist_node *flat_file;
    int buf_contig,boff,i,nblks;
    globus_off_t start,end,goff;
    globus_size_t bytes_written;
    globus_result_t result;

    MPI_Comm_rank(fd->comm,&myrank);
    MPI_Comm_size(fd->comm,&nprocs);
    etype_size=fd->etype_size;
    MPI_Type_size(fd->filetype,&ftype_size);
    MPI_Type_extent(fd->filetype,&ftype_extent);
    /* This is arguably unnecessary, as this routine assumes that the
       buffer in memory is contiguous */
    MPI_Type_size(datatype,&btype_size);
    MPI_Type_extent(datatype,&btype_extent);
    ADIOI_Datatype_iscontig(datatype,&buf_contig);
    
    if ( ( btype_extent!=btype_size ) || ( ! buf_contig ) )
	{
	    FPRINTF(stderr,"[%d/%d] %s called with discontigous memory buffer\n",
		    myrank,nprocs,myname);
	    fflush(stderr);
	    *error_code = MPIO_Err_create_code(MPI_SUCESS, MPIR_ERR_RECOVERABLE,
		    myname, __LINE__, MPI_ERR_IO,
		    "**io",
		    "**io %s", globus_object_printable_to_string(result));
	    return;
	}
    /* from here we can assume btype_extent==btype_size */

    /* Flatten out fd->filetype so we know which blocks to skip */
    ADIOI_Flatten_datatype(fd->filetype);
    flat_file = ADIOI_Flatlist;
    while (flat_file->type != fd->filetype && flat_file->next!=NULL)
	flat_file = flat_file->next;

    /* Figure out how big the area to write is */
    /* ASSUMPTION: ftype_size is an integer multiple of btype_size or vice versa. */
    start=(globus_off_t)(offset*etype_size);
    goff=start;
    boff=0;
    extent=0;
    nblks=0;
    while ( boff < (count*btype_size) )
	{
	    int blklen;

	    for (i=0;i<flat_file->count;i++)
		{
		    if ( (boff+flat_file->blocklens[i]) < (count*btype_size) )
			blklen=flat_file->blocklens[i];
		    else
			blklen=(count*btype_size)-boff;
		    boff+=blklen;
		    extent=MAX(extent,nblks*ftype_extent+flat_file->indices[i]+blklen);
		    if ( boff>=(count*btype_size) )
			break;
		}
	    nblks++;
	}
    if ( extent < count*btype_size )
	{
	    FPRINTF(stderr,"[%d/%d] %s error in computing extent -- extent %d is smaller than total bytes requested %d!\n",
		    myrank,nprocs,myname,extent,count*btype_size);
	    fflush(stderr);
	    *error_code = MPIO_Err_create_code(MPI_SUCESS, MPIR_ERR_RECOVERABLE,
		    myname, __LINE__, MPI_ERR_IO,
		    "**io",
		    "**io %s", globus_object_printable_to_string(result));
	    return;
	}
    end=start+(globus_off_t)extent;
    FPRINTF(stderr,"[%d/%d] %s writing %d bytes into extent of %d bytes starting at offset %Ld\n",
	    myrank,nprocs,myname,count*btype_size,extent,(long long)start);
    fflush(stderr);

    /* start up the globus partial write */
    globus_mutex_init(&writediscontig_ctl_lock, GLOBUS_NULL);
    globus_cond_init(&writediscontig_ctl_cond, GLOBUS_NULL);
    writediscontig_ctl_done=GLOBUS_FALSE;
    if ( (result=globus_ftp_client_partial_put(&(gridftp_fh[fd->fd_sys]),
					       fd->filename,
					       &(oattr[fd->fd_sys]),
					       GLOBUS_NULL,
					       start,
					       end,
					       writediscontig_ctl_cb,
					       GLOBUS_NULL))!=GLOBUS_SUCCESS )
	{
	    globus_err_handler("globus_ftp_client_partial_get",myname,result);
	    *error_code = MPIO_Err_create_code(MPI_SUCESS, MPIR_ERR_RECOVERABLE,
		    myname, __LINE__, MPI_ERR_IO,
		    "**io",
		    "**io %s", globus_object_printable_to_string(result));
	    return;
	}

    /* Do all the actual I/Os */
    boff=0;
    nblks=0;
    while ( boff < (count*btype_size) )
	{
	    int i,blklen;

	    for (i=0;i<flat_file->count;i++)
		{
		    if ( (boff+flat_file->blocklens[i]) < (count*btype_size) )
			blklen=flat_file->blocklens[i];
		    else
			blklen=(count*btype_size)-boff;
		    if ( blklen > 0 )
			{
			    goff=start+nblks*ftype_extent+((globus_off_t)flat_file->indices[i]);
			    /*
			    FPRINTF(stderr,"[%d/%d] %s writing %d bytes from boff=%d at goff=%Ld\n",myrank,nprocs,myname,blklen,boff,goff);
			    */
			    if ( (result=globus_ftp_client_register_write(&(gridftp_fh[fd->fd_sys]),
									  ((globus_byte_t *)buf)+boff,
									  (globus_size_t)blklen,
									  goff,
									  GLOBUS_TRUE,
									  writediscontig_data_cb,
									  (void *)(&bytes_written)))!=GLOBUS_SUCCESS )
				{
				    globus_err_handler("globus_ftp_client_register_write",myname,result);
				    *error_code=MPI_ERR_IO;
				    ADIOI_Error(fd,*error_code,myname);
				    return;
				}
			    boff+=blklen;
			    if ( boff>=(count*btype_size) )
				break;
			}
		}
	    nblks++;
	}

    
    /* The ctl callback won't start till the data callbacks complete, so it's
       safe to wait on just the ctl callback */
    globus_mutex_lock(&writediscontig_ctl_lock);
    while ( writediscontig_ctl_done!=GLOBUS_TRUE )
	globus_cond_wait(&writediscontig_ctl_cond,&writediscontig_ctl_lock);
    globus_mutex_unlock(&writediscontig_ctl_lock);
    globus_mutex_destroy(&writediscontig_ctl_lock);
    globus_cond_destroy(&writediscontig_ctl_cond);

#ifdef HAVE_STATUS_SET_BYTES
    MPIR_Status_set_bytes(status, datatype, bytes_written);
#endif
    if (file_ptr_type != ADIO_EXPLICIT_OFFSET)
    {
	fd->fp_ind += extent;
	fd->fp_sys_posn = fd->fp_ind;
#if 0
	FPRINTF(stdout, "[%d/%d]    new file position is %Ld\n", myrank, 
		nprocs, (long long) fd->fp_ind);
#endif
    }
    else {
	fd->fp_sys_posn = offset + extent;
    }
}


#define GRIDFTP_USE_GENERIC_STRIDED
void ADIOI_GRIDFTP_WriteStrided(ADIO_File fd, void *buf, int count,
			       MPI_Datatype datatype, int file_ptr_type,
			       ADIO_Offset offset, ADIO_Status *status,
			       int *error_code)
{
#ifdef GRIDFTP_USE_GENERIC_STRIDED
    int myrank, nprocs;

    if ( fd->access_mode&MPI_MODE_RDONLY )
	{
	    *error_code=MPI_ERR_AMODE;
	    return;
	}

    *error_code = MPI_SUCCESS;

    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);

    ADIOI_GEN_WriteStrided(fd, buf, count, datatype, file_ptr_type, offset, 
			   status, error_code);
    return;
#else
    char myname[]="ADIOI_GRIDFTP_WriteStrided";
    int myrank, nprocs;
    int buf_contig,file_contig;
    MPI_Aint btype_size,bufsize;
    globus_byte_t *intermediate;

    *error_code = MPI_SUCCESS;

    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);

    MPI_Type_size(datatype,&btype_size);
    bufsize=count*btype_size;
    ADIOI_Datatype_iscontig(fd->filetype,&file_contig);
    ADIOI_Datatype_iscontig(datatype,&buf_contig);
    if ( buf_contig && !file_contig )
	{
	    /* Contiguous in memory, discontig in file */
	    FPRINTF(stderr,"[%d/%d] %s called w/ contig mem, discontig file\n",
		    myrank,nprocs,myname);
	    fflush(stderr);

	    ADIOI_GRIDFTP_WriteDiscontig(fd, buf, count, datatype,
					file_ptr_type, offset, status, error_code);
	}
    else if ( !buf_contig && file_contig )
	{
	    /* Discontiguous in mem, contig in file -- comparatively easy */
	    int posn=0;

	    FPRINTF(stderr,"[%d/%d] %s called w/ discontig mem, contig file\n",
		    myrank,nprocs,myname);
	    fflush(stderr);


	    /* squeeze contents of main buffer into intermediate buffer*/
	    intermediate=(globus_byte_t *)ADIOI_Malloc((size_t)bufsize);
	    MPI_Pack(buf,count,datatype,intermediate,bufsize,&posn,fd->comm);

	    /* write contiguous data from intermediate buffer */
	    ADIOI_GRIDFTP_WriteContig(fd, intermediate, bufsize, MPI_BYTE,
				     file_ptr_type, offset, status, error_code);

	    ADIOI_Free(intermediate);
	}
    else if ( !buf_contig && !file_contig )
	{
	    /* Discontig in both mem and file -- the hardest case */
	    int posn=0;

	    FPRINTF(stderr,"[%d/%d] %s called w/ discontig mem, discontig file\n",
		    myrank,nprocs,myname);
	    fflush(stderr);

	    /* squeeze contents of main buffer into intermediate buffer*/
	    intermediate=(globus_byte_t *)ADIOI_Malloc((size_t)bufsize);
	    MPI_Pack(buf,count,datatype,intermediate,bufsize,&posn,fd->comm);

	    /* write contiguous data from intermediate buffer */
	    ADIOI_GRIDFTP_WriteDiscontig(fd, intermediate, bufsize, MPI_BYTE,
				     file_ptr_type, offset, status, error_code);

	    ADIOI_Free(intermediate);
	}
    else 
	{
	    /* Why did you bother calling WriteStrided?!?!?! */
	    FPRINTF(stderr,"[%d/%d] Why the heck did you call %s with contiguous buffer *and* file types?\n",
		    myrank,nprocs,myname);
	    ADIOI_GRIDFTP_WriteContig(fd, buf, count, datatype,
				      file_ptr_type, offset, status, error_code);
	}
#endif /* ! GRIDFTP_USE_GENERIC_STRIDED */
}

