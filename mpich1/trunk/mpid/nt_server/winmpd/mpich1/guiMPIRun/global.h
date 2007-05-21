/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef GLOBAL_H
#define GLOBAL_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <winsock2.h>
#include <windows.h>
#include <tchar.h>

#define FORWARD_NPROC_THRESHOLD 3
#define MAX_CMD_LENGTH  8192
#define MAX_HOST_LENGTH 64

struct HostNode
{
    TCHAR host[MAX_HOST_LENGTH];
    TCHAR exe[MAX_CMD_LENGTH];
    long nSMPProcs;
    HostNode *next;
};

struct ForwardHostStruct
{
    char pszHost[MAX_CMD_LENGTH];
    int nPort;
};

struct MapDriveNode
{
    char cDrive;
    char pszShare[MAX_PATH];
    //char pszAccount[40];
    //char pszPassword[40];
    MapDriveNode *pNext;
};

//bool UnmapDrives(SOCKET sock, MapDriveNode *pList);

// Global variables

extern bool g_bUseJobHost;
extern char g_pszJobHost[MAX_HOST_LENGTH];
extern bool g_bUseJobMPDPwd;
extern char g_pszJobHostMPDPwd[100];
extern int g_nLaunchTimeout;

#define NUM_GLOBAL_COLORS 8
extern COLORREF aGlobalColor[NUM_GLOBAL_COLORS];

#endif
