#include "mpijob.h"
#include "Translate_Error.h"

struct DisplayJobNode
{
    DisplayJobNode() { key[0] = '\0'; value = NULL; next = NULL; };
    DisplayJobNode(char *k, char *v) { strcpy(key, k); value = strdup(v); };
    ~DisplayJobNode() { if (value != NULL) free(value); key[0] = '\0'; value = NULL; next = NULL; };
    char key[100];
    char *value;
    DisplayJobNode *next;
};

void PrintSortedList(DisplayJobNode *list, FILE *fout)
{
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

	if (fout != NULL)
	    fprintf(fout, "%s = %s\n", node->key, node->value);
	else
	    printf("%s = %s\n", node->key, node->value);
	delete node;
    }
    fflush(stdout);
}

void PrintSortedListToFile(DisplayJobNode *list, char *filename)
{
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

	printf("%s = %s\n", node->key, node->value);
	delete node;
    }
    fflush(stdout);
}

struct CmdAndHostNode
{
    CmdAndHostNode();
    CmdAndHostNode(char *cmdline, int rank);
    ~CmdAndHostNode();
    bool MatchInsert(char *cmd, int rank);
    bool UpdateHost(char *host, int rank);
    void Print();

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

void CmdAndHostNode::Print()
{
    HostNode *n;

    if (hosts == NULL)
	return;

    // print the command line
    printf("%s\n", cmdline);

    // print the hosts
    n = hosts;
    while (n)
    {
	printf("%s(%d) ", n->host, n->rank);
	n = n->next;
    }
    printf("\n");
    fflush(stdout);
}

void PrintFormattedList(DisplayJobNode *list)
{
    int rank;
    char option[100];
    CmdAndHostNode *cmdlist = NULL, *iter;
    DisplayJobNode *node;
    bool found;

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
		    fflush(stdout);
		}
	    }
	}
	else
	{
	    printf("%s = %s\n", node->key, node->value);
	}
	delete node;
    }

    // print the list of hosts and commands
    while (cmdlist)
    {
	iter = cmdlist;
	cmdlist = cmdlist->next;

	iter->Print();
	delete iter;
    }
    fflush(stdout);
}

void DisplayJob(char *job, char *host, int port, char *altphrase, bool bFullOutput, bool bToFile, char *filename)
{
    SOCKET sock;
    char str[CONSOLE_STR_LENGTH+1];
    int error;
    char key[100];
    char value[CONSOLE_STR_LENGTH];
    DisplayJobNode *list = NULL, *node = NULL;
    char localhost[100];

    if (job == NULL || *job == '\0')
	return;

    if (host == NULL)
    {
	gethostname(localhost, 100);
	host = localhost;
    }

    if (ConnectToMPD(host, port, (altphrase == NULL) ? MPD_DEFAULT_PASSPHRASE : altphrase, &sock) != 0)
    {
	printf("Unable to connect to the mpd on %s\n", host);
	fflush(stdout);
	return;
    }

    if (bToFile)
    {
	FILE *fout = fopen(filename, "a+");
	if (fout == NULL)
	{
	    printf("Error: unable to open '%s'\n", filename);
	    fflush(stdout);
	    WriteString(sock, "done");
	    easy_closesocket(sock);
	    return;
	}
	fprintf(fout, "Job %s on %s:\n", job, host);
	fclose(fout);
    }
    else
    {
	printf("Job %s on %s:\n", job, host);
	fflush(stdout);
    }

    sprintf(str, "dbfirst %s", job);
    if (WriteString(sock, str) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	printf("writing '%s' failed, %d\n", str, error);
	Translate_Error(error, str);
	printf("%s\n", str);
	fflush(stdout);
	easy_closesocket(sock);
	return;
    }
    if (ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
    {
	if (strcmp(str, "DBS_FAIL") == 0)
	{
	    printf("job %s does not exist on %s\n", job, host);
	    fflush(stdout);
	    WriteString(sock, "done");
	    easy_closesocket(sock);
	    return;
	}
	if (strcmp(str, "DBS_END") == 0)
	{
	    printf("job %s does not exist on %s\n", job, host);
	    fflush(stdout);
	    WriteString(sock, "done");
	    easy_closesocket(sock);
	    return;
	}
	GetKeyAndValue(str, key, value);
	//printf("%s= %s\n", key, value);
	node = new DisplayJobNode(key, value);
	node->next = list;
	list = node;
    }
    else
    {
	printf("Unable to read the job on %s.\n", host);
	fflush(stdout);
	WriteString(sock, "done");
	easy_closesocket(sock);
	return;
    }

    while (true)
    {
	sprintf(str, "dbnext %s", job);
	if (WriteString(sock, str) == SOCKET_ERROR)
	{
	    error = WSAGetLastError();
	    printf("writing '%s' failed, %d\n", str, error);
	    Translate_Error(error, str);
	    printf("%s\n", str);
	    fflush(stdout);
	    easy_closesocket(sock);
	    return;
	}
	if (ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
	{
	    if (strcmp(str, "DBS_FAIL") == 0)
	    {
		printf("unexpected error reading the next key/value pair\n");
		fflush(stdout);
		WriteString(sock, "done");
		easy_closesocket(sock);
		return;
	    }
	    if (strcmp(str, "DBS_END") == 0)
	    {
		break;
	    }
	    GetKeyAndValue(str, key, value);
	    //printf("%s= %s\n", key, value);
	    node = new DisplayJobNode(key, value);
	    node->next = list;
	    list = node;
	}
	else
	{
	    printf("Unable to read the next job key/value pair on %s.\n", host);
	    fflush(stdout);
	    WriteString(sock, "done");
	    easy_closesocket(sock);
	    return;
	}
    }

    if (WriteString(sock, "done") == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	Translate_Error(error, str);
	printf("WriteString failed: %d\n%s\n", error, str);
	fflush(stdout);
    }
    easy_closesocket(sock);

    if (bFullOutput)
    {
	if (bToFile)
	{
	    FILE *fout = fopen(filename, "a+");
	    if (fout == NULL)
	    {
		printf("Error: DisplayJob, unable to open file '%s'\n", filename);
		fflush(stdout);
		return;
	    }
	    PrintSortedList(list, fout);
	    fclose(fout);
	}
	else
	{
	    PrintSortedList(list, NULL);
	}
    }
    else
	PrintFormattedList(list);
}
