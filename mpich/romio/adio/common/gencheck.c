/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2002 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"

/* we special-case TESTFS because all it does is wrap logging info around GEN */
int ADIOI_Uses_generic_read(ADIO_File fd)
{
    ADIOI_Fns *fns = fd->fns;
    if (fns->ADIOI_xxx_ReadStridedColl == ADIOI_GEN_ReadStridedColl || 
        fd->file_system == ADIO_TESTFS )
    {
        return 1;
    }
    return 0;
}

int ADIOI_Uses_generic_write(ADIO_File fd)
{
    ADIOI_Fns *fns = fd->fns;
    if (fns->ADIOI_xxx_WriteStridedColl == ADIOI_GEN_WriteStridedColl ||
        fd->file_system == ADIO_TESTFS )
    {
        return 1;
    }
    return 0;
}
