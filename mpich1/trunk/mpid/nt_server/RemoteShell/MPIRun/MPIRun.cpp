#include <stdio.h>
#include <tchar.h>
#include "..\Common\MPICH_pwd.h"
#include "..\Common\MPIJobDefs.h"
#include <conio.h>
#include <time.h>
#include "..\Common\Translate_Error.h"
#include "localonly.h"
#include "GetOpt.h"
#include "LaunchProcess.h"
#include "global.h"
#include "WaitThread.h"
#include "MPIRunBNR.h"
#include "Redirection.h"

// Global function pointer declaration
#define BNR_FUNCTION_DECLARATIONS
#include "bnr.h"
#undef BNR_FUNCTION_DECLARATIONS

BNR_Group g_myBNRgroup = BNR_GROUP_NULL;

#define DPRINTF(a) {}
bool LoadBNRFunctions()
{
	HMODULE hBNRLib;
	char pszLibrary[1024];
	
	// First initialize everythting to NULL
	BNR_Init = NULL;
	BNR_Finalize = NULL;
	BNR_Get_group = NULL;
	BNR_Get_parent = NULL;
	BNR_Get_rank = NULL;
	BNR_Get_size = NULL;
	BNR_Open_group = NULL;
	BNR_Close_group = NULL;
	BNR_Free_group = NULL;
	BNR_Merge = NULL;
	BNR_Spawn = NULL;
	BNR_Kill = NULL;
	BNR_Put = NULL;
	BNR_Get = NULL;
	BNR_Fence = NULL;
	BNR_Deposit = NULL;
	BNR_Withdraw = NULL;
	BNR_Lookup = NULL;
	
	BNR_Info_set = NULL;
	BNR_Info_get_valuelen = NULL;
	BNR_Info_get_nthkey = NULL;
	BNR_Info_get_nkeys = NULL;
	BNR_Info_get = NULL;
	BNR_Info_free = NULL;
	BNR_Info_dup = NULL;
	BNR_Info_delete = NULL;
	BNR_Info_create = NULL;

	if (!GetEnvironmentVariable("MPICH_BNR_LIB", pszLibrary, 1024))
	{
		// Try to load the default library:
		strcpy(pszLibrary, "mpichbnr.dll");
		// or just bail out:
		//return false;
	}

	hBNRLib = LoadLibrary(pszLibrary);

	if (hBNRLib == NULL)
		return false;

	// Add code to check if the return values are NULL ...
	BNR_Init = (int (BNR_CALL *)())GetProcAddress(hBNRLib, "BNR_Init");
	if (BNR_Init == NULL) DPRINTF(("BNR_Init == NULL\n"));
	BNR_Finalize = (int (BNR_CALL *)())GetProcAddress(hBNRLib, "BNR_Finallize");
	if (BNR_Finalize == NULL) DPRINTF(("BNR_Finalize == NULL\n"));
	BNR_Get_group = (int (BNR_CALL *)( BNR_Group *mygroup ))GetProcAddress(hBNRLib, "BNR_Get_group");
	if (BNR_Get_group == NULL) DPRINTF(("BNR_Get_group == NULL\n"));
	BNR_Get_parent = (int (BNR_CALL *)( BNR_Group *parent_group ))GetProcAddress(hBNRLib, "BNR_Get_parent");
	if (BNR_Get_parent == NULL) DPRINTF(("BNR_Get_parent == NULL\n"));
	BNR_Get_rank = (int (BNR_CALL *)( BNR_Group group, int *myrank ))GetProcAddress(hBNRLib, "BNR_Get_rank");
	if (BNR_Get_rank == NULL) DPRINTF(("BNR_Get_rank == NULL\n"));
	BNR_Get_size = (int (BNR_CALL *)( BNR_Group group, int *mysize ))GetProcAddress(hBNRLib, "BNR_Get_size");
	if (BNR_Get_size == NULL) DPRINTF(("BNR_Get_size == NULL\n"));
	BNR_Open_group = (int (BNR_CALL *)( BNR_Group local_group, BNR_Group *new_group ))GetProcAddress(hBNRLib, "BNR_Open_group");
	if (BNR_Open_group == NULL) DPRINTF(("BNR_Open_group == NULL\n"));
	BNR_Close_group = (int (BNR_CALL *)( BNR_Group group ))GetProcAddress(hBNRLib, "BNR_Close_group");
	if (BNR_Close_group == NULL) DPRINTF(("BNR_Close_group == NULL\n"));
	BNR_Free_group = (int (BNR_CALL *)( BNR_Group group ))GetProcAddress(hBNRLib, "BNR_Free_group");
	if (BNR_Free_group == NULL) DPRINTF(("BNR_Free_group == NULL\n"));
	BNR_Merge = (int (BNR_CALL *)(BNR_Group local_group, BNR_Group remote_group, BNR_Group *new_group ))GetProcAddress(hBNRLib, "BNR_Merge");
	if (BNR_Merge == NULL) DPRINTF(("BNR_Merge == NULL\n"));
	BNR_Spawn = (int (BNR_CALL *)(BNR_Group remote_group, int count, char *command, char *argv, char *env, BNR_Info info, int (notify_fn)(BNR_Group group, int rank, int exit_code) ))GetProcAddress(hBNRLib, "BNR_Spawn");
	if (BNR_Spawn == NULL) DPRINTF(("BNR_Spawn == NULL\n"));
	BNR_Kill = (int (BNR_CALL *)( BNR_Group group ))GetProcAddress(hBNRLib, "BNR_Kill");
	if (BNR_Kill == NULL) DPRINTF(("BNR_Kill == NULL\n"));
	BNR_Put = (int (BNR_CALL *)( BNR_Group group, char *attr, char *val, int rank_advice ))GetProcAddress(hBNRLib, "BNR_Put");
	if (BNR_Put == NULL) DPRINTF(("BNR_Put == NULL\n"));
	BNR_Get = (int (BNR_CALL *)( BNR_Group group, char *attr, char *val ))GetProcAddress(hBNRLib, "BNR_Get");
	if (BNR_Get == NULL) DPRINTF(("BNR_Get == NULL\n"));
	BNR_Fence = (int (BNR_CALL *)( BNR_Group ))GetProcAddress(hBNRLib, "BNR_Fence");
	if (BNR_Fence == NULL) DPRINTF(("BNR_Fence == NULL\n"));
	BNR_Deposit = (int (BNR_CALL *)( char *attr, char *value ))GetProcAddress(hBNRLib, "BNR_Deposit");
	if (BNR_Deposit == NULL) DPRINTF(("BNR_Deposit == NULL\n"));
	BNR_Withdraw = (int (BNR_CALL *)( char *attr, char *value ))GetProcAddress(hBNRLib, "BNR_Withdraw");
	if (BNR_Withdraw == NULL) DPRINTF(("BNR_Withdraw == NULL\n"));
	BNR_Lookup = (int (BNR_CALL *)( char *attr, char *value ))GetProcAddress(hBNRLib, "BNR_Lookup");
	if (BNR_Lookup == NULL) DPRINTF(("BNR_Lookup == NULL\n"));
	
	BNR_Info_set = (int (BNR_CALL *)(BNR_Info info, char *key, char *value))GetProcAddress(hBNRLib, "BNR_Info_set");
	if (BNR_Info_set == NULL) DPRINTF(("BNR_Info_set == NULL\n"));
	BNR_Info_get_valuelen = (int (BNR_CALL *)(BNR_Info info, char *key, int *valuelen, int *flag))GetProcAddress(hBNRLib, "BNR_Info_get_valuelen");
	if (BNR_Info_get_valuelen == NULL) DPRINTF(("BNR_Info_get_valuelen == NULL\n"));
	BNR_Info_get_nthkey = (int (BNR_CALL *)(BNR_Info info, int n, char *key))GetProcAddress(hBNRLib, "BNR_Info_get_nthkey");
	if (BNR_Info_get_nthkey == NULL) DPRINTF(("BNR_Info_get_nthkey == NULL\n"));
	BNR_Info_get_nkeys = (int (BNR_CALL *)(BNR_Info info, int *nkeys))GetProcAddress(hBNRLib, "BNR_Info_get_nkeys");
	if (BNR_Info_get_nkeys == NULL) DPRINTF(("BNR_Info_get_nkeys == NULL\n"));
	BNR_Info_get = (int (BNR_CALL *)(BNR_Info info, char *key, int valuelen, char *value, int *flag))GetProcAddress(hBNRLib, "BNR_Info_get");
	if (BNR_Info_get == NULL) DPRINTF(("BNR_Info_get == NULL\n"));
	BNR_Info_free = (int (BNR_CALL *)(BNR_Info *info))GetProcAddress(hBNRLib, "BNR_Info_free");
	if (BNR_Info_free == NULL) DPRINTF(("BNR_Info_free == NULL\n"));
	BNR_Info_dup = (int (BNR_CALL *)(BNR_Info info, BNR_Info *newinfo))GetProcAddress(hBNRLib, "BNR_Info_dup");
	if (BNR_Info_dup == NULL) DPRINTF(("BNR_Info_dup == NULL\n"));
	BNR_Info_delete = (int (BNR_CALL *)(BNR_Info info, char *key))GetProcAddress(hBNRLib, "BNR_Info_delete");
	if (BNR_Info_delete == NULL) DPRINTF(("BNR_Info_delete == NULL\n"));
	BNR_Info_create = (int (BNR_CALL *)(BNR_Info *info))GetProcAddress(hBNRLib, "BNR_Info_create");
	if (BNR_Info_create == NULL) DPRINTF(("BNR_Info_create == NULL\n"));

	return true;
}

