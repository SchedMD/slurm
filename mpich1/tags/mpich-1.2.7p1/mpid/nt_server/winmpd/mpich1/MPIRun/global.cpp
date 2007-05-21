#include "global.h"
#include "mpd.h" /* for MPD_DEFAULT_PORT */

// Global variables
MapDriveNode *g_pDriveMapList = NULL;
HANDLE g_hRedirectIOListenThread = NULL;
SOCKET g_sockStopIOSignalSocket = INVALID_SOCKET;
HANDLE g_hAbortEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
HostArray *g_pProcessHost = NULL;
SOCKET *g_pProcessSocket = NULL;
int *g_pProcessLaunchId = NULL;
int *g_pLaunchIdToRank = NULL;
LONG g_nNumProcessSockets = 0;
ForwardHostStruct *g_pForwardHost = NULL;
SOCKET g_sockBreak = INVALID_SOCKET;
HANDLE g_hBreakReadyEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
long g_nRootPort = 0;
HostNode *g_pHosts = NULL;
long g_nHosts = 1;
int g_nNproc = 1;
char g_pszAccount[100], g_pszPassword[100];
bool g_bNoMPI = false;
char g_pszExe[MAX_CMD_LENGTH] = "", g_pszArgs[MAX_CMD_LENGTH] = "", g_pszEnv[MAX_CMD_LENGTH] = "";
char g_pszDir[MAX_PATH] = "";
char g_pszExeOrig[MAX_CMD_LENGTH] = "";
char g_pszFirstHost[MAX_HOST_LENGTH] = "";
HANDLE g_hFinishedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
HANDLE g_hConsoleOutputMutex = CreateMutex(NULL, FALSE, NULL);
char g_pszIOHost[MAX_HOST_LENGTH];
int g_nIOPort;
bool g_bDoMultiColorOutput = true;
bool g_bUseJobHost = false;
bool g_bOutputExitCodes = false;
bool g_bLocalRoot = false;
bool g_bMPICH2 = false;
bool g_bIPRoot = true;
char g_pszJobHost[MAX_HOST_LENGTH] = "";
bool g_bUseJobMPDPwd = false;
char g_pszJobHostMPDPwd[100];
int g_nLaunchTimeout = MPIRUN_DEFAULT_TIMEOUT;
bool g_bSuppressErrorOutput = false;
bool g_bUseMPDUser = false;
int g_nMPIRUN_DEFAULT_TIMEOUT = MPIRUN_DEFAULT_TIMEOUT;
int g_nMPIRUN_SHORT_TIMEOUT = MPIRUN_SHORT_TIMEOUT;
int g_nMPIRUN_CREATE_PROCESS_TIMEOUT = MPIRUN_CREATE_PROCESS_TIMEOUT;
char pmi_host[MAX_HOST_LENGTH] = "";
int pmi_port = MPD_DEFAULT_PORT;
char pmi_kvsname[128] = "";
char pmi_phrase[100];

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

