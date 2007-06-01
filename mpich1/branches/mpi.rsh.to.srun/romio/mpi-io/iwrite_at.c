/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_iwrite_at = PMPI_File_iwrite_at
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_iwrite_at MPI_File_iwrite_at
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_iwrite_at as PMPI_File_iwrite_at
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_iwrite_at - Nonblocking write using explict offset

Input Parameters:
. fh - file handle (handle)
. offset - file offset (nonnegative integer)
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
typedef struct iwrite_at_args
{
    MPI_File file;
    MPI_Offset offset;
    void *buf;
    int count;
    MPI_Datatype datatype;
    MPIO_Request request;
    MPI_Status *status;
} iwrite_at_args;

static DWORD WINAPI iwrite_at_thread(LPVOID lpParameter)
{
    int error_code;
    iwrite_at_args *args = (iwrite_at_args *)lpParameter;

    error_code = MPI_File_write_at(args->file, args->offset, args->buf, args->count, args->datatype, args->status); 
    /* ROMIO-1 doesn't do anything with status.MPI_ERROR */
    args->status->MPI_ERROR = error_code;

    MPI_Grequest_complete(args->request);
    ADIOI_Free(args);
    return 0;
}
#endif

int MPI_File_iwrite_at(MPI_File mpi_fh, MPI_Offset offset, void *buf,
                       int count, MPI_Datatype datatype, 
                       MPIO_Request *request)
{
    int error_code;
    MPI_Status *status;
#if defined(HAVE_WINDOWS_H) && defined(USE_WIN_THREADED_IO)
    iwrite_at_args *args;
    HANDLE hThread;
#endif

    MPID_CS_ENTER();
    MPIR_Nest_incr();

    status = (MPI_Status *) ADIOI_Malloc(sizeof(MPI_Status));

#if defined(HAVE_WINDOWS_H) && defined(USE_WIN_THREADED_IO)
    /* kick off the request */
    MPI_Grequest_start(MPIU_Greq_query_fn, MPIU_Greq_free_fn, 
	MPIU_Greq_cancel_fn, status, request);

    args = (iwrite_at_args*) ADIOI_Malloc(sizeof(iwrite_at_args));
    args->file = mpi_fh;
    args->offset = offset;
    args->buf = buf;
    args->count = count;
    args->datatype = datatype;
    args->status = status;
    args->request = *request;
    hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)iwrite_at_thread, args, 0, NULL);
    if (hThread == NULL)
    {
	error_code = GetLastError();
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
	    "MPI_File_iwrite_at", __LINE__, MPI_ERR_OTHER,
	    "**fail", "**fail %d", error_code);
	error_code = MPIO_Err_return_file(args->file, error_code);
	return error_code;
    }
    CloseHandle(hThread);

#else

    /* for now, no threads or anything fancy. 
    * just call the blocking version */
    error_code = MPI_File_write_at(mpi_fh, offset, buf, count, datatype,
	status); 
    /* ROMIO-1 doesn't do anything with status.MPI_ERROR */
    status->MPI_ERROR = error_code;

    /* kick off the request */
    MPI_Grequest_start(MPIU_Greq_query_fn, MPIU_Greq_free_fn, 
	MPIU_Greq_cancel_fn, status, request);

    /* but we did all the work already */
    MPI_Grequest_complete(*request);
#endif

    MPIR_Nest_decr();
    MPID_CS_EXIT();

    /* passed the buck to the blocking version...*/
    return MPI_SUCCESS;
}
#else
int MPI_File_iwrite_at(MPI_File mpi_fh, MPI_Offset offset, void *buf,
                       int count, MPI_Datatype datatype, 
                       MPIO_Request *request)
{
    int error_code;
    ADIO_File fh;
    static char myname[] = "MPI_FILE_IWRITE_AT";

#ifdef MPI_hpux
    int fl_xmpi;

    HPMP_IO_START(fl_xmpi, BLKMPIFILEIWRITEAT, TRDTSYSTEM,
		  mpi_fh, datatype, count);
#endif /* MPI_hpux */


    fh = MPIO_File_resolve(mpi_fh);

    error_code = MPIOI_File_iwrite(fh, offset, ADIO_EXPLICIT_OFFSET, buf,
				   count, datatype, myname, request);

#ifdef MPI_hpux
    HPMP_IO_END(fl_xmpi, mpi_fh, datatype, count)
#endif /* MPI_hpux */

    return error_code;
}
#endif
