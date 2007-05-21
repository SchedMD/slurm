/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#ifndef AD_NFS_INCLUDE
#define AD_NFS_INCLUDE

#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include "adio.h"

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef HAVE_AIO_H
#include <aio.h>
#endif
#ifdef HAVE_SYS_AIO_H
#include <sys/aio.h>
#endif

/* Workaround for incomplete set of definitions if __REDIRECT is not 
   defined and large file support is used in aio.h */
#if !defined(__REDIRECT) && defined(__USE_FILE_OFFSET64)
#define aiocb aiocb64
#endif

int ADIOI_NFS_aio(ADIO_File fd, void *buf, int len, ADIO_Offset offset,
                  int wr, void *handle);

#ifdef SX4
#define lseek llseek
#endif

void ADIOI_NFS_Open(ADIO_File fd, int *error_code);
void ADIOI_NFS_ReadContig(ADIO_File fd, void *buf, int count, 
                      MPI_Datatype datatype, int file_ptr_type,
                     ADIO_Offset offset, ADIO_Status *status, int
		     *error_code);
void ADIOI_NFS_WriteContig(ADIO_File fd, void *buf, int count, 
                      MPI_Datatype datatype, int file_ptr_type,
                      ADIO_Offset offset, ADIO_Status *status, int
		      *error_code);   
void ADIOI_NFS_IwriteContig(ADIO_File fd, void *buf, int count, 
                      MPI_Datatype datatype, int file_ptr_type,
                      ADIO_Offset offset, ADIO_Request *request, int
		      *error_code);   
void ADIOI_NFS_IreadContig(ADIO_File fd, void *buf, int count, 
                      MPI_Datatype datatype, int file_ptr_type,
                      ADIO_Offset offset, ADIO_Request *request, int
		      *error_code);   
int ADIOI_NFS_ReadDone(ADIO_Request *request, ADIO_Status *status, int
		       *error_code);
int ADIOI_NFS_WriteDone(ADIO_Request *request, ADIO_Status *status, int
		       *error_code);
void ADIOI_NFS_ReadComplete(ADIO_Request *request, ADIO_Status *status, int
		       *error_code); 
void ADIOI_NFS_WriteComplete(ADIO_Request *request, ADIO_Status *status,
			int *error_code); 
void ADIOI_NFS_Fcntl(ADIO_File fd, int flag, ADIO_Fcntl_t *fcntl_struct, int
		*error_code); 
void ADIOI_NFS_WriteStrided(ADIO_File fd, void *buf, int count,
		       MPI_Datatype datatype, int file_ptr_type,
		       ADIO_Offset offset, ADIO_Status *status, int
		       *error_code);
void ADIOI_NFS_ReadStrided(ADIO_File fd, void *buf, int count,
		       MPI_Datatype datatype, int file_ptr_type,
		       ADIO_Offset offset, ADIO_Status *status, int
		       *error_code);
void ADIOI_NFS_SetInfo(ADIO_File fd, MPI_Info users_info, int *error_code);
void ADIOI_NFS_Get_shared_fp(ADIO_File fd, int size, ADIO_Offset *shared_fp, 
			 int *error_code);
void ADIOI_NFS_Set_shared_fp(ADIO_File fd, ADIO_Offset offset, int *error_code);

#endif
