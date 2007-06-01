#include <stdio.h>
#include "..\Common\MPICH_pwd.h"
#include "..\Common\MPIJobDefs.h"
#include <conio.h>
#include <time.h>
#include "Translate_Error.h"
#include "localonly.h"
#include "GetOpt.h"
#include "LaunchProcess.h"
#include "global.h"
#include "WaitThread.h"
#include "mpd.h"
#include "mpdutil.h"
#include "RedirectIO.h"
#include <ctype.h>
#include <stdlib.h>
#include "mpirun.h"
#include "parsecliques.h"

// Prototypes
void ExeToUnc(char *pszExe);

void PrintError(int error, char *msg, ...)
{
    int n;
    va_list list;
    HLOCAL str;
    int num_bytes;

    va_start(list, msg);
    n = vprintf(msg, list);
    va_end(list);
    
    num_bytes = FormatMessage(
	FORMAT_MESSAGE_FROM_SYSTEM |
	FORMAT_MESSAGE_ALLOCATE_BUFFER,
	0,
	error,
	MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
	(LPTSTR) &str,
	0,0);
    
    printf("Error %d: %s", error, (const char *)str);
    fflush(stdout);
    
    LocalFree(str);
}

// Function name	: PrintOptions
// Description	    : 
// Return type		: void 
void PrintOptions()
{
    printf("\n");
    printf("Usage:\n");
    printf("   MPIRun -np #processes [options] executable [args ...]\n");
    printf("   MPIRun [options] configfile [args ...]\n");
    printf("\n");
    printf("mpirun options:\n");
    printf("   -localonly\n");
    printf("   -env \"var1=val1|var2=val2|var3=val3...\"\n");
    printf("   -dir drive:\\my\\working\\directory\n");
    printf("   -map drive:\\\\host\\share\n");
    printf("   -logon\n");
    printf("\n");
    printf("Config file format:\n");
    printf("   >exe c:\\temp\\mpiprogram.exe\n");
    printf("     OR \\\\host\\share\\mpiprogram.exe\n");
    printf("   >[env var1=val1|var2=val2|var3=val3...]\n");
    printf("   >[dir drive:\\my\\working\\directory]\n");
    printf("   >[map drive:\\\\host\\share]\n");
    printf("   >[args arg1 arg2 ...]\n");
    printf("   >hosts\n");
    printf("   >hostname1 #procs [path\\mpiprogram.exe]\n");
    printf("   >hostname2 #procs [path\\mpiprogram.exe]\n");
    printf("   >hostname3 #procs [path\\mpiprogram.exe]\n");
    printf("   >...\n");
    printf("\n");
    printf("bracketed lines are optional\n");
    printf("\n");
    printf("For a list of all mpirun options, execute 'mpirun -help2'\n");
    printf("\n");
}

void PrintExtraOptions()
{
    printf("\n");
    printf("All options to mpirun:\n");
    printf("\n");
    printf("-np x\n");
    printf("  launch x processes\n");
    printf("-localonly x\n");
    printf("-np x -localonly\n");
    printf("  launch x processes on the local machine\n");
    printf("-machinefile filename\n");
    printf("  use a file to list the names of machines to launch on\n");
    printf("-hosts n host1 host2 ... hostn\n");
    printf("-hosts n host1 m1 host2 m2 ... hostn mn\n");
    printf("  launch on the specified hosts\n");
    printf("  the number of processes = m1 + m2 + ... + mn\n");
    printf("-map drive:\\\\host\\share\n");
    printf("  map a drive on all the nodes\n");
    printf("  this mapping will be removed when the processes exit\n");
    printf("-dir drive:\\my\\working\\directory\n");
    printf("  launch processes in the specified directory\n");
    printf("-env \"var1=val1|var2=val2|var3=val3...\"\n");
    printf("  set environment variables before launching the processes\n");
    printf("-logon\n");
    printf("  prompt for user account and password\n");
    printf("-pwdfile filename\n");
    printf("  read the account and password from the file specified\n");
    printf("  put the account on the first line and the password on the second\n");
    printf("-tcp\n");
    printf("  use tcp instead of shared memory on the local machine\n");
    printf("-getphrase\n");
    printf("  prompt for the passphrase to access remote mpds\n");
    printf("-nocolor\n");
    printf("  don't use process specific output coloring\n");
    printf("-nompi\n");
    printf("  launch processes without the mpi startup mechanism\n");
    printf("-nodots\n");
    printf("  don't output dots while logging on the user\n");
    printf("-nomapping\n");
    printf("  don't try to map the current directory on the remote nodes\n");
    printf("-nopopup_debug\n");
    printf("  disable the system popup dialog if the process crashes\n");
    printf("-dbg\n");
    printf("  catch unhandled exceptions\n");
    printf("-jobhost hostname\n");
    printf("  send job information to the specified host\n");
    printf("-jobhostmpdpwd passphrase\n");
    printf("  specify the jobhost passphrase\n");
    printf("-exitcodes\n");
    printf("  print the process exit codes when each process exits.\n");
    printf("-noprompt\n");
    printf("  prevent mpirun from prompting for user credentials.\n");
    printf("-priority class[:level]\n");
    printf("  set the process startup priority class and optionally level.\n");
    printf("  class = 0,1,2,3,4   = idle, below, normal, above, high\n");
    printf("  level = 0,1,2,3,4,5 = idle, lowest, below, normal, above, highest\n");
    printf("  the default is -priority 1:3\n");
    printf("-mpduser\n");
    printf("  use the installed mpd single user ignoring the current user credentials.\n");
    printf("-localroot\n");
    printf("  launch the root process without mpd if the host is local.\n");
    printf("  (This allows the root process to create windows and be debugged.)\n");
    printf("-iproot\n");
    printf("-noiproot\n");
    printf("  use or not the ip address of the root host instead of the host name.\n");
    printf("-mpich2\n");
    printf("  launch an mpich2 application.\n");
    printf("-mpich1\n");
    printf("  launch an mpich1 application.\n");
}

// Function name	: ConnectReadMPDRegistry
// Description	    : 
// Return type		: bool 
// Argument         : char *pszHost
// Argument         : int nPort
// Argument         : char *pszPassPhrase
// Argument         : char *name
// Argument         : char *value
// Argument         : DWORD *length = NULL
bool ConnectReadMPDRegistry(char *pszHost, int nPort, char *pszPassPhrase, char *name, char *value, DWORD *length = NULL)
{
    int error;
    SOCKET sock;
    char pszStr[1024];

    if ((error = ConnectToMPD(pszHost, nPort, pszPassPhrase, &sock)) == 0)
    {
	sprintf(pszStr, "lget %s", name);
	WriteString(sock, pszStr);
	ReadStringTimeout(sock, pszStr, g_nMPIRUN_SHORT_TIMEOUT);
	if (strlen(pszStr))
	{
	    WriteString(sock, "done");
	    easy_closesocket(sock);
	    strcpy(value, pszStr);
	    if (length)
		*length = strlen(pszStr);
	    //printf("ConnectReadMPDRegistry successfully used to read the host entry:\n%s\n", pszStr);fflush(stdout);
	    return true;
	}
	WriteString(sock, "done");
	easy_closesocket(sock);
    }
    else
    {
	//printf("MPIRunLaunchProcess: Connect to %s failed, error %d\n", pszHost, error);fflush(stdout);
    }
    return false;
}

// Function name	: ReadMPDRegistry
// Description	    : 
// Return type		: bool 
// Argument         : char *name
// Argument         : char *value
// Argument         : DWORD *length = NULL
bool ReadMPDRegistry(char *name, char *value, DWORD *length /*= NULL*/)
{
    HKEY tkey;
    DWORD len, result;
    
    // Open the root key
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, MPD_REGISTRY_KEY,
	0, 
	KEY_READ,
	&tkey) != ERROR_SUCCESS)
    {
	//printf("Unable to open the MPD registry key, error %d\n", GetLastError());
	return false;
    }
    
    if (length == NULL)
	len = MAX_CMD_LENGTH;
    else
	len = *length;
    result = RegQueryValueEx(tkey, name, 0, NULL, (unsigned char *)value, &len);
    if (result != ERROR_SUCCESS)
    {
	//printf("Unable to read the mpd registry key '%s', error %d\n", name, GetLastError());
	RegCloseKey(tkey);
	return false;
    }
    if (length != NULL)
	*length = len;
    
    RegCloseKey(tkey);
    return true;
}

// Function name	: ReadCachedPassword
// Description	    : 
// Return type		: bool 
bool ReadCachedPassword()
{
    int nError;
    char szAccount[100];
    char szPassword[300];
    
    TCHAR szKey[256];
    HKEY hRegKey = NULL;
    _tcscpy(szKey, MPICHKEY"\\cache");
    
    if (RegOpenKeyEx(HKEY_CURRENT_USER, szKey, 0, KEY_QUERY_VALUE, &hRegKey) == ERROR_SUCCESS) 
    {
	DWORD dwLength = 100;
	*szAccount = TEXT('\0');
	if ((nError = RegQueryValueEx(
	    hRegKey, 
	    _T("Account"), NULL, 
	    NULL, 
	    (BYTE*)szAccount, 
	    &dwLength))!=ERROR_SUCCESS)
	{
	    //PrintError(nError, "ReadPasswordFromRegistry:RegQueryValueEx(...) failed, error: %d\n", nError);
	    ::RegCloseKey(hRegKey);
	    return false;
	}
	if (_tcslen(szAccount) < 1)
	    return false;

	*szPassword = '\0';
	dwLength = 300;
	if ((nError = RegQueryValueEx(
	    hRegKey, 
	    _T("Password"), NULL, 
	    NULL, 
	    (BYTE*)szPassword, 
	    &dwLength))!=ERROR_SUCCESS)
	{
	    //PrintError(nError, "ReadPasswordFromRegistry:RegQueryValueEx(...) failed, error: %d\n", nError);
	    ::RegCloseKey(hRegKey);
	    return false;
	}

	::RegCloseKey(hRegKey);

	strcpy(g_pszAccount, szAccount);
	DecodePassword(szPassword);
	strcpy(g_pszPassword, szPassword);
	return true;
    }

    return false;
}

