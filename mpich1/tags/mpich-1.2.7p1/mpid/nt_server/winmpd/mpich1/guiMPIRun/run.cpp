#include "stdafx.h"
#include "guiMPIRun.h"

#include "guiMPIRunDoc.h"
#include "guiMPIRunView.h"
#include "MPIJobDefs.h"
#include "mpd.h"
#include "global.h"
#include "MPICH_pwd.h"
#include "LaunchProcess.h"
#include "WaitThread.h"
#include "UserPwdDialog.h"
#include "RedirectIO.h"
#include <Winnetwk.h>
#include "mpdutil.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

bool ReadMPDRegistry(char *name, char *value, DWORD *length = NULL);

bool ReadMpichRegistry(char *name, char *value, DWORD *length = NULL)
{
    HKEY tkey;
    DWORD len, result;
    
    // Open the root key
    if ((result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, MPICHKEY,
	0, 
	KEY_READ,
	&tkey)) != ERROR_SUCCESS)
    {
	//printf("Unable to open the MPD registry key, error %d\n", result);
	return false;
    }
    
    if (length == NULL)
	len = MAX_CMD_LENGTH;
    else
	len = *length;
    result = RegQueryValueEx(tkey, name, 0, NULL, (unsigned char *)value, &len);
    if (result != ERROR_SUCCESS)
    {
	//printf("Unable to read the mpd registry key '%s', error %d\n", name, result);
	RegCloseKey(tkey);
	return false;
    }
    if (length != NULL)
	*length = len;
    
    RegCloseKey(tkey);
    return true;
}

void WriteMpichRegistry(char *name, char *value)
{
    HKEY tkey;
    DWORD result;

    // Open the root key
    if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, MPICHKEY,
	0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &tkey, &result) != ERROR_SUCCESS)
    {
	return;
    }
    if ((result = RegSetValueEx(tkey, name, 0, REG_SZ, (const unsigned char *)value, strlen(value)+1)) != ERROR_SUCCESS)
    {
	//printf("WriteMpichRegistry failed to write '%s:%s', error %d\n", name, value, result);
    }
    RegCloseKey(tkey);
}

// Function name	: CtrlHandlerRoutine
// Description	    : 
// Return type		: BOOL WINAPI 
// Argument         : DWORD dwCtrlType
void CGuiMPIRunView::OnBreakBtn()
{
    // Hit Break once and I'll try to kill the remote processes
    if (m_bFirstBreak)
    {
	// Signal all the threads to stop
	m_bNormalExit = false;
	SetEvent(m_hAbortEvent);

	if (easy_send(m_sockBreak, "x", 1) == SOCKET_ERROR)
	{
	    if (easy_send(m_sockStopIOSignalSocket, "x", 1) == SOCKET_ERROR)
	    {
		MessageBox("Break failed", "Error");
	    }
	}
	
	m_bFirstBreak = false;

	m_break_btn.SetWindowText("Abort");

	return;
    }
    
    // Hit Break twice and I'll close all the connections to the remote processes
    Abort();
    m_break_btn.SetWindowText("Break");
    m_break_btn.EnableWindow(FALSE);

    return;
}

void CGuiMPIRunView::GetHosts()
{
    // Create a temporary list of all the hosts
    HostNode *list = NULL, *p;
    if (m_bAnyHosts)
    {
	list = p = new HostNode;
	for (int i=0; i<m_host_list.GetCount(); i++)
	{
	    m_host_list.GetText(i, p->host);
	    if (i<m_host_list.GetCount()-1)
	    {
		p->next = new HostNode;
		p = p->next;
	    }
	    else
		p->next = NULL;
	}
    }
    else
    {
	int pIndices[1024];
	int n;
	list = p = new HostNode;
	n = m_host_list.GetSelItems(1024, pIndices);
	for (int i=0; i<n; i++)
	{
	    m_host_list.GetText(pIndices[i], p->host);
	    if (i<n-1)
	    {
		p->next = new HostNode;
		p = p->next;
	    }
	    else
		p->next = NULL;
	}
    }
    // make the list a loop
    p->next = list;

    // Delete the old application host list
    if (m_pHosts)
    {
	while (m_pHosts)
	{
	    p = m_pHosts;
	    m_pHosts = m_pHosts->next;
	    delete p;
	}
    }

    // Create a list of hosts for this application, looping through the host list if necessary
    HostNode *pHost;
    int n = m_nproc;
    pHost = m_pHosts = new HostNode;
    p = list;
    while (n)
    {
	strcpy(pHost->host, p->host);
	if (m_bUseSlaveProcess)
	{
	    strncpy(pHost->exe, m_SlaveProcess, MAX_CMD_LENGTH);
	    pHost->exe[MAX_CMD_LENGTH-1] = '\0';
	}
	else
	{
	    strncpy(pHost->exe, m_app, MAX_CMD_LENGTH);
	    pHost->exe[MAX_CMD_LENGTH-1] = '\0';
	}
	pHost->nSMPProcs = 1;
	if (n>1)
	{
	    pHost->next = new HostNode;
	    pHost = pHost->next;
	}
	else
	    pHost->next = NULL;
	p = p->next;
	n--;
    }
    if (m_bUseSlaveProcess && m_pHosts)
    {
	strncpy(m_pHosts->exe, m_app, MAX_CMD_LENGTH);
	m_pHosts->exe[MAX_CMD_LENGTH-1] = '\0';
    }

    // Delete the temporary host list
    p = list->next;
    list->next = NULL;
    list = p;
    while (list)
    {
	p = list;
	list = list->next;
	delete p;
    }
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
    char pszHost[100];
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
    
    GetComputerName(pszHost, &size);
    
    sprintf(pszJobID, "%s.%d", pszHost, job_number);
}

