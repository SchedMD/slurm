/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pvfs.h"

void ADIOI_PVFS_Open(ADIO_File fd, int *error_code)
{
    int perm, amode, old_mask, flag;
    char *value;
    /* some really old versions of pvfs may not have a release nr */
    /* we changed the structure of pvfs_filestat in pvfs-1.5.7 */
    struct pvfs_filestat pstat = {-1,-1,-1};
    static char myname[] = "ADIOI_PVFS_OPEN";

    if (fd->perm == ADIO_PERM_NULL) {
	old_mask = umask(022);
	umask(old_mask);
	perm = old_mask ^ 0666;
    }
    else perm = fd->perm;

    amode = O_META;
    if (fd->access_mode & ADIO_CREATE)
	amode = amode | O_CREAT;
    if (fd->access_mode & ADIO_RDONLY)
	amode = amode | O_RDONLY;
    if (fd->access_mode & ADIO_WRONLY)
	amode = amode | O_WRONLY;
    if (fd->access_mode & ADIO_RDWR)
	amode = amode | O_RDWR;
    if (fd->access_mode & ADIO_EXCL)
	amode = amode | O_EXCL;

    value = (char *) ADIOI_Malloc((MPI_MAX_INFO_VAL+1)*sizeof(char));

    MPI_Info_get(fd->info, "striping_factor", MPI_MAX_INFO_VAL, 
		 value, &flag);
    if (flag && (atoi(value) > 0)) pstat.pcount = atoi(value);

    MPI_Info_get(fd->info, "striping_unit", MPI_MAX_INFO_VAL, 
		 value, &flag);
    if (flag && (atoi(value) > 0)) pstat.ssize = atoi(value);

    MPI_Info_get(fd->info, "start_iodevice", MPI_MAX_INFO_VAL, 
		 value, &flag);
    if (flag && (atoi(value) >= 0)) pstat.base = atoi(value);

    fd->fd_sys = pvfs_open64(fd->filename, amode, perm, &pstat, NULL);
    fd->fd_direct = -1;

    if ((fd->fd_sys != -1) && (fd->access_mode & ADIO_APPEND))
	fd->fp_ind = fd->fp_sys_posn = pvfs_lseek64(fd->fd_sys, 0, SEEK_END);

    if (fd->fd_sys != -1) {
	pvfs_ioctl(fd->fd_sys, GETMETA, &pstat);
	ADIOI_Snprintf(value, MPI_MAX_INFO_VAL+1, "%d", pstat.pcount);
	MPI_Info_set(fd->info, "striping_factor", value);
	ADIOI_Snprintf(value, MPI_MAX_INFO_VAL+1, "%d", pstat.ssize);
	MPI_Info_set(fd->info, "striping_unit", value);
	ADIOI_Snprintf(value, MPI_MAX_INFO_VAL+1, "%d", pstat.base);
	MPI_Info_set(fd->info, "start_iodevice", value);
    }

    ADIOI_Free(value);

    if (fd->fd_sys == -1) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", strerror(errno));
    }
    else *error_code = MPI_SUCCESS;
}
