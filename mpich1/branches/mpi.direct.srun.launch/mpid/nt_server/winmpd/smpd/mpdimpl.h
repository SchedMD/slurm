/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef MPDIMPL_H
#define MPDIMPL_H

#include <winsock2.h>
#include <windows.h>
#include "mpd.h"
#include "mpdutil.h"
#include <stdio.h>

// Definitions

#define USE_LINGER_SOCKOPT
#define USE_SET_ERROR_MODE
//#undef USE_SET_ERROR_MODE
#define INVALID_HOSTNAME    "nohost"
#define BLOCKING_TIMEOUT    2000
#define ACK_STRING	    "zzz"

#ifndef CREATE_OBJECT_RETRIES
#define CREATE_OBJECT_RETRIES            5
#endif
#ifndef CREATE_OBJECT_SLEEP_TIME
#define CREATE_OBJECT_SLEEP_TIME       250
#endif

// Enums and Structs

enum MPD_Type {
    MPD_SOCKET,
    MPD_CONSOLE_SOCKET,
};

enum MPD_State { 
    MPD_IDLE, 
    MPD_READING, 
    MPD_WRITING, 
    MPD_INVALID 
};

enum MPD_LowLevelState {
    MPD_WRITING_CMD,
    MPD_WRITING_LAUNCH_CMD,
    MPD_WRITING_LAUNCH_RESULT,
    MPD_WRITING_EXITCODE,
    MPD_WRITING_HOSTS_CMD,
    MPD_WRITING_KILL_CMD,
    MPD_WRITING_FIRST_EXITALL_CMD,
    MPD_WRITING_EXITALL_CMD,
    MPD_WRITING_HOSTS_RESULT,
    MPD_WRITING_RESULT,
    MPD_READING_CMD,
    MPD_WRITING_DONE_EXIT,
    MPD_WRITING_DONE,
    MPD_AUTHENTICATE_READING_APPEND,
    MPD_AUTHENTICATE_WRITING_APPEND,
    MPD_AUTHENTICATE_READING_CRYPTED,
    MPD_AUTHENTICATE_WRITING_CRYPTED,
    MPD_AUTHENTICATE_READING_RESULT,
    MPD_AUTHENTICATE_WRITING_RESULT,
    MPD_AUTHENTICATED,
    MPD_INVALID_LOWLEVEL
};

struct WriteNode
{
    WriteNode();
    WriteNode(char *p, MPD_LowLevelState n);
    ~WriteNode();
    char *pString;
    MPD_LowLevelState nState;
    WriteNode *pNext;
};

struct MPD_Context
{
    MPD_Context();
    ~MPD_Context();
    void Print(FILE *fout);
    int Print(char *str, int length);
    MPD_Type nType;
    SOCKET sock;
    OVERLAPPED ovl;
    DWORD dwNumRead;
    HANDLE hMutex;
    bool bReadPosted;
    bool bDeleted;
    char pszHost[MAX_HOST_LENGTH];
    char pszIn[MAX_CMD_LENGTH];
    char pszOut[MAX_CMD_LENGTH];
    int nCurPos;
    MPD_State nState;
    MPD_LowLevelState nLLState;
    bool bDeleteMe;
    WriteNode *pWriteList;
    bool bPassChecked;
    char pszCrypt[14];
    bool bFileInitCalled;
    char pszFileAccount[50];
    char pszFilePassword[50];
    MPD_Context *pNext;
};

struct RedirectSocketArg
{
    bool bReadisPipe;
    HANDLE hRead;
    SOCKET sockRead;
    bool bWriteisPipe;
    HANDLE hWrite;
    SOCKET sockWrite;
    HANDLE hProcess;
    DWORD dwPid;
    HANDLE hMutex;
    bool bFreeMutex;
    int nRank;
    char cType;
    HANDLE hOtherThread;
};

// Global variables
extern int g_nPort;
extern char g_pszHost[MAX_HOST_LENGTH];
extern char g_pszIP[25];
extern unsigned long g_nIP;
extern char g_pszTempDir[MAX_PATH];

extern MPD_Context *g_pList;
extern int g_nSignalCount;
extern bool g_bSingleUser;
extern bool g_bUseMPDUser;
extern bool g_bMPDUserCapable;
extern char g_pszMPDUserAccount[100];
extern char g_pszMPDUserPassword[100];

extern int g_nActiveW;
extern int g_nActiveR;
extern bool g_bStartAlone;
extern HANDLE g_hBombDiffuseEvent;
extern HANDLE g_hBombThread;
extern HANDLE g_hProcessStructMutex;
extern HANDLE g_hForwarderMutex;
extern HANDLE g_hLaunchMutex;
extern HANDLE g_hBarrierStructMutex;
extern HANDLE g_hCommPort;
extern HANDLE g_hCommPortEvent;
extern int g_NumCommPortThreads;

extern CRITICAL_SECTION g_ContextCriticalSection;

