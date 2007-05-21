/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2003 University of Chicago, Ohio Supercomputer Center. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_gridftp.h"
#include "adioi.h"
#include "adio_extern.h"

static globus_mutex_t readcontig_ctl_lock;
static globus_cond_t readcontig_ctl_cond;
static globus_bool_t readcontig_ctl_done;
static void readcontig_ctl_cb(void *myargs, globus_ftp_client_handle_t *handle, globus_object_t *error)
{
    if (error)
	{
	    FPRINTF(stderr, "%s\n", globus_object_printable_to_string(error));
	}
    globus_mutex_lock(&readcontig_ctl_lock);
    if ( readcontig_ctl_done!=GLOBUS_TRUE )
	readcontig_ctl_done=GLOBUS_TRUE;
    globus_cond_signal(&readcontig_ctl_cond);
    globus_mutex_unlock(&readcontig_ctl_lock);
    return;
}

static void readcontig_data_cb(void *myargs, globus_ftp_client_handle_t *handle, globus_object_t *error,
			       globus_byte_t *buffer, globus_size_t length, globus_off_t offset,
			       globus_bool_t eof)
{
   globus_size_t *bytes_read;

    bytes_read=(globus_size_t *)myargs;
    if (error)
	{
	    FPRINTF(stderr, "%s\n", globus_object_printable_to_string(error));
	}
    *bytes_read+=length;
    /* I don't understand why the data callback has to keep recalling register_read,
       but everything I've done and all the examples I've seen seem to require
       that behavior to work... */
    /*
     * Using buffer+length seems to work, but is probably not the correct
     * solution.  A big read of 256kB chunks will have lines like this:
	readcontig_data_cb: buffer 0x404e0008 length 0 offset 31719424 eof 1
	readcontig_data_cb: buffer 0x404a0008 length 65536 offset 31981568 eof 0
	readcontig_data_cb: buffer 0x404b0008 length 65536 offset 32047104 eof 0
	readcontig_data_cb: buffer 0x404c0008 length 65536 offset 32112640 eof 0
	readcontig_data_cb: buffer 0x404d0008 length 65536 offset 32178176 eof 0
     */
#if 0
    FPRINTF(stderr, "%s: buffer %p length %d offset %Ld eof %d\n",
      __func__, buffer, length, offset, eof);
#endif
    if ( !eof )
	    globus_ftp_client_register_read(handle,
					    buffer+length,
					    length,
					    readcontig_data_cb,
					    (void *)(bytes_read));
    return;
}

static globus_mutex_t readdiscontig_ctl_lock;
static globus_cond_t readdiscontig_ctl_cond;
static globus_bool_t readdiscontig_ctl_done;
static void readdiscontig_ctl_cb(void *myargs, globus_ftp_client_handle_t *handle, globus_object_t *error)
{
    if (error)
	{
	    FPRINTF(stderr, "%s\n", globus_object_printable_to_string(error));
	}
    globus_mutex_lock(&readdiscontig_ctl_lock);
    if ( readdiscontig_ctl_done!=GLOBUS_TRUE )
	readdiscontig_ctl_done=GLOBUS_TRUE;
    globus_cond_signal(&readdiscontig_ctl_cond);
    globus_mutex_unlock(&readdiscontig_ctl_lock);
    return;
}

static void readdiscontig_data_cb(void *myargs, globus_ftp_client_handle_t *handle, globus_object_t *error,
			       globus_byte_t *buffer, globus_size_t length, globus_off_t offset,
			       globus_bool_t eof)
{
   globus_size_t *bytes_read;

    bytes_read=(globus_size_t *)myargs;
    if (error)
	{
	    FPRINTF(stderr, "%s\n", globus_object_printable_to_string(error));
	}
    *bytes_read+=length;
    /* I don't understand why the data callback has to keep recalling register_read,
       but everything I've done and all the examples I've seen seem to require
       that behavior to work... */
    if ( !eof )
	    globus_ftp_client_register_read(handle,
					    buffer,
					    length,
					    readdiscontig_data_cb,
					    (void *)(bytes_read));
    return;
}

