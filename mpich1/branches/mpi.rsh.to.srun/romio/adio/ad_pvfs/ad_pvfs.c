/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2001 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pvfs.h"

/* adioi.h has the ADIOI_Fns_struct define */
#include "adioi.h"

struct ADIOI_Fns_struct ADIO_PVFS_operations = {
    ADIOI_PVFS_Open, /* Open */
    ADIOI_PVFS_ReadContig, /* ReadContig */
    ADIOI_PVFS_WriteContig, /* WriteContig */
    ADIOI_GEN_ReadStridedColl, /* ReadStridedColl */
    ADIOI_GEN_WriteStridedColl, /* WriteStridedColl */
    ADIOI_GEN_SeekIndividual, /* SeekIndividual */
    ADIOI_PVFS_Fcntl, /* Fcntl */
    ADIOI_PVFS_SetInfo, /* SetInfo */
    ADIOI_PVFS_ReadStrided, /* ReadStrided */
    ADIOI_PVFS_WriteStrided, /* WriteStrided */
    ADIOI_PVFS_Close, /* Close */
    ADIOI_FAKE_IreadContig, /* IreadContig */
    ADIOI_FAKE_IwriteContig, /* IwriteContig */
    ADIOI_FAKE_IODone, /* ReadDone */
    ADIOI_FAKE_IODone, /* WriteDone */
    ADIOI_FAKE_IOComplete, /* ReadComplete */
    ADIOI_FAKE_IOComplete, /* WriteComplete */
    ADIOI_FAKE_IreadStrided, /* IreadStrided */
    ADIOI_FAKE_IwriteStrided, /* IwriteStrided */
    ADIOI_PVFS_Flush, /* Flush */
    ADIOI_PVFS_Resize, /* Resize */
    ADIOI_PVFS_Delete, /* Delete */
};
