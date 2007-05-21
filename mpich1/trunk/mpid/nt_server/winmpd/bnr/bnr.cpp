#include "bnrimpl.h"
#include "mpdutil.h"
#include "mpd.h"
#include "bsocket.h"
#include <stdio.h>

char g_pszDBName[BNR_MAX_DB_NAME_LENGTH] = "";
char g_pszMPDHost[100] = "";
char g_pszBNRAccount[100] = "";
char g_pszBNRPassword[100] = "";
int g_nMPDPort = MPD_DEFAULT_PORT;
char g_pszMPDPhrase[MPD_PASSPHRASE_MAX_LENGTH] = MPD_DEFAULT_PASSPHRASE;
int g_bfdMPD = BFD_INVALID_SOCKET;
int g_nIproc = 0;
int g_nNproc = 1;
int g_bInitFinalized = BNR_FINALIZED;
bool g_bBNRFinalizeWaiting = false;

int BNR_Init(int *spawned)
{
    char *p;
    int error;

    p = getenv("BNR_SPAWN");
    *spawned = (p != NULL) ? 1 : 0;

    if (g_bInitFinalized == BNR_INITIALIZED)
	return BNR_SUCCESS;

    bsocket_init();

    p = getenv("BNR_DB");
    if (p != NULL)
	strcpy(g_pszDBName, p);

    p = getenv("BNR_MPD");
    if (p != NULL)
    {
	strcpy(g_pszMPDHost, p);
	p = strtok(g_pszMPDHost, ":");
	if (p != NULL)
	{
	    p = strtok(NULL, " \t\n");
	    if (p != NULL)
		g_nMPDPort = atoi(p);
	}
    }
    else
    {
	p = getenv("BNR_MPD_HOST");
	if (p != NULL)
	    strcpy(g_pszMPDHost, p);
	else
	{
	    gethostname(g_pszMPDHost, 100);
	}
	p = getenv("BNR_MPD_PORT");
	if (p != NULL)
	    g_nMPDPort = atoi(p);
    }

    p = getenv("BNR_PHRASE");
    if (p != NULL)
    {
	strcpy(g_pszMPDPhrase, p);
	putenv("BNR_PHRASE="); // erase the phrase from the environment
    }

    p = getenv("BNR_RANK");
    if (p != NULL)
	g_nIproc = atoi(p);

    p = getenv("BNR_SIZE");
    if (p != NULL)
	g_nNproc = atoi(p);

    p = getenv("BNR_USER");
    if (p != NULL)
    {
	strcpy(g_pszBNRAccount, p);
	putenv("BNR_USER="); // erase the user name from the environment
    }

    p = getenv("BNR_PWD");
    if (p != NULL)
    {
	strcpy(g_pszBNRPassword, p);
	putenv("BNR_PWD="); // erase the password from the environment
    }

    error = ConnectToMPD(g_pszMPDHost, g_nMPDPort, g_pszMPDPhrase, &g_bfdMPD);
    if (error)
    {
	return BNR_FAIL;
    }

    g_hSpawnMutex = CreateMutex(NULL, FALSE, NULL);

    g_bInitFinalized = BNR_INITIALIZED;

    return BNR_SUCCESS;
}

int BNR_Finalize()
{
    if (g_bInitFinalized == BNR_FINALIZED)
	return BNR_SUCCESS;

    // Close the connection to the mpd, insuring no further spawn calls
    WaitForSingleObject(g_hSpawnMutex, 10000);
    WriteString(g_bfdMPD, "done");
    beasy_closesocket(g_bfdMPD);
    g_bfdMPD = BFD_INVALID_SOCKET;
    g_bBNRFinalizeWaiting = true;
    ReleaseMutex(g_hSpawnMutex);

    // Wait for all spawned jobs to complete
    if (g_nNumJobThreads > 0)
    {
	WaitForMultipleObjects(g_nNumJobThreads, g_hJobThreads, TRUE, INFINITE);
    }

    CloseHandle(g_hSpawnMutex);
    g_hSpawnMutex = NULL;

    bsocket_finalize();

    g_bInitFinalized = BNR_FINALIZED;

    return BNR_SUCCESS;
}

