/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_iwrite = PMPI_File_iwrite
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_iwrite MPI_File_iwrite
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_iwrite as PMPI_File_iwrite
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_iwrite - Nonblocking write using individual file pointer

Input Parameters:
. fh - file handle (handle)
. buf - initial address of buffer (choice)
. count - number of elements in buffer (nonnegative integer)
. datatype - datatype of each buffer element (handle)

Output Parameters:
. request - request object (handle)

.N fortran
@*/
#ifdef HAVE_MPI_GREQUEST
#include "mpiu_greq.h"

#if defined(HAVE_WINDOWS_H) && defined(USE_WIN_THREADED_IO)
typedef struct iwrite_args
{
    MPI_File file;
    void *buf;
    int count;
    MPI_Datatype datatype;
    MPIO_Request request;
    MPI_Status *status;
} iwrite_args;

static DWORD WINAPI iwrite_thread(LPVOID lpParameter)
{
    int error_code;
    iwrite_args *args = (iwrite_args *)lpParameter;

    error_code = MPI_File_write(args->file, args->buf, args->count, args->datatype, args->status);
    /* ROMIO-1 doesn't do anything with status.MPI_ERROR */
    args->status->MPI_ERROR = error_code;

    MPI_Grequest_complete(args->request);
    ADIOI_Free(args);
    return 0;
}
#endif

int MPI_File_iwrite(MPI_File mpi_fh, void *buf, int count, 
		    MPI_Datatype datatype, MPIO_Request *request)
{
    int error_code;
    MPI_Status *status;
#if defined(HAVE_WINDOWS_H) && defined(USE_WIN_THREADED_IO)
    iwrite_args *args;
    HANDLE hThread;
#endif

    MPID_CS_ENTER();
    MPIR_Nest_incr();

    status = (MPI_Status *) ADIOI_Malloc(sizeof(MPI_Status));

#if defined(HAVE_WINDOWS_H) && defined(USE_WIN_THREADED_IO)
    /* kick off the request */
    MPI_Grequest_start(MPIU_Greq_query_fn, MPIU_Greq_free_fn, 
	MPIU_Greq_cancel_fn, status, request);

    args = (iwrite_args*) ADIOI_Malloc(sizeof(iwrite_args));
    args->file = mpi_fh;
    args->buf = buf;
    args->count = count;
    args->datatype = datatype;
    args->status = status;
    args->request = *request;
    hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)iwrite_thread, args, 0, NULL);
    if (hThread == NULL)
    {
	error_code = GetLastError();
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
	    "MPI_File_iwrite", __LINE__, MPI_ERR_OTHER,
	    "**fail", "**fail %d", error_code);
	error_code = MPIO_Err_return_file(args->file, error_code);
	return error_code;
    }
    CloseHandle(hThread);

#else

    /* for now, no threads or anything fancy. 
    * just call the blocking version */
    error_code = MPI_File_write(mpi_fh, buf, count, datatype, status); 
    /* ROMIO-1 doesn't do anything with status.MPI_ERROR */
    status->MPI_ERROR = error_code;

    /* kick off the request */
    MPI_Grequest_start(MPIU_Greq_query_fn, MPIU_Greq_free_fn, 
	MPIU_Greq_cancel_fn, status, request);

    /* but we did all the work already */
    MPI_Grequest_complete(*request);
#endif

    /* FIXME: Shouldn't status be freed before leaving this function */

    MPID_CS_EXIT();
    MPIR_Nest_decr();

    /* passed the buck to the blocking version...*/
    return MPI_SUCCESS;
}
#else
int MPI_File_iwrite(MPI_File mpi_fh, void *buf, int count, 
		    MPI_Datatype datatype, MPIO_Request *request)
{
    int error_code;
    static char myname[] = "MPI_FILE_IWRITE";

#ifdef MPI_hpux
    int fl_xmpi;

    HPMP_IO_START(fl_xmpi, BLKMPIFILEIWRITE, TRDTSYSTEM, mpi_fh, datatype,
		  count);
#endif /* MPI_hpux */

    error_code = MPIOI_File_iwrite(mpi_fh, (MPI_Offset) 0, ADIO_INDIVIDUAL,
				   buf, count, datatype, myname, request);

#ifdef MPI_hpux
    HPMP_IO_END(fl_xmpi, mpi_fh, datatype, count);
#endif /* MPI_hpux */

    return error_code;
}
#endif


