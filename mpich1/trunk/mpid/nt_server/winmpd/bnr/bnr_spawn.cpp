#include "bnrimpl.h"
#include "mpdutil.h"
#include "mpd.h"
#include "bsocket.h"
#include <stdio.h>
#include "mpichinfo.h"
#include "redirectio.h"

HANDLE g_hSpawnMutex = NULL;
HANDLE g_hJobThreads[100];
int g_nNumJobThreads = 0;

struct Spawn_struct
{
    Spawn_struct();
    Spawn_struct(int n);
    ~Spawn_struct();

    struct Spawn_node
    {
	int pid;
	int launchid;
	char fwd_host[100];
	int fwd_port;
    };
    int m_nNproc;
    Spawn_node *m_pNode;
    int m_bfd;
    int m_bfdStop;
    HANDLE m_hRedirectIOThread;
    HANDLE m_hThread;
    Spawn_struct *m_pNext;
};

Spawn_struct::Spawn_struct()
{
    m_bfd = BFD_INVALID_SOCKET;
    m_bfdStop = BFD_INVALID_SOCKET;
    m_hRedirectIOThread = NULL;
    m_nNproc = 0;
    m_pNode = NULL;
    m_pNext = NULL;
    m_hThread = NULL;
}

Spawn_struct::Spawn_struct(int n)
{
    Spawn_struct();
    m_nNproc = n;
    m_pNode = new Spawn_node[n];
    for (int i=0; i<n; i++)
    {
	m_pNode[i].fwd_host[0] = '\0';
	m_pNode[i].fwd_port = 0;
	m_pNode[i].launchid = -1;
	m_pNode[i].pid = 0;
    }
}

Spawn_struct::~Spawn_struct()
{
    m_nNproc = 0;
    if (m_pNode)
	delete m_pNode;
    m_pNode = NULL;
    if (m_hRedirectIOThread != NULL)
    {
	if (m_bfdStop != BFD_INVALID_SOCKET)
	{
	    beasy_send(m_bfdStop, "x", 1);
	    if (WaitForSingleObject(m_hRedirectIOThread, 10000) == WAIT_TIMEOUT)
		TerminateThread(m_hRedirectIOThread, 0);
	}
	else
	    TerminateThread(m_hRedirectIOThread, 0);
	CloseHandle(m_hRedirectIOThread);
    }
    m_hRedirectIOThread = NULL;
    if (m_bfd != BFD_INVALID_SOCKET)
    {
	beasy_closesocket(m_bfd);
	m_bfd = BFD_INVALID_SOCKET;
    }
    if (m_bfdStop != BFD_INVALID_SOCKET)
    {
	beasy_closesocket(m_bfdStop);
	m_bfdStop = BFD_INVALID_SOCKET;
    }
    m_pNext = NULL;

    // don't touch the m_hThread member because BNR_Finalize may be waiting on it?
    if (!g_bBNRFinalizeWaiting && m_hThread != NULL)
    {
	CloseHandle(m_hThread);
	m_hThread = NULL;
    }
}

Spawn_struct *g_pSpawnList = NULL;
char g_pszIOHost[100] = "";
int g_nIOPort = 0;

struct HostNode
{
    char pszHost[100];
    int nSMPProcs;
    HostNode *pNext;
};

