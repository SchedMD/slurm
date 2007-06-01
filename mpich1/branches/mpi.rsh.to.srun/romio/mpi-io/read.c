/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_read = PMPI_File_read
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_read MPI_File_read
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_read as PMPI_File_read
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/* status object not filled currently */

/*@
    MPI_File_read - Read using individual file pointer

Input Parameters:
. fh - file handle (handle)
. count - number of elements in buffer (nonnegative integer)
. datatype - datatype of each buffer element (handle)

Output Parameters:
. buf - initial address of buffer (choice)
. status - status object (Status)

.N fortran
@*/
int MPI_File_read(MPI_File mpi_fh, void *buf, int count, 
                  MPI_Datatype datatype, MPI_Status *status)
{
    int error_code;
    static char myname[] = "MPI_FILE_READ";
#ifdef MPI_hpux
    int fl_xmpi;

    HPMP_IO_START(fl_xmpi, BLKMPIFILEREAD, TRDTBLOCK, mpi_fh, datatype, count);
#endif /* MPI_hpux */

    error_code = MPIOI_File_read(mpi_fh, (MPI_Offset) 0, ADIO_INDIVIDUAL, buf,
				 count, datatype, myname, status);

#ifdef MPI_hpux
    HPMP_IO_END(fl_xmpi, mpi_fh, datatype, count);
#endif /* MPI_hpux */

    return error_code;
}

/* prevent multiple definitions of this routine */
#ifdef MPIO_BUILD_PROFILING
int MPIOI_File_read(MPI_File mpi_fh,
		    MPI_Offset offset,
		    int file_ptr_type,
		    void *buf,
		    int count,
		    MPI_Datatype datatype,
		    char *myname,
		    MPI_Status *status)
{
    int error_code, bufsize, buftype_is_contig, filetype_is_contig;
    int datatype_size;
    ADIO_File fh;
    ADIO_Offset off;

    MPID_CS_ENTER();
    MPIR_Nest_incr();

    fh = MPIO_File_resolve(mpi_fh);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_FILE_HANDLE(fh, myname, error_code);
    MPIO_CHECK_COUNT(fh, count, myname, error_code);
    MPIO_CHECK_DATATYPE(fh, datatype, myname, error_code);

    if (file_ptr_type == ADIO_EXPLICIT_OFFSET && offset < 0)
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "**iobadoffset", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }
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
    MPIO_CHECK_NOT_SEQUENTIAL_MODE(fh, myname, error_code);
    /* --END ERROR HANDLING-- */

    ADIOI_Datatype_iscontig(datatype, &buftype_is_contig);
    ADIOI_Datatype_iscontig(fh->filetype, &filetype_is_contig);

    ADIOI_TEST_DEFERRED(fh, myname, &error_code);

    if (buftype_is_contig && filetype_is_contig)
    {
    /* convert count and offset to bytes */
	bufsize = datatype_size * count;
	if (file_ptr_type == ADIO_EXPLICIT_OFFSET) {
	    off = fh->disp + fh->etype_size * offset;
	}
	else /* ADIO_INDIVIDUAL */ {
	    off = fh->fp_ind;
	}

        /* if atomic mode requested, lock (exclusive) the region, because
           there could be a concurrent noncontiguous request. Locking doesn't
           work on PIOFS and PVFS, and on NFS it is done in the
           ADIO_ReadContig.
	 */

        if ((fh->atomicity) && (fh->file_system != ADIO_PIOFS) && 
            (fh->file_system != ADIO_NFS) && (fh->file_system != ADIO_PVFS) && 
	   	 (fh->file_system != ADIO_PVFS2))
            ADIOI_WRITE_LOCK(fh, off, SEEK_SET, bufsize);

	ADIO_ReadContig(fh, buf, count, datatype, file_ptr_type,
			off, status, &error_code); 

        if ((fh->atomicity) && (fh->file_system != ADIO_PIOFS) && 
            (fh->file_system != ADIO_NFS) && (fh->file_system != ADIO_PVFS) &&
	    	(fh->file_system != ADIO_PVFS2))
            ADIOI_UNLOCK(fh, off, SEEK_SET, bufsize);
    }
    else
    {
	ADIO_ReadStrided(fh, buf, count, datatype, file_ptr_type,
			  offset, status, &error_code);
	/* For strided and atomic mode, locking is done in ADIO_ReadStrided */
    }

fn_exit:
    MPIR_Nest_decr();
    MPID_CS_EXIT();

    return error_code;
}
#endif
