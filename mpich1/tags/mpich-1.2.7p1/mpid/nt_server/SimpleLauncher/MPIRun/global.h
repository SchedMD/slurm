#ifndef GLOBAL_H
#define GLOBAL_H

#include <windows.h>
#include <tchar.h>

struct HostNode
{
	TCHAR host[100];
	long nSMPProcs;
	HostNode *next;
};

extern char g_pszExe[MAX_PATH], g_pszArgs[MAX_PATH], g_pszEnv[MAX_PATH];
extern char g_pszFirstHost[100];
extern int g_nFirstSMPProcs;
extern int g_nHosts;
extern int g_nPort;
extern HostNode *g_pHosts;
extern bool g_bNoMPI;
extern int g_nMPICHPort;

#endif
