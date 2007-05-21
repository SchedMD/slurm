/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_piofs.h"
#ifdef PROFILE
#include "mpe.h"
#endif

void ADIOI_PIOFS_Open(ADIO_File fd, int *error_code)
{
    int amode, perm, old_mask, err;
    piofs_fstat_t piofs_fstat;
    char *value;
#ifndef PRINT_ERR_MSG
    static char myname[] = "ADIOI_PIOFS_OPEN";
#endif

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
    if (fd->access_mode & ADIO_EXCL)
	amode = amode | O_EXCL;

#ifdef PROFILE
    MPE_Log_event(1, 0, "start open");
#endif
    fd->fd_sys = open(fd->filename, amode, perm);
    fd->fd_direct = -1;

#ifdef PROFILE
    MPE_Log_event(2, 0, "end open");
#endif

    llseek(fd->fd_sys, 0, SEEK_SET);
/* required to initiate use of 64-bit offset */

    if (fd->fd_sys != -1) {
	value = (char *) ADIOI_Malloc((MPI_MAX_INFO_VAL+1)*sizeof(char));

        /* get file striping information and set it in info */
	err = piofsioctl(fd->fd_sys, PIOFS_FSTAT, &piofs_fstat);

	if (!err) {
	    ADIOI_Snprintf(value, MPI_MAX_INFO_VAL+1, "%d", piofs_fstat.st_bsu);
	    MPI_Info_set(fd->info, "striping_unit", value);

	    ADIOI_Snprintf(value, MPI_MAX_INFO_VAL+1, "%d", piofs_fstat.st_cells);
	    MPI_Info_set(fd->info, "striping_factor", value);

	    ADIOI_Snprintf(value, MPI_MAX_INFO_VAL+1, "%d", piofs_fstat.st_base_node);
	    MPI_Info_set(fd->info, "start_iodevice", value);
	}
	ADIOI_Free(value);

	if (fd->access_mode & ADIO_APPEND)
	    fd->fp_ind = fd->fp_sys_posn = llseek(fd->fd_sys, 0, SEEK_END);
    }

    if (fd->fd_sys == -1) {
#ifdef MPICH2
	*error_code = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, myname, __LINE__, MPI_ERR_IO, "**io",
	    "**io %s", strerror(errno));
#elif defined(PRINT_ERR_MSG)
	*error_code =  MPI_ERR_UNKNOWN;
#else /* MPICH-1 */
	*error_code = MPIR_Err_setmsg(MPI_ERR_IO, MPIR_ADIO_ERROR,
			      myname, "I/O Error", "%s", strerror(errno));
	ADIOI_Error(ADIO_FILE_NULL, *error_code, myname);	    
#endif
    }
    else *error_code = MPI_SUCCESS;
}
