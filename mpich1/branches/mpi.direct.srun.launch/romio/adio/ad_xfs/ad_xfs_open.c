/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_xfs.h"
#ifdef HAVE_STDDEF_H
#include <stddef.h>
#endif

#if defined(MPISGI)
#include <mpitypedefs.h>
#include <mpifunctions.h>
#endif

void ADIOI_XFS_Open(ADIO_File fd, int *error_code)
{
    int perm, amode, amode_direct;
    unsigned int old_mask;
    struct dioattr st;
    static char myname[] = "ADIOI_XFS_OPEN";

    if (fd->perm == ADIO_PERM_NULL) {
	old_mask = umask(022);
	umask(old_mask);
	perm = old_mask ^ 0666;
    }
    else perm = fd->perm;

    amode = 0;
    if (fd->access_mode & ADIO_CREATE)
	amode = amode | O_CREAT;
    if (fd->access_mode & ADIO_RDONLY)
	amode = amode | O_RDONLY;
    if (fd->access_mode & ADIO_WRONLY)
	amode = amode | O_WRONLY;
    if (fd->access_mode & ADIO_RDWR)
	amode = amode | O_RDWR;

    amode_direct = amode | O_DIRECT;

    if (fd->access_mode & ADIO_EXCL)
	amode = amode | O_EXCL;

    fd->fd_sys = open(fd->filename, amode, perm);

    fd->fd_direct = open(fd->filename, amode_direct, perm);
    if (fd->fd_direct != -1) {

#if defined(LINUX) && defined(MPISGI)
	ioctl(fd->fd_direct, XFS_IOC_DIOINFO, &st);
#else
	fcntl(fd->fd_direct, F_DIOINFO, &st);
#endif

	fd->d_mem = st.d_mem;
	fd->d_miniosz = st.d_miniosz;
	fd->d_maxiosz = st.d_maxiosz;

	if (fd->d_mem > XFS_MEMALIGN) {
	    FPRINTF(stderr, "MPI: Run-time Direct-IO memory alignment, %d, does not match compile-time value, %d.\n",
		fd->d_mem, XFS_MEMALIGN);
	    FPRINTF(stderr, "MPI: Report this error and rerun with Direct-IO disabled.\n");
	    close(fd->fd_direct);
	    fd->fd_direct = -1;
	}
    }

    if ((fd->fd_sys != -1) && (fd->access_mode & ADIO_APPEND))
	fd->fp_ind = lseek64(fd->fd_sys, 0, SEEK_END);

    fd->fp_sys_posn = -1; /* set it to null because we use pread/pwrite */

    if ((fd->fd_sys == -1) || (fd->fd_direct == -1)) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO, "**io",
					   "**io %s", strerror(errno));
    }
    else *error_code = MPI_SUCCESS;
}