static bool GetHostsFromFile(char *pszFileName, HostNode **ppNode, int nNumWanted)
{
    FILE *fin;
    char buffer[1024] = "";
    char *pChar, *pChar2;
    HostNode *node = NULL, *list = NULL, *cur_node;

    // check the parameters
    if ((nNumWanted < 1) || (ppNode == NULL))
	return false;

    // open the file
    fin = fopen(pszFileName, "r");
    if (fin == NULL)
    {
	printf("Error: unable to open file '%s', error %d\n", pszFileName, GetLastError());
	return false;
    }
    
    // Read the host names from the file
    while (fgets(buffer, 1024, fin))
    {
	pChar = buffer;
	
	// Advance over white space
	while (*pChar != '\0' && isspace(*pChar))
	    pChar++;
	if (*pChar == '#' || *pChar == '\0')
	    continue;
	
	// Trim trailing white space
	pChar2 = &buffer[strlen(buffer)-1];
	while (isspace(*pChar2) && (pChar >= pChar))
	{
	    *pChar2 = '\0';
	    pChar2--;
	}
	
	// If there is anything left on the line, consider it a host name
	if (strlen(pChar) > 0)
	{
	    node = new HostNode;
	    node->nSMPProcs = 1;
	    node->pNext = NULL;
	    
	    // Copy the host name
	    pChar2 = node->pszHost;
	    while (*pChar != '\0' && !isspace(*pChar))
	    {
		*pChar2 = *pChar;
		pChar++;
		pChar2++;
	    }
	    *pChar2 = '\0';
	    pChar2 = strtok(node->pszHost, ":");
	    pChar2 = strtok(NULL, "\n");
	    if (pChar2 != NULL)
	    {
		node->nSMPProcs = atoi(pChar2);
		if (node->nSMPProcs < 1)
		    node->nSMPProcs = 1;
	    }
	    else
	    {
		// Advance over white space
		while (*pChar != '\0' && isspace(*pChar))
		    pChar++;
		// Get the number of SMP processes
		if (*pChar != '\0')
		{
		    node->nSMPProcs = atoi(pChar);
		    if (node->nSMPProcs < 1)
			node->nSMPProcs = 1;
		}
	    }

	    if (list == NULL)
	    {
		list = node;
		cur_node = node;
	    }
	    else
	    {
		cur_node->pNext = node;
		cur_node = node;
	    }
	}
    }

    fclose(fin);

    if (list == NULL)
	return false;

    // Allocate the first host node
    node = new HostNode;
    int num_left = nNumWanted;
    HostNode *n = list, *target = node;

    // add the nodes to the target list, cycling if necessary
    while (num_left)
    {
	target->pNext = NULL;
	strcpy(target->pszHost, n->pszHost);
	if (num_left <= n->nSMPProcs)
	{
	    target->nSMPProcs = num_left;
	    num_left = 0;
	}
	else
	{
	    target->nSMPProcs = n->nSMPProcs;
	    num_left = num_left - n->nSMPProcs;
	}

	if (num_left)
	{
	    target->pNext = new HostNode;
	    target = target->pNext;
	}

	n = n->pNext;
	if (n == NULL)
	    n = list;
    }
    
    // free the list created from the file
    while (list)
    {
	n = list;
	list = list->pNext;
	delete n;
    }

    // add the generated list to the end of the list passed in
    if (*ppNode == NULL)
    {
	*ppNode = node;
    }
    else
    {
	cur_node = *ppNode;
	while (cur_node->pNext != NULL)
	    cur_node = cur_node->pNext;
	cur_node->pNext = node;
    }

    return true;
}

static void FreeHosts(HostNode *pNode)
{
    HostNode *n;
    while (pNode)
    {
	n = pNode;
	pNode = pNode->pNext;
	delete n;
    }
}

static void GetHost(HostNode *pList, int nRank, char *pszHost)
{
    nRank++;
    while (nRank > 0)
    {
	if (pList == NULL)
	    return;
	nRank = nRank - pList->nSMPProcs;
	if (nRank > 0)
	    pList = pList->pNext;
    }
    if (pList == NULL)
	return;
    strcpy(pszHost, pList->pszHost);
}

static void CreateCommand(int count, int *maxprocs, char **cmds, char ***argvs, int nIproc, char *pszCmd)
{
    int i = 0;
    char **ppArg;

    nIproc++;
    while (nIproc > 0)
    {
	if (i >= count)
	    return;
	nIproc = nIproc - maxprocs[i];
	if (nIproc > 0)
	    i++;
    }
    if (i >= count)
	return;
    sprintf(pszCmd, "\"%s\"", cmds[i]);

    if (argvs == NULL)
	return;

    ppArg = argvs[i];
    while (ppArg)
    {
	strcat(pszCmd, " ");
	strcat(pszCmd, *ppArg);
	ppArg++;
    }
}

static void RemoveSpawnThread(Spawn_struct *pSpawn)
{
    if (g_bBNRFinalizeWaiting)
	return;
    WaitForSingleObject(g_hSpawnMutex, INFINITE);
    for (int i=0; i<g_nNumJobThreads; i++)
    {
	if (g_hJobThreads[i] == pSpawn->m_hThread)
	{
	    g_nNumJobThreads--;
	    g_hJobThreads[i] = g_hJobThreads[g_nNumJobThreads];
	    CloseHandle(pSpawn->m_hThread);
	    pSpawn->m_hThread = NULL;
	    break;
	}
    }
    ReleaseMutex(g_hSpawnMutex);
}