// Function prototypes
void RemoveAllCachedUsers();
bool AuthenticateAcceptedConnection(MPD_Context **pp);
bool AuthenticateConnectedConnection(MPD_Context **pp, char *passphrase = MPD_DEFAULT_PASSPHRASE);
void ConnectAndRestart(int *argc, char ***argv, char *host);
void GetMPDVersion(char *str, int length);
void GetMPICHVersion(char *str, int length);
bool snprintf_update(char *&pszStr, int &length, char *pszFormat, ...);
void statMPD(char *pszParam, char *pszStr, int length);
void statCachedUsers(char *pszOutput, int length);
void statLaunchList(char *pszOutput, int length);
void statProcessList(char *pszOutput, int length);
void statConfig(char *pszOutput, int length);
void statContext(char *pszOutput, int length);
void statTmp(char *pszOutput, int length);
void statBarrier(char *pszOutput, int length);
void statForwarders(char *pszOutput, int length);
void RedirectSocketThread(RedirectSocketArg *arg);
void RedirectLockedSocketThread(RedirectSocketArg *arg);
HANDLE BecomeUser(char *domainaccount, char *password, int *pnError);
void LoseTheUser(HANDLE hUser);
bool MapUserDrives(char *pszMap, char *pszAccount, char *pszPassword, char *pszError);
bool UnmapUserDrives(char *pszMap);
void FinalizeDriveMaps();
int ConsoleGetExitCode(int nPid);
void SetBarrier(char *pszName, int nCount, SOCKET sock);
void InformBarriers(int nId, int nExitCode);
void ConcatenateForwardersToString(char *pszStr);
int CreateIOForwarder(char *pszFwdHost, int nFwdPort);
void StopIOForwarder(int nPort, bool bWaitForEmpty = true);
void AbortAllForwarders();
void RemoveAllTmpFiles();
void GetDirectoryContents(SOCKET sock, char *pszInputStr);
void UpdateMPD(char *pszFileName);
void UpdateMPD(char *pszOldFileName, char *pszNewFileName, int nPid);
void RestartMPD();
void UpdateMPICH(char *pszFileName);
void UpdateMPICHd(char *pszFileName);
void ConcatenateProcessesToString(char *pszStr);
void GetNameKeyValue(char *str, char *name, char *key, char *value);
bool ValidateUser(char *pszAccount, char *pszPassword, bool bUseCache, int *pError);
HANDLE LaunchProcess(char *cmd, char *env, char *dir, int priorityClass, int priority, HANDLE *hIn, HANDLE *hOut, HANDLE *hErr, int *pdwPid, int *nError, char *pszError, bool bDebug);
HANDLE LaunchProcessLogon(char *domainaccount, char *password, char *cmd, char *env, char *map, char *dir, int priorityClass, int priority, HANDLE *hIn, HANDLE *hOut, HANDLE *hErr, int *pdwPid, int *nError, char *pszError, bool bDebug);
void DebugWaitForProcess(bool &bAborted, char *pszError);
void MPD_KillProcess(int nPid);
void ShutdownAllProcesses();
void SavePid(int nId, int nPid);
void SaveError(int nId, char *pszError);
void SaveExitCode(int nId, int nExitCode);
void SaveTimestamp(int nId, char *timestamp);
bool SaveMPIFinalized(int nId);
void SignalExit();
MPD_Context* GetContext(SOCKET sock);
void Launch(char *pszStr);
void HandleRemoteCommand(MPD_Context *p, char *host);
void HandleConsoleRead(MPD_Context *p);
void StringRead(MPD_Context *p);

int GetPortFromFile(char *pszFileName, int nPid, int *nPort);
bool DeleteTmpFile(char *pszFileName);
void CreateTmpFile(char *pszFileName, bool bDelete = true);

#define RUN_EXIT    0
#define RUN_RESTART 1
int Run();

void RemoveAllContexts();
MPD_Context *CreateContext();
void ContextInit();
void ContextFinalize();
void RemoveContext(MPD_Context *p);
void CreateMPDRegistry();
void CleanMPDRegistry();
bool ReadMPDRegistry(char *name, char *value, bool bPrintError = true);
void WriteMPDRegistry(char *name, char *value);
void DeleteMPDRegistry(char *name);
void MPDRegistryToString(char *pszStr, int length);
void ParseRegistry(bool bSetDefaults);
void DoConsole(char *host, int port, bool bAskPwd, char *altphrase);
void PrintState(FILE *fout);

bool ConnectAndRedirectInput(HANDLE hIn, char *pszHostPort, HANDLE hProcess, DWORD dwPid, int nRank);
bool ConnectAndRedirectOutput(HANDLE hOut, char *pszHostPort, HANDLE hProcess, DWORD dwPid, int nRank, char cType);
bool ConnectAndRedirect2Outputs(HANDLE hOut, HANDLE hErr, char *pszHostPort, HANDLE hProcess, DWORD dwPid, int nRank);

int ContextWriteString(MPD_Context *p, char *str = NULL);
int PostContextRead(MPD_Context *p);
char *ContextTypeToString(MPD_Context *p);

void InitMPDUser();
bool mpdSetupCryptoClient();
bool mpdSavePasswordToRegistry(TCHAR *szAccount, TCHAR *szPassword, bool persistent=true);
bool mpdReadPasswordFromRegistry(TCHAR *szAccount, TCHAR *szPassword);
bool mpdDeletePasswordRegistryEntry();
char *mpdCryptGetLastErrorString();

#if defined(__cplusplus)
extern "C" {
#endif
char *crypt(const char *buf,const char *salt);
#if defined(__cplusplus)
}
#endif

#endif