void CGuiMPIRunView::EnableRunning()
{
    m_nproc_edit.EnableWindow(FALSE);
    m_nproc_spin.EnableWindow(FALSE);
    m_app_combo.EnableWindow(FALSE);
    m_app_browse_btn.EnableWindow(FALSE);
    m_run_btn.EnableWindow(FALSE);
    m_advanced_btn.EnableWindow(FALSE);

    m_break_btn.EnableWindow();

    ResetEvent(m_hJobFinished);

    if (m_redirect)
    {
	//m_fout = fopen(m_output_filename, "a+");
	m_fout = fopen(m_output_filename, "w");
    }

    m_output.SetFocus();
}

void CGuiMPIRunView::DisableRunning()
{
    if (!m_bUseConfigFile)
    {
	m_nproc_edit.EnableWindow();
	m_nproc_spin.EnableWindow();
	m_app_combo.EnableWindow();
	m_app_browse_btn.EnableWindow();
    }
    m_run_btn.EnableWindow();
    m_advanced_btn.EnableWindow();

    m_break_btn.EnableWindow(FALSE);
    m_break_btn.SetWindowText("Break");

    SetEvent(m_hJobFinished);

    if (m_redirect && m_fout)
    {
	fclose(m_fout);
	m_fout = NULL;
    }
}

static bool NeedToMap(CString pszFullPath, char *pDrive, CString &pszShare)
{
    DWORD dwResult;
    DWORD dwLength;
    char pBuffer[4096];
    REMOTE_NAME_INFO *info = (REMOTE_NAME_INFO*)pBuffer;

    pszFullPath.Remove('"');

    dwLength = 4096;
    info->lpConnectionName = NULL;
    info->lpRemainingPath = NULL;
    info->lpUniversalName = NULL;
    dwResult = WNetGetUniversalName(pszFullPath, REMOTE_NAME_INFO_LEVEL, info, &dwLength);
    if (dwResult == NO_ERROR)
    {
	*pDrive = pszFullPath[0];
	pszShare = info->lpConnectionName;
	return true;
    }

    return false;
}

static void ExeToUnc(char *pszExe)
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

static void ExeToUnc(CString &pszExe)
{
    DWORD dwResult;
    DWORD dwLength;
    char pBuffer[4096];
    REMOTE_NAME_INFO *info = (REMOTE_NAME_INFO*)pBuffer;
    bool bQuoted = false;

    if (pszExe[0] == '"')
    {
	bQuoted = true;
	pszExe.Remove('"');
    }
    dwLength = 4096;
    info->lpConnectionName = NULL;
    info->lpRemainingPath = NULL;
    info->lpUniversalName = NULL;
    dwResult = WNetGetUniversalName(pszExe, REMOTE_NAME_INFO_LEVEL, info, &dwLength);
    if (dwResult == NO_ERROR)
    {
	if (bQuoted)
	    pszExe = '"' + info->lpUniversalName + '"';
	else
	    pszExe = info->lpUniversalName;
    }
}

static void SeparateCommand(CString pszApp, CString &pszExe, CString &pszArgs)
{
    int n;
    CString str;
    char pszTempExe[MAX_CMD_LENGTH], *namepart;

    if (GetFullPathName(pszApp, MAX_CMD_LENGTH, pszTempExe, &namepart) == 0)
    {
	pszArgs = "";
	pszExe = pszApp;
	return;
    }

    str = namepart;
    *namepart = '\0';
    pszExe = pszTempExe;
    n = str.FindOneOf(" \t\n");
    if (n > 0)
    {
	pszArgs = str.Right(str.GetLength() - n);
	pszExe += str.Left(n);
    }
    else
    {
	pszArgs = "";
	pszExe += str;
    }

    pszExe.TrimRight();
    pszArgs.TrimLeft();
    pszArgs.TrimRight();
}