// Function name	: CachePassword
// Description	    : 
// Return type		: void
void CachePassword()
{
    int nError;
    char *szEncodedPassword;
    
    TCHAR szKey[256];
    HKEY hRegKey = NULL;
    _tcscpy(szKey, MPICHKEY"\\cache");

    RegDeleteKey(HKEY_CURRENT_USER, szKey);
    if (RegCreateKeyEx(HKEY_CURRENT_USER, szKey,
	0, 
	NULL, 
	REG_OPTION_VOLATILE,
	KEY_ALL_ACCESS, 
	NULL,
	&hRegKey, 
	NULL) != ERROR_SUCCESS) 
    {
	nError = GetLastError();
	//PrintError(nError, "CachePassword:RegDeleteKey(...) failed, error: %d\n", nError);
	return;
    }
    
    // Store the account name
    if ((nError = ::RegSetValueEx(
	hRegKey, _T("Account"), 0, REG_SZ, 
	(BYTE*)g_pszAccount, 
	sizeof(TCHAR)*(_tcslen(g_pszAccount)+1)
	))!=ERROR_SUCCESS)
    {
	//PrintError(nError, "CachePassword:RegSetValueEx(%s) failed, error: %d\n", g_pszAccount, nError);
	::RegCloseKey(hRegKey);
	return;
    }

    // encode the password
    szEncodedPassword = EncodePassword(g_pszPassword);

    // Store the encoded password
    if ((nError = ::RegSetValueEx(
	hRegKey, _T("Password"), 0, REG_SZ, 
	(BYTE*)szEncodedPassword, 
	sizeof(TCHAR)*(_tcslen(szEncodedPassword)+1)
	))!=ERROR_SUCCESS)
    {
	//PrintError(nError, "CachePassword:RegSetValueEx(...) failed, error: %d\n", nError);
	::RegCloseKey(hRegKey);
	free(szEncodedPassword);
	return;
    }

    free(szEncodedPassword);
    ::RegCloseKey(hRegKey);
}

// Function name	: GetHostsFromRegistry
// Description	    : 
// Return type		: bool 
// Argument         : HostNode **list
bool GetHostsFromRegistry(HostNode **list)
{
    // Read the hosts entry
    char pszHosts[MAX_CMD_LENGTH+1];
    DWORD nLength = MAX_CMD_LENGTH;
    if (!ReadMPDRegistry("hosts", pszHosts, &nLength))
    {
	char localhost[100];
	gethostname(localhost, 100);
	if (!ConnectReadMPDRegistry(localhost, MPD_DEFAULT_PORT, MPD_DEFAULT_PASSPHRASE, "hosts", pszHosts, &nLength))
	    return false;
    }

    QVS_Container *phosts;
    phosts = new QVS_Container(pszHosts);
    if (phosts->first(pszHosts, MAX_CMD_LENGTH))
    {
	HostNode *n, *l = new HostNode;
	
	l->next = NULL;
	strncpy(l->host, pszHosts, MAX_HOST_LENGTH);
	l->host[MAX_HOST_LENGTH-1] = '\0';
	l->nSMPProcs = 1;
	n = l;
	while (phosts->next(pszHosts, MAX_CMD_LENGTH))
	{
	    n->next = new HostNode;
	    n = n->next;
	    n->next = NULL;
	    strncpy(n->host, pszHosts, MAX_HOST_LENGTH);
	    n->host[MAX_HOST_LENGTH-1] = '\0';
	    n->nSMPProcs = 1;
	}
	*list = l;
	delete phosts;

	// attempt to put the local host first in the list
	DWORD dwLength = MAX_CMD_LENGTH + 1;
	if (GetComputerName(pszHosts, &dwLength))
	{
	    if (stricmp(l->host, pszHosts) == 0)
		return true;
	    n = l->next;
	    while (n)
	    {
		if (stricmp(n->host, pszHosts) == 0)
		{
		    l->next = n->next;
		    n->next = *list;
		    *list = n;
		    return true;
		}
		n = n->next;
		l = l->next;
	    }
	}
	return true;
    }
    delete phosts;
    return false;
}

// Function name	: GetAvailableHosts
// Description	    : This function requires g_nHosts to have been previously set.
// Return type		: bool 
bool GetAvailableHosts()
{
    HostNode *list = NULL;
    
    //dbg_printf("finding available hosts\n");
    if (g_nHosts > 0)
    {
	if (GetHostsFromRegistry(&list))
	{
	    // Insert the first host into the list
	    g_pHosts = new HostNode;
	    strncpy(g_pHosts->host, list->host, MAX_HOST_LENGTH);
	    g_pHosts->host[MAX_HOST_LENGTH-1] = '\0';
	    strncpy(g_pHosts->exe, g_pszExe, MAX_CMD_LENGTH);
	    g_pHosts->exe[MAX_CMD_LENGTH-1] = '\0';
	    g_pHosts->nSMPProcs = 1;
	    g_pHosts->next = NULL;

	    // add the nodes to the target list, cycling if necessary
	    int num_left = g_nHosts-1;
	    HostNode *n = list->next, *target = g_pHosts;
	    if (n == NULL)
		n = list;
	    while (num_left)
	    {
		target->next = new HostNode;
		target = target->next;
		target->next = NULL;
		strncpy(target->host, n->host, MAX_HOST_LENGTH);
		target->host[MAX_HOST_LENGTH-1] = '\0';
		strncpy(target->exe, g_pHosts->exe, MAX_CMD_LENGTH);
		target->exe[MAX_CMD_LENGTH-1] = '\0';
		target->nSMPProcs = 1;
		
		n = n->next;
		if (n == NULL)
		    n = list;
		
		num_left--;
	    }
	    
	    // free the list
	    while (list)
	    {
		n = list;
		list = list->next;
		delete n;
	    }
	}
	else
	{
	    return false;
	}
    }
    return true;
}

// Function name	: GetHostsFromFile
// Description	    : 
// Return type		: bool 
// Argument         : char *pszFileName
bool GetHostsFromFile(char *pszFileName)
{
    FILE *fin;
    char buffer[1024] = "";
    char *pChar, *pChar2;
    HostNode *node = NULL, *list = NULL, *cur_node;

    fin = fopen(pszFileName, "r");
    if (fin == NULL)
    {
	printf("unable to open file '%s'\n", pszFileName);
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
	    node->next = NULL;
	    node->exe[0] = '\0';
	    
	    // Copy the host name
	    pChar2 = node->host;
	    while (*pChar != '\0' && !isspace(*pChar))
	    {
		*pChar2 = *pChar;
		pChar++;
		pChar2++;
	    }
	    *pChar2 = '\0';
	    pChar2 = strtok(node->host, ":");
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
		cur_node->next = node;
		cur_node = node;
	    }
	}
    }

    fclose(fin);

    if (list == NULL)
	return false;

    // Allocate the first host node
    g_pHosts = new HostNode;
    int num_left = g_nHosts;
    HostNode *n = list, *target = g_pHosts;

    // add the nodes to the target list, cycling if necessary
    while (num_left)
    {
	target->next = NULL;
	strncpy(target->host, n->host, MAX_HOST_LENGTH);
	target->host[MAX_HOST_LENGTH-1] = '\0';
	strncpy(target->exe, g_pszExe, MAX_CMD_LENGTH);
	target->exe[MAX_CMD_LENGTH-1] = '\0';
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
	    target->next = new HostNode;
	    target = target->next;
	}

	n = n->next;
	if (n == NULL)
	    n = list;
    }
    
    // free the list
    while (list)
    {
	n = list;
	list = list->next;
	delete n;
    }

    return true;
}

// Function name	: ParseLineIntoHostNode
// Description	    : 
// Return type		: HostNode* 
// Argument         : char * line
HostNode* ParseLineIntoHostNode(char * line)
{
    char buffer[1024];
    char *pChar, *pChar2;
    HostNode *node = NULL;
    
    strncpy(buffer, line, 1024);
    buffer[1023] = '\0';
    pChar = buffer;
    
    // Advance over white space
    while (*pChar != '\0' && isspace(*pChar))
	pChar++;
    if (*pChar == '#' || *pChar == '\0')
	return NULL;
    
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
	node->next = NULL;
	node->exe[0] = '\0';
	
	// Copy the host name
	pChar2 = node->host;
	while (*pChar != '\0' && !isspace(*pChar))
	{
	    *pChar2 = *pChar;
	    pChar++;
	    pChar2++;
	}
	*pChar2 = '\0';
	
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
	// Advance over the number
	while (*pChar != '\0' && isdigit(*pChar))
	    pChar++;
	
	// Advance over white space
	while (*pChar != '\0' && isspace(*pChar))
	    pChar++;
	// Copy the executable
	if (*pChar != '\0')
	{
	    strncpy(node->exe, pChar, MAX_CMD_LENGTH);
	    node->exe[MAX_CMD_LENGTH-1] = '\0';
	    ExeToUnc(node->exe);
	}
    }
    
    return node;
}

