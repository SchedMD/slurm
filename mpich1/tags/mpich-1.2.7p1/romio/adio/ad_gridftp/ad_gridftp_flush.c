/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2003 University of Chicago, Ohio Supercomputer Center. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_gridftp.h"
#include "adioi.h"

/* GridFTP doesn't give you a way to cache writes on the client side, so
   is essentially a no-op */
/* if there is a mechanism where we can ask the server to flush data to disk we
 * should do it here.  I'll leave that up to Troy */

void ADIOI_GRIDFTP_Flush(ADIO_File fd, int *error_code)
{
	return;
}
