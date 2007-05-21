/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

/* contains definitions, declarations, and macros specific to the
   implementation of ADIO on PFS */

#ifndef AD_PFS_INCLUDE
#define AD_PFS_INCLUDE

#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <nx.h>
#include <sys/uio.h>
#include "adio.h"

#ifdef tflops
#define lseek eseek
#define _gopen(n,m,i,p) open(n,m,p)
#endif

/* PFS file-pointer modes (removed most of them because they are unused) */
#ifndef M_ASYNC 
#define M_UNIX                    0
#define M_ASYNC                   5
#endif

void ADIOI_PFS_Open(ADIO_File fd, int *error_code);
void ADIOI_PFS_ReadContig(ADIO_File fd, void *buf, int count, 
                      MPI_Datatype datatype, int file_ptr_type,
                     ADIO_Offset offset, ADIO_Status *status, int
		     *error_code);
void ADIOI_PFS_WriteContig(ADIO_File fd, void *buf, int count, 
                      MPI_Datatype datatype, int file_ptr_type,
                      ADIO_Offset offset, ADIO_Status *status, int
		      *error_code);   
void ADIOI_PFS_IwriteContig(ADIO_File fd, void *buf, int count, 
                      MPI_Datatype datatype, int file_ptr_type,
                      ADIO_Offset offset, ADIO_Request *request, int
		      *error_code);   
void ADIOI_PFS_IreadContig(ADIO_File fd, void *buf, int count, 
                      MPI_Datatype datatype, int file_ptr_type,
                      ADIO_Offset offset, ADIO_Request *request, int
		      *error_code);   
int ADIOI_PFS_ReadDone(ADIO_Request *request, ADIO_Status *status, int
		       *error_code);
int ADIOI_PFS_WriteDone(ADIO_Request *request, ADIO_Status *status, int
		       *error_code);
void ADIOI_PFS_ReadComplete(ADIO_Request *request, ADIO_Status *status, int
		       *error_code); 
void ADIOI_PFS_WriteComplete(ADIO_Request *request, ADIO_Status *status,
			int *error_code); 
void ADIOI_PFS_Fcntl(ADIO_File fd, int flag, ADIO_Fcntl_t *fcntl_struct, int
		*error_code); 
void ADIOI_PFS_Flush(ADIO_File fd, int *error_code);
void ADIOI_PFS_SetInfo(ADIO_File fd, MPI_Info users_info, int *error_code);

#endif