#define PARSE_ERR_NO_FILE  -1
#define PARSE_SUCCESS       0
// Function name	: ParseConfigFile
// Description	    : 
// Return type		: int 
// Argument         : char * filename
int ParseConfigFile(char * filename)
{
    FILE *fin;
    char buffer[1024] = "";

    //dbg_printf("parsing configuration file '%s'\n", filename);

    fin = fopen(filename, "r");
    if (fin == NULL)
    {
	return PARSE_ERR_NO_FILE;
    }
    
    while (fgets(buffer, 1024, fin))
    {
	// Check for the name of the executable
	if (strnicmp(buffer, "exe ", 4) == 0)
	{
	    char *pChar = &buffer[4];
	    while (isspace(*pChar))
		pChar++;
	    strncpy(g_pszExe, pChar, MAX_CMD_LENGTH);
	    g_pszExe[MAX_CMD_LENGTH-1] = '\0';
	    pChar = &g_pszExe[strlen(g_pszExe)-1];
	    while (isspace(*pChar) && (pChar >= g_pszExe))
	    {
		*pChar = '\0';
		pChar--;
	    }
	    ExeToUnc(g_pszExe);
	}
	else
	{
	    // Check for program arguments
	    if (strnicmp(buffer, "args ", 5) == 0)
	    {
		char *pChar = &buffer[5];
		while (isspace(*pChar))
		    pChar++;
		strncpy(g_pszArgs, pChar, MAX_CMD_LENGTH);
		g_pszArgs[MAX_CMD_LENGTH-1] = '\0';
		pChar = &g_pszArgs[strlen(g_pszArgs)-1];
		while (isspace(*pChar) && (pChar >= g_pszArgs))
		{
		    *pChar = '\0';
		    pChar--;
		}
	    }
	    else
	    {
		// Check for environment variables
		if (strnicmp(buffer, "env ", 4) == 0)
		{
		    char *pChar = &buffer[4];
		    while (isspace(*pChar))
			pChar++;
		    if (strlen(pChar) >= MAX_CMD_LENGTH)
		    {
			printf("Warning: environment variables truncated.\n");
			fflush(stdout);
		    }
		    strncpy(g_pszEnv, pChar, MAX_CMD_LENGTH);
		    g_pszEnv[MAX_CMD_LENGTH-1] = '\0';
		    pChar = &g_pszEnv[strlen(g_pszEnv)-1];
		    while (isspace(*pChar) && (pChar >= g_pszEnv))
		    {
			*pChar = '\0';
			pChar--;
		    }
		}
		else
		{
		    if (strnicmp(buffer, "map ", 4) == 0)
		    {
			char *pszMap;
			pszMap = &buffer[strlen(buffer)-1];
			while (isspace(*pszMap) && (pszMap >= buffer))
			{
			    *pszMap = '\0';
			    pszMap--;
			}
			pszMap = &buffer[4];
			while (isspace(*pszMap))
			    pszMap++;
			if (*pszMap != '\0' && strlen(pszMap) > 6 && pszMap[1] == ':')
			{
			    MapDriveNode *pNode = new MapDriveNode;
			    pNode->cDrive = pszMap[0];
			    strcpy(pNode->pszShare, &pszMap[2]);
			    pNode->pNext = g_pDriveMapList;
			    g_pDriveMapList = pNode;
			}
		    }
		    else
		    {
			if (strnicmp(buffer, "dir ", 4) == 0)
			{
			    char *pChar = &buffer[4];
			    while (isspace(*pChar))
				pChar++;
			    strcpy(g_pszDir, pChar);
			    pChar = &g_pszDir[strlen(g_pszDir)-1];
			    while (isspace(*pChar) && (pChar >= g_pszDir))
			    {
				*pChar = '\0';
				pChar--;
			    }
			}
			else
			{
			    // Check for hosts
			    if (strnicmp(buffer, "hosts", 5) == 0)
			    {
				g_nHosts = 0;
				g_pHosts = NULL;
				HostNode *node, dummy;
				dummy.next = NULL;
				node = &dummy;
				while (fgets(buffer, 1024, fin))
				{
				    node->next = ParseLineIntoHostNode(buffer);
				    if (node->next != NULL)
				    {
					node = node->next;
					g_nHosts++;
				    }
				}
				g_pHosts = dummy.next;
				
				fclose(fin);
				return PARSE_SUCCESS;
			    }
			}
		    }
		}
	    }
	}
    }
    fclose(fin);
    return PARSE_SUCCESS;
}

// Function name	: GetAccountAndPasswordFromFile
// Description	    : Attempts to read the password from a file.
//                        upon failure it exits
// Return type		: void 
// Argument         : char pszFileName
void GetAccountAndPasswordFromFile(char *pszFileName)
{
    char line[1024];
    FILE *fin;

    // open the file
    fin = fopen(pszFileName, "r");
    if (fin == NULL)
    {
	printf("Error, unable to open account file '%s'\n", pszFileName);
	exit(0);
    }

    // read the account
    if (!fgets(line, 1024, fin))
    {
	printf("Error, unable to read the account in '%s'\n", pszFileName);
	exit(0);
    }

    // strip off the newline characters
    while (strlen(line) && (line[strlen(line)-1] == '\r' || line[strlen(line)-1] == '\n'))
	line[strlen(line)-1] = '\0';
    if (strlen(line) == 0)
    {
	printf("Error, first line in password file must be the account name. (%s)\n", pszFileName);
	exit(0);
    }

    // save the account
    strcpy(g_pszAccount, line);

    // read the password
    if (!fgets(line, 1024, fin))
    {
	printf("Error, unable to read the password in '%s'\n", pszFileName);
	exit(0);
    }
    // strip off the newline characters
    while (strlen(line) && (line[strlen(line)-1] == '\r' || line[strlen(line)-1] == '\n'))
	line[strlen(line)-1] = '\0';

    // save the password
    if (strlen(line))
	strcpy(g_pszPassword, line);
    else
	g_pszPassword[0] = '\0';
}

// Function name	: GetAccountAndPassword
// Description	    : Attempts to read the password from the registry, 
//	                  upon failure it requests the user to provide one
// Return type		: void 
void GetAccountAndPassword()
{
    char ch = 0;
    int index = 0;

    fprintf(stderr, "Mpd needs an account to launch processes with:\n");
    do
    {
	fprintf(stderr, "account (domain\\user): ");
	fflush(stderr);
	gets(g_pszAccount);
    } 
    while (strlen(g_pszAccount) == 0);
    
    fprintf(stderr, "password: ");
    fflush(stderr);
    
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD dwMode;
    if (!GetConsoleMode(hStdin, &dwMode))
	dwMode = ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_MOUSE_INPUT;
    SetConsoleMode(hStdin, dwMode & ~ENABLE_ECHO_INPUT);
    gets(g_pszPassword);
    SetConsoleMode(hStdin, dwMode);
    
    fprintf(stderr, "\n");
}

// Function name	: GetMPDPassPhrase
// Description	    : 
// Return type		: void 
// Argument         : char *phrase
void GetMPDPassPhrase(char *phrase)
{
    fprintf(stderr, "mpd password: ");
    fflush(stderr);
    
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD dwMode;
    if (!GetConsoleMode(hStdin, &dwMode))
	dwMode = ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_MOUSE_INPUT;
    SetConsoleMode(hStdin, dwMode & ~ENABLE_ECHO_INPUT);
    gets(phrase);
    SetConsoleMode(hStdin, dwMode);
    
    fprintf(stderr, "\n");
}

// Function name	: CreateJobIDFromTemp
// Description	    : 
// Return type		: void 
// Argument         : char * pszJobID
void CreateJobIDFromTemp(char * pszJobID)
{
    // Use the name of a temporary file as the job id
    char tFileName[MAX_PATH], tBuffer[MAX_PATH], *pChar;
    // Create a temporary file to get a unique name
    GetTempFileName(".", "mpi", 0, tFileName);
    // Get just the file name part
    GetFullPathName(tFileName, MAX_PATH, tBuffer, &pChar);
    // Delete the file
    DeleteFile(tFileName);
    // Use the filename as the jobid
    strcpy(pszJobID, pChar);
}