static void CmdLineToUnc(CString &pszApp)
{
    CString pszExe, pszArgs;
    SeparateCommand(pszApp, pszExe, pszArgs);
    ExeToUnc(pszExe);
    pszApp = pszExe;
    if (pszArgs.GetLength())
	pszApp += " " + pszArgs;
}

// Function name	: ParseLineIntoHostNode
// Description	    : 
// Return type		: HostNode* 
// Argument         : char * line
static HostNode* ParseLineIntoHostNode(char * line)
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

// Function name	: ParseConfigFile
// Description	    : 
// Return type		: int 
int CGuiMPIRunView::ParseConfigFile()
{
    CString sArgs("");
    FILE *fin;
    char buffer[1024] = "";

    fin = fopen(m_ConfigFileName, "r");
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
	    m_app = pChar;
	    m_app.TrimRight();
	    ExeToUnc(m_app);
	    m_app = "\"" + m_app + "\"";
	}
	else
	{
	    // Check for program arguments
	    if (strnicmp(buffer, "args ", 5) == 0)
	    {
		sArgs = &buffer[5];
		sArgs.TrimLeft();
		sArgs.TrimRight();
	    }
	    else
	    {
		// Check for environment variables
		if (strnicmp(buffer, "env ", 4) == 0)
		{
		    m_CommonEnvironment = &buffer[4];
		    m_CommonEnvironment.TrimLeft();
		    m_CommonEnvironment.TrimRight();
		    if (m_CommonEnvironment.GetLength() > 0)
			m_bUseCommonEnvironment = true;
		}
		else
		{
		    // Check for hosts
		    if (strnicmp(buffer, "hosts", 5) == 0)
		    {
			m_nproc = 0;
			m_pHosts = NULL;
			HostNode *node, dummy;
			dummy.next = NULL;
			node = &dummy;
			while (fgets(buffer, 1024, fin))
			{
			    node->next = ParseLineIntoHostNode(buffer);
			    if (node->next != NULL)
			    {
				node = node->next;
				m_nproc = m_nproc + node->nSMPProcs;
			    }
			}
			m_pHosts = dummy.next;
			
			fclose(fin);

			if (sArgs.GetLength() > 0)
			    m_app = m_app + " " + sArgs;

			return PARSE_SUCCESS;
		    }
		}
	    }
	}
    }
    fclose(fin);
    if (sArgs.GetLength() > 0)
	m_app = m_app + " " + sArgs;

    return PARSE_SUCCESS;
}

static MapDriveNode *MakeMapFromString(CString str)
{
    if (str.GetLength() > 7 && str[1] == ':')
    {
	MapDriveNode *pNode = new MapDriveNode;
	pNode->cDrive = str[0];
	strcpy(pNode->pszShare, str.Right(str.GetLength()-2));
	pNode->pNext = NULL;
	return pNode;
    }
    return NULL;
}

