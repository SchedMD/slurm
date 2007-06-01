/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2001 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pfs.h"

/* adioi.h has the ADIOI_Fns_struct define */
#include "adioi.h"

struct ADIOI_Fns_struct ADIO_PFS_operations = {
    ADIOI_PFS_Open, /* Open */
    ADIOI_PFS_ReadContig, /* ReadContig */
    ADIOI_PFS_WriteContig, /* WriteContig */
    ADIOI_GEN_ReadStridedColl, /* ReadStridedColl */
    ADIOI_GEN_WriteStridedColl, /* WriteStridedColl */
    ADIOI_GEN_SeekIndividual, /* SeekIndividual */
    ADIOI_PFS_Fcntl, /* Fcntl */
    ADIOI_PFS_SetInfo, /* SetInfo */
    ADIOI_GEN_ReadStrided, /* ReadStrided */
    ADIOI_GEN_WriteStrided, /* WriteStrided */
    ADIOI_GEN_Close, /* Close */
    ADIOI_PFS_IreadContig, /* IreadContig */
    ADIOI_PFS_IwriteContig, /* IwriteContig */
    ADIOI_PFS_ReadDone, /* ReadDone */
    ADIOI_PFS_WriteDone, /* WriteDone */
    ADIOI_PFS_ReadComplete, /* ReadComplete */
    ADIOI_PFS_WriteComplete, /* WriteComplete */
    ADIOI_FAKE_IreadStrided, /* IreadStrided */
    ADIOI_FAKE_IwriteStrided, /* IwriteStrided */
    ADIOI_PFS_Flush, /* Flush */
    ADIOI_GEN_Resize, /* Resize */
    ADIOI_GEN_Delete, /* Delete */
};
