/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#ifndef AD_NTFS_INCLUDE
#define AD_NTFS_INCLUDE

#include <sys/types.h>
#include <fcntl.h>
#include "adio.h"

#ifdef HAVE_INT64
#define DWORDLOW(x)        ( (DWORD) ( x & (__int64) 0xFFFFFFFF ) )
#define DWORDHIGH(x)       ( (DWORD) ( (x >> 32) & (__int64) 0xFFFFFFFF ) )
#define DWORDTOINT64(x,y)  ( (__int64) ( ( (__int64 x) << 32 ) + (__int64) y ) )
#else
#define DWORDLOW(x)         x
#define DWORDHIGH(x)        0
#define DWORDTOINT64(x,y)   x
#endif

int ADIOI_NTFS_aio(ADIO_File fd, void *buf, int len, ADIO_Offset offset,
		  int wr, void *handle);

void ADIOI_NTFS_Open(ADIO_File fd, int *error_code);
void ADIOI_NTFS_Close(ADIO_File fd, int *error_code);
void ADIOI_NTFS_ReadContig(ADIO_File fd, void *buf, int count, 
                      MPI_Datatype datatype, int file_ptr_type,
                     ADIO_Offset offset, ADIO_Status *status, int
		     *error_code);
void ADIOI_NTFS_WriteContig(ADIO_File fd, void *buf, int count, 
                      MPI_Datatype datatype, int file_ptr_type,
                      ADIO_Offset offset, ADIO_Status *status, int
		      *error_code);   
void ADIOI_NTFS_IwriteContig(ADIO_File fd, void *buf, int count, 
                      MPI_Datatype datatype, int file_ptr_type,
                      ADIO_Offset offset, ADIO_Request *request, int
		      *error_code);   
void ADIOI_NTFS_IreadContig(ADIO_File fd, void *buf, int count, 
                      MPI_Datatype datatype, int file_ptr_type,
                      ADIO_Offset offset, ADIO_Request *request, int
		      *error_code);   
int ADIOI_NTFS_ReadDone(ADIO_Request *request, ADIO_Status *status, int
		       *error_code);
int ADIOI_NTFS_WriteDone(ADIO_Request *request, ADIO_Status *status, int
		       *error_code);
void ADIOI_NTFS_ReadComplete(ADIO_Request *request, ADIO_Status *status, int
		       *error_code); 
void ADIOI_NTFS_WriteComplete(ADIO_Request *request, ADIO_Status *status,
			int *error_code); 
void ADIOI_NTFS_Fcntl(ADIO_File fd, int flag, ADIO_Fcntl_t *fcntl_struct, int
		*error_code); 
void ADIOI_NTFS_IwriteStrided(ADIO_File fd, void *buf, int count,
		       MPI_Datatype datatype, int file_ptr_type,
		       ADIO_Offset offset, ADIO_Request *request, int
		       *error_code);
void ADIOI_NTFS_Flush(ADIO_File fd, int *error_code);
void ADIOI_NTFS_Resize(ADIO_File fd, ADIO_Offset size, int *error_code);

const char * ADIOI_NTFS_Strerror(int error);

#endif
