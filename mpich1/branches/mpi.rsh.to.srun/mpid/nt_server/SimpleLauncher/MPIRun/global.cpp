#include "global.h"

char g_pszExe[MAX_PATH], g_pszArgs[MAX_PATH], g_pszEnv[MAX_PATH];
char g_pszFirstHost[100];
int g_nFirstSMPProcs = 1;
int g_nHosts = 0;
int g_nPort = 2020;
HostNode *g_pHosts = NULL;
bool g_bNoMPI = false;
int g_nMPICHPort = 12345;