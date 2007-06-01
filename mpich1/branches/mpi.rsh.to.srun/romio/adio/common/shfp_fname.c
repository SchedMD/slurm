/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"

/* The following function selects the name of the file to be used to 
   store the shared file pointer. The shared-file-pointer file is a 
   hidden file in the same directory as the real file being accessed.
   If the real file is /tmp/thakur/testfile, the shared-file-pointer
   file will be /tmp/thakur/.testfile.shfp.xxxx, where xxxx is
   a random number. This file is created only if the shared
   file pointer functions are used and is deleted when the real
   file is closed. */

void ADIOI_Shfp_fname(ADIO_File fd, int rank)
{
    double tm;
    int i, len;
    char *slash, *ptr, tmp[128];

    fd->shared_fp_fname = (char *) ADIOI_Malloc(256);

    if (!rank) {
	tm = MPI_Wtime();
	while (tm > 1000000000.0) tm -= 1000000000.0;
	i = (int) tm;
	tm = tm - (double) i;
	tm *= 1000000.0;
	i = (int) tm;
	
	ADIOI_Strncpy(fd->shared_fp_fname, fd->filename, 256);
	
#ifdef ROMIO_NTFS
	slash = strrchr(fd->filename, '\\');
#else
	slash = strrchr(fd->filename, '/');
#endif
	if (!slash) {
	    ADIOI_Strncpy(fd->shared_fp_fname, ".", 2);
	    ADIOI_Strncpy(fd->shared_fp_fname + 1, fd->filename, 255);
	}
	else {
	    ptr = slash;
#ifdef ROMIO_NTFS
		slash = strrchr(fd->shared_fp_fname, '\\');
#else
	    slash = strrchr(fd->shared_fp_fname, '/');
#endif
	    ADIOI_Strncpy(slash + 1, ".", 2);
	    len = 256 - (slash+2 - fd->shared_fp_fname);
	    ADIOI_Strncpy(slash + 2, ptr + 1, len);
	}
	    
	ADIOI_Snprintf(tmp, 128, ".shfp.%d", i);
	ADIOI_Strnapp(fd->shared_fp_fname, tmp, 256);
	
	len = (int)strlen(fd->shared_fp_fname);
	MPI_Bcast(&len, 1, MPI_INT, 0, fd->comm);
	MPI_Bcast(fd->shared_fp_fname, len+1, MPI_CHAR, 0, fd->comm);
    }
    else {
	MPI_Bcast(&len, 1, MPI_INT, 0, fd->comm);
	MPI_Bcast(fd->shared_fp_fname, len+1, MPI_CHAR, 0, fd->comm);
    }
}
