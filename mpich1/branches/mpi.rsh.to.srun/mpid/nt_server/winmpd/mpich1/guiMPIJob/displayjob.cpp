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

void GetKeyAndValue(char *str, char *key, char *value);

bool GetRankAndOption(char *str, int &rank, char *option)
{
    if (str == NULL)
	return false;
    if (!isdigit(*str))
	return false;
    rank = atoi(str);
    while (isdigit(*str))
	str++;
    if (*str == '\0')
	return false;
    strcpy(option, str);
    return true;
}

struct DisplayJobNode
{
    DisplayJobNode() { key[0] = '\0'; value = NULL; next = NULL; };
    DisplayJobNode(char *k, char *v) { strcpy(key, k); value = strdup(v); };
    ~DisplayJobNode() { if (value != NULL) free(value); key[0] = '\0'; value = NULL; next = NULL; };
    char key[100];
    char *value;
    DisplayJobNode *next;
};

void PrintSortedList(DisplayJobNode *list, CString &strout)
{
    CString str;
    DisplayJobNode *list2 = NULL, *node, *iter;

    // sort from list to list2
    while (list)
    {
	node = list;
	list = list->next;
	node->next = NULL;

	if (list2)
	{
	    if (strcmp(list2->key, node->key) > 0)
	    {
		node->next = list2;
		list2 = node;
	    }
	    else
	    {
		iter = list2;
		while (iter->next != NULL && strcmp(iter->next->key, node->key) <= 0)
		    iter = iter->next;
		node->next = iter->next;
		iter->next = node;
	    }
	}
	else
	{
	    list2 = node;
	}
    }

    // print the sorted list
    while (list2)
    {
	node = list2;
	list2 = list2->next;

	str.Format("%s = %s\r\n", node->key, node->value);
	strout += str;
	delete node;
    }
}

struct CmdAndHostNode
{
    CmdAndHostNode();
    CmdAndHostNode(char *cmdline, int rank);
    ~CmdAndHostNode();
    bool MatchInsert(char *cmd, int rank);
    bool UpdateHost(char *host, int rank);
    void Print(CString &strout);

    struct HostNode
    {
	char host[MAX_HOST_LENGTH];
	int rank;
	HostNode *next;
    };
    char cmdline[4096];
    HostNode *hosts;
    CmdAndHostNode *next;
};

CmdAndHostNode::CmdAndHostNode()
{
    cmdline[0] = '\0';
    hosts = NULL;
    next = NULL;
}

CmdAndHostNode::CmdAndHostNode(char *cmd, int rank)
{
    strcpy(cmdline, cmd);
    hosts = new HostNode;
    hosts->next = NULL;
    hosts->rank = rank;
    hosts->host[0] = '\0';
    next = NULL;
}

CmdAndHostNode::~CmdAndHostNode()
{
    HostNode *n;

    while (hosts)
    {
	n = hosts->next;
	delete hosts;
	hosts = n;
    }
    next = NULL;
    cmdline[0] = '\0';
}

bool CmdAndHostNode::MatchInsert(char *cmd, int rank)
{
    if (stricmp(cmdline, cmd) == 0)
    {
	HostNode *n = new HostNode;
	n->host[0] = '\0';
	n->rank = rank;

	// insert sorted
	if (hosts == NULL)
	{
	    n->next = NULL;
	    hosts = n;
	}
	else
	{
	    HostNode *iter;
	    if (hosts->rank > rank)
	    {
		n->next = hosts;
		hosts = n;
	    }
	    else
	    {
		iter = hosts;
		while (iter->next && iter->next->rank < rank)
		    iter = iter->next;
		n->next = iter->next;
		iter->next = n;
	    }
	}
	return true;
    }

    return false;
}

bool CmdAndHostNode::UpdateHost(char *host, int rank)
{
    HostNode *n = hosts;

    while (n)
    {
	if (n->rank == rank)
	{
	    strcpy(n->host, host);
	    return true;
	}
	n = n->next;
    }
    return false;
}

void CmdAndHostNode::Print(CString &strout)
{
    CString str;
    HostNode *n;

    if (hosts == NULL)
	return;

    // print the command line
    strout += cmdline;
    strout += "\r\n";

    // print the hosts
    n = hosts;
    while (n)
    {
	str.Format("%s(%d) ", n->host, n->rank);
	strout += str;
	n = n->next;
    }
    strout += "\r\n";
}