// Function name	: CreateJobID
// Description	    : 
// Return type		: void 
// Argument         : char * pszJobID
void CreateJobID(char * pszJobID)
{
    DWORD ret_val, job_number = 0, type, num_bytes = sizeof(DWORD);
    HANDLE hMutex = CreateMutex(NULL, FALSE, "MPIJobNumberMutex");
    char pszHost[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    HKEY hKey;
    
    // Synchronize access to the job number in the registry
    if ((ret_val = WaitForSingleObject(hMutex, 3000)) != WAIT_OBJECT_0)
    {
	CloseHandle(hMutex);
	CreateJobIDFromTemp(pszJobID);
	return;
    }
    
    // Open the MPICH root key
    if ((ret_val = RegOpenKeyEx(
	HKEY_LOCAL_MACHINE, 
	MPICHKEY,
	0, KEY_READ | KEY_WRITE, &hKey)) != ERROR_SUCCESS)
    {
	ReleaseMutex(hMutex);
	CloseHandle(hMutex);
	CreateJobIDFromTemp(pszJobID);
	return;
    }
    
    // Read the job number
    if ((ret_val = RegQueryValueEx(hKey, "Job Number", 0, &type, (BYTE *)&job_number, &num_bytes)) != ERROR_SUCCESS)
    {
	RegCloseKey(hKey);
	ReleaseMutex(hMutex);
	CloseHandle(hMutex);
	CreateJobIDFromTemp(pszJobID);
	return;
    }
    
    // Increment the job number and write it back to the registry
    job_number++;
    if ((ret_val = RegSetValueEx(hKey, "Job Number", 0, REG_DWORD, (CONST BYTE *)&job_number, sizeof(DWORD))) != ERROR_SUCCESS)
    {
	RegCloseKey(hKey);
	ReleaseMutex(hMutex);
	CloseHandle(hMutex);
	CreateJobIDFromTemp(pszJobID);
	return;
    }
    
    RegCloseKey(hKey);
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    
    if (!GetComputerName(pszHost, &size))
    {
	strcpy(pszHost, "tmphost");
    }
    
    sprintf(pszJobID, "%s.%d", pszHost, job_number);
}

// Function name	: PrintDots
// Description	    : 
// Return type		: void 
// Argument         : HANDLE hEvent
void PrintDots(HANDLE hEvent)
{
    if (WaitForSingleObject(hEvent, 3000) == WAIT_TIMEOUT)
    {
	printf(".");fflush(stdout);
	while (WaitForSingleObject(hEvent, 1000) == WAIT_TIMEOUT)
	{
	    printf(".");fflush(stdout);
	}
    }
    CloseHandle(hEvent);
}

// Function name	: NeedToMap
// Description	    : 
// Return type		: bool 
// Argument         : char *pszFullPath
// Argument         : char *pDrive
// Argument         : char *pszShare
// Argument         : char *pszDir
bool NeedToMap(char *pszFullPath, char *pDrive, char *pszShare)//, char *pszDir)
{
    DWORD dwResult;
    DWORD dwLength;
    char pBuffer[4096];
    REMOTE_NAME_INFO *info = (REMOTE_NAME_INFO*)pBuffer;
    char pszTemp[MAX_CMD_LENGTH];

    if (*pszFullPath == '"')
    {
	strncpy(pszTemp, &pszFullPath[1], MAX_CMD_LENGTH);
	pszTemp[MAX_CMD_LENGTH-1] = '\0';
	if (pszTemp[strlen(pszTemp)-1] == '"')
	    pszTemp[strlen(pszTemp)-1] = '\0';
	pszFullPath = pszTemp;
    }
    dwLength = 4096;
    info->lpConnectionName = NULL;
    info->lpRemainingPath = NULL;
    info->lpUniversalName = NULL;
    dwResult = WNetGetUniversalName(pszFullPath, REMOTE_NAME_INFO_LEVEL, info, &dwLength);
    if (dwResult == NO_ERROR)
    {
	*pDrive = *pszFullPath;
	strcpy(pszShare, info->lpConnectionName);
	return true;
    }

    //printf("WNetGetUniversalName: '%s'\n error %d\n", pszExe, dwResult);
    return false;
}

// Function name	: ExeToUnc
// Description	    : 
// Return type		: void 
// Argument         : char *pszExe
void ExeToUnc(char *pszExe)
{
    DWORD dwResult;
    DWORD dwLength;
    char pBuffer[4096];
    REMOTE_NAME_INFO *info = (REMOTE_NAME_INFO*)pBuffer;
    char pszTemp[MAX_CMD_LENGTH];
    bool bQuoted = false;
    char *pszOriginal;

    pszOriginal = pszExe;

    if (*pszExe == '"')
    {
	bQuoted = true;
	strncpy(pszTemp, &pszExe[1], MAX_CMD_LENGTH);
	pszTemp[MAX_CMD_LENGTH-1] = '\0';
	if (pszTemp[strlen(pszTemp)-1] == '"')
	    pszTemp[strlen(pszTemp)-1] = '\0';
	pszExe = pszTemp;
    }
    dwLength = 4096;
    info->lpConnectionName = NULL;
    info->lpRemainingPath = NULL;
    info->lpUniversalName = NULL;
    dwResult = WNetGetUniversalName(pszExe, REMOTE_NAME_INFO_LEVEL, info, &dwLength);
    if (dwResult == NO_ERROR)
    {
	if (bQuoted)
	    sprintf(pszOriginal, "\"%s\"", info->lpUniversalName);
	else
	    strcpy(pszOriginal, info->lpUniversalName);
    }
}

static void StripArgs(int &argc, char **&argv, int n)
{
    if (n+1 > argc)
    {
	printf("Error: cannot strip %d args, only %d left.\n", n, argc-1);
    }
    for (int i=n+1; i<=argc; i++)
    {
	argv[i-n] = argv[i];
    }
    argc -= n;
}

static bool isnumber(char *str)
{
    int i, n = strlen(str);
    for (i=0; i<n; i++)
    {
	if (!isdigit(str[i]))
	    return false;
    }
    return true;
}

bool ReadMPDDefault(char *str)
{
    DWORD length = 100;
    char value[100] = "no";

    if (ReadMPDRegistry(str, value, &length))
    {
	if ((stricmp(value, "yes") == 0) ||
	    (stricmp(value, "y") == 0) ||
	    (stricmp(value, "1") == 0))
	    return true;
    }

    return false;
}

bool CreateShmCliqueString(HostNode *pHosts, char *str)
{
    int i, iProc, iterProc, nProc = 0;
    HostNode *n, *iter;
    bool *pDone = NULL;
    char *strOrig = str;
    bool bRemoveRoot = false;

    str[0] = '\0';

    if (pHosts == NULL)
    {
	char temp[20];
	nProc = g_nHosts;
	for (i=0; i<nProc; i++)
	{
	    sprintf(temp, "(%d)", i);
	    strcat(str, temp);
	}
	return true;
    }

    if (g_bLocalRoot && HostIsLocal(pHosts->host))
	bRemoveRoot = true;

    n = pHosts;
    while (n)
    {
	nProc += n->nSMPProcs;
	n = n->next;
    }

    if (nProc < 1)
	return false;

    pDone = new bool[nProc];
    for (i=0; i<nProc; i++)
	pDone[i] = false;

    n = pHosts;
    iProc = 0;
    while (n)
    {
	if (!pDone[iProc])
	{
	    pDone[iProc] = true;
	    str += sprintf(str, "(%d", iProc);
	    iProc++;
	    for (i=1; i<n->nSMPProcs; i++)
	    {
		pDone[iProc] = true;
		str += sprintf(str, ",%d", iProc);
		iProc++;
	    }
	    iter = n->next;
	    iterProc = iProc;
	    while (iter)
	    {
		if (stricmp(iter->host, n->host) == 0)
		{
		    pDone[iterProc] = true;
		    str += sprintf(str, ",%d", iterProc);
		    iterProc++;
		    for (i=1; i<iter->nSMPProcs; i++)
		    {
			pDone[iterProc] = true;
			str += sprintf(str, ",%d", iterProc);
			iterProc++;
		    }
		}
		else
		{
		    for (i=0; i<iter->nSMPProcs; i++)
			iterProc++;
		}
		iter = iter->next;
	    }
	    str += sprintf(str, ")");
	}
	else
	{
	    for (i=0; i<n->nSMPProcs; i++)
		iProc++;
	}
	n = n->next;
    }

    if (bRemoveRoot)
    {
	if (strOrig[2] == '.')
	    strOrig[1] = '1';
	else
	    strOrig[1] = ' ';
	if (strOrig[2] == ',')
	    strOrig[2] = ' ';
	strcat(strOrig, "(0)");
    }
    delete [] pDone;
    return true;
}

void CreateSingleShmCliqueString(int nCliqueCount, int *pMembers, char *pszSingleShmCliqueString)
{
    int i;
    char *str = pszSingleShmCliqueString;

    if (nCliqueCount < 1)
	return;

    str += sprintf(str, "(");
    str += sprintf(str, "%d", pMembers[0]);
    for (i=1; i<nCliqueCount; i++)
    {
	str += sprintf(str, ",%d", pMembers[i]);
    }
    sprintf(str, ")");
    //printf("CreateSingleShmCliqueString produced: %s\n", pszSingleShmCliqueString);
    //exit(0);
}

void SetupTimeouts()
{
    char pszTimeout[20];
    DWORD length;
    char *pszEnvVariable;

    length = 20;
    if (ReadMPDRegistry("timeout", pszTimeout, &length))
    {
	g_nLaunchTimeout = atoi(pszTimeout);
	if (g_nLaunchTimeout < 1)
	    g_nLaunchTimeout = MPIRUN_DEFAULT_TIMEOUT;
    }
    length = 20;
    if (ReadMPDRegistry("short_timeout", pszTimeout, &length))
    {
	g_nMPIRUN_SHORT_TIMEOUT = atoi(pszTimeout);
	if (g_nMPIRUN_SHORT_TIMEOUT < 1)
	    g_nMPIRUN_SHORT_TIMEOUT = MPIRUN_SHORT_TIMEOUT;
    }
    length = 20;
    if (ReadMPDRegistry("startup_timeout", pszTimeout, &length))
    {
	g_nMPIRUN_CREATE_PROCESS_TIMEOUT = atoi(pszTimeout);
	if (g_nMPIRUN_CREATE_PROCESS_TIMEOUT < 1)
	    g_nMPIRUN_CREATE_PROCESS_TIMEOUT = MPIRUN_CREATE_PROCESS_TIMEOUT;
    }
    pszEnvVariable = getenv("MPIRUN_SHORT_TIMEOUT");
    if (pszEnvVariable)
    {
	g_nMPIRUN_SHORT_TIMEOUT = atoi(pszEnvVariable);
	if (g_nMPIRUN_SHORT_TIMEOUT < 1)
	    g_nMPIRUN_SHORT_TIMEOUT = MPIRUN_SHORT_TIMEOUT;
    }
    pszEnvVariable = getenv("MPIRUN_STARTUP_TIMEOUT");
    if (pszEnvVariable)
    {
	g_nMPIRUN_CREATE_PROCESS_TIMEOUT = atoi(pszEnvVariable);
	if (g_nMPIRUN_CREATE_PROCESS_TIMEOUT < 1)
	    g_nMPIRUN_CREATE_PROCESS_TIMEOUT = MPIRUN_CREATE_PROCESS_TIMEOUT;
    }
}

bool VerifyProcessMPIFinalized(char *pmi_host, int pmi_port, char *phrase, char *pmi_kvsname, int rank, bool &bFinalized)
{
    int error;
    SOCKET sock;
    char str[256];
    if ((error = ConnectToMPD(pmi_host, pmi_port, phrase, &sock)) == 0)
    {
	sprintf(str, "dbget name='%s' key='P-%d.finalized'", pmi_kvsname, rank);
	WriteString(sock, str);
	ReadString(sock, str);
	if (strcmp(str, "true") == 0)
	    bFinalized = true;
	else
	    bFinalized = false;
	WriteString(sock, "done");
	easy_closesocket(sock);
	return true;
    }

    printf("Unable to connect to mpd at %s:%d\n", pmi_host, pmi_port);
    bFinalized = false;
    return false;
}

bool CreatePMIDatabase(char *pmi_host, int pmi_port, char *phrase, char *pmi_kvsname)
{
    int error;
    SOCKET sock;
    if ((error = ConnectToMPD(pmi_host, pmi_port, phrase, &sock)) == 0)
    {
	WriteString(sock, "dbcreate");
	ReadString(sock, pmi_kvsname);
	WriteString(sock, "done");
	easy_closesocket(sock);
	return true;
    }

    printf("Unable to connect to mpd at %s:%d\n", pmi_host, pmi_port);
    return false;
}

bool DestroyPMIDatabase(char *pmi_host, int pmi_port, char *phrase, char *pmi_kvsname)
{
    int error;
    SOCKET sock;
    char str[256];

    if ((error = ConnectToMPD(pmi_host, pmi_port, phrase, &sock)) == 0)
    {
	sprintf(str, "dbdestroy %s", pmi_kvsname);
	WriteString(sock, str);
	ReadString(sock, str);
	WriteString(sock, "done");
	easy_closesocket(sock);
	return true;
    }

    printf("Unable to connect to mpd at %s:%d\n", pmi_host, pmi_port);
    return false;
}

// Function name	: main
// Description	    : 
// Return type		: void 
// Argument         : int argc
// Argument         : char *argv[]
int main(int argc, char *argv[])
{
    int i;
    int iproc = 0;
    char pszJobID[100];
    char pszEnv[MAX_CMD_LENGTH] = "";
    HANDLE *pThread = NULL;
    int nShmLow, nShmHigh;
    DWORD dwThreadID;
    bool bLogon = false;
    char pBuffer[MAX_CMD_LENGTH];
    //char phrase[MPD_PASSPHRASE_MAX_LENGTH + 1];// = MPD_DEFAULT_PASSPHRASE;
    bool bLogonDots = true;
    HANDLE hStdout;
    char cMapDrive, pszMapShare[MAX_PATH];
    int nArgsToStrip;
    bool bRunLocal;
    char pszMachineFileName[MAX_PATH] = "";
    bool bUseMachineFile;
    bool bDoSMP;
    bool bPhraseNeeded;
    DWORD dwType;
    bool bUsePwdFile = false;
    char pszPwdFileName[MAX_PATH];
    bool bUseDebugFlag = false;
    DWORD length;
    WSADATA wsaData;
    int err;
    bool bNoDriveMapping = false;
    bool bCredentialsPrompt = true;
    bool bUsePriorities = false;
    int nPriorityClass = 1;
    int nPriority = 3;
    char pszShmCliqueString[MAX_CMD_LENGTH];
    char pszSingleShmCliqueString[MAX_CMD_LENGTH];
    int nCliqueCount, *pMembers;
    int iter;
    DWORD pmi_host_length = MAX_HOST_LENGTH;

    if (argc < 2)
    {
	PrintOptions();
	return 0;
    }

    SetConsoleCtrlHandler(CtrlHandlerRoutine, TRUE);

    if ((err = WSAStartup( MAKEWORD( 2, 0 ), &wsaData )) != 0)
    {
	printf("Winsock2 dll not initialized, error %d ", err);
	switch (err)
	{
	case WSASYSNOTREADY:
	    printf("Indicates that the underlying network subsystem is not ready for network communication.\n");
	    break;
	case WSAVERNOTSUPPORTED:
	    printf("The version of Windows Sockets support requested is not provided by this particular Windows Sockets implementation.\n");
	    break;
	case WSAEINPROGRESS:
	    printf("A blocking Windows Sockets 1.1 operation is in progress.\n");
	    break;
	case WSAEPROCLIM:
	    printf("Limit on the number of tasks supported by the Windows Sockets implementation has been reached.\n");
	    break;
	case WSAEFAULT:
	    printf("The lpWSAData is not a valid pointer.\n");
	    break;
	default:
	    Translate_Error(err, pBuffer);
	    printf("%s\n", pBuffer);
	    break;
	}
	return 0;
    }

    GetComputerName(pmi_host, &pmi_host_length);

    // Set defaults
    g_bDoMultiColorOutput = !ReadMPDDefault("nocolor");
    bRunLocal = false;
    g_bNoMPI = false;
    bLogon = false;
    bLogonDots = !ReadMPDDefault("nodots");
    GetCurrentDirectory(MAX_PATH, g_pszDir);
    bUseMachineFile = false;
    bDoSMP = true;
    pmi_phrase[0] = '\0';
    bPhraseNeeded = true;
    g_nHosts = 0;
    g_pHosts = NULL;
    bNoDriveMapping = ReadMPDDefault("nomapping");
    g_bOutputExitCodes = ReadMPDDefault("exitcodes");
    if (ReadMPDDefault("nopopup_debug"))
    {
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    }
    if (ReadMPDDefault("usejobhost"))
    {
	length = MAX_HOST_LENGTH;
	if (ReadMPDRegistry("jobhost", g_pszJobHost, &length))
	{
	    g_bUseJobHost = true;
	    length = 100;
	    if (ReadMPDRegistry("jobhostpwd", g_pszJobHostMPDPwd, &length))
	    {
		g_bUseJobMPDPwd = true;
	    }
	}
    }
    bUseDebugFlag = ReadMPDDefault("dbg");
    g_bLocalRoot = ReadMPDDefault("localroot");
    g_bMPICH2 = ReadMPDDefault("mpich2");
    g_bIPRoot = ReadMPDDefault("iproot");
    SetupTimeouts();


    // Parse mpirun options
    while (argv[1] && (argv[1][0] == '-' || argv[1][0] == '/'))
    {
	nArgsToStrip = 1;
	if (stricmp(&argv[1][1], "np") == 0)
	{
	    if (argc < 3)
	    {
		printf("Error: no number specified after -np option.\n");
		return 0;
	    }
	    g_nHosts = atoi(argv[2]);
	    if (g_nHosts < 1)
	    {
		printf("Error: must specify a number greater than 0 after the -np option\n");
		return 0;
	    }
	    nArgsToStrip = 2;
	}
	else if (stricmp(&argv[1][1], "localonly") == 0)
	{
	    bRunLocal = true;
	    if (argc > 2)
	    {
		if (isnumber(argv[2]))
		{
		    g_nHosts = atoi(argv[2]);
		    if (g_nHosts < 1)
		    {
			printf("Error: If you specify a number after -localonly option,\n        it must be greater than 0.\n");
			return 0;
		    }
		    nArgsToStrip = 2;
		}
	    }
	}
	else if (stricmp(&argv[1][1], "machinefile") == 0)
	{
	    if (argc < 3)
	    {
		printf("Error: no filename specified after -machinefile option.\n");
		return 0;
	    }
	    strcpy(pszMachineFileName, argv[2]);
	    bUseMachineFile = true;
	    nArgsToStrip = 2;
	}
	else if (stricmp(&argv[1][1], "map") == 0)
	{
	    if (argc < 3)
	    {
		printf("Error: no drive specified after -map option.\n");
		return 0;
	    }
	    if ((strlen(argv[2]) > 2) && argv[2][1] == ':')
	    {
		MapDriveNode *pNode = new MapDriveNode;
		pNode->cDrive = argv[2][0];
		strcpy(pNode->pszShare, &argv[2][2]);
		pNode->pNext = g_pDriveMapList;
		g_pDriveMapList = pNode;
	    }
	    nArgsToStrip = 2;
	}
	else if (stricmp(&argv[1][1], "dir") == 0)
	{
	    if (argc < 3)
	    {
		printf("Error: no directory after -dir option\n");
		return 0;
	    }
	    strcpy(g_pszDir, argv[2]);
	    nArgsToStrip = 2;
	}
	else if (stricmp(&argv[1][1], "env") == 0)
	{
	    if (argc < 3)
	    {
		printf("Error: no environment variables after -env option\n");
		return 0;
	    }
	    strncpy(g_pszEnv, argv[2], MAX_CMD_LENGTH);
	    g_pszEnv[MAX_CMD_LENGTH-1] = '\0';
	    if (strlen(argv[2]) >= MAX_CMD_LENGTH)
	    {
		printf("Warning: environment variables truncated.\n");
	    }
	    nArgsToStrip = 2;
	}
	else if (stricmp(&argv[1][1], "logon") == 0)
	{
	    bLogon = true;
	}
	else if (stricmp(&argv[1][1], "noprompt") == 0)
	{
	    bCredentialsPrompt = false;
	}
	else if (stricmp(&argv[1][1], "dbg") == 0)
	{
	    bUseDebugFlag = true;
	}
	else if (stricmp(&argv[1][1], "pwdfile") == 0)
	{
	    bUsePwdFile = true;
	    if (argc < 3)
	    {
		printf("Error: no filename specified after -pwdfile option\n");
		return 0;
	    }
	    strncpy(pszPwdFileName, argv[2], MAX_PATH);
	    pszPwdFileName[MAX_PATH-1] = '\0';
	    nArgsToStrip = 2;
	}
	else if (stricmp(&argv[1][1], "mpduser") == 0)
	{
	    g_bUseMPDUser = true;
	}
	else if (stricmp(&argv[1][1], "hosts") == 0)
	{
	    if (g_nHosts != 0)
	    {
		printf("Error: only one option is allowed to determine the number of processes.\n");
		printf("       -hosts cannot be used with -np or -localonly\n");
		return 0;
	    }
	    if (argc > 2)
	    {
		if (isnumber(argv[2]))
		{
		    g_nHosts = atoi(argv[2]);
		    if (g_nHosts < 1)
		    {
			printf("Error: You must specify a number greater than 0 after -hosts.\n");
			return 0;
		    }
		    nArgsToStrip = 2 + g_nHosts;
		    int index = 3;
		    for (i=0; i<g_nHosts; i++)
		    {
			if (index >= argc)
			{
			    printf("Error: missing host name after -hosts option.\n");
			    return 0;
			}
			HostNode *pNode = new HostNode;
			pNode->next = NULL;
			pNode->nSMPProcs = 1;
			pNode->exe[0] = '\0';
			strcpy(pNode->host, argv[index]);
			index++;
			if (argc > index)
			{
			    if (isnumber(argv[index]))
			    {
				pNode->nSMPProcs = atoi(argv[index]);
				index++;
				nArgsToStrip++;
			    }
			}
			if (g_pHosts == NULL)
			{
			    g_pHosts = pNode;
			}
			else
			{
			    HostNode *pIter = g_pHosts;
			    while (pIter->next)
				pIter = pIter->next;
			    pIter->next = pNode;
			}
		    }
		}
		else
		{
		    printf("Error: You must specify the number of hosts after the -hosts option.\n");
		    return 0;
		}
	    }
	    else
	    {
		printf("Error: not enough arguments.\n");
		return 0;
	    }
	}
	else if (stricmp(&argv[1][1], "tcp") == 0)
	{
	    bDoSMP = false;
	}
	else if (stricmp(&argv[1][1], "getphrase") == 0)
	{
	    GetMPDPassPhrase(pmi_phrase);
	    bPhraseNeeded = false;
	}
	else if (stricmp(&argv[1][1], "nocolor") == 0)
	{
	    g_bDoMultiColorOutput = false;
	}
	else if (stricmp(&argv[1][1], "nompi") == 0)
	{
	    g_bNoMPI = true;
	}
	else if (stricmp(&argv[1][1], "nodots") == 0)
	{
	    bLogonDots = false;
	}
	else if (stricmp(&argv[1][1], "nomapping") == 0)
	{
	    bNoDriveMapping = true;
	}
	else if (stricmp(&argv[1][1], "nopopup_debug") == 0)
	{
	    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	}
	else if (stricmp(&argv[1][1], "help") == 0 || argv[1][1] == '?')
	{
	    PrintOptions();
	    return 0;
	}
	else if (stricmp(&argv[1][1], "help2") == 0)
	{
	    PrintExtraOptions();
	    return 0;
	}
	else if (stricmp(&argv[1][1], "jobhost") == 0)
	{
	    g_bUseJobHost = true;
	    if (argc < 3)
	    {
		printf("Error: no host name specified after -jobhost option\n");
		return 0;
	    }
	    strncpy(g_pszJobHost, argv[2], MAX_HOST_LENGTH);
	    g_pszJobHost[MAX_HOST_LENGTH-1] = '\0';
	    nArgsToStrip = 2;
	}
	else if (stricmp(&argv[1][1], "jobhostmpdpwd") == 0)
	{
	    g_bUseJobMPDPwd = true;
	    if (argc < 3)
	    {
		printf("Error: no passphrase specified after -jobhostmpdpwd option\n");
		return 0;
	    }
	    strncpy(g_pszJobHostMPDPwd, argv[2], 100);
	    g_pszJobHostMPDPwd[99] = '\0';
	    nArgsToStrip = 2;
	}
	else if (stricmp(&argv[1][1], "exitcodes") == 0)
	{
	    g_bOutputExitCodes = true;
	}
	else if (stricmp(&argv[1][1], "localroot") == 0)
	{
	    g_bLocalRoot = true;
	}
	else if (stricmp(&argv[1][1], "priority") == 0)
	{
	    char *str;
	    nPriorityClass = atoi(argv[2]);
	    str = strchr(argv[2], ':');
	    if (str)
	    {
		str++;
		nPriority = atoi(str);
	    }
	    //printf("priorities = %d:%d\n", nPriorityClass, nPriority);fflush(stdout);
	    bUsePriorities = true;
	    nArgsToStrip = 2;
	}
	else if (stricmp(&argv[1][1], "iproot") == 0)
	{
	    g_bIPRoot = true;
	}
	else if (stricmp(&argv[1][1], "noiproot") == 0)
	{
	    g_bIPRoot = false;
	}
	else if (stricmp(&argv[1][1], "mpich2") == 0)
	{
	    g_bMPICH2 = true;
	}
	else if (stricmp(&argv[1][1], "mpich1") == 0)
	{
	    g_bMPICH2 = false;
	}
	else
	{
	    printf("Unknown option: %s\n", argv[1]);
	}
	StripArgs(argc, argv, nArgsToStrip);
    }

    if (argc < 2)
    {
	printf("Error: no executable or configuration file specified\n");
	return 0;
    }

    // The next argument is the executable or a configuration file
    strncpy(g_pszExe, argv[1], MAX_CMD_LENGTH);
    g_pszExe[MAX_CMD_LENGTH-1] = '\0';

    // All the rest of the arguments are passed to the application
    g_pszArgs[0] = '\0';
    for (i = 2; i<argc; i++)
    {
	strncat(g_pszArgs, argv[i], MAX_CMD_LENGTH - 1 - strlen(g_pszArgs));
	if (i < argc-1)
	{
	    strncat(g_pszArgs, " ", MAX_CMD_LENGTH - 1 - strlen(g_pszArgs));
	}
    }

    if (g_nHosts == 0)
    {
	// If -np or -localonly options have not been specified, check if the first
	// parameter is an executable or a configuration file
	if (GetBinaryType(g_pszExe, &dwType) || (ParseConfigFile(g_pszExe) == PARSE_ERR_NO_FILE))
	{
	    g_nHosts = 1;
	    bRunLocal = true;
	}
    }

    // Fix up the executable name
    char pszTempExe[MAX_CMD_LENGTH], *namepart;
    if (g_pszExe[0] == '\\' && g_pszExe[1] == '\\')
    {
	strncpy(pszTempExe, g_pszExe, MAX_CMD_LENGTH);
	pszTempExe[MAX_CMD_LENGTH-1] = '\0';
    }
    else
	GetFullPathName(g_pszExe, MAX_PATH, pszTempExe, &namepart);
    // Quote the executable in case there are spaces in the path
    sprintf(g_pszExe, "\"%s\"", pszTempExe);

    easy_socket_init();

    // This block must be executed to fix up g_pszExe before GetHostsFromFile() 
    // or GetAvailableHosts() is called because they make copies of g_pszExe.
    if (!bRunLocal)
    {
	// Save the original file name in case we end up running locally
	strncpy(pszTempExe, g_pszExe, MAX_CMD_LENGTH);
	pszTempExe[MAX_CMD_LENGTH-1] = '\0';

	// Convert the executable to its unc equivalent. This negates
	// the need to map network drives on remote machines just to locate
	// the executable.
	ExeToUnc(g_pszExe);
    }

    if (!bRunLocal && g_pHosts == NULL)
    {
	// If we are not running locally and the hosts haven't been set up with a configuration file,
	// create the host list now
	if (bUseMachineFile)
	{
	    if (!GetHostsFromFile(pszMachineFileName))
	    {
		printf("Error parsing the machine file '%s'\n", pszMachineFileName);
		return 0;
	    }
	}
	else if (!GetAvailableHosts())
	{
	    strncpy(g_pszExe, pszTempExe, MAX_CMD_LENGTH);
	    g_pszExe[MAX_CMD_LENGTH-1] = '\0';
	    bRunLocal = true;
	}
    }

    // Setup multi-color output
    if (g_bDoMultiColorOutput)
    {
	char pszTemp[10];
	DWORD len = 10;
	if (ReadMPDRegistry("color", pszTemp, &len))
	{
	    g_bDoMultiColorOutput = (stricmp(pszTemp, "yes") == 0);
	}
    }
    if (g_bDoMultiColorOutput)
    {
	CONSOLE_SCREEN_BUFFER_INFO info;
	// Save the state of the console so it can be restored
	hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	GetConsoleScreenBufferInfo(hStdout, &info);
	g_ConsoleAttribute = info.wAttributes;
    }
    
    // Check if the directory needs to be mapped on the remote machines
    if (!bNoDriveMapping && NeedToMap(g_pszDir, &cMapDrive, pszMapShare))
    {
	MapDriveNode *pNode = new MapDriveNode;
	pNode->cDrive = cMapDrive;
	strcpy(pNode->pszShare, pszMapShare);
	pNode->pNext = g_pDriveMapList;
	g_pDriveMapList = pNode;
    }

    // If -getphrase was not specified, get the mpd passphrase from
    // the registry or use the default
    if (bPhraseNeeded)
    {
	if (!ReadMPDRegistry("phrase", pmi_phrase, NULL))
	{
	    strcpy(pmi_phrase, MPD_DEFAULT_PASSPHRASE);
	}
    }

    if (g_bMPICH2)
    {
	char pmi_port_str[100];
	DWORD port_str_length = 100;
	if (ReadMPDRegistry("port", pmi_port_str, &port_str_length))
	{
	    pmi_port = atoi(pmi_port_str);
	    if (pmi_port < 1)
		pmi_port = MPD_DEFAULT_PORT;
	}
	// Put the pmi database on the root node to reduce contention on this host where mpirun has been executed.
	// This assumes that the pmi passphrase is the same on the root host as it is here.
	if (g_pHosts && strlen(g_pHosts->host))
	    strcpy(pmi_host, g_pHosts->host);
	CreatePMIDatabase(pmi_host, pmi_port, pmi_phrase, pmi_kvsname);
    }
    
    if (bRunLocal)
    {
	RunLocal(bDoSMP);
	if (g_bMPICH2)
	    DestroyPMIDatabase(pmi_host, pmi_port, pmi_phrase, pmi_kvsname);
	easy_socket_finalize();
	return 0;
    }

    //dbg_printf("retrieving account information\n");
    if (g_bUseMPDUser)
    {
	bLogon = false;
	g_pszAccount[0] = '\0';
	g_pszPassword[0] = '\0';
    }
    else
    {
	if (bUsePwdFile)
	{
	    bLogon = true;
	    GetAccountAndPasswordFromFile(pszPwdFileName);
	}
	else
	{
	    if (bLogon)
		GetAccountAndPassword();
	    else
	    {
		char pszTemp[10] = "no";
		ReadMPDRegistry("SingleUser", pszTemp, NULL);
		if (stricmp(pszTemp, "yes"))
		{
		    if (!ReadCachedPassword())
		    {
			if (bLogonDots)
			{
			    DWORD dwThreadId;
			    HANDLE hEvent, hDotThread;
			    hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
			    hDotThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)PrintDots, hEvent, 0, &dwThreadId);
			    if (!ReadPasswordFromRegistry(g_pszAccount, g_pszPassword))
			    {
				SetEvent(hEvent);
				if (bCredentialsPrompt)
				    GetAccountAndPassword();
				else
				{
				    printf("Error: unable to acquire the necessary user credentials to launch a job.\n");
				    ExitProcess(-1);
				}
			    }
			    else
				SetEvent(hEvent);
			    CloseHandle(hDotThread);
			}
			else
			{
			    if (!ReadPasswordFromRegistry(g_pszAccount, g_pszPassword))
			    {
				if (bCredentialsPrompt)
				    GetAccountAndPassword();
				else
				{
				    printf("Error: unable to acquire the necessary user credentials to launch a job.\n");
				    ExitProcess(-1);
				}
			    }
			}
			CachePassword();
		    }
		    bLogon = true;
		}
	    }
	}
    }
    
    // Figure out how many processes to launch
    int nProc = 0;
    HostNode *n = g_pHosts;
    if (g_pHosts == NULL)
	nProc = g_nHosts;
    while (n)
    {
	nProc += n->nSMPProcs;
	n = n->next;
    }
    g_nNproc = nProc;

    CreateJobID(pszJobID);
    
    // Set the environment variables common to all processes
    if (g_bNoMPI)
	pszEnv[0] = '\0';
    else
    {
	if (g_bMPICH2)
	{
	    sprintf(pszEnv, "PMI_SIZE=%d|PMI_MPD=%s:%d|PMI_KVS=%s", nProc, pmi_host, pmi_port, pmi_kvsname);
	}
	else
	{
	    if (g_bIPRoot)
		easy_get_ip_string(g_pHosts->host, g_pHosts->host);
	    sprintf(pszEnv, "MPICH_JOBID=%s|MPICH_NPROC=%d|MPICH_ROOTHOST=%s",
		pszJobID, nProc, g_pHosts->host);
	}
    }
    
    // Allocate an array to hold handles to the LaunchProcess threads, sockets, ids, ranks, and forward host structures
    pThread = new HANDLE[nProc];
    g_pProcessSocket = new SOCKET[nProc];
    if (g_pProcessSocket == NULL)
    {
	printf("Error: Unable to allocate memory for %d sockets\n", nProc);
	return 0;
    }
    g_pProcessHost = new HostArray[nProc];
    for (i=0; i<nProc; i++)
	g_pProcessSocket[i] = INVALID_SOCKET;
    g_pProcessLaunchId = new int[nProc];
    g_pLaunchIdToRank = new int [nProc];
    g_nNumProcessSockets = 0;
    g_pForwardHost = new ForwardHostStruct[nProc];
    for (i=0; i<nProc; i++)
	g_pForwardHost[i].nPort = 0;
    if (pThread == NULL || g_pProcessHost == NULL || g_pProcessLaunchId == NULL || g_pLaunchIdToRank == NULL || g_pForwardHost == NULL)
    {
	printf("Error: Unable to allocate memory for process and socket structures\n");
	return 0;
    }
    
    // Start the IO redirection thread
    HANDLE hEvent;
    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
    {
	hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (hEvent != NULL)
	    break;
	Sleep(CREATE_THREAD_SLEEP_TIME);
    }
    if (hEvent == NULL)
    {
	printf("CreateEvent failed, error %d\n", GetLastError());
	return 0;
    }
    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
    {
	g_hRedirectIOListenThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectIOThread, hEvent, 0, &dwThreadID);
	if (g_hRedirectIOListenThread != NULL)
	    break;
	Sleep(CREATE_THREAD_SLEEP_TIME);
    }
    SwitchToThread();
    if (g_hRedirectIOListenThread)
    {
	if (WaitForSingleObject(hEvent, 120000) != WAIT_OBJECT_0)
	{
	    printf("RedirectIOThread failed to initialize\n");
	    if (g_bMPICH2)
		DestroyPMIDatabase(pmi_host, pmi_port, pmi_phrase, pmi_kvsname);
	    return 0;
	}
    }
    else
    {
	printf("Unable to create RedirectIOThread, error %d\n", GetLastError());
	if (g_bMPICH2)
	    DestroyPMIDatabase(pmi_host, pmi_port, pmi_phrase, pmi_kvsname);
	return 0;
    }
    CloseHandle(hEvent);

    strncpy(g_pForwardHost[0].pszHost, g_pszIOHost, MAX_HOST_LENGTH);
    g_pForwardHost[0].pszHost[MAX_HOST_LENGTH-1] = '\0';
    g_pForwardHost[0].nPort = g_nIOPort;
    //printf("io redirection: %s:%d\n", g_pForwardHost[0].pszHost, g_pForwardHost[0].nPort);fflush(stdout);

