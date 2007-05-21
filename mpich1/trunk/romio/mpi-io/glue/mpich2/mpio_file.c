/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2004 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#include "adio_extern.h"

/* Hooks for allocation of MPI_File handles.
 *
 * Three functions are used in ROMIO for allocation/deallocation
 * of MPI_File structures:
 * - MPIO_File_create(size)
 * - MPIO_File_resolve(mpi_fh)
 * - MPIO_File_free(mpi_fh)
 *
 */

MPI_File MPIO_File_create(int size)
{
    MPI_File mpi_fh;

    mpi_fh = (MPI_File) ADIOI_Malloc(size);
    return mpi_fh;
}

ADIO_File MPIO_File_resolve(MPI_File mpi_fh)
{
    return mpi_fh;
}

void MPIO_File_free(MPI_File *mpi_fh)
{
    ADIOI_Free(*mpi_fh);
    *mpi_fh = MPI_FILE_NULL;
}

MPI_File MPIO_File_f2c(MPI_Fint fh)
{
#ifndef INT_LT_POINTER
    return (MPI_File) ((void *) fh);  
    /* the extra cast is to get rid of a compiler warning on Exemplar.
       The warning is because MPI_File points to a structure containing
       longlongs, which may be 8-byte aligned. But MPI_Fint itself
       may not be 8-byte aligned.*/
#else
    if (!fh) return MPI_FILE_NULL;
    if ((fh < 0) || (fh > ADIOI_Ftable_ptr)) {
	/* there is no way to return an error from MPI_File_f2c */
	return MPI_FILE_NULL;
    }
    return ADIOI_Ftable[fh];
#endif
}

MPI_Fint MPIO_File_c2f(MPI_File fh)
{
#ifndef INT_LT_POINTER
    return (MPI_Fint) fh;
#else
    int i;

    if ((fh <= (MPI_File) 0) || (fh->cookie != ADIOI_FILE_COOKIE))
	return (MPI_Fint) 0;
    if (!ADIOI_Ftable) {
	ADIOI_Ftable_max = 1024;
	ADIOI_Ftable = (MPI_File *)
	    ADIOI_Malloc(ADIOI_Ftable_max*sizeof(MPI_File)); 
        ADIOI_Ftable_ptr = 0;  /* 0 can't be used though, because 
                                  MPI_FILE_NULL=0 */
	for (i=0; i<ADIOI_Ftable_max; i++) ADIOI_Ftable[i] = MPI_FILE_NULL;
    }
    if (ADIOI_Ftable_ptr == ADIOI_Ftable_max-1) {
	ADIOI_Ftable = (MPI_File *) ADIOI_Realloc(ADIOI_Ftable, 
                           (ADIOI_Ftable_max+1024)*sizeof(MPI_File));
	for (i=ADIOI_Ftable_max; i<ADIOI_Ftable_max+1024; i++) 
	    ADIOI_Ftable[i] = MPI_FILE_NULL;
	ADIOI_Ftable_max += 1024;
    }
    ADIOI_Ftable_ptr++;
    ADIOI_Ftable[ADIOI_Ftable_ptr] = fh;
    return (MPI_Fint) ADIOI_Ftable_ptr;
#endif
}