int BNR_Get_size(int *size)
{
    if (g_bInitFinalized == BNR_FINALIZED || size == NULL)
	return BNR_FAIL;

    *size = g_nNproc;

    return BNR_SUCCESS;
}

int BNR_Get_rank(int *rank)
{
    if (g_bInitFinalized == BNR_FINALIZED || rank == NULL)
	return BNR_FAIL;

    *rank = g_nIproc;

    return BNR_SUCCESS;
}

int BNR_Barrier()
{
    if (g_bInitFinalized == BNR_FINALIZED)
	return BNR_FAIL;

    char pszStr[256];
    
    sprintf(pszStr, "barrier name=%s count=%d", g_pszDBName, g_nNproc);
    if (WriteString(g_bfdMPD, pszStr) == SOCKET_ERROR)
    {
	printf("BNR_Barrier: WriteString('%s') failed, error %d\n", pszStr, WSAGetLastError());
	return BNR_FAIL;
    }
    if (!ReadString(g_bfdMPD, pszStr))
    {
	printf("BNR_Barrier: ReadString failed, error %d\n", WSAGetLastError());
	return BNR_FAIL;
    }
    if (strcmp(pszStr, "SUCCESS") == 0)
	return BNR_SUCCESS;

    printf("BNR_Barrier returned: '%s'\n", pszStr);
    return BNR_FAIL;
}

int BNR_KM_Get_my_name(char *dbname)
{
    if (g_bInitFinalized == BNR_FINALIZED || dbname == NULL)
	return BNR_FAIL;

    strcpy(dbname, g_pszDBName);

    return BNR_SUCCESS;
}

int BNR_KM_Get_name_length_max()
{
    return BNR_MAX_DB_NAME_LENGTH;
}

int BNR_KM_Get_key_length_max()
{
    return BNR_MAX_KEY_LEN;
}

int BNR_KM_Get_value_length_max()
{
    return BNR_MAX_VALUE_LEN;
}

int BNR_KM_Create(char * dbname)
{
    if (g_bInitFinalized == BNR_FINALIZED || g_bfdMPD == BFD_INVALID_SOCKET || dbname == NULL)
	return BNR_FAIL;

    if (WriteString(g_bfdMPD, "dbcreate") == SOCKET_ERROR)
    {
	printf("BNR_KM_Create: WriteString('dbcreate') failed, error %d\n", WSAGetLastError());
	return BNR_FAIL;
    }

    if (!ReadString(g_bfdMPD, dbname))
    {
	printf("BNR_KM_Create: ReadString failed, error %d\n", WSAGetLastError());
	return BNR_FAIL;
    }

    return BNR_SUCCESS;
}

int BNR_KM_Destroy(char * dbname)
{
    char str[BNR_MAX_DB_NAME_LENGTH+20];

    if (g_bInitFinalized == BNR_FINALIZED || g_bfdMPD == BFD_INVALID_SOCKET || dbname == NULL)
	return BNR_FAIL;

    sprintf(str, "dbdestroy %s", dbname);
    if (WriteString(g_bfdMPD, str) == SOCKET_ERROR)
    {
	printf("BNR_KM_Destroy: WriteString('%s') failed, error %d\n", str, WSAGetLastError());
	return BNR_FAIL;
    }

    if (!ReadString(g_bfdMPD, str))
    {
	printf("BNR_KM_Destroy('%s'): ReadString failed, error %d\n", dbname, WSAGetLastError());
	return BNR_FAIL;
    }
    if (stricmp(str, DBS_SUCCESS_STR) == 0)
	return BNR_SUCCESS;

    return BNR_FAIL;
}

int BNR_KM_Put(char *dbname, char *key, char *value)
{
    char str[MAX_CMD_LENGTH];
    if (g_bInitFinalized == BNR_FINALIZED || g_bfdMPD == BFD_INVALID_SOCKET || dbname == NULL || key == NULL || value == NULL)
	return BNR_FAIL;

    sprintf(str, "dbput %s:%s:%s", dbname, key, value);
    if (WriteString(g_bfdMPD, str) == SOCKET_ERROR)
    {
	printf("BNR_KM_Put: WriteString('%s') failed, error %d\n", str, WSAGetLastError());
	return BNR_FAIL;
    }

    if (!ReadString(g_bfdMPD, str))
    {
	printf("BNR_KM_Put('%s'): ReadString failed, error %d\n", str, WSAGetLastError());
	return BNR_FAIL;
    }
    if (stricmp(str, DBS_SUCCESS_STR) == 0)
	return BNR_SUCCESS;

    return BNR_SUCCESS;
}

