/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_sfs.h"

void ADIOI_SFS_Flush(ADIO_File fd, int *error_code)
{
#ifndef PRINT_ERR_MSG
    static char myname[] = "ADIOI_SFS_FLUSH";
#endif

     /* there is no fsync on SX-4 */
#ifdef MPICH2
    *error_code = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, myname, __LINE__, MPI_ERR_IO, "**io",
	"**io %s", strerror(errno));
#elif defined(PRINT_ERR_MSG)
     *error_code = MPI_ERR_UNKNOWN; 
#else /* MPICH-1 */
     *error_code = MPIR_Err_setmsg(MPI_ERR_UNSUPPORTED_OPERATION, 1,
			      myname, (char *) 0, (char *) 0);
     ADIOI_Error(fd, *error_code, myname);	    
#endif
}