void RunJob(CGuiMPIRunView *pDlg)
{
    int i;
    int iproc = 0;
    char pszJobID[100];
    char pszEnv[MAX_CMD_LENGTH] = "";
    char pszDir[MAX_PATH] = ".";
    int nShmLow, nShmHigh;
    DWORD dwThreadID;
    char pBuffer[MAX_CMD_LENGTH];
    char cMapDrive;
    CString pszMapShare;
    CString sAppOriginal;
    CString sMapping = "";
    int iter;

    try{
    sAppOriginal = pDlg->m_app;
    CmdLineToUnc(pDlg->m_app);
    if (pDlg->m_bUseSlaveProcess)
	CmdLineToUnc(pDlg->m_SlaveProcess);

    if (!pDlg->m_bUseConfigFile)
    {
	// Make a list of hosts to launch on based on m_nproc and m_host_list
	pDlg->GetHosts();
    }

    /*
    if (pDlg->m_bUseMapping)
    {
	// Build the mapping nodes here
	CString str, sub;
	MapDriveNode *pNode;

	str = pDlg->m_Mappings;
	int index = 0, n;
	n = str.Find(';', index);
	while (n != -1)
	{
	    sub = str.Mid(index, n-index);
	    pNode = MakeMapFromString(sub);
	    if (pNode != NULL)
	    {
		pNode->pNext = pDlg->m_pDriveMapList;
		pDlg->m_pDriveMapList = pNode;
	    }
	    n++; // skip over the ;
	    index = n;
	    n = str.Find(';', index);
	}
	sub = str.Mid(index);
	pNode = MakeMapFromString(sub);
	if (pNode != NULL)
	{
	    pNode->pNext = pDlg->m_pDriveMapList;
	    pDlg->m_pDriveMapList = pNode;
	}
    }
    */

    // Create a job id string
    CreateJobID(pszJobID);
    
    // Set the environment variables common to all processes
    if (pDlg->m_bNoMPI)
    {
	if (pDlg->m_bUseCommonEnvironment && (pDlg->m_CommonEnvironment.GetLength() > 0))
	{
	    if (_snprintf(pszEnv, MAX_CMD_LENGTH, "%s", pDlg->m_CommonEnvironment) < 0)
	    {
		// environment variables truncated
		pszEnv[MAX_CMD_LENGTH-1] = '\0';
	    }
	}
	else
	    pszEnv[0] = '\0';
    }
    else
    {
	easy_get_ip_string(pDlg->m_pHosts->host, pDlg->m_pHosts->host);
	if (pDlg->m_bUseCommonEnvironment && (pDlg->m_CommonEnvironment.GetLength() > 0))
	{
	    if (_snprintf(pszEnv, MAX_CMD_LENGTH, "%s|MPICH_JOBID=%s|MPICH_NPROC=%d|MPICH_ROOTHOST=%s",
		pDlg->m_CommonEnvironment, pszJobID, pDlg->m_nproc, pDlg->m_pHosts->host) < 0)
	    {
		// environment variables truncated
		pszEnv[MAX_CMD_LENGTH-1] = '\0';
	    }
	}
	else
	{
	    if (_snprintf(pszEnv, MAX_CMD_LENGTH, "MPICH_JOBID=%s|MPICH_NPROC=%d|MPICH_ROOTHOST=%s",
		pszJobID, pDlg->m_nproc, pDlg->m_pHosts->host) < 0)
	    {
		// environment variables truncated
		pszEnv[MAX_CMD_LENGTH-1] = '\0';
	    }
	}
    }

    // Get the directory from the executable or use the user selected working directory
    if (pDlg->m_bUseWorkingDirectory)
    {
	strcpy(pszDir, pDlg->m_WorkingDirectory);
    }
    else
    {
	if (sAppOriginal[0] == '\\' && sAppOriginal[1] == '\\')
	{
	    GetCurrentDirectory(MAX_PATH, pszDir);
	}
	else
	{
	    char pszTempExe[MAX_CMD_LENGTH] = ".", *namepart;
	    if (GetFullPathName(sAppOriginal, MAX_CMD_LENGTH, pszTempExe, &namepart) > 0)
	    {
		if (namepart - pszTempExe < MAX_CMD_LENGTH)
		{
		    strncpy(pszDir, pszTempExe, namepart - pszTempExe);
		    pszDir[namepart - pszTempExe] = '\0';
		}
	    }
	    else
	    {
		CString str = sAppOriginal.Left(pDlg->m_app.ReverseFind('\\'));
		if (GetFullPathName(str, MAX_CMD_LENGTH, pszTempExe, &namepart) > 0)
		{
		    if (namepart - pszTempExe < MAX_CMD_LENGTH)
		    {
			strncpy(pszDir, pszTempExe, namepart - pszTempExe);
			pszDir[namepart - pszTempExe] = '\0';
		    }
		}
	    }
	}
    }
    if (NeedToMap(pszDir, &cMapDrive, pszMapShare))
    {
	if (pDlg->m_bUseMapping)
	    sMapping.Format(" m='%c:%s;%s'", cMapDrive, pszMapShare, pDlg->m_Mappings);
	else
	    sMapping.Format(" m='%c:%s'", cMapDrive, pszMapShare);
	/*
	MapDriveNode *pNode = new MapDriveNode;
	pNode->cDrive = cMapDrive;
	strcpy(pNode->pszShare, pszMapShare);
	pNode->pNext = pDlg->m_pDriveMapList;
	pDlg->m_pDriveMapList = pNode;
	*/
    }
    else
    {
	if (pDlg->m_bUseMapping)
	    sMapping.Format(" m='%s'", pDlg->m_Mappings);
    }

    // Allocate an array to hold handles to the LaunchProcess threads
    pDlg->m_nNumProcessThreads = 0;
    pDlg->m_pProcessThread = new HANDLE[pDlg->m_nproc];
    pDlg->m_pProcessSocket = new SOCKET[pDlg->m_nproc];
    pDlg->m_pProcessLaunchId = new int[pDlg->m_nproc];
    pDlg->m_pLaunchIdToRank = new int[pDlg->m_nproc];
    pDlg->m_nNumProcessSockets = 0;
    pDlg->m_pForwardHost = new ForwardHostStruct[pDlg->m_nproc];
    for (i=0; i<pDlg->m_nproc; i++)
	pDlg->m_pForwardHost[i].nPort = 0;
    
    // Start the IO redirection thread
    RedirectIOArg *pArg = new RedirectIOArg;
    pArg->hReadyEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    pArg->pDlg = pDlg;
    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
    {
	pDlg->m_hRedirectIOListenThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectIOThread, pArg, 0, &dwThreadID);
	if (pDlg->m_hRedirectIOListenThread != NULL)
	    break;
	Sleep(CREATE_THREAD_SLEEP_TIME);
    }
    if (pDlg->m_hRedirectIOListenThread)
    {
	if (WaitForSingleObject(pArg->hReadyEvent, 10000) != WAIT_OBJECT_0)
	{
	    MessageBox(NULL, "RedirectIOThread failed to initialize", "Error", MB_OK);
	    delete pArg;
	    delete pDlg->m_pProcessThread;
	    delete pDlg->m_pProcessSocket;
	    delete pDlg->m_pProcessLaunchId;
	    delete pDlg->m_pLaunchIdToRank;
	    delete pDlg->m_pForwardHost;
	    pDlg->m_pProcessThread = NULL;
	    pDlg->m_pProcessSocket = NULL;
	    pDlg->m_nNumProcessSockets = 0;
	    pDlg->m_pProcessLaunchId = NULL;
	    pDlg->m_pLaunchIdToRank = NULL;
	    pDlg->m_pForwardHost = NULL;
	    return;
	}
    }
    else
    {
	char str[256];
	sprintf(str, "Unable to create RedirectIOThread, error %d\n", GetLastError());
	MessageBox(NULL, str, "Error", MB_OK);
	delete pArg;
	delete pDlg->m_pProcessThread;
	delete pDlg->m_pProcessSocket;
	delete pDlg->m_pProcessLaunchId;
	delete pDlg->m_pLaunchIdToRank;
	delete pDlg->m_pForwardHost;
	pDlg->m_pProcessThread = NULL;
	pDlg->m_pProcessSocket = NULL;
	pDlg->m_nNumProcessSockets = 0;
	pDlg->m_pProcessLaunchId = NULL;
	pDlg->m_pLaunchIdToRank = NULL;
	pDlg->m_pForwardHost = NULL;
	return;
    }
    CloseHandle(pArg->hReadyEvent);
    delete pArg;

    // Copy the io redirection thread stuff into the first forwarder entry
    strncpy(pDlg->m_pForwardHost[0].pszHost, pDlg->m_pszIOHost, MAX_HOST_LENGTH);
    pDlg->m_pForwardHost[0].pszHost[MAX_HOST_LENGTH-1] = '\0';
    pDlg->m_pForwardHost[0].nPort = pDlg->m_nIOPort;

    // Launch the processes
    while (pDlg->m_pHosts)
    {
	nShmLow = iproc;
	nShmHigh = iproc + pDlg->m_pHosts->nSMPProcs - 1;
	for (int i = 0; i<pDlg->m_pHosts->nSMPProcs; i++)
	{
	    MPIRunLaunchProcessArg *arg = new MPIRunLaunchProcessArg;
	    if (sMapping.GetLength() > 0)
		strcpy(arg->pszMap, sMapping);
	    else
		arg->pszMap[0] = '\0';
	    arg->bUseDebugFlag = pDlg->m_bCatch;
	    arg->pDlg = pDlg;
	    arg->n = pDlg->m_nproc;
	    sprintf(arg->pszIOHostPort, "%s:%d", pDlg->m_pszIOHost, pDlg->m_nIOPort);
	    strcpy(arg->pszPassPhrase, pDlg->m_Phrase);
	    arg->i = iproc;
	    arg->bLogon = pDlg->m_bLogon;
	    if (pDlg->m_bLogon)
	    {
		strcpy(arg->pszAccount, pDlg->m_Account);
		strcpy(arg->pszPassword, pDlg->m_Password);
	    }
	    if (strlen(pDlg->m_pHosts->exe) > 0)
	    {
		strncpy(arg->pszCmdLine, pDlg->m_pHosts->exe, MAX_CMD_LENGTH);
		arg->pszCmdLine[MAX_CMD_LENGTH-1] = '\0';
	    }
	    else
	    {
		strncpy(arg->pszCmdLine, pDlg->m_app, MAX_CMD_LENGTH);
		arg->pszCmdLine[MAX_CMD_LENGTH-1] = '\0';
	    }
	    strcpy(arg->pszDir, pszDir);
	    if (strlen(pszEnv) >= MAX_CMD_LENGTH)
	    {
		// environment variables truncated
	    }
	    strncpy(arg->pszEnv, pszEnv, MAX_CMD_LENGTH);
	    arg->pszEnv[MAX_CMD_LENGTH-1] = '\0';
	    strncpy(arg->pszHost, pDlg->m_pHosts->host, MAX_HOST_LENGTH);
	    arg->pszHost[MAX_HOST_LENGTH-1] = '\0';
	    strcpy(arg->pszJobID, pszJobID);
	    
	    if (pDlg->m_bNoMPI)
	    {
		if (pDlg->m_bUseCommonEnvironment)
		{
		    if (_snprintf(arg->pszEnv, MAX_CMD_LENGTH, "%s", pDlg->m_CommonEnvironment) < 0)
		    {
			// environment variables truncated
			arg->pszEnv[MAX_CMD_LENGTH-1] = '\0';
		    }
		}
		else
		    arg->pszEnv[0] = '\0';
	    }
	    else
	    {
		if (iproc == 0)
		    sprintf(pBuffer, "MPICH_ROOTPORT=-1|MPICH_IPROC=%d|MPICH_SHM_LOW=%d|MPICH_SHM_HIGH=%d", iproc, nShmLow, nShmHigh);
		else
		    sprintf(pBuffer, "MPICH_ROOTPORT=%d|MPICH_IPROC=%d|MPICH_SHM_LOW=%d|MPICH_SHM_HIGH=%d", pDlg->m_nRootPort, iproc, nShmLow, nShmHigh);
		if (strlen(arg->pszEnv) > 0)
		    strncat(arg->pszEnv, "|", MAX_CMD_LENGTH - 1 - strlen(arg->pszEnv));
		if (strlen(pBuffer) + strlen(arg->pszEnv) >= MAX_CMD_LENGTH)
		{
		    // environment variables truncated
		}
		strncat(arg->pszEnv, pBuffer, MAX_CMD_LENGTH - 1 - strlen(arg->pszEnv));
	    }
	    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
	    {
		pDlg->m_pProcessThread[iproc] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MPIRunLaunchProcess, arg, 0, &dwThreadID);
		if (pDlg->m_pProcessThread[iproc] != NULL)
		    break;
		Sleep(CREATE_THREAD_SLEEP_TIME);
	    }
	    if (pDlg->m_pProcessThread[iproc] == NULL)
	    {
		MessageBox(NULL, "Unable to create LaunchProcess thread", "Error", MB_OK);
		// free stuff
		delete arg;
		delete pDlg->m_pProcessThread;
		pDlg->m_nNumProcessThreads = 0;
		pDlg->m_pProcessThread = NULL;
		SetEvent(pDlg->m_hAbortEvent);
		CloseHandle(pDlg->m_hJobThread);
		pDlg->m_hJobThread = NULL;
		pDlg->Abort();
		delete pDlg->m_pProcessSocket;
		delete pDlg->m_pProcessLaunchId;
		delete pDlg->m_pLaunchIdToRank;
		delete pDlg->m_pForwardHost;
		pDlg->m_pProcessSocket = NULL;
		pDlg->m_nNumProcessSockets = 0;
		pDlg->m_pProcessLaunchId = NULL;
		pDlg->m_pLaunchIdToRank = NULL;
		pDlg->m_pForwardHost = NULL;
		pDlg->DisableRunning();
		return;
	    }
	    pDlg->m_nNumProcessThreads++;
	    if (iproc == 0 && !pDlg->m_bNoMPI)
	    {
		// Wait for the root port to be valid
		while (pDlg->m_nRootPort == 0 && (WaitForSingleObject(pDlg->m_hAbortEvent, 0) != WAIT_OBJECT_0))
		    Sleep(200);
		if (pDlg->m_nRootPort == 0)
		{
		    // free stuff
		    CloseHandle(pDlg->m_pProcessThread[0]);
		    delete pDlg->m_pProcessThread;
		    pDlg->m_nNumProcessThreads = 0;
		    pDlg->m_pProcessThread = NULL;
		    CloseHandle(pDlg->m_hJobThread);
		    pDlg->m_hJobThread = NULL;
		    delete pDlg->m_pProcessSocket;
		    delete pDlg->m_pProcessLaunchId;
		    delete pDlg->m_pLaunchIdToRank;
		    delete pDlg->m_pForwardHost;
		    pDlg->m_pProcessSocket = NULL;
		    pDlg->m_nNumProcessSockets = 0;
		    pDlg->m_pProcessLaunchId = NULL;
		    pDlg->m_pLaunchIdToRank = NULL;
		    pDlg->m_pForwardHost = NULL;
		    pDlg->DisableRunning();
		    return;
		}
	    }
	    iproc++;
	}
	
	HostNode *n = pDlg->m_pHosts;
	pDlg->m_pHosts = pDlg->m_pHosts->next;
	delete n;
    }

    // Wait for all the process starting threads to complete
    WaitForLotsOfObjects(pDlg->m_nproc, pDlg->m_pProcessThread);
    pDlg->m_nNumProcessThreads = 0;
    for (i = 0; i<pDlg->m_nproc; i++)
	CloseHandle(pDlg->m_pProcessThread[i]);
    delete pDlg->m_pProcessThread;
    pDlg->m_pProcessThread = NULL;

    if (WaitForSingleObject(pDlg->m_hAbortEvent, 0) == WAIT_OBJECT_0)
    {
	char pszStr[100];
	for (i=0; i<pDlg->m_nproc; i++)
	{
	    if (pDlg->m_pProcessSocket[i]!= INVALID_SOCKET)
	    {
		sprintf(pszStr, "kill %d", pDlg->m_pProcessLaunchId[i]);
		WriteString(pDlg->m_pProcessSocket[i], pszStr);
		//UnmapDrives(pDlg->m_pProcessSocket[i], pDlg->m_pDriveMapList);
		sprintf(pszStr, "freeprocess %d", pDlg->m_pProcessLaunchId[i]);
		WriteString(pDlg->m_pProcessSocket[i], pszStr);
		ReadString(pDlg->m_pProcessSocket[i], pszStr);
		WriteString(pDlg->m_pProcessSocket[i], "done");
		easy_closesocket(pDlg->m_pProcessSocket[i]);
	    }
	}
	delete pDlg->m_pProcessThread;
	pDlg->m_nNumProcessThreads = 0;
	pDlg->m_pProcessThread = NULL;
	CloseHandle(pDlg->m_hJobThread);
	pDlg->m_hJobThread = NULL;
	delete pDlg->m_pProcessSocket;
	delete pDlg->m_pProcessLaunchId;
	delete pDlg->m_pLaunchIdToRank;
	delete pDlg->m_pForwardHost;
	pDlg->m_pProcessSocket = NULL;
	pDlg->m_nNumProcessSockets = 0;
	pDlg->m_pProcessLaunchId = NULL;
	pDlg->m_pLaunchIdToRank = NULL;
	pDlg->m_pForwardHost = NULL;
	pDlg->DisableRunning();
	if (g_bUseJobHost)
	    UpdateJobState("ABORTED");
	return;
    }

    if (g_bUseJobHost)
	UpdateJobState("RUNNING");

    pDlg->WaitForExitCommands();

    delete pDlg->m_pForwardHost;
    pDlg->m_pForwardHost = NULL;

    // Signal the IO redirection thread to stop
    char ch = 0;
    easy_send(pDlg->m_sockStopIOSignalSocket, &ch, 1);

    // Signal all the threads to stop
    SetEvent(pDlg->m_hAbortEvent);

    // Wait for the redirection thread to complete.  Kill it if it takes too long.
    if (WaitForSingleObject(pDlg->m_hRedirectIOListenThread, 10000) != WAIT_OBJECT_0)
    {
	//printf("Terminating the IO redirection control thread\n");
	TerminateThread(pDlg->m_hRedirectIOListenThread, 0);
    }
    CloseHandle(pDlg->m_hRedirectIOListenThread);
    pDlg->m_hRedirectIOListenThread = NULL;
    easy_closesocket(pDlg->m_sockStopIOSignalSocket);

    if (g_bUseJobHost)
	UpdateJobState("FINISHED");

    // Should I free the handle to this thread here?
    CloseHandle(pDlg->m_hJobThread);
    pDlg->m_hJobThread = NULL;

    delete pDlg->m_pProcessSocket;
    delete pDlg->m_pProcessLaunchId;
    delete pDlg->m_pLaunchIdToRank;
    pDlg->m_pProcessSocket = NULL;
    pDlg->m_nNumProcessSockets = 0;
    pDlg->m_pProcessLaunchId = NULL;
    pDlg->m_pLaunchIdToRank = NULL;

    pDlg->DisableRunning();
    }catch(...)
    {
	MessageBox(NULL, "Unhandled exception caught in RunJob thread", "Error", MB_OK);
    }
}

