/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 2001 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_testfs.h"
#include "adioi.h"

void ADIOI_TESTFS_ReadContig(ADIO_File fd, void *buf, int count, 
			     MPI_Datatype datatype, int file_ptr_type,
			     ADIO_Offset offset, ADIO_Status *status, int
			     *error_code)
{
    int myrank, nprocs, datatype_size;

    *error_code = MPI_SUCCESS;

    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);
    MPI_Type_size(datatype, &datatype_size);
    FPRINTF(stdout, "[%d/%d] ADIOI_TESTFS_ReadContig called on %s\n", myrank, 
	    nprocs, fd->filename);
    if (file_ptr_type != ADIO_EXPLICIT_OFFSET)
    {
	offset = fd->fp_ind;
	fd->fp_ind += datatype_size * count;
	fd->fp_sys_posn = fd->fp_ind;
#if 0
	FPRINTF(stdout, "[%d/%d]    new file position is %lld\n", myrank, 
		nprocs, (long long) fd->fp_ind);
#endif
    }
    else {
	fd->fp_sys_posn = offset + datatype_size * count;
    }

    FPRINTF(stdout, "[%d/%d]    reading (buf = %p, loc = %lld, sz = %lld)\n",
	    myrank, nprocs, buf, (long long) offset, 
	    (long long) datatype_size * count);

#ifdef HAVE_STATUS_SET_BYTES
    MPIR_Status_set_bytes(status, datatype, datatype_size * count);
#endif
}

void ADIOI_TESTFS_ReadStrided(ADIO_File fd, void *buf, int count,
			      MPI_Datatype datatype, int file_ptr_type,
			      ADIO_Offset offset, ADIO_Status *status, int
			      *error_code)
{
    int myrank, nprocs;

    *error_code = MPI_SUCCESS;

    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);
    FPRINTF(stdout, "[%d/%d] ADIOI_TESTFS_ReadStrided called on %s\n", myrank, 
	    nprocs, fd->filename);
    FPRINTF(stdout, "[%d/%d]    calling ADIOI_GEN_ReadStrided\n", myrank, 
	    nprocs);

    ADIOI_GEN_ReadStrided(fd, buf, count, datatype, file_ptr_type, offset,
			  status, error_code);
}
