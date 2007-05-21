/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2003 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pvfs2.h"

/* adioi.h has the ADIOI_Fns_struct define */
#include "adioi.h"

struct ADIOI_Fns_struct ADIO_PVFS2_operations = {
    ADIOI_PVFS2_Open, /* Open */
    ADIOI_PVFS2_ReadContig, /* ReadContig */
    ADIOI_PVFS2_WriteContig, /* WriteContig */
    ADIOI_GEN_ReadStridedColl, /* ReadStridedColl */
    ADIOI_GEN_WriteStridedColl, /* WriteStridedColl */
    ADIOI_GEN_SeekIndividual, /* SeekIndividual */
    ADIOI_PVFS2_Fcntl, /* Fcntl */
    ADIOI_PVFS2_SetInfo, /* SetInfo */
    ADIOI_PVFS2_ReadStrided, /* ReadStrided */
    ADIOI_PVFS2_WriteStrided, /* WriteStrided */
    ADIOI_PVFS2_Close, /* Close */
    ADIOI_FAKE_IreadContig, /* IreadContig */
    ADIOI_FAKE_IwriteContig, /* IwriteContig */
    ADIOI_FAKE_IODone, /* ReadDone */
    ADIOI_FAKE_IODone, /* WriteDone */
    ADIOI_FAKE_IOComplete, /* ReadComplete */
    ADIOI_FAKE_IOComplete, /* WriteComplete */
    ADIOI_FAKE_IreadStrided, /* IreadStrided */
    ADIOI_FAKE_IwriteStrided, /* IwriteStrided */
    ADIOI_PVFS2_Flush, /* Flush */
    ADIOI_PVFS2_Resize, /* Resize */
    ADIOI_PVFS2_Delete, /* Delete */
};

/* 
 * vim: ts=8 sts=4 sw=4 noexpandtab 
 */
