/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#ifndef AD_SFS_INCLUDE
#define AD_SFS_INCLUDE

#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include "adio.h"

void ADIOI_SFS_Open(ADIO_File fd, int *error_code);
void ADIOI_SFS_Fcntl(ADIO_File fd, int flag, ADIO_Fcntl_t *fcntl_struct, int
		*error_code); 
void ADIOI_SFS_Flush(ADIO_File fd, int *error_code);

#endif
