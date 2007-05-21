#ifndef LAUNCH_PROCESS_H
#define LAUNCH_PROCESS_H

#include <windows.h>

HANDLE LaunchProcess(char *cmd, char *env, char *dir, HANDLE *hIn, HANDLE *hOut, HANDLE *hErr, DWORD *pdwPid);

#endif