static void RemoveSpawnStruct(Spawn_struct *pSpawn)
{
    Spawn_struct *p, *pTrailer;
    WaitForSingleObject(g_hSpawnMutex, INFINITE);

    if (pSpawn == g_pSpawnList)
    {
	g_pSpawnList = g_pSpawnList->m_pNext;
	ReleaseMutex(g_hSpawnMutex);
	return;
    }
    pTrailer = g_pSpawnList;
    p = g_pSpawnList->m_pNext;
    while (p)
    {
	if (p == pSpawn)
	{
	    pTrailer->m_pNext = p->m_pNext;
	    ReleaseMutex(g_hSpawnMutex);
	    return;
	}
	pTrailer = pTrailer->m_pNext;
	p = p->m_pNext;
    }
    ReleaseMutex(g_hSpawnMutex);
}

void SpawnWaitThread(Spawn_struct *pSpawn)
{
    int i,j;
    char pszStr[1024];
    int nPid;

    for (i=0; i<pSpawn->m_nNproc; i++)
    {
	sprintf(pszStr, "getexitcodewait %d", pSpawn->m_pNode[i].launchid);
	if (WriteString(pSpawn->m_bfd, pszStr) == SOCKET_ERROR)
	{
	    printf("WriteString('%s') failed, error %d\n", pszStr, WSAGetLastError());
	    RemoveSpawnThread(pSpawn);
	    RemoveSpawnStruct(pSpawn);
	    delete pSpawn;
	    return;
	}
    }

    for (i=0; i<pSpawn->m_nNproc; i++)
    {
	if (!ReadString(pSpawn->m_bfd, pszStr))
	{
	    printf("ReadString(exitcode) failed, error %d\n", WSAGetLastError());
	    RemoveSpawnThread(pSpawn);
	    RemoveSpawnStruct(pSpawn);
	    delete pSpawn;
	    return;
	}
	char *token = strtok(pszStr, ":");
	if (token != NULL)
	{
	    token = strtok(NULL, "\n");
	    if (token != NULL)
	    {
		nPid = atoi(token);
		for (j=0; j<pSpawn->m_nNproc; j++)
		{
		    if (pSpawn->m_pNode[j].pid == nPid)
		    {
			if ((j > 0) && ((pSpawn->m_nNproc/2) > j))
			{
			    sprintf(pszStr, "stopforwarder host=%s port=%d abort=no", pSpawn->m_pNode[j].fwd_host, pSpawn->m_pNode[j].fwd_port);
			    if (WriteString(pSpawn->m_bfd, pszStr) == SOCKET_ERROR)
			    {
				printf("WriteString('%s') failed, error %d\n", pszStr, WSAGetLastError());
				RemoveSpawnThread(pSpawn);
				RemoveSpawnStruct(pSpawn);
				delete pSpawn;
				return;
			    }
			}
			sprintf(pszStr, "freeprocess %d", pSpawn->m_pNode[j].launchid);
			if (WriteString(pSpawn->m_bfd, pszStr) == SOCKET_ERROR)
			{
			    printf("WriteString('%s') failed, error %d\n", pszStr, WSAGetLastError());
			    RemoveSpawnThread(pSpawn);
			    RemoveSpawnStruct(pSpawn);
			    delete pSpawn;
			    return;
			}
		    }
		}
	    }
	}
    }

    if (pSpawn->m_bfdStop != BFD_INVALID_SOCKET)
    {
	// tell the redirection thread to stop
	pszStr[0] = 0;
	beasy_send(pSpawn->m_bfdStop, pszStr, 1);
    }

    if (pSpawn->m_hRedirectIOThread != NULL)
    {
	if (WaitForSingleObject(pSpawn->m_hRedirectIOThread, 10000) != WAIT_OBJECT_0)
	{
	    TerminateThread(pSpawn->m_hRedirectIOThread, 0);
	}
	CloseHandle(pSpawn->m_hRedirectIOThread);
	pSpawn->m_hRedirectIOThread = NULL;
    }

    WriteString(pSpawn->m_bfd, "done");
    beasy_closesocket(pSpawn->m_bfd);

    RemoveSpawnThread(pSpawn);
    RemoveSpawnStruct(pSpawn);
    delete pSpawn;
}

