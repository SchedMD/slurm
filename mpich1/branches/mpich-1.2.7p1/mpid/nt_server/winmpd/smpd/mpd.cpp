// Includes

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpdimpl.h"
#include "service.h"
#include "GetOpt.h"
#include "GetStringOpt.h"
#include "database.h"
#include "Translate_Error.h"

// Global variables

int g_nPort = 0;
char g_pszHost[MAX_HOST_LENGTH] = "";
char g_pszIP[25] = "";
unsigned long g_nIP = 0;
char g_pszTempDir[MAX_PATH] = "C:\\";

MPD_Context *g_pList = NULL;

int g_nSignalCount = 2;
bool g_bSingleUser = false;
bool g_bStartAlone = false;
bool g_bUseMPDUser = false;
bool g_bMPDUserCapable = false;
char g_pszMPDUserAccount[100] = "";
char g_pszMPDUserPassword[100] = "";

extern "C" {
__declspec(dllexport) int mpdVersionRelease = VERSION_RELEASE;
__declspec(dllexport) int mpdVersionMajor = VERSION_MAJOR;
__declspec(dllexport) int mpdVersionMinor = VERSION_MINOR;
__declspec(dllexport) char mpdVersionDate[] = __DATE__;
}

void GetMPDVersion(char *str, int length)
{
    _snprintf(str, length, "%d.%d.%d %s", VERSION_RELEASE, VERSION_MAJOR, VERSION_MINOR, __DATE__);
}

void GetMPICHVersion(char *str, int length)
{
    void (*pGetMPICHVersion)(char *str, int length);
    char *filename = NULL, *name_part;
    DWORD len;
    HMODULE hModule;
    char err_msg[1024];

    if (length < 1)
	return;

    len = SearchPath(NULL, "mpich.dll", NULL, 0, filename, &name_part);

    if (len == 0)
    {
	err_printf("GetMPICHVersion::unable to find mpich.dll\n");
	*str = '\0';
	return;
    }

    filename = new char[len*2+2];
    len = SearchPath(NULL, "mpich.dll", NULL, len*2, filename, &name_part);
    if (len == 0)
    {
	err_printf("GetMPICHVersion::unable to find mpich.dll\n");
	*str = '\0';
	delete filename;
	return;
    }

    hModule = LoadLibrary(filename);
    delete filename;

    if (hModule == NULL)
    {
	Translate_Error(GetLastError(), err_msg, NULL);
	err_printf("GetMPICHVersion::LoadLibrary(mpich.dll) failed, ");
	err_printf("%s\n", err_msg);
	*str = '\0';
	return;
    }

    pGetMPICHVersion = (void (*)(char *, int))GetProcAddress(hModule, "GetMPICHVersion");

    if (pGetMPICHVersion == NULL)
    {
	Translate_Error(GetLastError(), err_msg, "GetProcAddress(\"GetMPICHVersion\") failed, ");
	err_printf("GetMPICHVersion::%s\n", err_msg);
	*str = '\0';
	FreeLibrary(hModule);
	return;
    }

    pGetMPICHVersion(str, length);
    //dbg_printf("%s\n", version);
    FreeLibrary(hModule);
}

void SignalExit()
{
    g_nSignalCount--;
    if (g_nSignalCount == 0)
	ServiceStop();
}

void PrintState(FILE *fout)
{
    MPD_Context *p;

    fprintf(fout, "STATE------------------------------------------------\n");
    fprintf(fout, "g_pList of contexts:\n");
    p = g_pList;
    while (p)
    {
	p->Print(fout);
	p = p->pNext;
    }
    fprintf(fout, "g_nIP: %d, g_pszIP: %s\n", g_nIP, g_pszIP);
    fprintf(fout, "g_nPort: %d\n", g_nPort);
    fprintf(fout, "g_pszHost:        '%s'\n", g_pszHost);
    fprintf(fout, "STATE------------------------------------------------\n");
}
