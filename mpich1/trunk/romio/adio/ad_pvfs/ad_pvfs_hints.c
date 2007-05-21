/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pvfs.h"

void ADIOI_PVFS_SetInfo(ADIO_File fd, MPI_Info users_info, int *error_code)
{
    char *value;
    int flag, tmp_val, str_factor=-1, str_unit=-1, start_iodev=-1; 
    static char myname[] = "ADIOI_PVFS_SETINFO";

    if ((fd->info) == MPI_INFO_NULL) {
	/* This must be part of the open call. can set striping parameters 
           if necessary. */ 
	MPI_Info_create(&(fd->info));
	MPI_Info_set(fd->info, "romio_pvfs_listio_read", "disable");
	MPI_Info_set(fd->info, "romio_pvfs_listio_write", "disable");
	fd->hints->fs_hints.pvfs.listio_read = ADIOI_HINT_DISABLE;
	fd->hints->fs_hints.pvfs.listio_write = ADIOI_HINT_DISABLE;
	
	/* has user specified any pvfs-specific hints (striping params, listio)
           and do they have the same value on all processes? */
	if (users_info != MPI_INFO_NULL) {
	    value = (char *) ADIOI_Malloc((MPI_MAX_INFO_VAL+1)*sizeof(char));

	    MPI_Info_get(users_info, "striping_factor", MPI_MAX_INFO_VAL, 
			 value, &flag);
	    if (flag) {
		str_factor=atoi(value);
		tmp_val = str_factor;
		MPI_Bcast(&tmp_val, 1, MPI_INT, 0, fd->comm);
		if (tmp_val != str_factor) {
		    /* --BEGIN ERROR HANDLING-- */
		    MPIO_ERR_CREATE_CODE_INFO_NOT_SAME(myname,
						       "striping_factor",
						       error_code);
		    return;
		    /* --END ERROR HANDLING-- */
		}
		else MPI_Info_set(fd->info, "striping_factor", value);
	    }

	    MPI_Info_get(users_info, "striping_unit", MPI_MAX_INFO_VAL, 
			 value, &flag);
	    if (flag) {
		str_unit=atoi(value);
		tmp_val = str_unit;
		MPI_Bcast(&tmp_val, 1, MPI_INT, 0, fd->comm);
		if (tmp_val != str_unit) {
		    /* --BEGIN ERROR HANDLING-- */
		    MPIO_ERR_CREATE_CODE_INFO_NOT_SAME(myname,
						       "striping_unit",
						       error_code);
		    return;
		    /* --END ERROR HANDLING-- */
		}
		else MPI_Info_set(fd->info, "striping_unit", value);
	    }

	    MPI_Info_get(users_info, "start_iodevice", MPI_MAX_INFO_VAL, 
			 value, &flag);
	    if (flag) {
		start_iodev=atoi(value);
		tmp_val = start_iodev;
		MPI_Bcast(&tmp_val, 1, MPI_INT, 0, fd->comm);
		if (tmp_val != start_iodev) {
		    /* --BEGIN ERROR HANDLING-- */
		    MPIO_ERR_CREATE_CODE_INFO_NOT_SAME(myname,
						       "start_iodevice",
						       error_code);
		    return;
		    /* --END ERROR HANDLING-- */
		}
		else MPI_Info_set(fd->info, "start_iodevice", value);
	    }

	    MPI_Info_get(users_info, "romio_pvfs_listio_read",
			 MPI_MAX_INFO_VAL,
			 value, &flag);
	    if (flag) {
		if ( !strcmp(value, "enable") || !strcmp(value, "ENABLE")) 
		{
		    MPI_Info_set(fd->info, "romio_pvfs_listio_read", value);
		    fd->hints->fs_hints.pvfs.listio_read = ADIOI_HINT_ENABLE;
		} else if ( !strcmp(value, "disable") || !strcmp(value, "DISABLE")) 
		{
		    MPI_Info_set(fd->info , "romio_pvfs_listio_read", value);
		    fd->hints->fs_hints.pvfs.listio_read = ADIOI_HINT_DISABLE;
		}
		else if ( !strcmp(value, "automatic") || !strcmp(value, "AUTOMATIC")) 
		{
		    MPI_Info_set(fd->info, "romio_pvfs_listio_read", value);
		    fd->hints->fs_hints.pvfs.listio_read = ADIOI_HINT_AUTO;
		}
		tmp_val = fd->hints->fs_hints.pvfs.listio_read;
		MPI_Bcast(&tmp_val, 1, MPI_INT, 0, fd->comm);
		if (tmp_val != fd->hints->fs_hints.pvfs.listio_read) {
		    /* --BEGIN ERROR HANDLING-- */
		    MPIO_ERR_CREATE_CODE_INFO_NOT_SAME(myname,
						       "romio_pvfs_listio_read",
						       error_code);
		    return;
		    /* --END ERROR HANDLING-- */
		}
	    }
	    MPI_Info_get(users_info, "romio_pvfs_listio_write", MPI_MAX_INFO_VAL,
			 value, &flag);
	    if (flag) {
		if ( !strcmp(value, "enable") || !strcmp(value, "ENABLE")) 
		{
		    MPI_Info_set(fd->info, "romio_pvfs_listio_write", value);
		    fd->hints->fs_hints.pvfs.listio_write = ADIOI_HINT_ENABLE;
		} else if ( !strcmp(value, "disable") || !strcmp(value, "DISABLE")) 
		{
		    MPI_Info_set(fd->info, "romio_pvfs_listio_write", value);
		    fd->hints->fs_hints.pvfs.listio_write = ADIOI_HINT_DISABLE;
		}
		else if ( !strcmp(value, "automatic") || !strcmp(value, "AUTOMATIC")) 
		{
		    MPI_Info_set(fd->info, "romio_pvfs_listio_write", value);
		    fd->hints->fs_hints.pvfs.listio_write = ADIOI_HINT_AUTO;
		}
		tmp_val = fd->hints->fs_hints.pvfs.listio_write;
		MPI_Bcast(&tmp_val, 1, MPI_INT, 0, fd->comm);
		if (tmp_val != fd->hints->fs_hints.pvfs.listio_write) {
		    /* --BEGIN ERROR HANDLING-- */
		    MPIO_ERR_CREATE_CODE_INFO_NOT_SAME(myname,
						       "romio_pvfs_listio_write",
						       error_code);
		    return;
		    /* --END ERROR HANDLING-- */
		}
	    }		    
	    ADIOI_Free(value);
	}
    }	

    /* set the values for collective I/O and data sieving parameters */
    ADIOI_GEN_SetInfo(fd, users_info, error_code);

    *error_code = MPI_SUCCESS;
}