int BNR_KM_Commit(char *dbname)
{
    if (g_bInitFinalized == BNR_FINALIZED || g_bfdMPD == BFD_INVALID_SOCKET || dbname == NULL)
	return BNR_FAIL;

    return BNR_SUCCESS;
}

int BNR_KM_Get(char *dbname, char *key, char *value)
{
    char str[MAX_CMD_LENGTH];
    if (g_bInitFinalized == BNR_FINALIZED || g_bfdMPD == BFD_INVALID_SOCKET || dbname == NULL || key == NULL || value == NULL)
	return BNR_FAIL;

    sprintf(str, "dbget %s:%s:%s", dbname, key, value);
    if (WriteString(g_bfdMPD, str) == SOCKET_ERROR)
    {
	printf("BNR_KM_Get: WriteString('%s') failed, error %d\n", str, WSAGetLastError());
	return BNR_FAIL;
    }

    if (!ReadString(g_bfdMPD, value))
    {
	printf("BNR_KM_Get('%s'): ReadString failed, error %d\n", str, WSAGetLastError());
	return BNR_FAIL;
    }
    if (strcmp(value, DBS_FAIL_STR) == 0)
	return BNR_FAIL;

    return BNR_SUCCESS;
}

int BNR_KM_Iter_first(char *dbname, char *key, char *value)
{
    char str[MAX_CMD_LENGTH];
    char *token;

    if (g_bInitFinalized == BNR_FINALIZED || g_bfdMPD == BFD_INVALID_SOCKET || dbname == NULL || key == NULL || value == NULL)
	return BNR_FAIL;

    sprintf(str, "dbfirst %s", dbname);
    if (WriteString(g_bfdMPD, str) == SOCKET_ERROR)
    {
	printf("BNR_KM_Iter_first: WriteString('%s') failed, error %d\n", str, WSAGetLastError());
	return BNR_FAIL;
    }

    if (!ReadString(g_bfdMPD, str))
    {
	printf("BNR_KM_Iter_first('%s'): ReadString failed, error %d\n", str, WSAGetLastError());
	return BNR_FAIL;
    }
    if (strcmp(str, DBS_FAIL_STR) == 0)
	return BNR_FAIL;
    
    *key = '\0';
    *value = '\0';
    if (strcmp(str, DBS_END_STR) == 0)
	return BNR_SUCCESS;
    token = strtok(str, "=");
    if (token == NULL)
	return BNR_FAIL;

    strcpy(key, str);
    strcpy(value, token);

    return BNR_SUCCESS;
}

int BNR_KM_Iter_next(char *dbname, char *key, char *value)
{
    char str[MAX_CMD_LENGTH];
    char *token;

    if (g_bInitFinalized == BNR_FINALIZED || g_bfdMPD == BFD_INVALID_SOCKET || dbname == NULL || key == NULL || value == NULL)
	return BNR_FAIL;

    sprintf(str, "dbnext %s", dbname);
    if (WriteString(g_bfdMPD, str) == SOCKET_ERROR)
    {
	printf("BNR_KM_Iter_next: WriteString('%s') failed, error %d\n", str, WSAGetLastError());
	return BNR_FAIL;
    }

    if (!ReadString(g_bfdMPD, str))
    {
	printf("BNR_KM_Iter_next('%s'): ReadString failed, error %d\n", str, WSAGetLastError());
	return BNR_FAIL;
    }
    if (strcmp(str, DBS_FAIL_STR) == 0)
	return BNR_FAIL;
    
    *key = '\0';
    *value = '\0';
    if (strcmp(str, DBS_END_STR) == 0)
	return BNR_SUCCESS;
    token = strtok(str, "=");
    if (token == NULL)
	return BNR_FAIL;

    strcpy(key, str);
    strcpy(value, token);

    return BNR_SUCCESS;
}