void ADIOI_GRIDFTP_ReadContig(ADIO_File fd, void *buf, int count, 
			     MPI_Datatype datatype, int file_ptr_type,
			     ADIO_Offset offset, ADIO_Status *status, int
			     *error_code)
{
    static char myname[]="ADIOI_GRIDFTP_ReadContig";
    int myrank, nprocs, datatype_size;
    globus_size_t len,bytes_read=0;
    globus_off_t goff;
    globus_result_t result;

    if ( fd->access_mode&MPI_MODE_WRONLY )
	{
	    *error_code=MPIR_ERR_MODE_WRONLY;
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

    globus_mutex_init(&readcontig_ctl_lock, GLOBUS_NULL);
    globus_cond_init(&readcontig_ctl_cond, GLOBUS_NULL);
    readcontig_ctl_done=GLOBUS_FALSE;
    if ( (result=globus_ftp_client_partial_get(&(gridftp_fh[fd->fd_sys]),
					       fd->filename,
					       &(oattr[fd->fd_sys]),
					       GLOBUS_NULL,
					       goff,
					       goff+(globus_off_t)len,
					       readcontig_ctl_cb,
					       GLOBUS_NULL))!=GLOBUS_SUCCESS )
	{
	    globus_err_handler("globus_ftp_client_partial_get",myname,result);
	    *error_code=MPI_ERR_IO;
	    ADIOI_Error(fd,*error_code,myname);
	    return;
	}
    result=globus_ftp_client_register_read(&(gridftp_fh[fd->fd_sys]),
		    (globus_byte_t *)buf, len, readcontig_data_cb,
		    (void *)(&bytes_read));
    if ( result != GLOBUS_SUCCESS )
	{
	    globus_err_handler("globus_ftp_client_register_read",myname,result);
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS, 
			    MPIR_ERR_RECOVERABLE, myname, __LINE__, 
			    MPI_ERR_IO, "**io", "**io %s", 
			    globus_object_printable_to_string(result));
	    return;
	}  


    /* The ctl callback won't start till the data callbacks complete, so it's
       safe to wait on just the ctl callback */
    globus_mutex_lock(&readcontig_ctl_lock);
    while ( readcontig_ctl_done!=GLOBUS_TRUE )
	globus_cond_wait(&readcontig_ctl_cond,&readcontig_ctl_lock);
    globus_mutex_unlock(&readcontig_ctl_lock);

    globus_mutex_destroy(&readcontig_ctl_lock);
    globus_cond_destroy(&readcontig_ctl_cond);

#ifdef HAVE_STATUS_SET_BYTES
    MPIR_Status_set_bytes(status, datatype, bytes_read);
#endif
    if (file_ptr_type != ADIO_EXPLICIT_OFFSET)
    {
	fd->fp_ind += bytes_read;
	fd->fp_sys_posn = fd->fp_ind;
    }
    else {
	fd->fp_sys_posn = offset + bytes_read;
    }
}

