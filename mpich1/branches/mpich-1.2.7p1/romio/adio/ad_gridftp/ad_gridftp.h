/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 2003 University of Chicago, Ohio Supercomputer Center. 
 *   See COPYRIGHT notice in top-level directory.
 */

#ifndef AD_GRIDFTP_INCLUDE
#define AD_GRIDFTP_INCLUDE

#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <fcntl.h>
#include "adio.h"
#include <globus_ftp_client.h>

/* These live in globus_routines.c */
extern int num_gridftp_handles;
#ifndef ADIO_GRIDFTP_HANDLES_MAX
#define ADIO_GRIDFTP_HANDLES_MAX 200
#endif /* ! ADIO_GRIDFTP_HANDLES_MAX */
extern globus_ftp_client_handle_t gridftp_fh[ADIO_GRIDFTP_HANDLES_MAX];
extern globus_ftp_client_operationattr_t oattr[ADIO_GRIDFTP_HANDLES_MAX];


/* TODO: weed out the now-unused prototypes  */
void ADIOI_GRIDFTP_Open(ADIO_File fd, int *error_code);
void ADIOI_GRIDFTP_Close(ADIO_File fd, int *error_code);
void ADIOI_GRIDFTP_ReadContig(ADIO_File fd, void *buf, int count, 
			     MPI_Datatype datatype, int file_ptr_type,
			     ADIO_Offset offset, ADIO_Status *status, int
			     *error_code);
void ADIOI_GRIDFTP_WriteContig(ADIO_File fd, void *buf, int count, 
			      MPI_Datatype datatype, int file_ptr_type,
			      ADIO_Offset offset, ADIO_Status *status, int
			      *error_code);   
void ADIOI_GRIDFTP_IwriteContig(ADIO_File fd, void *buf, int count, 
			       MPI_Datatype datatype, int file_ptr_type,
			       ADIO_Offset offset, ADIO_Request *request, int
			       *error_code);   
void ADIOI_GRIDFTP_IreadContig(ADIO_File fd, void *buf, int count, 
			      MPI_Datatype datatype, int file_ptr_type,
			      ADIO_Offset offset, ADIO_Request *request, int
			      *error_code);   
int ADIOI_GRIDFTP_ReadDone(ADIO_Request *request, ADIO_Status *status, int
			  *error_code);
int ADIOI_GRIDFTP_WriteDone(ADIO_Request *request, ADIO_Status *status, int
			   *error_code);
void ADIOI_GRIDFTP_ReadComplete(ADIO_Request *request, ADIO_Status *status, int
			       *error_code); 
void ADIOI_GRIDFTP_WriteComplete(ADIO_Request *request, ADIO_Status *status,
				int *error_code); 
void ADIOI_GRIDFTP_Fcntl(ADIO_File fd, int flag, ADIO_Fcntl_t *fcntl_struct, 
			int *error_code); 
void ADIOI_GRIDFTP_WriteStrided(ADIO_File fd, void *buf, int count,
			       MPI_Datatype datatype, int file_ptr_type,
			       ADIO_Offset offset, ADIO_Status *status,
			       int *error_code);
void ADIOI_GRIDFTP_ReadStrided(ADIO_File fd, void *buf, int count,
			      MPI_Datatype datatype, int file_ptr_type,
			      ADIO_Offset offset, ADIO_Status *status, int
			      *error_code);
void ADIOI_GRIDFTP_WriteStridedColl(ADIO_File fd, void *buf, int count,
				   MPI_Datatype datatype, int file_ptr_type,
				   ADIO_Offset offset, ADIO_Status *status, int
				   *error_code);
void ADIOI_GRIDFTP_ReadStridedColl(ADIO_File fd, void *buf, int count,
				  MPI_Datatype datatype, int file_ptr_type,
				  ADIO_Offset offset, ADIO_Status *status, int
				  *error_code);
void ADIOI_GRIDFTP_IreadStrided(ADIO_File fd, void *buf, int count,
			       MPI_Datatype datatype, int file_ptr_type,
			       ADIO_Offset offset, ADIO_Request *request, int
			       *error_code);
void ADIOI_GRIDFTP_IwriteStrided(ADIO_File fd, void *buf, int count,
				MPI_Datatype datatype, int file_ptr_type,
				ADIO_Offset offset, ADIO_Request *request, int
				*error_code);
void ADIOI_GRIDFTP_Flush(ADIO_File fd, int *error_code);
void ADIOI_GRIDFTP_Resize(ADIO_File fd, ADIO_Offset size, int *error_code);
ADIO_Offset ADIOI_GRIDFTP_SeekIndividual(ADIO_File fd, ADIO_Offset offset, 
					int whence, int *error_code);
void ADIOI_GRIDFTP_SetInfo(ADIO_File fd, MPI_Info users_info, int *error_code);
void ADIOI_GRIDFTP_Get_shared_fp(ADIO_File fd, int size, 
				ADIO_Offset *shared_fp, 
				int *error_code);
void ADIOI_GRIDFTP_Set_shared_fp(ADIO_File fd, ADIO_Offset offset, 
				int *error_code);
void ADIOI_GRIDFTP_Delete(char *filename, int *error_code);

void globus_err_handler(const char *routine, const char *caller,
			globus_result_t result);

#endif




