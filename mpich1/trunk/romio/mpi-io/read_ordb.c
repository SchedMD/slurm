/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_read_ordered_begin = PMPI_File_read_ordered_begin
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_read_ordered_begin MPI_File_read_ordered_begin
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_read_ordered_begin as PMPI_File_read_ordered_begin
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_read_ordered_begin - Begin a split collective read using shared file pointer

Input Parameters:
. fh - file handle (handle)
. count - number of elements in buffer (nonnegative integer)
. datatype - datatype of each buffer element (handle)

Output Parameters:
. buf - initial address of buffer (choice)

.N fortran
@*/
int MPI_File_read_ordered_begin(MPI_File mpi_fh, void *buf, int count, 
				MPI_Datatype datatype)
{
    int error_code, datatype_size, nprocs, myrank, incr;
    int source, dest;
    ADIO_Offset shared_fp;
    ADIO_File fh;
    static char myname[] = "MPI_FILE_READ_ORDERED_BEGIN";

    MPID_CS_ENTER();
    MPIR_Nest_incr();

    fh = MPIO_File_resolve(mpi_fh);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_FILE_HANDLE(fh, myname, error_code);
    MPIO_CHECK_COUNT(fh, count, myname, error_code);
    MPIO_CHECK_DATATYPE(fh, datatype, myname, error_code);

    if (fh->split_coll_count)
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_IO, 
					  "**iosplitcoll", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    fh->split_coll_count = 1;


    MPI_Type_size(datatype, &datatype_size);
    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_INTEGRAL_ETYPE(fh, count, datatype_size, myname, error_code);
    MPIO_CHECK_FS_SUPPORTS_SHARED(fh, myname, error_code);
    /* --END ERROR HANDLING-- */

    ADIOI_TEST_DEFERRED(fh, myname, &error_code);

    MPI_Comm_size(fh->comm, &nprocs);
    MPI_Comm_rank(fh->comm, &myrank);

    incr = (count*datatype_size)/fh->etype_size;
    /* Use a message as a 'token' to order the operations */
    source = myrank - 1;
    dest   = myrank + 1;
    if (source < 0) source = MPI_PROC_NULL;
    if (dest >= nprocs) dest = MPI_PROC_NULL;
    MPI_Recv(NULL, 0, MPI_BYTE, source, 0, fh->comm, MPI_STATUS_IGNORE);

    ADIO_Get_shared_fp(fh, incr, &shared_fp, &error_code);
    /* --BEGIN ERROR HANDLING-- */
    if (error_code != MPI_SUCCESS)
    {
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    MPI_Send(NULL, 0, MPI_BYTE, dest, 0, fh->comm);

    ADIO_ReadStridedColl(fh, buf, count, datatype, ADIO_EXPLICIT_OFFSET,
			 shared_fp, &fh->split_status, &error_code);

fn_exit:
    MPIR_Nest_decr();
    MPID_CS_EXIT();

    return error_code;
}