int BNR_Spawn_multiple(int count, char **cmds, char ***argvs, 
		       int *maxprocs, void *info, int *errors, 
		       bool_t *same_domain, void *preput_info)
{
    char pszStr[4096];
    int nNproc, nIproc;
    int i, j, error;
    int nNumHostsNeeded = 0;
    char pszHost[100];
    char pszHostFile[MAX_PATH];
    HostNode *pHosts = NULL;
    int flag;
    Spawn_struct *pSpawn = NULL;
    char pszCmd[1024];
    char pszDb[100];
    char pszTemp[1024];
    int nKeys;
    char pszKey[MPICH_MAX_INFO_KEY], pszValue[MPICH_MAX_INFO_VAL];

    // should the user and password be passed in by info?
    // should this information be in each info allowing for multiple user credentials?
    if (info != NULL)
    {
	if (MPICH_Info_get(((MPICH_Info*)info)[0], "user", 100, g_pszBNRAccount, &flag) != MPICH_SUCCESS)
	{
	    printf("Error: MPICH_Info_get('user') failed\n");
	    return BNR_FAIL;
	}
	if (MPICH_Info_get(((MPICH_Info*)info)[0], "password", 100, g_pszBNRPassword, &flag) != MPICH_SUCCESS)
	{
	    printf("Error: MPICH_Info_get('password') failed\n");
	    return BNR_FAIL;
	}
    }

    nNproc = 0;
    for (i=0; i<count; i++)
    {
	if (maxprocs[i] < 1)
	{
	    FreeHosts(pHosts);
	    return BNR_FAIL;
	}
	nNproc += maxprocs[i];
	flag = 0;
	if (MPICH_Info_get(((MPICH_Info*)info)[i], "host", 100, pszHost, &flag) != MPICH_SUCCESS)
	{
	    printf("Error: MPICH_Info_get failed\n");
	    FreeHosts(pHosts);
	    return BNR_FAIL;
	}
	if (flag)
	{
	    // user specified a single host
	    HostNode *n;
	    if (pHosts == NULL)
		pHosts = n = new HostNode;
	    else
	    {
		n = pHosts;
		while (n->pNext != NULL)
		    n = n->pNext;
		n->pNext = new HostNode;
		n = n->pNext;
	    }
	    for (j=0; j<maxprocs[i]; j++)
	    {
		n->nSMPProcs = 1;
		strcpy(n->pszHost, pszHost);
		if (j<maxprocs[i]-1)
		{
		    n->pNext = new HostNode;
		    n = n->pNext;
		}
		else
		    n->pNext = NULL;
	    }
	}
	else
	{
	    flag = 0;
	    if (MPICH_Info_get(((MPICH_Info*)info)[i], "hostfile", MAX_PATH, pszHostFile, &flag) != MPICH_SUCCESS)
	    {
		printf("Error: MPICH_Info_get failed\n");
		FreeHosts(pHosts);
		return BNR_FAIL;
	    }
	    if (flag)
	    {
		// user specified a host file
		if (!GetHostsFromFile(pszHostFile, &pHosts, maxprocs[i]))
		{
		    FreeHosts(pHosts);
		    return BNR_FAIL;
		}
	    }
	    else
	    {
		// user did not specify any hosts
		// create a list of blank host nodes to be filled in later
		nNumHostsNeeded += maxprocs[i];
		HostNode *n;
		if (pHosts == NULL)
		    pHosts = n = new HostNode;
		else
		{
		    n = pHosts;
		    while (n->pNext != NULL)
			n = n->pNext;
		    n->pNext = new HostNode;
		    n = n->pNext;
		}
		for (j=0; j<maxprocs[i]; j++)
		{
		    n->nSMPProcs = 1;
		    n->pszHost[0] = '\0';
		    if (j<maxprocs[i]-1)
		    {
			n->pNext = new HostNode;
			n = n->pNext;
		    }
		    else
			n->pNext = NULL;
		}
	    }
	}
    }

    // fill in the blank host nodes
    if (nNumHostsNeeded > 0)
    {
	sprintf(pszStr, "next %d", nNumHostsNeeded);
	if (WriteString(g_bfdMPD, pszStr) == SOCKET_ERROR)
	{
	    printf("WriteString('%s') failed, error %d\n", pszStr, WSAGetLastError());
	    FreeHosts(pHosts);
	    return BNR_FAIL;
	}
	HostNode *n = pHosts;
	for (i=0; i<nNumHostsNeeded; i++)
	{
	    while (n->pszHost[0] != '\0')
		n = n->pNext;
	    if (!ReadString(g_bfdMPD, n->pszHost))
	    {
		printf("ReadString(next host) failed, error %d\n", WSAGetLastError());
		FreeHosts(pHosts);
		return BNR_FAIL;
	    }
	}
    }

    // allocate a spawn structure to hold all the data structures for this spawn call
    pSpawn = new Spawn_struct(nNproc);
    if (pSpawn == NULL)
    {
	FreeHosts(pHosts);
	return BNR_FAIL;
    }

    // give this spawn its own connection to the mpd
    error = ConnectToMPD(g_pszMPDHost, g_nMPDPort, g_pszMPDPhrase, &pSpawn->m_bfd);
    if (error)
    {
	FreeHosts(pHosts);
	return BNR_FAIL;
    }

    // if there isn't already a host to redirect io to, create one
    if (g_pszIOHost[0] == '\0')
    {
	DWORD dwThreadId;
	HANDLE hEvent;
	RedirectIOArg *pArg = new RedirectIOArg;
	pArg->hReadyEvent = hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	pArg->m_pbfdStopIOSignalSocket = &pSpawn->m_bfdStop;
	pSpawn->m_hRedirectIOThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectIOThread, pArg, 0, &dwThreadId);
	if (pSpawn->m_hRedirectIOThread == NULL)
	{
	    printf("Error: Unable to create the redirect io thread, error %d\n", GetLastError());
	    FreeHosts(pHosts);
	    CloseHandle(hEvent);
	    delete pArg;
	    delete pSpawn;
	    return BNR_FAIL;
	}
	if (WaitForSingleObject(hEvent, 10000) == WAIT_TIMEOUT)
	{
	    printf("Error: timed out waiting for io redirection thread to initialize\n");
	    FreeHosts(pHosts);
	    CloseHandle(hEvent);
	    delete pSpawn;
	    return BNR_FAIL;
	}
	CloseHandle(hEvent);
    }
    strcpy(pSpawn->m_pNode[0].fwd_host, g_pszIOHost);
    pSpawn->m_pNode[0].fwd_port = g_nIOPort;

    // create a database for the spawned processes
    if (WriteString(pSpawn->m_bfd, "dbcreate") == SOCKET_ERROR)
    {
	printf("WriteString('dbcreate') failed, error %d\n", WSAGetLastError());
	FreeHosts(pHosts);
	delete pSpawn;
	return BNR_FAIL;
    }
    if (!ReadString(pSpawn->m_bfd, pszDb))
    {
	printf("ReadString(db) failed, error %d\n", WSAGetLastError());
	FreeHosts(pHosts);
	delete pSpawn;
	return BNR_FAIL;
    }

    // pre-put any data provided into the spawnee's database
    MPICH_Info_get_nkeys((MPICH_Info)preput_info, &nKeys);
    for (i=0; i<nKeys; i++)
    {
	MPICH_Info_get_nthkey((MPICH_Info)preput_info, i, pszKey);
	MPICH_Info_get((MPICH_Info)preput_info, pszKey, MPICH_MAX_INFO_VAL, pszValue, &flag);
	if (flag)
	{
	    BNR_KM_Put(pszDb, pszKey, pszValue);
	}
    }

    // launch each process
    for (nIproc = 0; nIproc < nNproc; nIproc++)
    {
	// get the host name for this process
	GetHost(pHosts, nIproc, pszHost);
	// create the command
	CreateCommand(count, maxprocs, cmds, argvs, nIproc, pszCmd);
	// possibly start an io forwarder
	if ((nIproc > 0) && ((nNproc/2) > nIproc))
	{
	    sprintf(pszStr, "createforwarder host=%s forward=%s:%d",
		pszHost, pSpawn->m_pNode[(nIproc-1)/2].fwd_host, pSpawn->m_pNode[(nIproc-1)/2].fwd_port);
	    if (WriteString(pSpawn->m_bfd, pszStr) == SOCKET_ERROR)
	    {
		printf("WriteString('%s') failed, error %d\n", pszStr, WSAGetLastError());
		FreeHosts(pHosts);
		delete pSpawn;
		return BNR_FAIL;
	    }
	    if (!ReadString(pSpawn->m_bfd, pszStr))
	    {
		printf("ReadString(forwarder port) failed, error %d\n", WSAGetLastError());
		FreeHosts(pHosts);
		delete pSpawn;
		return BNR_FAIL;
	    }
	    strcpy(pSpawn->m_pNode[nIproc].fwd_host, pszHost);
	    pSpawn->m_pNode[nIproc].fwd_port = atoi(pszStr);
	}
	// create the command line
	sprintf(pszStr, "launch h=%s c='%s' 12=%s:%d k=%d e='BNR_SPAWN=yes|BNR_RANK=%d|BNR_SIZE=%d|BNR_DB=%s|BNR_MPD=%s|BNR_IO=%s:%d",
	    pszHost, pszCmd, 
	    pSpawn->m_pNode[(nIproc-1)/2].fwd_host, pSpawn->m_pNode[(nIproc-1)/2].fwd_port,
	    nIproc, nIproc, nNproc, pszDb, pszHost, 
	    pSpawn->m_pNode[(nIproc-1)/2].fwd_host, pSpawn->m_pNode[(nIproc-1)/2].fwd_port);
	if (strlen(g_pszBNRAccount))
	{
	    sprintf(pszTemp, "|BNR_USER=%s|BNR_PWD=%s' a=%s p=%s",
		g_pszBNRAccount, g_pszBNRPassword, g_pszBNRAccount, g_pszBNRPassword);
	    strcat(pszStr, pszTemp);
	}
	else
	    strcat(pszStr, "'");
	// write the launch command
	if (WriteString(pSpawn->m_bfd, pszStr) == SOCKET_ERROR)
	{
	    printf("WriteString('launch h=%s c='%s' ...') failed, error %d\n", pszHost, pszCmd, WSAGetLastError());
	    FreeHosts(pHosts);
	    delete pSpawn;
	    return BNR_FAIL;
	}
	if (!ReadString(pSpawn->m_bfd, pszStr))
	{
	    printf("ReadString(launchid) failed, error %d\n", WSAGetLastError());
	    FreeHosts(pHosts);
	    delete pSpawn;
	    return BNR_FAIL;
	}
	pSpawn->m_pNode[nIproc].launchid = atoi(pszStr);
    }
    FreeHosts(pHosts);
    pHosts = NULL;

    // get the process ids
    for (i=0; i<nNproc; i++)
    {
	sprintf(pszStr, "getpid %d", pSpawn->m_pNode[i].launchid);
	if (WriteString(pSpawn->m_bfd, pszStr) == SOCKET_ERROR)
	{
	    printf("WriteString('%s') failed, error %d\n", pszStr, WSAGetLastError());
	    delete pSpawn;
	    return BNR_FAIL;
	}
	if (!ReadString(pSpawn->m_bfd, pszStr))
	{
	    printf("ReadString(pid) failed, error %d\n", WSAGetLastError());
	    delete pSpawn;
	    return BNR_FAIL;
	}
	pSpawn->m_pNode[i].pid = atoi(pszStr);
	if (pSpawn->m_pNode[i].pid == -1)
	{
	    sprintf(pszStr, "geterror %d", pSpawn->m_pNode[i].launchid);
	    if (WriteString(pSpawn->m_bfd, pszStr) == SOCKET_ERROR)
	    {
		printf("Error: launching process %d failed, unable to determine the error.\nWriting the request for the error message failed, error %d", i, WSAGetLastError());
		delete pSpawn;
		return BNR_FAIL;
	    }
	    if (!ReadString(pSpawn->m_bfd, pszStr))
	    {
		printf("Error: launching process %d failed, unable to determine the error.\nReading the error message failed, error %d", i, WSAGetLastError());
		delete pSpawn;
		return BNR_FAIL;
	    }
	    printf("Error: launching process %d failed, %s\n", i, pszStr);
	    delete pSpawn;
	    return BNR_FAIL;
	}
    }

    // Start a thread to monitor the spawned processes until they all exit and all output has been redirected
    // Add the spawn data structure to the global list
    WaitForSingleObject(g_hSpawnMutex, INFINITE);
    
    pSpawn->m_pNext = g_pSpawnList;
    g_pSpawnList = pSpawn;

    DWORD dwThreadId;
    pSpawn->m_hThread = g_hJobThreads[g_nNumJobThreads] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)SpawnWaitThread, pSpawn, 0, &dwThreadId);
    if (g_hJobThreads[g_nNumJobThreads] == NULL)
    {
	printf("Error: Unable to create the job wait thread, error %d\n", GetLastError());
	g_pSpawnList = pSpawn->m_pNext;
	ReleaseMutex(g_hSpawnMutex);
	delete pSpawn;
	return BNR_FAIL;
    }
    g_nNumJobThreads++;

    ReleaseMutex(g_hSpawnMutex);

    return BNR_SUCCESS;
}