#ifdef SERIALIZE_ROOT_PROCESS
    HANDLE hRootMutex = NULL;
    if (!g_bMPICH2)
	hRootMutex = CreateMutex(NULL, FALSE, "MPIRunRootMutex");
#endif

    CreateShmCliqueString(g_pHosts, pszShmCliqueString);
    /*printf("shmem clique string: %s\n", pszShmCliqueString);fflush(stdout);*/

    // Launch the threads to launch the processes
    iproc = 0;
    while (g_pHosts)
    {
	nShmLow = iproc;
	nShmHigh = iproc + g_pHosts->nSMPProcs - 1;
	for (int i = 0; i<g_pHosts->nSMPProcs; i++)
	{
	    MPIRunLaunchProcessArg *arg = new MPIRunLaunchProcessArg;
	    arg->bUsePriorities = bUsePriorities;
	    arg->nPriorityClass = nPriorityClass;
	    arg->nPriority = nPriority;
	    arg->bUseDebugFlag = bUseDebugFlag;
	    arg->n = g_nNproc;
	    sprintf(arg->pszIOHostPort, "%s:%d", g_pszIOHost, g_nIOPort);
	    strcpy(arg->pszPassPhrase, pmi_phrase);
	    arg->i = iproc;
	    arg->bLogon = bLogon;
	    if (bLogon)
	    {
		strcpy(arg->pszAccount, g_pszAccount);
		strcpy(arg->pszPassword, g_pszPassword);
	    }
	    else
	    {
		arg->pszAccount[0] = '\0';
		arg->pszPassword[0] = '\0';
	    }
	    if (strlen(g_pHosts->exe) > 0)
	    {
		strncpy(arg->pszCmdLine, g_pHosts->exe, MAX_CMD_LENGTH);
		arg->pszCmdLine[MAX_CMD_LENGTH-1] = '\0';
	    }
	    else
	    {
		strncpy(arg->pszCmdLine, g_pszExe, MAX_CMD_LENGTH);
		arg->pszCmdLine[MAX_CMD_LENGTH-1] = '\0';
	    }
	    if (strlen(g_pszArgs) > 0)
	    {
		strncat(arg->pszCmdLine, " ", MAX_CMD_LENGTH - 1 - strlen(arg->pszCmdLine));
		strncat(arg->pszCmdLine, g_pszArgs, MAX_CMD_LENGTH - 1 - strlen(arg->pszCmdLine));
	    }
	    strcpy(arg->pszDir, g_pszDir);
	    if (strlen(pszEnv) >= MAX_CMD_LENGTH)
	    {
		printf("Warning: environment variables truncated.\n");
		fflush(stdout);
	    }
	    strncpy(arg->pszEnv, pszEnv, MAX_CMD_LENGTH);
	    arg->pszEnv[MAX_CMD_LENGTH-1] = '\0';
	    strncpy(arg->pszHost, g_pHosts->host, MAX_HOST_LENGTH);
	    arg->pszHost[MAX_HOST_LENGTH-1] = '\0';
	    strcpy(g_pProcessHost[iproc].host, arg->pszHost);
	    strcpy(arg->pszJobID, pszJobID);

	    if (g_bNoMPI)
	    {
		if (strlen(g_pszEnv) >= MAX_CMD_LENGTH)
		{
		    printf("Warning: environment variables truncated.\n");
		    fflush(stdout);
		}
		strncpy(arg->pszEnv, g_pszEnv, MAX_CMD_LENGTH);
		arg->pszEnv[MAX_CMD_LENGTH-1] = '\0';
	    }
	    else
	    {
		if (ParseCliques(pszShmCliqueString, iproc, g_nNproc, &nCliqueCount, &pMembers) == 0)
		{
		    if (nCliqueCount > 1)
		    {
			CreateSingleShmCliqueString(nCliqueCount, pMembers, pszSingleShmCliqueString);
			if (g_bMPICH2)
			{
			    sprintf(pBuffer, "PMI_RANK=%d|PMI_SHM_CLIQUES=%s", iproc, pszSingleShmCliqueString);
			}
			else
			{
			    if (iproc == 0)
				sprintf(pBuffer, "MPICH_ROOTPORT=-1|MPICH_IPROC=%d|MPICH_SHM_CLIQUES=%s", iproc, pszSingleShmCliqueString);
			    else
				sprintf(pBuffer, "MPICH_ROOTPORT=%d|MPICH_IPROC=%d|MPICH_SHM_CLIQUES=%s", g_nRootPort, iproc, pszSingleShmCliqueString);
			}
		    }
		    else
		    {
			if (g_bMPICH2)
			{
			    sprintf(pBuffer, "PMI_RANK=%d|PMI_SHM_CLIQUES=(%d..%d)", iproc, nShmLow, nShmHigh);
			}
			else
			{
			    if (iproc == 0)
				sprintf(pBuffer, "MPICH_ROOTPORT=-1|MPICH_IPROC=%d|MPICH_SHM_LOW=%d|MPICH_SHM_HIGH=%d", iproc, nShmLow, nShmHigh);
			    else
				sprintf(pBuffer, "MPICH_ROOTPORT=%d|MPICH_IPROC=%d|MPICH_SHM_LOW=%d|MPICH_SHM_HIGH=%d", g_nRootPort, iproc, nShmLow, nShmHigh);
			}
		    }
		    if (pMembers)
		    {
			FREE(pMembers);
			pMembers = NULL;
		    }
		}
		else
		{
		    if (g_bMPICH2)
		    {
			sprintf(pBuffer, "PMI_RANK=%d|PMI_SHM_CLIQUES=(%d..%d)", iproc, nShmLow, nShmHigh);
		    }
		    else
		    {
			if (iproc == 0)
			    sprintf(pBuffer, "MPICH_ROOTPORT=-1|MPICH_IPROC=%d|MPICH_SHM_LOW=%d|MPICH_SHM_HIGH=%d", iproc, nShmLow, nShmHigh);
			else
			    sprintf(pBuffer, "MPICH_ROOTPORT=%d|MPICH_IPROC=%d|MPICH_SHM_LOW=%d|MPICH_SHM_HIGH=%d", g_nRootPort, iproc, nShmLow, nShmHigh);
		    }
		}
		/*
		if (iproc == 0)
		    sprintf(pBuffer, "MPICH_ROOTPORT=-1|MPICH_IPROC=%d|MPICH_SHM_LOW=%d|MPICH_SHM_HIGH=%d", iproc, nShmLow, nShmHigh);
		else
		    sprintf(pBuffer, "MPICH_ROOTPORT=%d|MPICH_IPROC=%d|MPICH_SHM_LOW=%d|MPICH_SHM_HIGH=%d", g_nRootPort, iproc, nShmLow, nShmHigh);
		*/

		if (strlen(arg->pszEnv) > 0)
		    strncat(arg->pszEnv, "|", MAX_CMD_LENGTH - 1 - strlen(arg->pszEnv));
		if (strlen(pBuffer) + strlen(arg->pszEnv) >= MAX_CMD_LENGTH)
		{
		    printf("Warning: environment variables truncated.\n");
		    fflush(stdout);
		}
		strncat(arg->pszEnv, pBuffer, MAX_CMD_LENGTH - 1 - strlen(arg->pszEnv));
		
		if (strlen(g_pszEnv) > 0)
		{
		    if (strlen(arg->pszEnv) + strlen(g_pszEnv) + 1 >= MAX_CMD_LENGTH)
		    {
			printf("Warning: environment variables truncated.\n");
		    }
		    strncat(arg->pszEnv, "|", MAX_CMD_LENGTH - 1 - strlen(arg->pszEnv));
		    strncat(arg->pszEnv, g_pszEnv, MAX_CMD_LENGTH - 1 - strlen(arg->pszEnv));
		}
	    }
	    //printf("creating MPIRunLaunchProcess thread\n");fflush(stdout);
#ifdef SERIALIZE_ROOT_PROCESS
	    if (iproc == 0 && !g_bNoMPI && !g_bMPICH2)
		WaitForSingleObject(hRootMutex, INFINITE);
#endif
	    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
	    {
		pThread[iproc] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MPIRunLaunchProcess, arg, 0, &dwThreadID);
		if (pThread[iproc] != NULL)
		    break;
		Sleep(CREATE_THREAD_SLEEP_TIME);
	    }
	    if (pThread[iproc] == NULL)
	    {
		printf("Unable to create LaunchProcess thread\n");fflush(stdout);
		// Signal launch threads to abort
		// Wait for them to return
		
		// ... insert code here

		// In the mean time, just exit
		if (g_bDoMultiColorOutput)
		{
		    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), g_ConsoleAttribute);
		}
