/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"
#include "adio_extern.h"

/* NOTE: THIS FUNCTION IS DEPRECATED AND ONLY EXISTS HERE BECAUSE
 * SOME DEPRECATED ADIO IMPLEMENTATIONS STILL CALL IT (SFS, HFS, PIOFS).
 */
int ADIOI_Error(ADIO_File fd, int error_code, char *string)
{
    char buf[MPI_MAX_ERROR_STRING];
    int myrank, result_len; 
    MPI_Errhandler err_handler;

    if (fd == ADIO_FILE_NULL) err_handler = ADIOI_DFLT_ERR_HANDLER;
    else err_handler = fd->err_handler;

    MPI_Comm_rank(MPI_COMM_WORLD, &myrank);
    if (err_handler == MPI_ERRORS_ARE_FATAL) {
	MPI_Error_string(error_code, buf, &result_len);
	FPRINTF(stderr, "[%d] - %s : %s\n", myrank, string, buf);
	MPI_Abort(MPI_COMM_WORLD, 1);
    }
    else if (err_handler != MPI_ERRORS_RETURN) {
	/* MPI_File_call_errorhandler(fd, error_code); */

	FPRINTF(stderr, "Only MPI_ERRORS_RETURN and MPI_ERRORS_ARE_FATAL are currently supported as error handlers for files\n");
	MPI_Abort(MPI_COMM_WORLD, 1);
    }

    return error_code;
}

