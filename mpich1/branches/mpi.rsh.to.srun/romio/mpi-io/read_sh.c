/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_read_shared = PMPI_File_read_shared
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_read_shared MPI_File_read_shared
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_read_shared as PMPI_File_read_shared
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/* status object not filled currently */

/*@
    MPI_File_read_shared - Read using shared file pointer

Input Parameters:
. fh - file handle (handle)
. count - number of elements in buffer (nonnegative integer)
. datatype - datatype of each buffer element (handle)

Output Parameters:
. buf - initial address of buffer (choice)
. status - status object (Status)

.N fortran
@*/
int MPI_File_read_shared(MPI_File mpi_fh, void *buf, int count, 
			 MPI_Datatype datatype, MPI_Status *status)
{
    int error_code, bufsize, buftype_is_contig, filetype_is_contig;
    static char myname[] = "MPI_FILE_READ_SHARED";
    int datatype_size, incr;
    ADIO_Offset off, shared_fp;
    ADIO_File fh;

    MPID_CS_ENTER();
    MPIR_Nest_incr();

    fh = MPIO_File_resolve(mpi_fh);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_FILE_HANDLE(fh, myname, error_code);
    MPIO_CHECK_COUNT(fh, count, myname, error_code);
    MPIO_CHECK_DATATYPE(fh, datatype, myname, error_code);
    /* --END ERROR HANDLING-- */

    MPI_Type_size(datatype, &datatype_size);
    if (count*datatype_size == 0)
    {
#ifdef HAVE_STATUS_SET_BYTES
	MPIR_Status_set_bytes(status, datatype, 0);
#endif
	error_code = MPI_SUCCESS;
	goto fn_exit;
    }

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_INTEGRAL_ETYPE(fh, count, datatype_size, myname, error_code);
    MPIO_CHECK_READABLE(fh, myname, error_code);
    MPIO_CHECK_FS_SUPPORTS_SHARED(fh, myname, error_code);
    /* --END ERROR HANDLING-- */

    ADIOI_Datatype_iscontig(datatype, &buftype_is_contig);
    ADIOI_Datatype_iscontig(fh->filetype, &filetype_is_contig);

    ADIOI_TEST_DEFERRED(fh, myname, &error_code);

    incr = (count*datatype_size)/fh->etype_size;

    ADIO_Get_shared_fp(fh, incr, &shared_fp, &error_code);
    /* --BEGIN ERROR HANDLING-- */
    if (error_code != MPI_SUCCESS)
    {
        error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    /* contiguous or strided? */
    if (buftype_is_contig && filetype_is_contig)
    {
	/* convert count and shared_fp to bytes */
        bufsize = datatype_size * count;
        off = fh->disp + fh->etype_size * shared_fp;

        /* if atomic mode requested, lock (exclusive) the region, because there
           could be a concurrent noncontiguous request. On NFS, locking 
           is done in the ADIO_ReadContig.*/

        if ((fh->atomicity) && (fh->file_system != ADIO_NFS))
            ADIOI_WRITE_LOCK(fh, off, SEEK_SET, bufsize);

        ADIO_ReadContig(fh, buf, count, datatype, ADIO_EXPLICIT_OFFSET,
                        off, status, &error_code); 

        if ((fh->atomicity) && (fh->file_system != ADIO_NFS))
            ADIOI_UNLOCK(fh, off, SEEK_SET, bufsize);
    }
    else
    {
	ADIO_ReadStrided(fh, buf, count, datatype, ADIO_EXPLICIT_OFFSET,
                          shared_fp, status, &error_code);
	/* For strided and atomic mode, locking is done in ADIO_ReadStrided */
    }

fn_exit:
    MPID_CS_EXIT();
    MPIR_Nest_decr();

    return error_code;
}