// Function name	: PrintError
// Description	    : 
// Return type		: void 
// Argument         : HRESULT hr
void PrintError(HRESULT hr)
{
	HLOCAL str;
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
		0,
		hr,
		MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
		(LPTSTR) &str,
		0,0);
	
	_tprintf(TEXT("error %d: %s\n"), hr, str);
	LocalFree(str);
}


// Function name	: PrintOptions
// Description	    : 
// Return type		: void 
void PrintOptions()
{
	_tprintf(TEXT("\n"));
	_tprintf(TEXT("Usage:\n"));
	_tprintf(TEXT("   MPIRun configfile [-logon] [args ...]\n"));
	_tprintf(TEXT("   MPIRun -np #processes [-logon] [-env \"var1=val1|var2=val2...\"] executable [args ...]\n"));
	_tprintf(TEXT("   MPIRun -localonly #processes [-env \"var1=val1|var2=val2...\"] exe [args ...]\n"));
	_tprintf(TEXT("\n"));
	_tprintf(TEXT("Config file format:\n"));
	_tprintf(TEXT("   >exe c:\\temp\\mpiprogram.exe\n"));
	_tprintf(TEXT("     OR \\\\host\\share\\mpiprogram.exe\n"));
	_tprintf(TEXT("   >[env var1=val1|var2=val2|var3=val3...]\n"));
	_tprintf(TEXT("   >[args arg1 arg2 ...]\n"));
	_tprintf(TEXT("   >hosts\n"));
	_tprintf(TEXT("   >hostname1 #procs [path\\mpiprogram.exe]\n"));
	_tprintf(TEXT("   >hostname2 #procs [path\\mpiprogram.exe]\n"));
	_tprintf(TEXT("   >hostname3 #procs [path\\mpiprogram.exe]\n"));
	_tprintf(TEXT("   >...\n"));
	_tprintf(TEXT("\n"));
	_tprintf(TEXT("bracketed lines are optional\n"));
	_tprintf(TEXT("\n"));
}

