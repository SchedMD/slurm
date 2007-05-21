/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2003 University of Chicago, Ohio Supercomputer Center. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_gridftp.h"

/* adioi.h has the ADIOI_Fns_struct define */
#include "adioi.h"

struct ADIOI_Fns_struct ADIO_GRIDFTP_operations = {
    ADIOI_GRIDFTP_Open, /* Open */
    ADIOI_GRIDFTP_ReadContig, /* ReadContig */
    ADIOI_GRIDFTP_WriteContig, /* WriteContig */
    ADIOI_GEN_ReadStridedColl, /* ReadStridedColl */
    ADIOI_GEN_WriteStridedColl, /* WriteStridedColl */
    ADIOI_GEN_SeekIndividual, /* SeekIndividual */
    ADIOI_GRIDFTP_Fcntl, /* Fcntl */
    ADIOI_GRIDFTP_SetInfo, /* SetInfo */
    ADIOI_GRIDFTP_ReadStrided, /* ReadStrided */
    ADIOI_GRIDFTP_WriteStrided, /* WriteStrided */
    ADIOI_GRIDFTP_Close, /* Close */
    ADIOI_FAKE_IreadContig, /* IreadContig */
    ADIOI_FAKE_IwriteContig, /* IwriteContig */
    ADIOI_FAKE_IODone, /* ReadDone */
    ADIOI_FAKE_IODone, /* WriteDone */
    ADIOI_FAKE_IOComplete, /* ReadComplete */
    ADIOI_FAKE_IOComplete, /* WriteComplete */
    ADIOI_FAKE_IreadStrided, /* IreadStrided */
    ADIOI_FAKE_IwriteStrided, /* IwriteStrided */
    ADIOI_GRIDFTP_Flush, /* Flush */
    ADIOI_GRIDFTP_Resize, /* Resize */
    ADIOI_GRIDFTP_Delete, /* Delete */
};
