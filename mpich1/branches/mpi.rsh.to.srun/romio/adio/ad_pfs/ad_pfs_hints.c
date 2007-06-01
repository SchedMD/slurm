/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pfs.h"

void ADIOI_PFS_SetInfo(ADIO_File fd, MPI_Info users_info, int *error_code)
{
    char *value, *value_in_fd;
    int flag, tmp_val, str_factor=-1, str_unit=-1, start_iodev=-1;
    struct sattr attr;
    int err, myrank, fd_sys, perm, amode, old_mask;

    if ( (fd->info) == MPI_INFO_NULL) {
	/* This must be part of the open call. can set striping parameters 
           if necessary. */ 
	MPI_Info_create(&(fd->info));
	
	/* has user specified striping or server buffering parameters 
           and do they have the same value on all processes? */
	if (users_info != MPI_INFO_NULL) {
	    value = (char *) ADIOI_Malloc((MPI_MAX_INFO_VAL+1)*sizeof(char));

	    MPI_Info_get(users_info, "striping_factor", MPI_MAX_INFO_VAL, 
			 value, &flag);
	    if (flag) {
		str_factor=atoi(value);
		tmp_val = str_factor;
		MPI_Bcast(&tmp_val, 1, MPI_INT, 0, fd->comm);
		/* --BEGIN ERROR HANDLING-- */
		if (tmp_val != str_factor) {
		    MPIO_ERR_CREATE_CODE_INFO_NOT_SAME(myname,
						       "striping_factor",
						       error_code);
		    return;
		}
		/* --END ERROR HANDLING-- */
	    }

	    MPI_Info_get(users_info, "striping_unit", MPI_MAX_INFO_VAL, 
			 value, &flag);
	    if (flag) {
		str_unit=atoi(value);
		tmp_val = str_unit;
		MPI_Bcast(&tmp_val, 1, MPI_INT, 0, fd->comm);
		/* --BEGIN ERROR HANDLING-- */
		if (tmp_val != str_unit) {
		    MPIO_ERR_CREATE_CODE_INFO_NOT_SAME(myname,
						       "striping_unit",
						       error_code);
		    return;
		}
		/* --END ERROR HANDLING-- */
	    }

	    MPI_Info_get(users_info, "start_iodevice", MPI_MAX_INFO_VAL, 
			 value, &flag);
	    if (flag) {
		start_iodev=atoi(value);
		tmp_val = start_iodev;
		MPI_Bcast(&tmp_val, 1, MPI_INT, 0, fd->comm);
		/* --BEGIN ERROR HANDLING-- */
		if (tmp_val != start_iodev) {
		    MPIO_ERR_CREATE_CODE_INFO_NOT_SAME(myname,
						       "start_iodevice",
						       error_code);
		    return;
		}
		/* --END ERROR HANDLING-- */
	    }

         /* if user has specified striping info, process 0 tries to set it */
	    if ((str_factor > 0) || (str_unit > 0) || (start_iodev >= 0)) {
		MPI_Comm_rank(fd->comm, &myrank);
		if (!myrank) {
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

		    fd_sys = open(fd->filename, amode, perm);
		    err = fcntl(fd_sys, F_GETSATTR, &attr);

		    if (!err) {
			if (str_unit > 0) attr.s_sunitsize = str_unit;
			if ((start_iodev >= 0) && 
			    (start_iodev < attr.s_sfactor))
			    attr.s_start_sdir = start_iodev;
			if ((str_factor > 0) && (str_factor < attr.s_sfactor))
			    attr.s_sfactor = str_factor;

			err = fcntl(fd_sys, F_SETSATTR, &attr);
		    }

		    close(fd_sys);
		}

		MPI_Barrier(fd->comm);
	    }

	    /* Has user asked for pfs server buffering to be turned on?
	       If so, mark it as true in fd->info and turn it on in 
	       ADIOI_PFS_Open after the file is opened */

	    MPI_Info_get(users_info, "pfs_svr_buf", MPI_MAX_INFO_VAL, 
			 value, &flag);
	    if (flag && (!strcmp(value, "true")))
		MPI_Info_set(fd->info, "pfs_svr_buf", "true");
	    else MPI_Info_set(fd->info, "pfs_svr_buf", "false");

	    ADIOI_Free(value);
	}
	else MPI_Info_set(fd->info, "pfs_svr_buf", "false");
	
	/* set the values for collective I/O and data sieving parameters */
	ADIOI_GEN_SetInfo(fd, users_info, error_code);
    }
    
    else {
	/* The file has been opened previously and fd->fd_sys is a valid
           file descriptor. cannot set striping parameters now. */
	
	/* set the values for collective I/O and data sieving parameters */
	ADIOI_GEN_SetInfo(fd, users_info, error_code);

	/* has user specified value for pfs_svr_buf? */
	if (users_info != MPI_INFO_NULL) {
	    value = (char *) ADIOI_Malloc((MPI_MAX_INFO_VAL+1)*sizeof(char));

	    MPI_Info_get(users_info, "pfs_svr_buf", MPI_MAX_INFO_VAL, 
			 value, &flag);
	    if (flag && (!strcmp(value, "true") || !strcmp(value, "false"))) {
		value_in_fd = (char *) 
                          ADIOI_Malloc((MPI_MAX_INFO_VAL+1)*sizeof(char));
		MPI_Info_get(fd->info, "pfs_svr_buf", MPI_MAX_INFO_VAL, 
			 value_in_fd, &flag);
		if (strcmp(value, value_in_fd)) {
		    if (!strcmp(value, "true")) {
			err = fcntl(fd->fd_sys, F_PFS_SVR_BUF, TRUE);
			if (!err) 
			    MPI_Info_set(fd->info, "pfs_svr_buf", "true");
		    }
		    else {
			err = fcntl(fd->fd_sys, F_PFS_SVR_BUF, FALSE);
			if (!err) 
			    MPI_Info_set(fd->info, "pfs_svr_buf", "false");
		    }
		}
		ADIOI_Free(value_in_fd);
	    }
	    ADIOI_Free(value);
	}

    }
    
    *error_code = MPI_SUCCESS;
}
