#include "LaunchProcess.h"
#include <stdio.h>
#include "global.h"
#include "..\Common\MPIJobDefs.h"
#include "Translate_Error.h"
#include "mpdutil.h"
#include "mpd.h"
#include "RedirectIO.h"
#include <stdlib.h>

static char s_pszRootHost[MAX_HOST_LENGTH];
static int s_nPort = MPD_DEFAULT_PORT;
static char s_pszPassPhrase[MPD_PASSPHRASE_MAX_LENGTH];
static char s_pszJobId[256];
static char s_pszRankFormat[10];

void PutJobInDatabase(MPIRunLaunchProcessArg *arg)
{
    int error;
    SOCKET sock;
    SYSTEMTIME stime;
    char pszStr[MAX_CMD_LENGTH+1];
    char pszResult[MAX_CMD_LENGTH+1];

    if (!g_bUseJobHost || g_bNoMPI)
	return;

    // save the host
    strcpy(s_pszRootHost, g_pszJobHost);
    // save the passphrase
    if (g_bUseJobMPDPwd)
	strcpy(s_pszPassPhrase, g_pszJobHostMPDPwd);
    else
	strcpy(s_pszPassPhrase, MPD_DEFAULT_PASSPHRASE);
    // save the jobid
    strcpy(s_pszJobId, arg->pszJobID);

    if ((error = ConnectToMPD(s_pszRootHost, s_nPort, s_pszPassPhrase, &sock)) == 0)
    {
	// open the jobs database
	sprintf(pszStr, "dbcreate jobs");
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
	    printf("ERROR: Unable to write '%s' to socket[%d]\n", pszStr, sock);
	    easy_closesocket(sock);
	    return;
	}
	if (!ReadStringTimeout(sock, pszResult, g_nMPIRUN_SHORT_TIMEOUT))
	{
	    printf("ERROR: ReadString failed to read the result of the jobs database query: error %d\n", WSAGetLastError());
	    easy_closesocket(sock);
	    return;
	}
	if (strnicmp(pszResult, "DBS_SUCCESS", 11) != 0)
	{
	    printf("Unable to open the jobs database on '%s'\n%s", s_pszRootHost, pszResult);fflush(stdout);
	    easy_closesocket(sock);
	    return;
	}
	
	// create a database for this job with the jobid as its name
	sprintf(pszStr, "dbcreate %s", arg->pszJobID);
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
	    printf("ERROR: Unable to write '%s' to socket[%d]\n", pszStr, sock);
	    easy_closesocket(sock);
	    return;
	}
	if (!ReadStringTimeout(sock, pszResult, g_nMPIRUN_SHORT_TIMEOUT))
	{
	    printf("ERROR: ReadString failed to read the result of the job database creation request: error %d\n", WSAGetLastError());
	    easy_closesocket(sock);
	    return;
	}
	if (strnicmp(pszResult, "DBS_SUCCESS", 11) != 0)
	{
	    printf("Unable to create the job database(%s) on '%s'\n%s", arg->pszJobID, s_pszRootHost, pszResult);fflush(stdout);
	    easy_closesocket(sock);
	    return;
	}
	
	// get the current time and put it in the jobs database with this jobid
	GetLocalTime(&stime);
	sprintf(pszStr, "dbput jobs:%d.%02d.%02d<%02dh.%02dm.%02ds>:%s@%s", stime.wYear, stime.wMonth, stime.wDay, stime.wHour, stime.wMinute, stime.wSecond, arg->pszAccount, arg->pszJobID);
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
	    printf("ERROR: Unable to write '%s' to socket[%d]\n", pszStr, sock);
	    easy_closesocket(sock);
	    return;
	}
	if (!ReadStringTimeout(sock, pszResult, g_nMPIRUN_SHORT_TIMEOUT))
	{
	    printf("ERROR: ReadString failed to read the result of the jobs timestamp put operation: error %d\n", WSAGetLastError());
	    easy_closesocket(sock);
	    return;
	}
	if (strnicmp(pszResult, "DBS_SUCCESS", 11) != 0)
	{
	    printf("Unable to put the job timestamp in the jobs database on '%s'\n%s", s_pszRootHost, pszResult);fflush(stdout);
	    easy_closesocket(sock);
	    return;
	}
	
	// put the user name in the job database
	sprintf(pszStr, "dbput %s:user:%s", arg->pszJobID, (arg->pszAccount[0] == '\0') ? "<single user mode>" : arg->pszAccount);
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
	    printf("ERROR: Unable to write '%s' to socket[%d]\n", pszStr, sock);
	    easy_closesocket(sock);
	    return;
	}
	if (!ReadStringTimeout(sock, pszResult, g_nMPIRUN_SHORT_TIMEOUT))
	{
	    printf("ERROR: ReadString failed to read the result of the job timestamp put operation: error %d\n", WSAGetLastError());
	    easy_closesocket(sock);
	    return;
	}
	if (strnicmp(pszResult, "DBS_SUCCESS", 11) != 0)
	{
	    printf("ERROR: put operation('%s') failed on '%s'\n%s", pszStr, s_pszRootHost, pszResult);fflush(stdout);
	    easy_closesocket(sock);
	    return;
	}

	// put the size of the parallel process in the job database
	sprintf(pszStr, "dbput %s:nproc:%d", arg->pszJobID, arg->n);
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
	    printf("ERROR: Unable to write '%s' to socket[%d]\n", pszStr, sock);
	    easy_closesocket(sock);
	    return;
	}
	if (!ReadStringTimeout(sock, pszResult, g_nMPIRUN_SHORT_TIMEOUT))
	{
	    printf("ERROR: ReadString failed to read the result of the job nproc put operation: error %d\n", WSAGetLastError());
	    easy_closesocket(sock);
	    return;
	}
	if (strnicmp(pszResult, "DBS_SUCCESS", 11) != 0)
	{
	    printf("ERROR: put operation('%s') failed on '%s'\n%s", pszStr, s_pszRootHost, pszResult);fflush(stdout);
	    easy_closesocket(sock);
	    return;
	}

	// put the state of the job database
	sprintf(pszStr, "dbput %s:state:LAUNCHING", arg->pszJobID);
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
	    printf("ERROR: Unable to write '%s' to socket[%d]\n", pszStr, sock);
	    easy_closesocket(sock);
	    return;
	}
	if (!ReadStringTimeout(sock, pszResult, g_nMPIRUN_SHORT_TIMEOUT))
	{
	    printf("ERROR: ReadString failed to read the result of the job state put operation: error %d\n", WSAGetLastError());
	    easy_closesocket(sock);
	    return;
	}
	if (strnicmp(pszResult, "DBS_SUCCESS", 11) != 0)
	{
	    printf("ERROR: put operation('%s') failed on '%s'\n%s", pszStr, s_pszRootHost, pszResult);fflush(stdout);
	    easy_closesocket(sock);
	    return;
	}

	// close the session with the mpd
	if (WriteString(sock, "done") == SOCKET_ERROR)
	{
	    printf("Error: Unable to write 'done' to socket[%d]\n", sock);
	    easy_closesocket(sock);
	    return;
	}

	easy_closesocket(sock);
    }
    else
    {
	printf("PutJobInDatabase: Connect to %s failed, error %d\n", s_pszRootHost, error);fflush(stdout);
    }
}