void CachePassword(const char *pszAccount, const char *pszPassword)
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
    if (::RegSetValueEx(
	hRegKey, _T("Account"), 0, REG_SZ, 
	(BYTE*)pszAccount, 
	sizeof(TCHAR)*(_tcslen(pszAccount)+1)
	)!=ERROR_SUCCESS)
    {
	nError = GetLastError();
	//PrintError(nError, "CachePassword:RegSetValueEx(...) failed, error: %d\n", nError);
	::RegCloseKey(hRegKey);
	return;
    }

    // encode the password
    szEncodedPassword = EncodePassword((char*)pszPassword);

    // Store the encoded password
    if (::RegSetValueEx(
	hRegKey, _T("Password"), 0, REG_SZ, 
	(BYTE*)szEncodedPassword, 
	sizeof(TCHAR)*(_tcslen(szEncodedPassword)+1)
	)!=ERROR_SUCCESS)
    {
	nError = GetLastError();
	//PrintError(nError, "CachePassword:RegSetValueEx(...) failed, error: %d\n", nError);
	::RegCloseKey(hRegKey);
	free(szEncodedPassword);
	return;
    }

    free(szEncodedPassword);
    ::RegCloseKey(hRegKey);
}

bool ReadCachedPassword(char *pszAccount, char *pszPassword)
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
	if (RegQueryValueEx(
	    hRegKey, 
	    _T("Account"), NULL, 
	    NULL, 
	    (BYTE*)szAccount, 
	    &dwLength)!=ERROR_SUCCESS)
	{
	    nError = GetLastError();
	    //PrintError(nError, "ReadPasswordFromRegistry:RegQueryValueEx(...) failed, error: %d\n", nError);
	    ::RegCloseKey(hRegKey);
	    return false;
	}
	if (_tcslen(szAccount) < 1)
	    return false;

	*szPassword = '\0';
	dwLength = 300;
	if (RegQueryValueEx(
	    hRegKey, 
	    _T("Password"), NULL, 
	    NULL, 
	    (BYTE*)szPassword, 
	    &dwLength)!=ERROR_SUCCESS)
	{
	    nError = GetLastError();
	    //PrintError(nError, "ReadPasswordFromRegistry:RegQueryValueEx(...) failed, error: %d\n", nError);
	    ::RegCloseKey(hRegKey);
	    return false;
	}

	::RegCloseKey(hRegKey);

	strcpy(pszAccount, szAccount);
	DecodePassword(szPassword);
	strcpy(pszPassword, szPassword);
	return true;
    }

    return false;
}

