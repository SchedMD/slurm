#include "stdafx.h"
#include "guiMPIJob.h"
#include "guiMPIJobDlg.h"
#include "mpd.h"
#include "mpdutil.h"
#include "MPDConnectDlg.h"
#include "Translate_Error.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

bool GetRankAndOption(char *str, int &rank, char *option);
void GetKeyAndValue(char *str, char *key, char *value);

struct KillHostNode
{
    int rank;
    int pid;
    char host[MAX_HOST_LENGTH];
    KillHostNode *next;
};

static KillHostNode *g_pKillList = NULL;

static KillHostNode *GetKillNode(int rank)
{
    KillHostNode *pNode;

    if (g_pKillList == NULL)
    {
	pNode = new KillHostNode;
	pNode->host[0] = '\0';
	pNode->pid = -1;
	pNode->rank = rank;
	pNode->next = NULL;
	
	g_pKillList = pNode;
	return pNode;
    }

    pNode = g_pKillList;
    while (pNode)
    {
	if (pNode->rank == rank)
	    return pNode;
	pNode = pNode->next;
    }

    pNode = new KillHostNode;
    pNode->host[0] = '\0';
    pNode->pid = -1;
    pNode->rank = rank;
    pNode->next = NULL;
    
    pNode->next = g_pKillList;
    g_pKillList = pNode;
    return pNode;
}

static void InsertHost(int rank, char *host)
{
    KillHostNode *pNode;

    pNode = GetKillNode(rank);
    strcpy(pNode->host, host);
}

static void InsertPid(int rank, int pid)
{
    KillHostNode *pNode;

    pNode = GetKillNode(rank);
    pNode->pid = pid;
}

static void FindSaveHostPid(char *key, char *value)
{
    int rank;
    char option[20];

    if (GetRankAndOption(key, rank, option))
    {
	if (strcmp(option, "host") == 0)
	    InsertHost(rank, value);
	else if (strcmp(option, "pid") == 0)
	    InsertPid(rank, atoi(value));
    }
}

void KillJobProcess(char *host, int port, const char *altphrase, int pid)
{
    SOCKET sock;
    char str[256];
    int error;

    if (ConnectToMPD(host, port, (altphrase == NULL) ? MPD_DEFAULT_PASSPHRASE : altphrase, &sock) != 0)
    {
	printf("Error: KillJobProcess(%s:%d) unable to connect to the mpd on %s\n", host, pid, host);
	return;
    }

    sprintf(str, "kill host=%s pid=%d", host, pid);
    if (WriteString(sock, str) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	printf("Error: KillJobProcess, writing '%s' failed, %d\n", str, error);
	Translate_Error(error, str);
	printf("%s\n", str);
	easy_closesocket(sock);
	return;
    }

    if (WriteString(sock, "done") == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	Translate_Error(error, str);
	printf("Error: KillJobProcess, WriteString failed: %d\n%s\n", error, str);
	fflush(stdout);
    }
    easy_closesocket(sock);
}

void KillJobProcesses(int port, const char *altphrase)
{
    KillHostNode *pNode;
    while (g_pKillList)
    {
	pNode = g_pKillList;
	g_pKillList = g_pKillList->next;

	KillJobProcess(pNode->host, port, altphrase, pNode->pid);
	delete pNode;
    }
}

void CGuiMPIJobDlg::OnKillBtn() 
{
    char str[CONSOLE_STR_LENGTH+1];
    int error;
    char key[100];
    char value[CONSOLE_STR_LENGTH];

    CString jobstr;

    UpdateData();

    if ((m_job.GetLength() < 1) || (m_sock == INVALID_SOCKET))
	return;

    jobstr = m_job;
    jobstr.Delete(0, jobstr.Find('@')+1);
    jobstr = jobstr.Left(jobstr.Find(' '));

    HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );

    sprintf(str, "dbfirst %s", jobstr);
    if (WriteString(m_sock, str) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(value, "Error: KillJob, writing '%s' failed, %d\n", str, error);
	Translate_Error(error, str);
	strcat(value, str);
	MessageBox(value, "Connection Error");
	Disconnect();
	SetCursor(hOldCursor);
	return;
    }
    if (ReadStringTimeout(m_sock, str, MPD_DEFAULT_TIMEOUT))
    {
	if (strcmp(str, "DBS_FAIL") == 0)
	{
	    sprintf(value, "job %s does not exist on %s\n", jobstr, m_host);
	    MessageBox(value, "Note");
	    SetCursor(hOldCursor);
	    return;
	}
	if (strcmp(str, "DBS_END") == 0)
	{
	    sprintf(value, "job %s does not exist on %s\n", jobstr, m_host);
	    MessageBox(value, "Note");
	    SetCursor(hOldCursor);
	    return;
	}
	GetKeyAndValue(str, key, value);
	FindSaveHostPid(key, value);
    }
    else
    {
	MessageBox("Unable to read the job", "Connection Error");
	SetCursor(hOldCursor);
	return;
    }

    while (true)
    {
	sprintf(str, "dbnext %s", jobstr);
	if (WriteString(m_sock, str) == SOCKET_ERROR)
	{
	    error = WSAGetLastError();
	    sprintf(value, "Error: KillJob, writing '%s' failed, %d\n", str, error);
	    Translate_Error(error, str);
	    strcat(value, str);
	    MessageBox(value, "Connection Error");
	    Disconnect();
	    SetCursor(hOldCursor);
	    return;
	}
	if (ReadStringTimeout(m_sock, str, MPD_DEFAULT_TIMEOUT))
	{
	    if (strcmp(str, "DBS_FAIL") == 0)
	    {
		MessageBox("KillJob, unexpected error reading the next key/value pair", "Error");
		SetCursor(hOldCursor);
		return;
	    }
	    if (strcmp(str, "DBS_END") == 0)
	    {
		break;
	    }
	    GetKeyAndValue(str, key, value);
	    FindSaveHostPid(key, value);
	}
	else
	{
	    MessageBox("KillJob, unable to read the next job key/value pair", "Error");
	    SetCursor(hOldCursor);
	    return;
	}
    }

    KillJobProcesses(m_port, m_passphrase);

    Sleep(1000); // give a little time for the processes to be cleaned up
    SetCursor(hOldCursor);
    OnRefreshBtn();
}
