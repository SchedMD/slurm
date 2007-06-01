/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_xfs.h"
#include "adio_extern.h"

void ADIOI_XFS_SetInfo(ADIO_File fd, MPI_Info users_info, int *error_code)
{
    char *value;
    int flag;

    if (fd->info == MPI_INFO_NULL) MPI_Info_create(&(fd->info));

    /* the nightly builds say somthing is calling MPI_Info_set w/ a null info,
     * so protect the calls to MPI_Info_set */
    if (fd->info != MPI_INFO_NULL ) {
	    MPI_Info_set(fd->info, "direct_read", "false");
	    MPI_Info_set(fd->info, "direct_write", "false");
	    fd->direct_read = fd->direct_write = 0;
    }
	
    /* has user specified values for keys "direct_read" and "direct wirte"? */
    if (users_info != MPI_INFO_NULL) {
	value = (char *) ADIOI_Malloc((MPI_MAX_INFO_VAL+1)*sizeof(char));

	MPI_Info_get(users_info, "direct_read", MPI_MAX_INFO_VAL, 
			 value, &flag);
	if (flag && !strcmp(value, "true")) {
	    MPI_Info_set(fd->info, "direct_read", "true");
	    fd->direct_read = 1;
	}

	MPI_Info_get(users_info, "direct_write", MPI_MAX_INFO_VAL, 
			 value, &flag);
	if (flag && !strcmp(value, "true")) {
	    MPI_Info_set(fd->info, "direct_write", "true");
	    fd->direct_write = 1;
	}

	ADIOI_Free(value);
    }
    
    /* set the values for collective I/O and data sieving parameters */
    ADIOI_GEN_SetInfo(fd, users_info, error_code);

    if (ADIOI_Direct_read) fd->direct_read = 1;
    if (ADIOI_Direct_write) fd->direct_write = 1;
    /* environment variables checked in ADIO_Init */

    *error_code = MPI_SUCCESS;
}
