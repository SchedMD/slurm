/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef BNRIMPL_H
#define BNRIMPL_H

#include "bnr.h"
#include "mpd.h"
#include <winsock2.h>
#include <windows.h>

#define BNR_MAX_KEY_LEN         256
#define BNR_MAX_VALUE_LEN       1024
#define BNR_MAX_DB_NAME_LENGTH  100

#define BNR_INITIALIZED 0
#define BNR_FINALIZED   1

extern char g_pszDBName[BNR_MAX_DB_NAME_LENGTH];
extern char g_pszMPDHost[100];
extern int g_nMPDPort;
extern char g_pszBNRAccount[100];
extern char g_pszBNRPassword[100];
extern char g_pszMPDPhrase[MPD_PASSPHRASE_MAX_LENGTH];
extern int g_bfdMPD;
extern int g_nIproc;
extern int g_nNproc;
extern int g_bInitFinalized;
extern HANDLE g_hSpawnMutex;
extern char g_pszIOHost[100];
extern int g_nIOPort;
extern HANDLE g_hJobThreads[100];
extern int g_nNumJobThreads;
extern bool g_bBNRFinalizeWaiting;

#endif