#ifdef SERIALIZE_ROOT_PROCESS
		if (iproc == 0 && !g_bNoMPI && !g_bMPICH2)
		{
		    ReleaseMutex(hRootMutex);
		    CloseHandle(hRootMutex);
		}
#endif
		ExitProcess(1);
	    }
	    if (iproc == 0 && !g_bNoMPI && !g_bMPICH2)
	    {
		// Wait for the root port to be valid
		while (g_nRootPort == 0 && (WaitForSingleObject(g_hAbortEvent, 0) != WAIT_OBJECT_0))
		    Sleep(200);
#ifdef SERIALIZE_ROOT_PROCESS
		ReleaseMutex(hRootMutex);
		CloseHandle(hRootMutex);
#endif
		if (g_nRootPort == 0)
		{
		    // free stuff
		    // ... <insert code here>
		    CloseHandle(pThread[0]);
		    delete [] pThread;
		    delete [] g_pProcessSocket;
		    delete [] g_pProcessHost;
		    delete [] g_pProcessLaunchId;
		    delete [] g_pLaunchIdToRank;
		    delete [] g_pForwardHost;
		    if (g_bDoMultiColorOutput)
		    {
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), g_ConsoleAttribute);
		    }
		    return 0;
		}
	    }
	    iproc++;
	}
	
	HostNode *n = g_pHosts;
	g_pHosts = g_pHosts->next;
	delete n;
    }

    //printf("Waiting for processes\n");fflush(stdout);
    // Wait for all the process launching threads to complete
    WaitForLotsOfObjects(nProc, pThread);
    for (i = 0; i<nProc; i++)
	CloseHandle(pThread[i]);
    delete [] pThread;
    pThread = NULL;

    if (WaitForSingleObject(g_hAbortEvent, 0) == WAIT_OBJECT_0)
    {
	char pszStr[100];
	printf("aborting...\n");fflush(stdout);
	for (i=0; i<nProc; i++)
	{
	    if (g_pProcessSocket[i] != INVALID_SOCKET)
	    {
		sprintf(pszStr, "kill %d", g_pProcessLaunchId[i]);
		WriteString(g_pProcessSocket[i], pszStr);
		sprintf(pszStr, "freeprocess %d", g_pProcessLaunchId[i]);
		g_pProcessLaunchId[i] = -1; // nobody should use the id after we free it
		WriteString(g_pProcessSocket[i], pszStr);
		ReadStringTimeout(g_pProcessSocket[i], pszStr, g_nMPIRUN_SHORT_TIMEOUT);
		WriteString(g_pProcessSocket[i], "done");
		easy_closesocket(g_pProcessSocket[i]);
	    }
	}
	if (g_bUseJobHost && !g_bNoMPI)
	    UpdateJobState("ABORTED");
	if (g_bMPICH2)
	    DestroyPMIDatabase(pmi_host, pmi_port, pmi_phrase, pmi_kvsname);
	ExitProcess(0);
    }
    // Note: If the user hits Ctrl-C between the above if statement and the following ResetEvent statement
    // nothing will happen and the user will have to hit Ctrl-C again.
    ResetEvent(g_hLaunchThreadsRunning);
    //printf("____g_hLaunchThreadsRunning event is reset, Ctrl-C should work now____\n");fflush(stdout);

    if (g_bUseJobHost && !g_bNoMPI)
	UpdateJobState("RUNNING");

    //printf("Waiting for exit codes\n");fflush(stdout);
    // Wait for the mpds to return the exit codes of all the processes
    WaitForExitCommands();

    delete [] g_pForwardHost;
    g_pForwardHost = NULL;

    // Signal the IO redirection thread to stop
    char ch = 0;
    easy_send(g_sockStopIOSignalSocket, &ch, 1);

    //printf("Waiting for redirection thread to exit\n");fflush(stdout);
    // Wait for the redirection thread to complete.  Kill it if it takes too long.
    if (WaitForSingleObject(g_hRedirectIOListenThread, 10000) != WAIT_OBJECT_0)
    {
	//printf("Terminating the IO redirection control thread\n");
	TerminateThread(g_hRedirectIOListenThread, 0);
    }
    CloseHandle(g_hRedirectIOListenThread);
    easy_closesocket(g_sockStopIOSignalSocket);
    CloseHandle(g_hAbortEvent);

    if (g_bUseJobHost && !g_bNoMPI)
	UpdateJobState("FINISHED");

    if (g_bDoMultiColorOutput)
    {
	SetConsoleTextAttribute(hStdout, g_ConsoleAttribute);
    }
    if (g_bMPICH2)
    {
	DestroyPMIDatabase(pmi_host, pmi_port, pmi_phrase, pmi_kvsname);
    }
    easy_socket_finalize();

    delete [] g_pProcessSocket;
    delete [] g_pProcessHost;
    delete [] g_pProcessLaunchId;
    delete [] g_pLaunchIdToRank;

    while (g_pDriveMapList)
    {
	MapDriveNode *pNode = g_pDriveMapList;
	g_pDriveMapList = g_pDriveMapList->pNext;
	delete pNode;
    }

    return 0;
}

