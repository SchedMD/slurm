/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef LAUNCH_PROCESS_H
#define LAUNCH_PROCESS_H

#include <winsock2.h>
#include <windows.h>

#include "global.h"

class CGuiMPIRunView;

struct MPIRunLaunchProcessArg
{
    int i, n;
    char pszJobID[100];
    char pszHost[MAX_HOST_LENGTH];
    char pszEnv[MAX_CMD_LENGTH];
    char pszMap[MAX_CMD_LENGTH];
    char pszDir[MAX_PATH];
    char pszCmdLine[MAX_CMD_LENGTH];
    bool bLogon;
    char pszAccount[100];
    char pszPassword[300];
    char pszIOHostPort[100];
    char pszPassPhrase[257];
    bool bUseDebugFlag;
    CGuiMPIRunView *pDlg;
};

void MPIRunLaunchProcess(MPIRunLaunchProcessArg *arg);
void PutJobInDatabase(MPIRunLaunchProcessArg *arg);
void PutJobProcessInDatabase(MPIRunLaunchProcessArg *arg, int pid);
void UpdateJobState(char *state);
void UpdateJobKeyValue(int rank, char *key, char *value);

#endif
