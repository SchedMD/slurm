/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#ifndef AD_PVFS2_INCLUDE
#define AD_PVFS2_INCLUDE

#include "adio.h"
#ifdef HAVE_PVFS2_H
#include "pvfs2.h"
#endif

void ADIOI_PVFS2_Open(ADIO_File fd, int *error_code);
void ADIOI_PVFS2_Close(ADIO_File fd, int *error_code);
void ADIOI_PVFS2_ReadContig(ADIO_File fd, void *buf, int count, 
                      MPI_Datatype datatype, int file_ptr_type,
                     ADIO_Offset offset, ADIO_Status *status, int
		     *error_code);
void ADIOI_PVFS2_WriteContig(ADIO_File fd, void *buf, int count, 
                      MPI_Datatype datatype, int file_ptr_type,
                      ADIO_Offset offset, ADIO_Status *status, int
		      *error_code);   
void ADIOI_PVFS2_Fcntl(ADIO_File fd, int flag, ADIO_Fcntl_t *fcntl_struct, int
		*error_code); 
void ADIOI_PVFS2_WriteStrided(ADIO_File fd, void *buf, int count,
		       MPI_Datatype datatype, int file_ptr_type,
		       ADIO_Offset offset, ADIO_Status *status, int
		       *error_code);
void ADIOI_PVFS2_ReadStrided(ADIO_File fd, void *buf, int count,
		       MPI_Datatype datatype, int file_ptr_type,
		       ADIO_Offset offset, ADIO_Status *status, int
		       *error_code);
void ADIOI_PVFS2_Flush(ADIO_File fd, int *error_code);
void ADIOI_PVFS2_Delete(char *filename, int *error_code);
void ADIOI_PVFS2_Resize(ADIO_File fd, ADIO_Offset size, int *error_code);
void ADIOI_PVFS2_SetInfo(ADIO_File fd, MPI_Info users_info, int *error_code);
#endif
