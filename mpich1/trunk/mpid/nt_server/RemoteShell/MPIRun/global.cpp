#include "global.h"

#include <objbase.h>
#include "..\RemoteShellServer\RemoteShellserver_i.c"

// Global variables
HANDLE g_hAbortEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
HANDLE *g_pAbortThreads = NULL;
bool g_bNormalExit = true;
long g_nRootPort = 0;
HostNode *g_pHosts = NULL;
long g_nHosts = 1;
VARIANT g_vHosts, g_vSMPInfo;
long g_nFirstSMPProcs = 1;
TCHAR g_pszAccount[100], g_pszPassword[100];
bool g_bNoMPI = false;
TCHAR g_pszExe[MAX_PATH] = _T(""), g_pszArgs[MAX_PATH] = _T(""), g_pszEnv[1024] = _T("");
TCHAR g_pszFirstHost[100] = _T("");
HANDLE g_hFinishedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
HANDLE g_hConsoleOutputMutex = CreateMutex(NULL, FALSE, NULL);
bool g_bUseBNR = false;

#ifdef MULTI_COLOR_OUTPUT

WORD g_ConsoleAttribute;
WORD aConsoleColorAttribute[NUM_OUTPUT_COLORS] = {
	frgnd_rgb, 
	frgnd_RG,
	frgnd_RB,
	frgnd_R,
	frgnd_GB,
	frgnd_G,
	frgnd_RGB,
	frgnd_RGB | bkgnd_rb,
	frgnd_RGB | bkgnd_r,
	frgnd_RGB | bkgnd_gb,
	frgnd_RGB | bkgnd_g,
	frgnd_RGB | bkgnd_b,
	frgnd_RG | bkgnd_rb,
	frgnd_RG | bkgnd_r,
	frgnd_RG | bkgnd_gb,
	frgnd_RG | bkgnd_g,
	frgnd_RG | bkgnd_b,
	frgnd_RB | bkgnd_rb,
	frgnd_RB | bkgnd_b,
	frgnd_R | bkgnd_r,
	frgnd_R | bkgnd_b,
	frgnd_GB | bkgnd_rb,
	frgnd_GB | bkgnd_r,
	frgnd_GB | bkgnd_gb,
	frgnd_GB | bkgnd_g,
	frgnd_GB | bkgnd_b,
	frgnd_G | bkgnd_r,
	frgnd_G | bkgnd_gb,
	frgnd_G | bkgnd_g,
	frgnd_G | bkgnd_b,
	frgnd_rb | bkgnd_gb,
	frgnd_r | bkgnd_gb
};

#endif

