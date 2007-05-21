#include "stdafx.h"
#include "global.h"

// Global variables
bool g_bUseJobHost = false;
char g_pszJobHost[MAX_HOST_LENGTH] = "";
bool g_bUseJobMPDPwd = false;
char g_pszJobHostMPDPwd[100];
int g_nLaunchTimeout = 10;

COLORREF aGlobalColor[NUM_GLOBAL_COLORS] = {
    RGB(0,0,0),
    RGB(132,132,0),
    RGB(128,0,128),
    RGB(255,0,0),
    RGB(0,128,128),
    RGB(0,196,0),
    RGB(64,64,128),
    RGB(0,0,255)
};