void PutJobProcessInDatabase(MPIRunLaunchProcessArg *arg, int pid)
{
    SOCKET sock;
    char pszStr[MAX_CMD_LENGTH+1];
    char pszResult[MAX_CMD_LENGTH+1];
    int error;
    char pszRank[100];
    int extent;

    if (!g_bUseJobHost || g_bNoMPI)
	return;

    if (arg->n < 10)
	extent = 1;
    else if (arg->n < 100)
	extent = 2;
    else if (arg->n < 1000)
	extent = 3;
    else extent = 4;

    sprintf(s_pszRankFormat, "%%0%dd", extent);
    sprintf(pszRank, s_pszRankFormat, arg->i);

    if ((error = ConnectToMPD(s_pszRootHost, s_nPort, s_pszPassPhrase, &sock)) == 0)
    {
	// put host
	sprintf(pszStr, "dbput %s:%shost:%s", arg->pszJobID, pszRank, arg->pszHost);
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
	    printf("ERROR: Unable to write '%s' to socket[%d]\n", pszStr, sock);
	    easy_closesocket(sock);
	    return;
	}
	if (!ReadStringTimeout(sock, pszResult, g_nMPIRUN_SHORT_TIMEOUT))
	{
	    printf("ERROR: ReadString failed to read the result of the put operation: '%s', error %d\n", pszStr, WSAGetLastError());
	    easy_closesocket(sock);
	    return;
	}
	if (strnicmp(pszResult, "DBS_SUCCESS", 11) != 0)
	{
	    printf("ERROR: put operation('%s') failed on '%s'\n%s", pszStr, s_pszRootHost, pszResult);fflush(stdout);
	    easy_closesocket(sock);
	    return;
	}
	
	// put command line
	sprintf(pszStr, "dbput name=%s key=%scmd value=%s", arg->pszJobID, pszRank, arg->pszCmdLine);
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
	    printf("ERROR: Unable to write '%s' to socket[%d]\n", pszStr, sock);
	    easy_closesocket(sock);
	    return;
	}
	if (!ReadStringTimeout(sock, pszResult, g_nMPIRUN_SHORT_TIMEOUT))
	{
	    printf("ERROR: ReadString failed to read the result of the put operation: '%s', error %d\n", pszStr, WSAGetLastError());
	    easy_closesocket(sock);
	    return;
	}
	if (strnicmp(pszResult, "DBS_SUCCESS", 11) != 0)
	{
	    printf("ERROR: put operation('%s') failed on '%s'\n%s", pszStr, s_pszRootHost, pszResult);fflush(stdout);
	    easy_closesocket(sock);
	    return;
	}
	
	// put working directory
	sprintf(pszStr, "dbput name=%s key=%sdir value=%s", arg->pszJobID, pszRank, arg->pszDir);
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
	    printf("ERROR: Unable to write '%s' to socket[%d]\n", pszStr, sock);
	    easy_closesocket(sock);
	    return;
	}
	if (!ReadStringTimeout(sock, pszResult, g_nMPIRUN_SHORT_TIMEOUT))
	{
	    printf("ERROR: ReadString failed to read the result of the put operation: '%s', error %d\n", pszStr, WSAGetLastError());
	    easy_closesocket(sock);
	    return;
	}
	if (strnicmp(pszResult, "DBS_SUCCESS", 11) != 0)
	{
	    printf("ERROR: put operation('%s') failed on '%s'\n%s", pszStr, s_pszRootHost, pszResult);fflush(stdout);
	    easy_closesocket(sock);
	    return;
	}
	
	// put environment variables
	sprintf(pszStr, "dbput %s:%senv:%s", arg->pszJobID, pszRank, arg->pszEnv);
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
	    printf("ERROR: Unable to write '%s' to socket[%d]\n", pszStr, sock);
	    easy_closesocket(sock);
	    return;
	}
	if (!ReadStringTimeout(sock, pszResult, g_nMPIRUN_SHORT_TIMEOUT))
	{
	    printf("ERROR: ReadString failed to read the result of the put operation: '%s', error %d\n", pszStr, WSAGetLastError());
	    easy_closesocket(sock);
	    return;
	}
	if (strnicmp(pszResult, "DBS_SUCCESS", 11) != 0)
	{
	    printf("ERROR: put operation('%s') failed on '%s'\n%s", pszStr, s_pszRootHost, pszResult);fflush(stdout);
	    easy_closesocket(sock);
	    return;
	}
	
	// put process id
	sprintf(pszStr, "dbput %s:%spid:%d", arg->pszJobID, pszRank, pid);
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
	    printf("ERROR: Unable to write '%s' to socket[%d]\n", pszStr, sock);
	    easy_closesocket(sock);
	    return;
	}
	if (!ReadStringTimeout(sock, pszResult, g_nMPIRUN_SHORT_TIMEOUT))
	{
	    printf("ERROR: ReadString failed to read the result of the put operation: '%s', error %d\n", pszStr, WSAGetLastError());
	    easy_closesocket(sock);
	    return;
	}
	if (strnicmp(pszResult, "DBS_SUCCESS", 11) != 0)
	{
	    printf("ERROR: put operation('%s') failed on '%s'\n%s", pszStr, s_pszRootHost, pszResult);fflush(stdout);
	    easy_closesocket(sock);
	    return;
	}

	// close the session with the mpd
	if (WriteString(sock, "done") == SOCKET_ERROR)
	{
	    printf("Error: Unable to write 'done' to socket[%d]\n", sock);
	    easy_closesocket(sock);
	    return;
	}

	easy_closesocket(sock);
    }
    else
    {
	printf("PutJobProcessInRootMPD: Connect to %s failed, error %d\n", s_pszRootHost, error);fflush(stdout);
    }
}