// Function name	: GetHostsFromRegistry
// Description	    : 
// Return type		: bool 
// Argument         : HostNode **list
bool GetHostsFromRegistry(HostNode **list)
{
	DWORD ret_val;
	HKEY hKey;

	// Open the MPICH root key
	if ((ret_val = RegOpenKeyEx(
			HKEY_LOCAL_MACHINE, 
			MPICHKEY,
			0, 
			//KEY_ALL_ACCESS, 
			KEY_QUERY_VALUE,
			&hKey)) != ERROR_SUCCESS)
	{
	    //printf("unable to open mpich registry key\n");fflush(stdout);
		return false;
	}

	// Read the hosts entry
	//TCHAR pszHosts[1024];
	TCHAR *pszHosts = NULL;
	DWORD type, num_bytes=0;//1024*sizeof(TCHAR);
	ret_val = RegQueryValueEx(hKey, _T("Hosts"), 0, &type, NULL, &num_bytes);
	if (ret_val != ERROR_SUCCESS)
	{
	    //printf("unable to query Hosts value length in mpich registry key\n");fflush(stdout);
		return false;
	}
	pszHosts = new TCHAR[num_bytes];
	ret_val = RegQueryValueEx(hKey, _T("Hosts"), 0, &type, (BYTE *)pszHosts, &num_bytes);
	RegCloseKey(hKey);
	if (ret_val != ERROR_SUCCESS)
	{
		delete pszHosts;
		//printf("unable to query Hosts value in mpich registry key\n");fflush(stdout);
		return false;
	}

	TCHAR *token = NULL;
	token = _tcstok(pszHosts, _T("|"));
	if (token != NULL)
	{
		HostNode *n, *l = new HostNode;

		// Make a list of the available nodes
		l->next = NULL;
		_tcscpy(l->host, token);
		l->nSMPProcs = 1;
		n = l;
		while ((token = _tcstok(NULL, _T("|"))) != NULL)
		{
			n->next = new HostNode;
			n = n->next;
			n->next = NULL;
			_tcscpy(n->host, token);
			n->nSMPProcs = 1;
		}
		// add the current host to the end of the list
		n->next = new HostNode;
		n = n->next;
		n->next = NULL;
		_tcscpy(n->host, g_pHosts->host);
		n->nSMPProcs = 1;

		*list = l;

		delete pszHosts;
		return true;
	}

	delete pszHosts;
	return false;
}

