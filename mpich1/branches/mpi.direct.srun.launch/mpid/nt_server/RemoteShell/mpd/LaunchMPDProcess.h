#ifndef LAUNCHMPDPROCESS_H
#define LAUNCHMPDPROCESS_H

#include <stdio.h>
#include "LaunchNode.h"

struct LaunchMPDProcessArg
{
	unsigned long nIP, nSrcIP;
	int nPort, nSrcPort;
	char *pszCommand;
	LaunchNode *pNode;
	HANDLE hEndOutput;
};

void LaunchMPDProcess(LaunchMPDProcessArg *pArg);
void KillMPDProcess(int nPid);
void KillMPDProcesses(int nGroupID);
void KillRemainingMPDProcesses();
void PrintMPDProcessesToBuffer(char *pBuffer, char *pszHostPort = NULL);

#endif
