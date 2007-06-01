/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2001 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_ntfs.h"

/* adioi.h has the ADIOI_Fns_struct define */
#include "adioi.h"

struct ADIOI_Fns_struct ADIO_NTFS_operations = {
    ADIOI_NTFS_Open, /* Open */
    ADIOI_NTFS_ReadContig, /* ReadContig */
    ADIOI_NTFS_WriteContig, /* WriteContig */
    ADIOI_GEN_ReadStridedColl, /* ReadStridedColl */
    ADIOI_GEN_WriteStridedColl, /* WriteStridedColl */
    ADIOI_GEN_SeekIndividual, /* SeekIndividual */
    ADIOI_NTFS_Fcntl, /* Fcntl */
    ADIOI_GEN_SetInfo, /* SetInfo */
    ADIOI_GEN_ReadStrided, /* ReadStrided */
    ADIOI_GEN_WriteStrided, /* WriteStrided */
    ADIOI_NTFS_Close, /* Close */
    ADIOI_NTFS_IreadContig, /* IreadContig */
    ADIOI_NTFS_IwriteContig, /* IwriteContig */
    ADIOI_NTFS_ReadDone, /* ReadDone */
    ADIOI_NTFS_WriteDone, /* WriteDone */
    ADIOI_NTFS_ReadComplete, /* ReadComplete */
    ADIOI_NTFS_WriteComplete, /* WriteComplete */
    ADIOI_FAKE_IreadStrided, /* IreadStrided */
    ADIOI_FAKE_IwriteStrided, /* IwriteStrided */
    ADIOI_NTFS_Flush, /* Flush */
    ADIOI_NTFS_Resize, /* Resize */
    ADIOI_GEN_Delete, /* Delete */
};
