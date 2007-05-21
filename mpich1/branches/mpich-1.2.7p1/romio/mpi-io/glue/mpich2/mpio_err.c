/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 2004 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include <stdarg.h>
#include <stdio.h>

#include "mpioimpl.h"
#include "adio_extern.h"

/* MPICH2 error handling implementation */
int MPIR_Err_create_code_valist(int, int, const char [], int, int, 
				const char [], const char [], va_list );
int MPIR_Err_is_fatal(int);

void MPIR_Get_file_error_routine( MPI_Errhandler, 
				  void (**)(MPI_File *, int *, ...), 
				  int * );

int MPIO_Err_create_code(int lastcode, int fatal, const char fcname[],
			 int line, int error_class, const char generic_msg[],
			 const char specific_msg[], ... )
{
    va_list Argp;
    int error_code;

    va_start(Argp, specific_msg);

    error_code = MPIR_Err_create_code_valist(lastcode, fatal, fcname, line,
					     error_class, generic_msg,
					     specific_msg, Argp);
    
    va_end(Argp);

    return error_code;
}

int MPIO_Err_return_file(MPI_File mpi_fh, int error_code)
{
    MPI_Errhandler e;
    void (*c_errhandler)(MPI_File *, int *, ... );
    int  kind;   /* Error handler kind (see below) */
    char error_msg[4096];
    int len;

    /* If the file pointer is not valid, we use the handler on
       MPI_FILE_NULL (MPI-2, section 9.7).  For now, this code assumes that 
       MPI_FILE_NULL has the default handler (return).  FIXME.  See
       below - the set error handler uses ADIOI_DFLT_ERR_HANDLER; 
    */

    /* First, get the handler and the corresponding function */
    if (mpi_fh == MPI_FILE_NULL) {
	e = ADIOI_DFLT_ERR_HANDLER;
    }
    else {
	ADIO_File fh;

	fh = MPIO_File_resolve(mpi_fh);
	e = fh->err_handler;
    }

    /* Actually, e is just the value provide by the MPICH2 routines
       file_set_errhandler.  This is actually a *pointer* to the
       errhandler structure.  We don't know that, so we ask
       the MPICH2 code to translate this object into an error handler.
       kind = 0: errors are fatal
       kind = 1: errors return
       kind = 2: errors call function
    */
    if (e == MPI_ERRORS_RETURN || !e) {
	/* FIXME: This is a hack in case no error handler was set */
	kind = 1;
	c_errhandler = 0;
    }
    else {
	MPIR_Get_file_error_routine( e, &c_errhandler, &kind );
    }

    /* --BEGIN ERROR HANDLING-- */
    if (MPIR_Err_is_fatal(error_code) || kind == 0) 
    {
	ADIOI_Snprintf(error_msg, 4096, "I/O error: ");
	len = (int)strlen(error_msg);
	MPIR_Err_get_string(error_code, &error_msg[len], 4096-len, NULL);
	MPID_Abort(NULL, MPI_SUCCESS, error_code, error_msg);
    }
    /* --END ERROR HANDLING-- */
    else if (kind == 2) {
	(*c_errhandler)( &mpi_fh, &error_code, 0 );
    }

    /* kind == 1 just returns */
    return error_code;
}

int MPIO_Err_return_comm(MPI_Comm mpi_comm, int error_code)
{
    /* note: MPI calls inside the MPICH2 implementation are prefixed
     * with an "N", indicating a nested call.
     */
    MPI_Comm_call_errhandler(mpi_comm, error_code);
    return error_code;
}
