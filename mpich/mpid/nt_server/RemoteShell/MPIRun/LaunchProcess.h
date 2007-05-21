#ifndef LAUNCH_PROCESS_H
#define LAUNCH_PROCESS_H

#include <windows.h>

struct LaunchProcessArg
{
	int i;
	WCHAR pszJobID[100];
	WCHAR pszHost[100];
	WCHAR pszEnv[MAX_PATH];
	WCHAR pszDir[MAX_PATH];
	WCHAR pszCmdLine[MAX_PATH];
	bool bLogon;
	WCHAR pszAccount[100];
	WCHAR pszPassword[100];
};

void LaunchProcess(LaunchProcessArg *arg);
void LaunchProcessWithMSH(LaunchProcessArg *arg);

#endif
