/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2004 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include <stdarg.h>
#include <stdio.h>

#include "mpioimpl.h"
#include "adio_extern.h"

/* MPICH1 error handling implementation */

int MPIO_Err_create_code(int lastcode, int fatal, const char fcname[],
			 int line, int error_class, const char generic_msg[],
			 const char specific_msg[], ... )
{
    va_list Argp;
    int error_code;

    va_start(Argp, specific_msg);
	
    error_code = MPIR_Err_setmsg(error_class, 0, fcname, generic_msg,
				 specific_msg, Argp);

    vfprintf(stderr, specific_msg, Argp);
    
    va_end(Argp);

    return error_code;
}

int MPIO_Err_return_file(MPI_File mpi_fh, int error_code)
{
    char buf[MPI_MAX_ERROR_STRING];
    int myrank, result_len; 
    MPI_Errhandler err_handler;

    if (mpi_fh == MPI_FILE_NULL) err_handler = ADIOI_DFLT_ERR_HANDLER;
    else {
	ADIO_File fh;
	fh = MPIO_File_resolve(mpi_fh);
	err_handler = fh->err_handler;
    }

    MPI_Comm_rank(MPI_COMM_WORLD, &myrank);
    if (err_handler == MPI_ERRORS_ARE_FATAL) {
	MPI_Error_string(error_code, buf, &result_len);
	FPRINTF(stderr, "[%d] %s\n", myrank, buf);
	MPI_Abort(MPI_COMM_WORLD, 1);
    }
    else if (err_handler != MPI_ERRORS_RETURN) {
	FPRINTF(stderr, "Only MPI_ERRORS_RETURN and MPI_ERRORS_ARE_FATAL are currently supported as error handlers for files\n");
	MPI_Abort(MPI_COMM_WORLD, 1);
    }

    return error_code;
}

int MPIO_Err_return_comm(MPI_Comm mpi_comm, int error_code)
{
    return MPIO_Err_return_file(MPI_FILE_NULL, error_code);
}
