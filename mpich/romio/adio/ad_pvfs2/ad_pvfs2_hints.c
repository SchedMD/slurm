/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include <stdlib.h>
#include "ad_pvfs2.h"

void ADIOI_PVFS2_SetInfo(ADIO_File fd, MPI_Info users_info, int *error_code)
{
    char *value;
    int flag, tmp_value;
    static char myname[] = "ADIOI_PVFS_SETINFO";

    if ((fd->info) == MPI_INFO_NULL) {
	/* part of the open call */
	MPI_Info_create(&(fd->info));
	MPI_Info_set(fd->info, "romio_pvfs2_debugmask", "0");
	fd->hints->fs_hints.pvfs2.debugmask = 0;
	
	/* any user-provided hints? */
	if (users_info != MPI_INFO_NULL) {
	    /* pvfs2 debugging */
	    value = (char *) ADIOI_Malloc( (MPI_MAX_INFO_VAL+1)*sizeof(char));
	    MPI_Info_get(users_info, "romio_pvfs2_debugmask", 
		    MPI_MAX_INFO_VAL, value, &flag);
	    if (flag) {
		tmp_value = fd->hints->fs_hints.pvfs2.debugmask = 
		    PVFS_debug_eventlog_to_mask(value);

		MPI_Bcast(&tmp_value, 1, MPI_INT, 0, fd->comm);
		/* --BEGIN ERROR HANDLING-- */
		if (tmp_value != fd->hints->fs_hints.pvfs2.debugmask) {
		    MPIO_ERR_CREATE_CODE_INFO_NOT_SAME(myname,
						       "romio_pvfs2_debugmask",
						       error_code);
		    return;
		}
		/* --END ERROR HANDLING-- */
		
		MPI_Info_set(fd->info, "romio_pvfs2_debugmask", value);
	    }

	    /* the striping factor */
	    MPI_Info_get(users_info, "striping_factor", 
		    MPI_MAX_INFO_VAL, value, &flag);
	    if (flag) {
		tmp_value = fd->hints->striping_factor =  atoi(value);

		MPI_Bcast(&tmp_value, 1, MPI_INT, 0, fd->comm);
		/* --BEGIN ERROR HANDLING-- */
		if (tmp_value != fd->hints->striping_factor) {
		    MPIO_ERR_CREATE_CODE_INFO_NOT_SAME(myname,
						       "striping_factor",
						       error_code);
		    return;
		}
		/* --END ERROR HANDLING-- */
		
		MPI_Info_set(fd->info, "striping_factor", value);
	    }

            ADIOI_Free(value);
	}
    }
    /* set the values for collective I/O and data sieving parameters */
    ADIOI_GEN_SetInfo(fd, users_info, error_code);

    *error_code = MPI_SUCCESS;
}

/*
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
