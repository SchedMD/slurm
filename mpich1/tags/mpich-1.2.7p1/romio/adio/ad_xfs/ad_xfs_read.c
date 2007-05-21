/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_xfs.h"
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

/* style: allow:free:2 sig:0 */

static void ADIOI_XFS_Aligned_Mem_File_Read(ADIO_File fd, void *buf, int len, 
					     ADIO_Offset offset, int *err);

void ADIOI_XFS_ReadContig(ADIO_File fd, void *buf, int count, 
                     MPI_Datatype datatype, int file_ptr_type,
		     ADIO_Offset offset, ADIO_Status *status, int *error_code)
{
    int err=-1, datatype_size, len, diff, size, nbytes;
    void *newbuf;
    static char myname[] = "ADIOI_XFS_READCONTIG";

    MPI_Type_size(datatype, &datatype_size);
    len = datatype_size * count;

    fd->fp_sys_posn = -1; /* set it to null, since we are using pread */

    if (file_ptr_type == ADIO_INDIVIDUAL) offset = fd->fp_ind;

    if (!(fd->direct_read))     /* direct I/O not enabled */
	err = pread(fd->fd_sys, buf, len, offset);
    else {       /* direct I/O enabled */

	/* (1) if mem_aligned && file_aligned 
                    use direct I/O to read up to correct io_size
                    use buffered I/O for remaining  */

	if (!(((long) buf) % fd->d_mem) && !(offset % fd->d_miniosz)) 
	    ADIOI_XFS_Aligned_Mem_File_Read(fd, buf, len, offset, &err);

        /* (2) if !file_aligned
                    use buffered I/O to read up to file_aligned
                    At that point, if still mem_aligned, use (1)
   		        else copy into aligned buf and then use (1) */
	else if (offset % fd->d_miniosz) {
	    diff = fd->d_miniosz - (offset % fd->d_miniosz);
	    diff = ADIOI_MIN(diff, len);
	    nbytes = pread(fd->fd_sys, buf, diff, offset);

	    buf = ((char *) buf) + diff;
	    offset += diff;
	    size = len - diff;
	    if (!(((long) buf) % fd->d_mem)) {
		ADIOI_XFS_Aligned_Mem_File_Read(fd, buf, size, offset, &err);
		nbytes += err;
	    }
	    else {
		newbuf = (void *) memalign(XFS_MEMALIGN, size);
		if (newbuf) {
		    ADIOI_XFS_Aligned_Mem_File_Read(fd, newbuf, size, offset, &err);
		    if (err > 0) memcpy(buf, newbuf, err);
		    nbytes += err;
		    free(newbuf);
		}
		else nbytes += pread(fd->fd_sys, buf, size, offset);
	    }
	    err = nbytes;
	}

        /* (3) if !mem_aligned && file_aligned
    	            copy into aligned buf, then use (1)  */
	else {
	    newbuf = (void *) memalign(XFS_MEMALIGN, len);
	    if (newbuf) {
		ADIOI_XFS_Aligned_Mem_File_Read(fd, newbuf, len, offset, &err);
		if (err > 0) memcpy(buf, newbuf, err);
		free(newbuf);
	    }
	    else err = pread(fd->fd_sys, buf, len, offset);
	}
    }

    if (file_ptr_type == ADIO_INDIVIDUAL) fd->fp_ind += err;

#ifdef HAVE_STATUS_SET_BYTES
    if (err != -1) MPIR_Status_set_bytes(status, datatype, err);
#endif

    if (err == -1) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO, "**io",
					   "**io %s", strerror(errno));
    }
    else *error_code = MPI_SUCCESS;
}


void ADIOI_XFS_Aligned_Mem_File_Read(ADIO_File fd, void *buf, int len, 
              ADIO_Offset offset, int *err)
{
    int ntimes, rem, newrem, i, size, nbytes;

    /* memory buffer is aligned, offset in file is aligned,
       io_size may or may not be of the right size.
       use direct I/O to read up to correct io_size,
       use buffered I/O for remaining. */

    if (!(len % fd->d_miniosz) && 
	(len >= fd->d_miniosz) && (len <= fd->d_maxiosz))
	*err = pread(fd->fd_direct, buf, len, offset);
    else if (len < fd->d_miniosz)
	*err = pread(fd->fd_sys, buf, len, offset);
    else if (len > fd->d_maxiosz) {
	ntimes = len/(fd->d_maxiosz);
	rem = len - ntimes * fd->d_maxiosz;
	nbytes = 0;
	for (i=0; i<ntimes; i++) {
	    nbytes += pread(fd->fd_direct, ((char *)buf) + i * fd->d_maxiosz,
			 fd->d_maxiosz, offset);
	    offset += fd->d_maxiosz;
	}
	if (rem) {
	    if (!(rem % fd->d_miniosz))
		nbytes += pread(fd->fd_direct, 
		     ((char *)buf) + ntimes * fd->d_maxiosz, rem, offset);
	    else {
		newrem = rem % fd->d_miniosz;
		size = rem - newrem;
		if (size) {
		    nbytes += pread(fd->fd_direct, 
		         ((char *)buf) + ntimes * fd->d_maxiosz, size, offset);
		    offset += size;
		}
		nbytes += pread(fd->fd_sys, 
	              ((char *)buf) + ntimes*fd->d_maxiosz + size, newrem, offset);
	    }
	}
	*err = nbytes;
    }
    else {
	rem = len % fd->d_miniosz;
	size = len - rem;
	nbytes = pread(fd->fd_direct, buf, size, offset);
	nbytes += pread(fd->fd_sys, (char *)buf + size, rem, offset+size);
	*err = nbytes;
    }
}