void UpdateJobState(char *state)
{
    SOCKET sock;
    char pszStr[MAX_CMD_LENGTH+1];
    char pszResult[MAX_CMD_LENGTH+1];
    int error;

    if (!g_bUseJobHost || g_bNoMPI)
	return;

    if ((error = ConnectToMPD(s_pszRootHost, s_nPort, s_pszPassPhrase, &sock)) == 0)
    {
	// put the state string
	sprintf(pszStr, "dbput %s:state:%s", s_pszJobId, state);
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
	    printf("ERROR: Unable to write '%s' to socket[%d]\n", pszStr, sock);
	    easy_closesocket(sock);
	    return;
	}
	if (!ReadStringTimeout(sock, pszResult, g_nMPIRUN_SHORT_TIMEOUT))
	{
	    printf("ERROR: ReadString failed to read the result of the put operation: '%s', error %d\n", pszStr, WSAGetLastError());
	    easy_closesocket(sock);
	    return;
	}
	if (strnicmp(pszResult, "DBS_SUCCESS", 11) != 0)
	{
	    printf("ERROR: put operation('%s') failed on '%s'\n%s", pszStr, s_pszRootHost, pszResult);fflush(stdout);
	    easy_closesocket(sock);
	    return;
	}

	// close the session with the mpd
	if (WriteString(sock, "done") == SOCKET_ERROR)
	{
	    printf("Error: Unable to write 'done' to socket[%d]\n", sock);
	    easy_closesocket(sock);
	    return;
	}

	easy_closesocket(sock);
    }
    else
    {
	printf("UpdateJobState(%s): Connect to %s failed, error %d\n", state, s_pszRootHost, error);fflush(stdout);
    }
}

