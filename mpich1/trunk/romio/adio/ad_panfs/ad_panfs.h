/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   ad_panfs.h
 *
 *   Copyright (C) 2001 University of Chicago.
 *   See COPYRIGHT notice in top-level directory.
 */

#ifndef AD_UNIX_INCLUDE
#define AD_UNIX_INCLUDE

#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include "adio.h"

#ifndef NO_AIO
#ifdef AIO_SUN
#include <sys/asynch.h>
#else
#include <aio.h>
#ifdef NEEDS_ADIOCB_T
typedef struct adiocb adiocb_t;
#endif
#endif
#endif

int ADIOI_PANFS_aio(ADIO_File fd, void *buf, int len, ADIO_Offset offset,
		    int wr, void *handle);

void ADIOI_PANFS_Open(ADIO_File fd, int *error_code);
void ADIOI_PANFS_IwriteContig(ADIO_File fd, void *buf, int count, 
			      MPI_Datatype datatype, int file_ptr_type,
			      ADIO_Offset offset, ADIO_Request *request, int
			      *error_code);   
void ADIOI_PANFS_IreadContig(ADIO_File fd, void *buf, int count, 
			     MPI_Datatype datatype, int file_ptr_type,
			     ADIO_Offset offset, ADIO_Request *request, int
			     *error_code);   
int ADIOI_PANFS_ReadDone(ADIO_Request *request, ADIO_Status *status, int
			 *error_code);
int ADIOI_PANFS_WriteDone(ADIO_Request *request, ADIO_Status *status, int
			  *error_code);
void ADIOI_PANFS_ReadComplete(ADIO_Request *request, ADIO_Status *status, int
			      *error_code); 
void ADIOI_PANFS_WriteComplete(ADIO_Request *request, ADIO_Status *status,
			       int *error_code); 
void ADIOI_PANFS_Fcntl(ADIO_File fd, int flag, ADIO_Fcntl_t *fcntl_struct, int
		       *error_code); 
void ADIOI_PANFS_SetInfo(ADIO_File fd, MPI_Info users_info, int *error_code);

#endif