void ADIOI_GRIDFTP_ReadDiscontig(ADIO_File fd, void *buf, int count,
				 MPI_Datatype datatype, int file_ptr_type,
				 ADIO_Offset offset, ADIO_Status *status, int
				 *error_code)
{
    char myname[]="ADIOI_GRIDFTP_ReadDiscontig";
    int myrank,nprocs;
    /* size and extent of buffer in memory */
    MPI_Aint btype_size,btype_extent;
    /* size and extent of file record layout */
    MPI_Aint ftype_size,ftype_extent;
    /* size of file elemental type; seeks are done in units of this */
    MPI_Aint etype_size;
    MPI_Aint extent;
    ADIOI_Flatlist_node *flat_file;
    int i,buf_contig,boff,nblks;
    globus_off_t start,end,goff;
    globus_size_t bytes_read;
    globus_result_t result;
    globus_byte_t *tmp;

    if ( fd->access_mode&MPI_MODE_WRONLY )
	{
	    *error_code=MPIR_ERR_MODE_WRONLY;
	    return;
	}

    *error_code=MPI_SUCCESS;

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
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS, 
			    MPIR_ERR_RECOVERABLE, myname, __LINE__, 
			    MPI_ERR_IO, "**io", 0 );
	    return;
	}
    /* from here we can assume btype_extent==btype_size */

    /* Flatten out fd->filetype so we know which blocks to skip */
    ADIOI_Flatten_datatype(fd->filetype);
    flat_file = ADIOI_Flatlist;
    while (flat_file->type != fd->filetype && flat_file->next!=NULL)
	flat_file = flat_file->next;

    /* Figure out how big the area to read is */
    start=(globus_off_t)(offset*etype_size);
    goff=start;
    boff=0;
    extent=0;
    nblks=0;
    while ( boff < (count*btype_size) )
	{
	    int blklen=0;

	    for (i=0;i<flat_file->count;i++)
		{
		    /* find the length of the next block */
		    if ( (boff+flat_file->blocklens[i]) < (count*btype_size) )
			blklen=flat_file->blocklens[i];
		    else
			blklen=(count*btype_size)-boff;
		    /* increment buffer size to be used */
		    boff+=blklen;
		    /* compute extent -- the nblks*ftype_extent bit is
		       there so we remember how many ftypes we've already
		       been through */
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
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS, 
			    MPIR_ERR_RECOVERABLE, myanem, __LINE__, 
			    MPI_ERR_IO, "**io", 0);
	    return;
	}
    end=start+(globus_off_t)extent;
    tmp=(globus_byte_t *)ADIOI_Malloc((size_t)extent*sizeof(globus_byte_t));

    /* start up the globus partial read */
    globus_mutex_init(&readdiscontig_ctl_lock, GLOBUS_NULL);
    globus_cond_init(&readdiscontig_ctl_cond, GLOBUS_NULL);
    readdiscontig_ctl_done=GLOBUS_FALSE;
    if ( (result=globus_ftp_client_partial_get(&(gridftp_fh[fd->fd_sys]),
					       fd->filename,
					       &(oattr[fd->fd_sys]),
					       GLOBUS_NULL,
					       start,
					       end,
					       readdiscontig_ctl_cb,
					       GLOBUS_NULL))!=GLOBUS_SUCCESS )
	{
	    globus_err_handler("globus_ftp_client_partial_get",myname,result);
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS, 
			    MPIR_ERR_RECOVERABLE, myanem, __LINE__, 
			    MPI_ERR_IO, "**io", "**io %s", 
			    globus_object_printable_to_string(result));
	    return;
	}

    /* Do all the actual I/Os */
    /* Since globus_ftp_client_register_read() is brain-dead and doesn't
       let you specify an offset, we have to slurp the entire extent into
       memory and then parse out the pieces we want...  Sucks, doesn't it?

       This should probably be done in chunks (preferably of a size
       set using a file hint), but that'll have to come later.
       --TB */
    if ( (result=globus_ftp_client_register_read(&(gridftp_fh[fd->fd_sys]),
						 tmp,
						 (globus_size_t)extent,
						 readdiscontig_data_cb,
						 (void *)(&bytes_read)))!=GLOBUS_SUCCESS )
	{
	    globus_err_handler("globus_ftp_client_register_read",myname,result);
	    *error_code = MPIO_Err_create_code(MPI_SUCESS, MPIR_ERR_RECOVERABLE,
		    myname, __LINE__, MPI_ERR_IO,
		    "**io",
		    "**io %s", globus_object_printable_to_string(result));
	    return;
	}
    /* The ctl callback won't start till the data callbacks complete, so it's
       safe to wait on just the ctl callback */
    globus_mutex_lock(&readdiscontig_ctl_lock);
    while ( readdiscontig_ctl_done!=GLOBUS_TRUE )
	globus_cond_wait(&readdiscontig_ctl_cond,&readdiscontig_ctl_lock);
    globus_mutex_unlock(&readdiscontig_ctl_lock);

    globus_mutex_destroy(&readdiscontig_ctl_lock);
    globus_cond_destroy(&readdiscontig_ctl_cond);

    boff=0;
    nblks=0;
    goff=0;
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
			    goff=nblks*ftype_extent+flat_file->indices[i];
			    memcpy((globus_byte_t *)buf+boff,tmp+goff,(size_t)blklen);
			    boff+=blklen;
			    if ( boff>=(count*btype_size) )
				break;
			}
		}
	    nblks++;
	}
    ADIOI_Free(tmp);

