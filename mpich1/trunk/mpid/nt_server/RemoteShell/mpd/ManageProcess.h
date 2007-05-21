#ifndef MANAGE_PROCESS_H
#define MANAGE_PROCESS_H

#include <winsock2.h>
#include <windows.h>

void ManageProcess(char *pszCmdLine, char *pszArgs, char *pszEnv, char *pszDir, int nGroupID, int nGroupRank, char *pszIn, char *pszOut, char *pszErr, HANDLE hAbortEvent);

#endif