void UpdateJobKeyValue(int rank, char *key, char *value)
{
    SOCKET sock;
    char pszRank[20];
    char pszStr[MAX_CMD_LENGTH+1];
    char pszResult[MAX_CMD_LENGTH+1];
    int error;

    if (!g_bUseJobHost || g_bNoMPI)
	return;

    if ((key == NULL) || (value == NULL) || (rank < 0))
	return;

    sprintf(pszRank, s_pszRankFormat, rank);

    if ((error = ConnectToMPD(s_pszRootHost, s_nPort, s_pszPassPhrase, &sock)) == 0)
    {
	// put the key/value string
	sprintf(pszStr, "dbput name=%s key=%s%s value=%s", s_pszJobId, pszRank, key, value);
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
	    printf("ERROR: Unable to write '%s' to socket[%d]\n", pszStr, sock);
	    easy_closesocket(sock);
	    return;
	}
	if (!ReadStringTimeout(sock, pszResult, g_nMPIRUN_SHORT_TIMEOUT))
	{
	    printf("ERROR: ReadString failed to read the result of the put operation: '%s', error %d\n", pszStr, WSAGetLastError());
	    easy_closesocket(sock);
	    return;
	}
	if (strnicmp(pszResult, "DBS_SUCCESS", 11) != 0)
	{
	    printf("ERROR: put operation('%s') failed on '%s'\n%s", pszStr, s_pszRootHost, pszResult);fflush(stdout);
	    easy_closesocket(sock);
	    return;
	}

	// close the session with the mpd
	if (WriteString(sock, "done") == SOCKET_ERROR)
	{
	    printf("Error: Unable to write 'done' to socket[%d]\n", sock);
	    easy_closesocket(sock);
	    return;
	}

	easy_closesocket(sock);
    }
    else
    {
	printf("UpdateJobKeyValue(%s:%s): Connect to %s failed, error %d\n", key, value, s_pszRootHost, error);fflush(stdout);
    }
}