void PrintFormattedList(DisplayJobNode *list, CString &strout)
{
    int rank;
    char option[100];
    CmdAndHostNode *cmdlist = NULL, *iter;
    DisplayJobNode *node;
    bool found;
    CString str;

    // insert all the commands into CmdAndHostNode structures
    node = list;
    while (node)
    {
	if (GetRankAndOption(node->key, rank, option))
	{
	    if (stricmp(option, "cmd") == 0)
	    {
		found = false;
		iter = cmdlist;
		while (iter)
		{
		    if (iter->MatchInsert(node->value, rank))
		    {
			found = true;
			break;
		    }
		    iter = iter->next;
		}
		if (!found)
		{
		    CmdAndHostNode *h = new CmdAndHostNode(node->value, rank);
		    h->next = cmdlist;
		    cmdlist = h;
		}
	    }
	}
	node = node->next;
    }

    // match the hosts with the commands or print out the key/value pair
    while (list)
    {
	node = list;
	list = list->next;

	if (GetRankAndOption(node->key, rank, option))
	{
	    if (stricmp(option, "host") == 0)
	    {
		found = false;
		iter = cmdlist;
		while (iter)
		{
		    if (iter->UpdateHost(node->value, rank))
		    {
			found = true;
			break;
		    }
		    iter = iter->next;
		}
		if (!found)
		{
		    printf("Unmatched host: %s\n", node->value);
		}
	    }
	}
	else
	{
	    str.Format("%s = %s\r\n", node->key, node->value);
	    strout += str;
	}
	delete node;
    }

    // print the list of hosts and commands
    while (cmdlist)
    {
	iter = cmdlist;
	cmdlist = cmdlist->next;

	iter->Print(strout);
	delete iter;
    }
}

void CGuiMPIJobDlg::GetJobDetails()
{
    char str[CONSOLE_STR_LENGTH+1];
    int error;
    char key[100];
    char value[CONSOLE_STR_LENGTH];
    DisplayJobNode *list = NULL, *node = NULL;
    CString jobstr;

    UpdateData();

    if ((m_job.GetLength() < 1) || (m_sock == INVALID_SOCKET))
	return;

    jobstr = m_job;
    jobstr.Delete(0, jobstr.Find('@')+1);
    jobstr = jobstr.Left(jobstr.Find(' '));

    m_job_details = "";

    sprintf(str, "dbfirst %s", jobstr);
    if (WriteString(m_sock, str) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(value, "writing '%s' failed, %d\r\n", str, error);
	Translate_Error(error, str);
	strcat(value, str);
	MessageBox(value, "Connection Error");
	Disconnect();
	return;
    }
    if (ReadStringTimeout(m_sock, str, MPD_DEFAULT_TIMEOUT))
    {
	if (strcmp(str, "DBS_FAIL") == 0)
	{
	    sprintf(str, "job %s does not exist on %s\r\n", jobstr, m_host);
	    m_job_details = str;
	    UpdateData(FALSE);
	    return;
	}
	if (strcmp(str, "DBS_END") == 0)
	{
	    sprintf(str, "job %s does not exist on %s\r\n", jobstr, m_host);
	    m_job_details = str;
	    UpdateData(FALSE);
	    return;
	}
	GetKeyAndValue(str, key, value);
	node = new DisplayJobNode(key, value);
	node->next = list;
	list = node;
    }
    else
    {
	sprintf(str, "Unable to read the job on %s.", m_host);
	MessageBox(str, "Error");
	return;
    }

    while (true)
    {
	sprintf(str, "dbnext %s", jobstr);
	if (WriteString(m_sock, str) == SOCKET_ERROR)
	{
	    error = WSAGetLastError();
	    sprintf(value, "writing '%s' failed, %d\r\n", str, error);
	    Translate_Error(error, str);
	    strcat(value, str);
	    MessageBox(value, "Connection Error");
	    Disconnect();
	    return;
	}
	if (ReadStringTimeout(m_sock, str, MPD_DEFAULT_TIMEOUT))
	{
	    if (strcmp(str, "DBS_FAIL") == 0)
	    {
		m_job_details = "unexpected error reading the next key/value pair\r\n";
		UpdateData(FALSE);
		return;
	    }
	    if (strcmp(str, "DBS_END") == 0)
	    {
		break;
	    }
	    GetKeyAndValue(str, key, value);
	    node = new DisplayJobNode(key, value);
	    node->next = list;
	    list = node;
	}
	else
	{
	    m_job_details = "unexpected error reading the next key/value pair\r\n";
	    UpdateData(FALSE);
	    return;
	}
    }

    if (m_bFullChecked)
	PrintSortedList(list, m_job_details);
    else
	PrintFormattedList(list, m_job_details);

    UpdateData(FALSE);
}
