/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpioimpl.h"
#include "adio_extern.h"

#ifdef MPICH2

/* Forward ref for the routine to extract and set the error handler
   in a ROMIO File structure.  FIXME: These should be imported from a common
   header file that is also used in errhan/file_set_errhandler.c
 */
int MPIR_ROMIO_Get_file_errhand( MPI_File, MPI_Errhandler * );
int MPIR_ROMIO_Set_file_errhand( MPI_File, MPI_Errhandler );
void MPIR_Get_file_error_routine( MPI_Errhandler, 
				  void (**)(MPI_File *, int *, ...), 
				  int * );

/* These next two routines are used to allow MPICH2 to access/set the
   error handers in the MPI_File structure until MPICH2 knows about the
   file structure, and to handle the errhandler structure, which 
   includes a reference count.  Not currently used. */
int MPIR_ROMIO_Set_file_errhand( MPI_File file_ptr, MPI_Errhandler e )
{
    if (file_ptr == MPI_FILE_NULL) ADIOI_DFLT_ERR_HANDLER = e;
    /* --BEGIN ERROR HANDLING-- */
    else if (file_ptr->cookie != ADIOI_FILE_COOKIE) {
	return MPI_ERR_FILE;
    }
    /* --END ERROR HANDLING-- */
    else 
	file_ptr->err_handler = e;
    return 0;
}
int MPIR_ROMIO_Get_file_errhand( MPI_File file_ptr, MPI_Errhandler *e )
{
    if (file_ptr == MPI_FILE_NULL) {
	if (ADIOI_DFLT_ERR_HANDLER == MPI_ERRORS_RETURN)
	    *e = 0;
	else {
	    *e = ADIOI_DFLT_ERR_HANDLER;
	}
    }
    /* --BEGIN ERROR HANDLING-- */
    else if (file_ptr->cookie != ADIOI_FILE_COOKIE) {
	return MPI_ERR_FILE;
    }
    /* --END ERROR HANDLING-- */
    else {
	if (file_ptr->err_handler == MPI_ERRORS_RETURN) 
	    *e = 0;
	else
	    *e = file_ptr->err_handler;
    }
    return 0;
}

#endif /* MPICH2 */
