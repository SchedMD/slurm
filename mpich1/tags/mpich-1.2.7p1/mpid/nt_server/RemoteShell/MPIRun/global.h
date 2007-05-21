#ifndef GLOBAL_H
#define GLOBAL_H

// RemoteShellServer must be compiled before the following files are generated
#include "..\RemoteShellServer\RemoteShellServer.h"

#include <windows.h>
#include <tchar.h>

void PrintError(HRESULT hr);

struct HostNode
{
	TCHAR host[100];
	TCHAR exe[MAX_PATH];
	long nSMPProcs;
	HostNode *next;
};

// Global variables
extern HANDLE g_hAbortEvent;
extern HANDLE *g_pAbortThreads;
extern bool g_bNormalExit;
extern HostNode *g_pHosts;
extern long g_nHosts;
extern long g_nRootPort;
extern long g_nFirstSMPProcs;
extern TCHAR g_pszAccount[100], g_pszPassword[100];
extern bool g_bNoMPI;
extern TCHAR g_pszExe[MAX_PATH], g_pszArgs[MAX_PATH], g_pszEnv[1024];
extern TCHAR g_pszFirstHost[100];
extern HANDLE g_hFinishedEvent;
extern HANDLE g_hConsoleOutputMutex;

#define MULTI_COLOR_OUTPUT
 
#ifdef MULTI_COLOR_OUTPUT

extern WORD g_ConsoleAttribute;

#include <wincon.h>
#define NUM_OUTPUT_COLORS 32

// foregrounds
#define frgnd_RGB	FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY
#define frgnd_RG	FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY
#define frgnd_RB	FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY
#define frgnd_R		FOREGROUND_RED | FOREGROUND_INTENSITY
#define frgnd_GB	FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY
#define frgnd_G		FOREGROUND_GREEN | FOREGROUND_INTENSITY
#define frgnd_B		FOREGROUND_BLUE | FOREGROUND_INTENSITY
#define frgnd_rgb	FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE
#define frgnd_rg	FOREGROUND_RED | FOREGROUND_GREEN
#define frgnd_rb	FOREGROUND_RED | FOREGROUND_BLUE
#define frgnd_r		FOREGROUND_RED
#define frgnd_gb	FOREGROUND_GREEN | FOREGROUND_BLUE
#define frgnd_g		FOREGROUND_GREEN
#define frgnd_b		FOREGROUND_BLUE

// backgrounds
#define bkgnd_RGB	BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY
#define bkgnd_RG	BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_INTENSITY
#define bkgnd_RB	BACKGROUND_RED | BACKGROUND_BLUE | BACKGROUND_INTENSITY
#define bkgnd_R		BACKGROUND_RED | BACKGROUND_INTENSITY
#define bkgnd_GB	BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY
#define bkgnd_G		BACKGROUND_GREEN | BACKGROUND_INTENSITY
#define bkgnd_B		BACKGROUND_BLUE | BACKGROUND_INTENSITY
#define bkgnd_rgb	BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE
#define bkgnd_rg	BACKGROUND_RED | BACKGROUND_GREEN
#define bkgnd_rb	BACKGROUND_RED | BACKGROUND_BLUE
#define bkgnd_r		BACKGROUND_RED
#define bkgnd_gb	BACKGROUND_GREEN | BACKGROUND_BLUE
#define bkgnd_g		BACKGROUND_GREEN
#define bkgnd_b		BACKGROUND_BLUE
extern WORD aConsoleColorAttribute[NUM_OUTPUT_COLORS];

#endif

#endif