#ifdef HAVE_STATUS_SET_BYTES
    MPIR_Status_set_bytes(status, datatype, bytes_read);
#endif
    if (file_ptr_type != ADIO_EXPLICIT_OFFSET)
    {
	fd->fp_ind += extent;
	fd->fp_sys_posn = fd->fp_ind;
    }
    else {
	fd->fp_sys_posn = offset + extent;
    }
}

void ADIOI_GRIDFTP_ReadStrided(ADIO_File fd, void *buf, int count,
			      MPI_Datatype datatype, int file_ptr_type,
			      ADIO_Offset offset, ADIO_Status *status, int
			      *error_code)
{
    /*
    int myrank, nprocs;

    *error_code = MPI_SUCCESS;

    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);
#ifdef PRINT_ERR_MSG
    FPRINTF(stdout, "[%d/%d] ADIOI_GRIDFTP_ReadStrided called on %s\n", myrank, 
	    nprocs, fd->filename);
    FPRINTF(stdout, "[%d/%d]    calling ADIOI_GEN_ReadStrided\n", myrank, 
	    nprocs);
#endif

    ADIOI_GEN_ReadStrided(fd, buf, count, datatype, file_ptr_type, offset,
			  status, error_code);
    
    */

    char myname[]="ADIOI_GRIDFTP_ReadStrided";
    int myrank, nprocs;
    int i,j;
    int buf_contig,file_contig;
    MPI_Aint btype_size,bufsize;
    globus_off_t start,disp;
    globus_size_t bytes_read;
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
	    ADIOI_GRIDFTP_ReadDiscontig(fd, buf, count, datatype,
					file_ptr_type, offset, status, error_code);
	}
    else if ( !buf_contig && file_contig )
	{
	    /* Discontiguous in mem, contig in file -- comparatively easy */
	    int posn=0;

	    /* read contiguous data into intermediate buffer */
	    intermediate=(globus_byte_t *)ADIOI_Malloc((size_t)bufsize);
	    ADIOI_GRIDFTP_ReadContig(fd, intermediate, bufsize, MPI_BYTE,
				     file_ptr_type, offset, status, error_code);

	    /* explode contents of intermediate buffer into main buffer */
	    MPI_Unpack(intermediate,bufsize,&posn,buf,count,datatype,fd->comm);

	    ADIOI_Free(intermediate);
	}
    else if ( !buf_contig && !file_contig )
	{
	    /* Discontig in both mem and file -- the hardest case */
	    int posn=0;

	    /* Read discontiguous data into intermediate buffer */
	    intermediate=(globus_byte_t *)ADIOI_Malloc((size_t)bufsize);
	    ADIOI_GRIDFTP_ReadDiscontig(fd, intermediate, bufsize, MPI_BYTE,
					file_ptr_type, offset, status, error_code);

	    /* explode contents of intermediate buffer into main buffer */
	    posn=0;
	    MPI_Unpack(intermediate,bufsize,&posn,buf,count,datatype,fd->comm);

	    ADIOI_Free(intermediate);
	}
    else 
	{
	    /* Why did you bother calling ReadStrided?!?!?! */
	    ADIOI_GRIDFTP_ReadContig(fd, buf, count, datatype,
				     file_ptr_type, offset, status, error_code);
	}

}