void CGuiMPIRunView::OnRunBtn()
{
    DWORD dwThreadID;
    int iter;

    UpdateData();

    EnableRunning();

    // Reset the variables generated by a previous run, if there has been one
    m_nRootPort = 0;
    m_bNormalExit = true;
    ResetEvent(m_hAbortEvent);
    ResetEvent(m_hBreakReadyEvent);
    if (!m_bNoClear)
    {
	m_output.SetSel(0, -1);
	m_output.Clear();
    }
    m_bLogon = false;
    m_bFirstBreak = true;
    /*
    while (m_pDriveMapList)
    {
	MapDriveNode *pNode = m_pDriveMapList;
	m_pDriveMapList = m_pDriveMapList->pNext;
	delete pNode;
    }
    */

    if (m_bUseConfigFile)
    {
	if (ParseConfigFile() != PARSE_SUCCESS)
	{
	    MessageBox(m_ConfigFileName, "Error: unable to parse configuration file");
	    DisableRunning();
	    return;
	}
    }
    else
    {
	// Check dialog parameters
	if (m_app.GetLength() < 1)
	{
	    MessageBox("Please specify the application to run", "No executable specified");
	    DisableRunning();
	    return;
	}
	if (!m_bAnyHosts)
	{
	    if (m_host_list.GetSelCount() < 1)
	    {
		MessageBox("Please highlight the hosts you want to launch processes on or choose any hosts.", "No hosts specified");
		DisableRunning();
		return;
	    }
	}
	//CmdLineToUnc(m_app);
    }

    // Get an account/password if necessary
    if (m_bForceLogon)
    {
	CUserPwdDialog dlg;
	dlg.m_remember = FALSE;
	if (dlg.DoModal() != IDOK)
	{
	    MessageBox("No user account supplied", "Aborting application");
	    DisableRunning();
	    return;
	}
	m_Account = dlg.m_account;
	m_Password = dlg.m_password;
	if (dlg.m_remember)
	{
	    if (!SavePasswordToRegistry(m_Account.GetBuffer(0), m_Password.GetBuffer(0), true))
	    {
		DeleteCurrentPasswordRegistryEntry();
	    }
	}
	m_bLogon = true;
    }
    else
    {
	if (m_Account.GetLength() < 1)
	{
	    char pszTemp[10] = "no";
	    ReadMPDRegistry("SingleUser", pszTemp, NULL);
	    if (stricmp(pszTemp, "yes"))
	    {
		if (!ReadCachedPassword(m_Account.GetBuffer(100), m_Password.GetBuffer(100)))
		{
		    HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );
		    if (!ReadPasswordFromRegistry(m_Account.GetBuffer(100), m_Password.GetBuffer(100)))
		    {
			CUserPwdDialog dlg;
			dlg.m_remember = FALSE;
			if (dlg.DoModal() != IDOK)
			{
			    MessageBox("No user account supplied", "Aborting application");
			    DisableRunning();
			    SetCursor(hOldCursor);
			    return;
			}
			m_Account = dlg.m_account;
			m_Password = dlg.m_password;
			if (dlg.m_remember)
			{
			    if (!SavePasswordToRegistry(m_Account.GetBuffer(0), m_Password.GetBuffer(0), true))
			    {
				DeleteCurrentPasswordRegistryEntry();
			    }
			}
		    }
		    CachePassword(m_Account, m_Password);
		    SetCursor(hOldCursor);
		}
		m_bLogon = true;
	    }
	}
	else
	{
	    m_bLogon = true;
	}
    }

    SaveAppToMRU();

    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
    {
	m_hJobThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RunJob, this, 0, &dwThreadID);
	if (m_hJobThread != NULL)
	    break;
	Sleep(CREATE_THREAD_SLEEP_TIME);
    }
    if (m_hJobThread == NULL)
    {
	MessageBox("CreateThread(RunJob) failed");
	DisableRunning();
    }
}
