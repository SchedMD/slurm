/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2001 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_testfs.h"

/* adioi.h has the ADIOI_Fns_struct define */
#include "adioi.h"

struct ADIOI_Fns_struct ADIO_TESTFS_operations = {
    ADIOI_TESTFS_Open, /* Open */
    ADIOI_TESTFS_ReadContig, /* ReadContig */
    ADIOI_TESTFS_WriteContig, /* WriteContig */
    ADIOI_TESTFS_ReadStridedColl, /* ReadStridedColl */
    ADIOI_TESTFS_WriteStridedColl, /* WriteStridedColl */
    ADIOI_TESTFS_SeekIndividual, /* SeekIndividual */
    ADIOI_TESTFS_Fcntl, /* Fcntl */
    ADIOI_TESTFS_SetInfo, /* SetInfo */
    ADIOI_TESTFS_ReadStrided, /* ReadStrided */
    ADIOI_TESTFS_WriteStrided, /* WriteStrided */
    ADIOI_TESTFS_Close, /* Close */
    ADIOI_TESTFS_IreadContig, /* IreadContig */
    ADIOI_TESTFS_IwriteContig, /* IwriteContig */
    ADIOI_TESTFS_ReadDone, /* ReadDone */
    ADIOI_TESTFS_WriteDone, /* WriteDone */
    ADIOI_TESTFS_ReadComplete, /* ReadComplete */
    ADIOI_TESTFS_WriteComplete, /* WriteComplete */
    ADIOI_TESTFS_IreadStrided, /* IreadStrided */
    ADIOI_TESTFS_IwriteStrided, /* IwriteStrided */
    ADIOI_TESTFS_Flush, /* Flush */
    ADIOI_TESTFS_Resize, /* Resize */
    ADIOI_TESTFS_Delete, /* Delete */
};
