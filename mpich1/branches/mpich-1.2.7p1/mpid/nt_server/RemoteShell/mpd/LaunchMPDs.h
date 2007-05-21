#ifndef LAUNCH_MPDS_H
#define LAUNCH_MPDS_H

#include "GetHosts.h"
#include <winsock2.h>
#include <windows.h>

struct LaunchMPDArg
{
	char pszHost[100];
	int nPort;
	HANDLE hReadyEvent;
	int timeout;

	HostNode *pHostInfo;
	LaunchMPDArg *pRight;
};

void LaunchMPDs(HostNode *pHosts, HANDLE *phLeftThread, HANDLE *phRightThread, int timeout = 0);

#endif