// Function name	: GetAvailableHosts
// Description	    : This function requires g_nHosts to have been previously set.
// Return type		: void 
bool GetAvailableHosts()
{
	DWORD size = 100;
	GetComputerName(g_pszFirstHost, &size);
	g_nFirstSMPProcs = 1;
	HostNode *list = NULL;

	// Insert the first host into the list
	g_pHosts = new HostNode;
	_tcscpy(g_pHosts->host, g_pszFirstHost);
	_tcscpy(g_pHosts->exe, g_pszExe);
	g_pHosts->nSMPProcs = 1;
	g_pHosts->next = NULL;

	if (g_nHosts > 1)
	{
		if (GetHostsFromRegistry(&list))
		{
			// add the nodes to the target list, cycling if necessary
			int num_left = g_nHosts-1;
			HostNode *n = list, *target = g_pHosts;
			while (num_left)
			{
				target->next = new HostNode;
				target = target->next;
				target->next = NULL;
				_tcscpy(target->host, n->host);
				_tcscpy(target->exe, g_pHosts->exe);
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
		    /*
			//printf("Processes will launch locally.\n");
			HostNode *n = g_pHosts;

			for (int i=1; i<g_nHosts; i++)
			{
				n->next = new HostNode;
				_tcscpy(n->next->host, n->host);
				_tcscpy(n->next->exe, n->exe);
				n = n->next;
				n->nSMPProcs = 1;
				n->next = NULL;
			}
			*/
		}
	}
	return true;
}

// Function name	: ParseLineIntoHostNode
// Description	    : 
// Return type		: HostNode* 
// Argument         : LPTSTR line
HostNode* ParseLineIntoHostNode(LPTSTR line)
{
	TCHAR buffer[1024];
	LPTSTR pChar, pChar2;
	HostNode *node = NULL;

	_tcscpy(buffer, line);
	pChar = buffer;

	// Advance over white space
	while (*pChar != _T('\0') && _istspace(*pChar))
		pChar++;
	if (*pChar == _T('#') || *pChar == _T('\0'))
		return NULL;

	// Trim trailing white space
	pChar2 = &buffer[_tcslen(buffer)-1];
	while (_istspace(*pChar2) && (pChar >= pChar))
	{
		*pChar2 = '\0';
		pChar2--;
	}
	
	// If there is anything left on the line, consider it a host name
	if (_tcslen(pChar) > 0)
	{
		node = new HostNode;
		node->nSMPProcs = 1;
		node->next = NULL;
		node->exe[0] = _T('\0');

		// Copy the host name
		pChar2 = node->host;
		while (*pChar != _T('\0') && !_istspace(*pChar))
		{
			*pChar2 = *pChar;
			pChar++;
			pChar2++;
		}
		*pChar2 = _T('\0');

		// Advance over white space
		while (*pChar != _T('\0') && _istspace(*pChar))
			pChar++;
		// Get the number of SMP processes
		if (*pChar != _T('\0'))
		{
			node->nSMPProcs = _ttoi(pChar);
			if (node->nSMPProcs < 1)
				node->nSMPProcs = 1;
		}
		// Advance over the number
		while (*pChar != _T('\0') && _istdigit(*pChar))
			pChar++;

		// Advance over white space
		while (*pChar != _T('\0') && _istspace(*pChar))
			pChar++;
		// Copy the executable
		if (*pChar != _T('\0'))
			_tcscpy(node->exe, pChar);
	}

	return node;
}

#define PARSE_ERR_NO_FILE  -1
#define PARSE_SUCCESS       0
// Function name	: ParseConfigFile
// Description	    : 
// Return type		: void 
// Argument         : LPTSTR filename
int ParseConfigFile(LPTSTR filename)
{
	FILE *fin;
	TCHAR buffer[1024] = TEXT("");

	fin = _tfopen(filename, TEXT("r"));
	if (fin == NULL)
	{
		//_tprintf(TEXT("Unable to open file: %s\n"), filename);
		return PARSE_ERR_NO_FILE;
	}

	while (_fgetts(buffer, 1024, fin))
	{
		// Check for the name of the executable
		if (_tcsnicmp(buffer, TEXT("exe "), 4) == 0)
		{
			TCHAR *pChar = &buffer[4];
			while (_istspace(*pChar))
				pChar++;
			_tcscpy(g_pszExe, pChar);
			pChar = &g_pszExe[_tcslen(g_pszExe)-1];
			while (_istspace(*pChar) && (pChar >= g_pszExe))
			{
				*pChar = '\0';
				pChar--;
			}
		}
		else
		// Check for program arguments
		if (_tcsnicmp(buffer, TEXT("args "), 5) == 0)
		{
			TCHAR *pChar = &buffer[5];
			while (_istspace(*pChar))
				pChar++;
			_tcscpy(g_pszArgs, pChar);
			pChar = &g_pszArgs[_tcslen(g_pszArgs)-1];
			while (_istspace(*pChar) && (pChar >= g_pszArgs))
			{
				*pChar = '\0';
				pChar--;
			}
		}
		else
		// Check for environment variables
		if (_tcsnicmp(buffer, TEXT("env "), 4) == 0)
		{
			TCHAR *pChar = &buffer[4];
			while (_istspace(*pChar))
				pChar++;
			_tcscpy(g_pszEnv, pChar);
			pChar = &g_pszEnv[_tcslen(g_pszEnv)-1];
			while (_istspace(*pChar) && (pChar >= g_pszEnv))
			{
				*pChar = '\0';
				pChar--;
			}
		}
		else
		// Check for hosts
		if (_tcsnicmp(buffer, TEXT("hosts"), 5) == 0)
		{
			g_nHosts = 0;
			g_pHosts = NULL;
			HostNode *node, dummy;
			dummy.next = NULL;
			node = &dummy;
			while (_fgetts(buffer, 1024, fin))
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
	fclose(fin);
	return PARSE_SUCCESS;
}

// Function name	: GetAccountAndPassword
// Description	    : Attempts to read the password from the registry, 
//	                  upon failure it requests the user to provide one
// Return type		: void 
void GetAccountAndPassword()
{
	TCHAR ch=0;
	int index = 0;
	
	do
	{
		_ftprintf(stderr, TEXT("account: "));
		fflush(stderr);
		_getts(g_pszAccount);
	} while (_tcslen(g_pszAccount) == 0);
	
	_ftprintf(stderr, TEXT("password: "));
	fflush(stderr);
	
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	DWORD dwMode;
	if (!GetConsoleMode(hStdin, &dwMode))
		dwMode = ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_MOUSE_INPUT;
	SetConsoleMode(hStdin, dwMode & ~ENABLE_ECHO_INPUT);
	_getts(g_pszPassword);
	SetConsoleMode(hStdin, dwMode);
	
	_ftprintf(stderr, TEXT("\n"));
}

// Function name	: CtrlHandlerRoutine
// Description	    : 
// Return type		: BOOL WINAPI 
// Argument         : DWORD dwCtrlType
bool g_bFirst = true;
BOOL WINAPI CtrlHandlerRoutine(DWORD dwCtrlType)
{
	if (g_bFirst)
	{
		fprintf(stderr, "User break\n");

		// Signal all the threads to stop
		g_bNormalExit = false;
		SetEvent(g_hAbortEvent);

		g_bFirst = false;
		return TRUE;
	}

	ExitProcess(1);
	return TRUE;
}

// Function name	: CreateJobIDFromTemp
// Description	    : 
// Return type		: void 
// Argument         : LPTSTR pszJobID
void CreateJobIDFromTemp(LPTSTR pszJobID)
{
	// Use the name of a temporary file as the job id
	TCHAR tBuffer[MAX_PATH], *pChar;
	GetTempFileName(_T("."), _T("mpi"), 0, pszJobID);
	GetFullPathName(pszJobID, 100, tBuffer, &pChar);
	DeleteFile(pszJobID);
	_tcscpy(pszJobID, pChar);
}

// Function name	: CreateJobID
// Description	    : 
// Return type		: void 
// Argument         : LPTSTR pszJobID
void CreateJobID(LPTSTR pszJobID)
{
	DWORD ret_val, job_number=0, type, num_bytes = sizeof(DWORD);
	HANDLE hMutex = CreateMutex(NULL, FALSE, TEXT("MPIJobNumberMutex"));
	TCHAR pszHost[100];
	DWORD size = 100;
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
	if ((ret_val = RegQueryValueEx(hKey, TEXT("Job Number"), 0, &type, (BYTE *)&job_number, &num_bytes)) != ERROR_SUCCESS)
	{
		RegCloseKey(hKey);
		ReleaseMutex(hMutex);
		CloseHandle(hMutex);
		CreateJobIDFromTemp(pszJobID);
		return;
	}

	// Increment the job number and write it back to the registry
	job_number++;
	if ((ret_val = RegSetValueEx(hKey, TEXT("Job Number"), 0, REG_DWORD, (CONST BYTE *)&job_number, sizeof(DWORD))) != ERROR_SUCCESS)
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

	GetComputerName(pszHost, &size);

	_stprintf(pszJobID, TEXT("%s.%d"), pszHost, job_number);
}

// Function name	: main
// Description	    : 
// Return type		: void 
// Argument         : int argc
// Argument         : TCHAR *argv[]
void main(int argc, TCHAR *argv[])
{
	int i;
	int iproc = 0;
	TCHAR pszJobID[100];
	TCHAR pszEnv[MAX_PATH] = TEXT("");
	TCHAR pszDir[MAX_PATH] = TEXT(".");
	HANDLE *pThread;
	int nShmLow, nShmHigh;
	DWORD dwThreadID;
	HRESULT hr;
	bool bLogon = false;
	bool bUseBNRnp = false;
	bool bUseMPICH2 = false;

	SetConsoleCtrlHandler(CtrlHandlerRoutine, TRUE);

#ifdef MULTI_COLOR_OUTPUT
	CONSOLE_SCREEN_BUFFER_INFO info;
	// Save the state of the console so it can be restored after each change
	HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	GetConsoleScreenBufferInfo(hStdout, &info);
	g_ConsoleAttribute = info.wAttributes;
#endif

	if (argc < 2 || GetOpt(argc, argv, "-help") || GetOpt(argc, argv, "-?") || GetOpt(argc, argv, "/?"))
	{
		PrintOptions();
		return;
	}

	g_bUseBNR = GetOpt(argc, argv, "-bnr");
	bUseMPICH2 = GetOpt(argc, argv, "-mpich2");
	if (bUseMPICH2)
		g_bUseBNR = true;
	if (g_bUseBNR)
	{
	    if (!LoadBNRFunctions())
	    {
		printf("Unable to load the BNR process managing dynamic library, exiting\n");
		return;
	    }
		if (BNR_Init() == BNR_FAIL)
			g_bUseBNR = false;
	}
	g_bNoMPI = GetOpt(argc, argv, "-nompi");
	GetOpt(argc, argv, "-env", g_pszEnv);
	bLogon = GetOpt(argc, argv, "-logon");
	if (!GetOpt(argc, argv, "-dir", pszDir))
		GetCurrentDirectory(MAX_PATH, pszDir);
	
	if (argc < 2)
	{
	    PrintOptions();
	    return;
	}

	DWORD dwType;
	if (GetBinaryType(argv[1], &dwType))
	{
		// The first argument is an executable so set things up to run one process
		g_nHosts = 1;
		TCHAR pszTempExe[MAX_PATH], *namepart;
		_tcscpy(g_pszExe, argv[1]);
		GetFullPathName(g_pszExe, MAX_PATH, pszTempExe, &namepart);
		// Quote the executable in case there are spaces in the path
		_stprintf(g_pszExe, TEXT("\"%s\""), pszTempExe);
		g_pszArgs[0] = TEXT('\0');
		for (int i=2; i<argc; i++)
		{
			_tcscat(g_pszArgs, argv[i]);
			if (i < argc-1)
				_tcscat(g_pszArgs, TEXT(" "));
		}
		//g_bNoMPI = true;
		RunLocal(true);
		return;
	}
	else
	{
		if (GetOpt(argc, argv, "-np", &g_nHosts))
		{
			if (g_nHosts < 1)
			{
				printf("Error: must specify a number greater than 0 after the -np option\n");
				return;
			}
			if (argc < 2)
			{
				printf("Error: not enough arguments.\n");
				return;
			}
			TCHAR pszTempExe[MAX_PATH], *namepart;
			_tcscpy(g_pszExe, argv[1]);
			GetFullPathName(g_pszExe, MAX_PATH, pszTempExe, &namepart);
			// Quote the executable in case there are spaces in the path
			_stprintf(g_pszExe, TEXT("\"%s\""), pszTempExe);
			g_pszArgs[0] = TEXT('\0');
			for (int i=2; i<argc; i++)
			{
				_tcscat(g_pszArgs, argv[i]);
				if (i < argc-1)
					_tcscat(g_pszArgs, TEXT(" "));
			}
			if (g_bUseBNR)
				bUseBNRnp = true;
			else
			{
				if (!GetAvailableHosts())
				{
				    RunLocal(true);
				    return;
				}
			}
		}
		else
		if (GetOpt(argc, argv, "-localonly", &g_nHosts))
		{
			bool bDoSMP = !GetOpt(argc, argv, "-tcp");
			if (g_nHosts < 1)
			{
				printf("Error: must specify a number greater than 0 after the -localonly option\n");
				return;
			}
			if (argc < 2)
			{
				printf("Error: not enough arguments.\n");
				return;
			}
			TCHAR pszTempExe[MAX_PATH], *namepart;
			_tcscpy(g_pszExe, argv[1]);
			GetFullPathName(g_pszExe, MAX_PATH, pszTempExe, &namepart);
			// Quote the executable in case there are spaces in the path
			_stprintf(g_pszExe, TEXT("\"%s\""), pszTempExe);
			g_pszArgs[0] = TEXT('\0');
			for (int i=2; i<argc; i++)
			{
				_tcscat(g_pszArgs, argv[i]);
				if (i < argc-1)
					_tcscat(g_pszArgs, TEXT(" "));
			}
			RunLocal(bDoSMP);
			return;
		}
		else
		{
			if (ParseConfigFile(argv[1]) == PARSE_ERR_NO_FILE)
			{
				// The first argument might be an executable with the extension missing (.exe, .bat, .com, etc.) 
				// so set things up to run one process
				g_nHosts = 1;
				TCHAR pszTempExe[MAX_PATH], *namepart;
				_tcscpy(g_pszExe, argv[1]);
				GetFullPathName(g_pszExe, MAX_PATH, pszTempExe, &namepart);
				// Quote the executable in case there are spaces in the path
				_stprintf(g_pszExe, TEXT("\"%s\""), pszTempExe);
				g_pszArgs[0] = TEXT('\0');
				for (int i=2; i<argc; i++)
				{
					_tcscat(g_pszArgs, argv[i]);
					if (i < argc-1)
						_tcscat(g_pszArgs, TEXT(" "));
				}
				//g_bNoMPI = true;
				RunLocal(true);
				return;
			}
			else
			{
				if ((_tcslen(g_pszArgs) > 0) && (argc > 2))
					_tcscat(g_pszArgs, TEXT(" "));
				for (int i=2; i<argc; i++)
				{
					_tcscat(g_pszArgs, argv[i]);
					if (i < argc-1)
						_tcscat(g_pszArgs, TEXT(" "));
				}
			}
		}
	}

	TCHAR pszTempExe[MAX_PATH], *namepart;
	GetFullPathName(g_pszExe, MAX_PATH, pszTempExe, &namepart);
	// Quote the executable in case there are spaces in the path
	_stprintf(g_pszExe, TEXT("\"%s\""), pszTempExe);

	if (bLogon)
		GetAccountAndPassword();
	else if (ReadPasswordFromRegistry(g_pszAccount, g_pszPassword))
		bLogon = true;

	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

	if (FAILED(hr))
	{
		_tprintf(TEXT("CoInitialize() failed.\n"));
		PrintError(hr);
		return;
	}

	hr = CoInitializeSecurity(NULL, -1, NULL, NULL,
		//RPC_C_AUTHN_LEVEL_NONE,	RPC_C_IMP_LEVEL_ANONYMOUS, NULL, EOAC_NONE, NULL);
		//RPC_C_AUTHN_LEVEL_NONE,	RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
		RPC_C_AUTHN_LEVEL_CONNECT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
		//RPC_C_AUTHN_LEVEL_PKT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
	if (FAILED(hr))
	{
		if (hr == RPC_E_TOO_LATE)
			printf("CoInitializeSecurity failed because it has already been set.\n");
		else
		{
			char error_msg[256];
			Translate_HRError(hr, error_msg);
			printf("CoInitializeSecurity failed\nError: %s", error_msg);
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
	
	CreateJobID(pszJobID);
	
	if (bUseMPICH2)
	{
		char pszKey[100], pBuffer[4096];
		BNR_Group mpirun_group, spawned_group, joint_group;
		BNR_Info info;
		char pszEnv[4096];

		//SetConnectionsLeft(nProc * 2);

		HANDLE hReadyEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		DWORD dwThreadID;
		HANDLE hIOThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectIOLoopThread, hReadyEvent, 0, &dwThreadID);
		if (WaitForSingleObject(hReadyEvent, 5000) != WAIT_OBJECT_0)
		{
			printf("Wait for hReadyEvent failed, error %d\n", GetLastError());
			ExitProcess(1);
		}

		BNR_Info_create(&info);
		strcpy(pBuffer, g_pszIOListenHost);
		BNR_Info_set(info, "stdinHost", pBuffer);
		sprintf(pBuffer, "%d", g_nIOListenPort);
		BNR_Info_set(info, "stdinPort", pBuffer);
		strcpy(pBuffer, g_pszIOListenHost);
		BNR_Info_set(info, "stdoutHost", pBuffer);
		sprintf(pBuffer, "%d", g_nIOListenPort);
		BNR_Info_set(info, "stdoutPort", pBuffer);
		strcpy(pBuffer, g_pszIOListenHost);
		BNR_Info_set(info, "stderrHost", pBuffer);
		sprintf(pBuffer, "%d", g_nIOListenPort);
		BNR_Info_set(info, "stderrPort", pBuffer);

		g_hBNRProcessesFinishedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		g_nNumBNRProcessesRemaining = nProc;

		BNR_Get_group(&mpirun_group);
		BNR_Open_group(mpirun_group, &spawned_group);
		//BNR_Spawn(spawned_group, nProc, g_pszExe, g_pszArgs, g_pszEnv, info, ExitBNRProcess);
		for (int i=0; i<nProc; i++)
		{
			if (strlen(g_pszEnv))
				sprintf(pszEnv, "SHMEMKEY=%s|SHMEMGRPSIZE=%d|SHMEMGRPRANK=%d|%s", pszJobID, nProc, i, g_pszEnv);
			else
				sprintf(pszEnv, "SHMEMKEY=%s|SHMEMGRPSIZE=%d|SHMEMGRPRANK=%d", pszJobID, nProc, i);
			BNR_Spawn(spawned_group, 1, g_pszExe, g_pszArgs, pszEnv, info, ExitBNRProcess);
		}
		BNR_Close_group(spawned_group);
		BNR_Merge(mpirun_group, spawned_group, &joint_group);

		for (i=0; i<nProc; i++)
		{
			sprintf(pBuffer, "MPICH_JOBID=%s|MPICH_NPROC=%d|MPICH_IPROC=%d", pszJobID, nProc, i);
			sprintf(pszKey, "env%d", i);
			BNR_Put(joint_group, pszKey, pBuffer, i);
		}
		BNR_Fence(joint_group);
		
		WaitForSingleObject(g_hBNRProcessesFinishedEvent, INFINITE);
		//WaitForAllConnections();

		BNR_Free_group(joint_group);
		BNR_Free_group(spawned_group);
		BNR_Free_group(mpirun_group);

		BNR_Finalize();
	}
	else
	if (g_bUseBNR)
	{
		char pszKey[100], pBuffer[4096];
		BNR_Group mpirun_group, spawned_group, joint_group;
		BNR_Info info;

		//SetConnectionsLeft(nProc * 2);

		HANDLE hReadyEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		DWORD dwThreadID;
		HANDLE hIOThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectIOLoopThread, hReadyEvent, 0, &dwThreadID);
		if (WaitForSingleObject(hReadyEvent, 5000) != WAIT_OBJECT_0)
		{
			printf("Wait for hReadyEvent failed, error %d\n", GetLastError());
			ExitProcess(1);
		}

		BNR_Info_create(&info);
		strcpy(pBuffer, g_pszIOListenHost);
		BNR_Info_set(info, "stdinHost", pBuffer);
		sprintf(pBuffer, "%d", g_nIOListenPort);
		BNR_Info_set(info, "stdinPort", pBuffer);
		strcpy(pBuffer, g_pszIOListenHost);
		BNR_Info_set(info, "stdoutHost", pBuffer);
		sprintf(pBuffer, "%d", g_nIOListenPort);
		BNR_Info_set(info, "stdoutPort", pBuffer);
		strcpy(pBuffer, g_pszIOListenHost);
		BNR_Info_set(info, "stderrHost", pBuffer);
		sprintf(pBuffer, "%d", g_nIOListenPort);
		BNR_Info_set(info, "stderrPort", pBuffer);

		g_hBNRProcessesFinishedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		g_nNumBNRProcessesRemaining = nProc;

		BNR_Get_group(&mpirun_group);
		BNR_Open_group(mpirun_group, &spawned_group);
		BNR_Spawn(spawned_group, nProc, g_pszExe, g_pszArgs, g_pszEnv, info, ExitBNRProcess);
		BNR_Close_group(spawned_group);
		BNR_Merge(mpirun_group, spawned_group, &joint_group);

		for (i=0; i<nProc; i++)
		{
			sprintf(pBuffer, "MPICH_JOBID=%s|MPICH_NPROC=%d|MPICH_IPROC=%d", pszJobID, nProc, i);
			sprintf(pszKey, "env%d", i);
			BNR_Put(joint_group, pszKey, pBuffer, i);
		}
		BNR_Fence(joint_group);
		
		WaitForSingleObject(g_hBNRProcessesFinishedEvent, INFINITE);
		//WaitForAllConnections();

		BNR_Free_group(joint_group);
		BNR_Free_group(spawned_group);
		BNR_Free_group(mpirun_group);

		BNR_Finalize();
	}
	else
	{
		// Set the environment variables common to all processes
		sprintf(pszEnv, "MPICH_JOBID=%s|MPICH_NPROC=%d|MPICH_ROOTHOST=%s",
			pszJobID, nProc, g_pHosts->host);
		
		// Allocate an array to hold handles to the LaunchProcess threads
		pThread = new HANDLE[nProc];
		g_pAbortThreads = new HANDLE[nProc];
		for (i=0; i<nProc; i++)
		    g_pAbortThreads[i] = NULL;
		
		// Launch the processes
		while (g_pHosts)
		{
			nShmLow = iproc;
			nShmHigh = iproc + g_pHosts->nSMPProcs - 1;
			for (int i=0; i<g_pHosts->nSMPProcs; i++)
			{
				LaunchProcessArg *arg = new LaunchProcessArg;
				arg->i = iproc;
				arg->bLogon = bLogon;
#ifdef UNICODE
				if (bLogon)
				{
					wcscpy(arg->pszAccount, g_pszAccount);
					wcscpy(arg->pszPassword, g_pszPassword);
				}
				if (wcslen(g_pHosts->exe) > 0)
					wcscpy(arg->pszCmdLine, g_pHosts->exe);
				else
					wcscpy(arg->pszCmdLine, g_pszExe);
				if (wcslen(g_pszArgs) > 0)
				{
					wcscat(arg->pszCmdLine, L" ");
					wcscat(arg->pszCmdLine, g_pszArgs);
				}
				wcscpy(arg->pszDir, pszDir);
				wcscpy(arg->pszEnv, pszEnv);
				wcscpy(arg->pszHost, g_pHosts->host);
				wcscpy(arg->pszJobID, pszJobID);
				
				if (iproc == 0)
					swprintf(pBuffer, L"MPICH_ROOTPORT=-1|MPICH_IPROC=%d|MPICH_SHM_LOW=%d|MPICH_SHM_HIGH=%d", iproc, nShmLow, nShmHigh);
				else
					swprintf(pBuffer, L"|MPICH_ROOTPORT=%d|MPICH_IPROC=%d|MPICH_SHM_LOW=%d|MPICH_SHM_HIGH=%d", g_nRootPort, iproc, nShmLow, nShmHigh);
				if (wcslen(arg->pszEnv) > 0)
					wcscat(arg->pszEnv, L"|");
				wcscat(arg->pszEnv, pBuffer);
				
				if (wcslen(g_pszEnv) > 0)
				{
					wcscat(arg->pszEnv, L"|");
					wcscat(arg->pszEnv, g_pszEnv);
				}
#else
				WCHAR wTemp[MAX_PATH];
				if (bLogon)
				{
					mbstowcs(arg->pszAccount, g_pszAccount, strlen(g_pszAccount)+1);
					mbstowcs(arg->pszPassword, g_pszPassword, strlen(g_pszPassword)+1);
				}
				if (strlen(g_pHosts->exe) > 0)
					mbstowcs(arg->pszCmdLine, g_pHosts->exe, strlen(g_pHosts->exe)+1);
				else
					mbstowcs(arg->pszCmdLine, g_pszExe, strlen(g_pszExe)+1);
				if (strlen(g_pszArgs) > 0)
				{
					wcscat(arg->pszCmdLine, L" ");
					mbstowcs(wTemp, g_pszArgs, strlen(g_pszArgs)+1);
					wcscat(arg->pszCmdLine, wTemp);
				}
				mbstowcs(arg->pszDir, pszDir, strlen(pszDir)+1);
				mbstowcs(arg->pszEnv, pszEnv, strlen(pszEnv)+1);
				mbstowcs(arg->pszHost, g_pHosts->host, strlen(g_pHosts->host)+1);
				mbstowcs(arg->pszJobID, pszJobID, strlen(pszJobID)+1);
				
				if (iproc == 0)
					swprintf(wTemp, L"MPICH_ROOTPORT=-1|MPICH_IPROC=%d|MPICH_SHM_LOW=%d|MPICH_SHM_HIGH=%d", iproc, nShmLow, nShmHigh);
				else
					swprintf(wTemp, L"MPICH_ROOTPORT=%d|MPICH_IPROC=%d|MPICH_SHM_LOW=%d|MPICH_SHM_HIGH=%d", g_nRootPort, iproc, nShmLow, nShmHigh);
				if (wcslen(arg->pszEnv) > 0)
					wcscat(arg->pszEnv, L"|");
				wcscat(arg->pszEnv, wTemp);
				
				if (strlen(g_pszEnv) > 0)
				{
					wcscat(arg->pszEnv, L"|");
					mbstowcs(wTemp, g_pszEnv, strlen(g_pszEnv)+1);
					wcscat(arg->pszEnv, wTemp);
				}
#endif			
				pThread[iproc] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)LaunchProcess, arg, 0, &dwThreadID);
				if (pThread[iproc] == NULL)
				{
					printf("Unable to create LaunchProcess thread\n");
					ExitProcess(1);
				}
				if (iproc == 0)
				{
					// Wait for the root port to be valid
					while (g_nRootPort == 0)
						Sleep(200);
				}
				iproc++;
			}
			
			HostNode *n = g_pHosts;
			g_pHosts = g_pHosts->next;
			delete n;
		}
		
		WaitForLotsOfObjects(nProc, pThread);
		
		SetEvent(g_hAbortEvent);
		for (i=0; i<nProc; i++)
			CloseHandle(pThread[i]);
		delete pThread;

		WaitForLotsOfObjects(nProc, g_pAbortThreads);
		for (i=0; i<nProc; i++)
		    CloseHandle(g_pAbortThreads[i]);
		delete g_pAbortThreads;
		CloseHandle(g_hAbortEvent);
	}
	
#ifdef MULTI_COLOR_OUTPUT
	SetConsoleTextAttribute(hStdout, g_ConsoleAttribute);
#endif
	
	CoUninitialize();
}