#ifndef HAVE_MPI_GREQUEST
/* prevent multiple definitions of this routine */
#ifdef MPIO_BUILD_PROFILING
int MPIOI_File_iwrite(MPI_File mpi_fh,
		      MPI_Offset offset,
		      int file_ptr_type,
		      void *buf,
		      int count,
		      MPI_Datatype datatype,
		      char *myname,
		      MPIO_Request *request)
{
    int error_code, bufsize, buftype_is_contig, filetype_is_contig;
    int datatype_size;
    ADIO_Status status;
    ADIO_Offset off;
    ADIO_File fh;

    MPID_CS_ENTER();
    MPIR_Nest_incr();

    fh = MPIO_File_resolve(mpi_fh);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_FILE_HANDLE(fh, myname, error_code);
    MPIO_CHECK_COUNT(fh, count, myname, error_code);
    MPIO_CHECK_DATATYPE(fh, datatype, myname, error_code);

    if (file_ptr_type == ADIO_EXPLICIT_OFFSET && offset < 0) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "**iobadoffset", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    MPI_Type_size(datatype, &datatype_size);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_INTEGRAL_ETYPE(fh, count, datatype_size, myname, error_code);
    MPIO_CHECK_WRITABLE(fh, myname, error_code);
    MPIO_CHECK_NOT_SEQUENTIAL_MODE(fh, myname, error_code);
    /* --END ERROR HANDLING-- */

    ADIOI_Datatype_iscontig(datatype, &buftype_is_contig);
    ADIOI_Datatype_iscontig(fh->filetype, &filetype_is_contig);
    
    ADIOI_TEST_DEFERRED(fh, myname, &error_code);

    if (buftype_is_contig && filetype_is_contig) {
	/* convert sizes to bytes */
	bufsize = datatype_size * count;
	if (file_ptr_type == ADIO_EXPLICIT_OFFSET) {
	    off = fh->disp + fh->etype_size * offset;
	}
	else {
	    off = fh->fp_ind;
	}

        if (!(fh->atomicity)) {
	    ADIO_IwriteContig(fh, buf, count, datatype, file_ptr_type,
			      off, request, &error_code);
	}
	else {
            /* to maintain strict atomicity semantics with other concurrent
              operations, lock (exclusive) and call blocking routine */

            *request = ADIOI_Malloc_request();
            (*request)->optype = ADIOI_WRITE;
            (*request)->fd = fh;
            (*request)->datatype = datatype;
            (*request)->queued = 0;
	    (*request)->handle = 0;

            if ((fh->file_system != ADIO_PIOFS) && 
              (fh->file_system != ADIO_NFS) && (fh->file_system != ADIO_PVFS)
	      && (fh->file_system != ADIO_PVFS2))
	    {
                ADIOI_WRITE_LOCK(fh, off, SEEK_SET, bufsize);
	    }

            ADIO_WriteContig(fh, buf, count, datatype, file_ptr_type, off, 
			     &status, &error_code);  

            if ((fh->file_system != ADIO_PIOFS) && 
               (fh->file_system != ADIO_NFS) && (fh->file_system != ADIO_PVFS)
	       && (fh->file_system != ADIO_PVFS2))
	    {
                ADIOI_UNLOCK(fh, off, SEEK_SET, bufsize);
	    }

            fh->async_count++;
            /* status info. must be linked to the request structure, so that it
               can be accessed later from a wait */

	}
    }
    else {
	ADIO_IwriteStrided(fh, buf, count, datatype, file_ptr_type,
			   offset, request, &error_code);
    }
fn_exit:
    MPID_CS_EXIT();
    MPIR_Nest_decr();

    return error_code;
}
#endif
#endif
