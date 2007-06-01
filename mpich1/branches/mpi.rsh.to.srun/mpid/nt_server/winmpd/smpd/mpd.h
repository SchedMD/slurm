/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef MPD_H
#define MPD_H

#define COPYRIGHT "Copyright 2002 Argonne National Lab"
#define VERSION_RELEASE 1
#define VERSION_MAJOR   2
#define VERSION_MINOR   5

#define MAX_CMD_LENGTH	    8192
#define MAX_HOST_LENGTH	    64
#define MPD_PASSPHRASE_MAX_LENGTH    256
#define MPD_SALT_VALUE               "14"

#define MPD_DEFAULT_PORT             8675
#define MPD_DEFAULT_PASSPHRASE       "MPICHIsGreat"
#define MPD_REGISTRY_KEY             "SOFTWARE\\MPICH\\MPD"
#ifndef MPICHKEY
#define MPICHKEY                     "SOFTWARE\\MPICH"
#endif

#define DBS_SUCCESS_STR	    "DBS_SUCCESS"
#define DBS_FAIL_STR	    "DBS_FAIL"
#define DBS_END_STR	    "DBS_END"

#define CONSOLE_STR_LENGTH 10*MAX_CMD_LENGTH

#define MPD_DEFAULT_TIMEOUT 45
#define MPD_SHORT_TIMEOUT   20

#endif
