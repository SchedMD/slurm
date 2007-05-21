#ifndef LAUNCH_PROCESS_H
#define LAUNCH_PROCESS_H

#include <windows.h>

struct LaunchProcessArg
{
	int i, nPort;
	char pszHost[100];
	char pszEnv[MAX_PATH];
	char pszDir[MAX_PATH];
	char pszCmdLine[MAX_PATH];
};

void LaunchProcessSocket(LaunchProcessArg *arg);

#endif
