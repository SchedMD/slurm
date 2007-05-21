// guiMPIRunView.cpp : implementation of the CGuiMPIRunView class
//

#include "stdafx.h"
#include "guiMPIRun.h"

#include "guiMPIRunDoc.h"
#include "guiMPIRunView.h"
#include "MPIJobDefs.h"
#include "mpd.h"
#include "RedirectIO.h"
#include "global.h"
#include "AdvancedOptionsDlg.h"
#include "mpdutil.h"
#include "launchprocess.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CGuiMPIRunView

IMPLEMENT_DYNCREATE(CGuiMPIRunView, CFormView)

BEGIN_MESSAGE_MAP(CGuiMPIRunView, CFormView)
//{{AFX_MSG_MAP(CGuiMPIRunView)
ON_WM_SIZE()
ON_NOTIFY(UDN_DELTAPOS, IDC_NPROC_SPIN, OnDeltaposNprocSpin)
ON_BN_CLICKED(IDC_APP_BROWSE_BTN, OnAppBrowseBtn)
ON_BN_CLICKED(IDC_RUN_BTN, OnRunBtn)
ON_BN_CLICKED(IDC_HOSTS_RADIO, OnHostsRadio)
ON_BN_CLICKED(IDC_ANY_HOSTS_RADIO, OnAnyHostsRadio)
ON_COMMAND(ID_EDIT_COPY, OnEditCopy)
ON_WM_CLOSE()
	ON_BN_CLICKED(IDC_ADD_HOST_BTN, OnAddHostBtn)
	ON_BN_CLICKED(IDC_BREAK_BTN, OnBreakBtn)
	ON_BN_CLICKED(IDC_ADVANCED_BTN, OnAdvancedBtn)
	ON_NOTIFY(EN_MSGFILTER, IDC_OUTPUT, OnMsgfilterOutput)
	ON_BN_CLICKED(IDC_RESET_HOSTS_BTN, OnResetHostsBtn)
	ON_WM_VKEYTOITEM()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CGuiMPIRunView construction/destruction

CGuiMPIRunView::CGuiMPIRunView()
: CFormView(CGuiMPIRunView::IDD)
{
    //{{AFX_DATA_INIT(CGuiMPIRunView)
    m_nproc = 1;
    m_app = _T("");
    m_host = _T("");
	//}}AFX_DATA_INIT
    m_bAnyHosts = true;
    m_pHosts = NULL;
    m_hJobThread = NULL;
    m_bForceLogon = false;
    m_Account = "";
    m_Password = "";
    m_bFirstBreak = true;
    m_bUseWorkingDirectory = false;
    m_WorkingDirectory = "";
    m_bUseCommonEnvironment = false;
    m_CommonEnvironment = "";
    m_bUseSlaveProcess = false;
    m_SlaveProcess = "";
    m_bNoClear = false;
    m_nMaxMRU = 10;
    m_hJobFinished = CreateEvent(NULL, TRUE, TRUE, NULL);
    m_hRedirectStdinEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    m_hRedirectStdinMutex = CreateMutex(NULL, FALSE, NULL);
    m_pRedirectStdinList = NULL;
    m_curoutput = "";
    m_fout = NULL;
    m_bUseConfigFile = false;
    m_ConfigFileName = "";
    m_redirect = false;
    m_output_filename = "";
    m_pProcessThread = NULL;
    m_nNumProcessThreads = 0;
    m_pProcessSocket = NULL;
    m_nNumProcessSockets = 0;
    m_sockBreak = INVALID_SOCKET;
    m_hBreakReadyEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    m_pForwardHost = NULL;
    m_pLaunchIdToRank = NULL;
    m_hRedirectRicheditThread = NULL;
    //m_pDriveMapList = NULL;
    m_Mappings = "";
    m_bUseMapping = false;
    m_bCatch = false;
}

CGuiMPIRunView::~CGuiMPIRunView()
{
}

void CGuiMPIRunView::DoDataExchange(CDataExchange* pDX)
{
    CFormView::DoDataExchange(pDX);
    //{{AFX_DATA_MAP(CGuiMPIRunView)
	DDX_Control(pDX, IDC_RESET_HOSTS_BTN, m_reset_btn);
	DDX_Control(pDX, IDC_ADVANCED_BTN, m_advanced_btn);
	DDX_Control(pDX, IDC_NPROC, m_nproc_edit);
	DDX_Control(pDX, IDC_RUN_BTN, m_run_btn);
	DDX_Control(pDX, IDC_NPROC_SPIN, m_nproc_spin);
	DDX_Control(pDX, IDC_BREAK_BTN, m_break_btn);
	DDX_Control(pDX, IDC_ADD_HOST_BTN, m_add_btn);
	DDX_Control(pDX, IDC_ANY_HOSTS_RADIO, m_any_hosts_btn);
    DDX_Control(pDX, IDC_HOST_EDIT, m_host_edit);
    DDX_Control(pDX, IDC_OUTPUT, m_output);
    DDX_Control(pDX, IDC_HOST_LIST, m_host_list);
    DDX_Control(pDX, IDC_APP_COMBO, m_app_combo);
    DDX_Control(pDX, IDC_APP_BROWSE_BTN, m_app_browse_btn);
    DDX_Text(pDX, IDC_NPROC, m_nproc);
    DDV_MinMaxInt(pDX, m_nproc, 1, 1024);
    DDX_CBString(pDX, IDC_APP_COMBO, m_app);
    DDX_Text(pDX, IDC_HOST_EDIT, m_host);
	//}}AFX_DATA_MAP
}

BOOL CGuiMPIRunView::PreCreateWindow(CREATESTRUCT& cs)
{
    return CFormView::PreCreateWindow(cs);
}

bool ReadMPDRegistry(char *name, char *value, DWORD *length = NULL)
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

void CGuiMPIRunView::OnInitialUpdate()
{
    CFormView::OnInitialUpdate();
    GetParentFrame()->RecalcLayout();
    ResizeParentToFit();

    RECT r;
    GetClientRect(&r);
    m_nMinWidth = r.right;
    m_nMinHeight = r.bottom;

    rOutput.SetInitialPosition(m_output.m_hWnd, RSR_STRETCH);
    rHostList.SetInitialPosition(m_host_list.m_hWnd, RSR_ANCHOR_RIGHT_STRETCH);
    rAppCombo.SetInitialPosition(m_app_combo.m_hWnd, RSR_STRETCH_RIGHT);
    rAppBrowse.SetInitialPosition(m_app_browse_btn.m_hWnd, RSR_ANCHOR_RIGHT);
    
    rAnyHost.SetInitialPosition(::GetDlgItem(m_hWnd, IDC_ANY_HOSTS_RADIO), RSR_ANCHOR_RIGHT);
    rHost.SetInitialPosition(::GetDlgItem(m_hWnd, IDC_HOSTS_RADIO), RSR_ANCHOR_RIGHT);
    rHostEdit.SetInitialPosition(m_host_edit.m_hWnd, RSR_ANCHOR_RIGHT);
    rAdd.SetInitialPosition(m_add_btn.m_hWnd, RSR_ANCHOR_RIGHT);
    rAdvanced.SetInitialPosition(m_advanced_btn.m_hWnd, RSR_ANCHOR_RIGHT);
    rReset.SetInitialPosition(m_reset_btn.m_hWnd, RSR_ANCHOR_RIGHT);
    
    easy_socket_init();
    
    // Get hosts from registry
    char pszHosts[4096];
    DWORD nLength = 4096;
    if (ReadMPDRegistry("hosts", pszHosts, &nLength))
    {
	/*
	char *token = NULL;
	token = strtok(pszHosts, "|");
	if (token != NULL)
	{
	    m_host_list.InsertString(-1, token);
	    while ((token = strtok(NULL, "|")) != NULL)
	    {
		m_host_list.InsertString(-1, token);
	    }
	}
	*/
	QVS_Container *phosts;
	phosts = new QVS_Container(pszHosts);
	if (phosts->first(pszHosts, 4096))
	{
	    m_host_list.InsertString(-1, pszHosts);
	    while (phosts->next(pszHosts, 4096))
	    {
		m_host_list.InsertString(-1, pszHosts);
	    }
	}
	delete phosts;
    }
    else
    {
	char temp[100];
	gethostname(temp, 100);
	m_host_list.InsertString(-1, temp);
    }

    char pszPhrase[100];
    nLength = 100;
    if (ReadMPDRegistry("phrase", pszPhrase, &nLength))
    {
	m_Phrase = pszPhrase;
    }
    else
    {
	m_Phrase = MPD_DEFAULT_PASSPHRASE;
    }

    m_any_hosts_btn.SetCheck(1);
    // Initially "any hosts" is selected, so the host list is disabled
    m_host_list.EnableWindow(FALSE);
    m_host_edit.EnableWindow(FALSE);
    
    m_hAbortEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    m_bNormalExit = true;
    m_bNoMPI = false;
    m_bNoColor = false;
    m_hConsoleOutputMutex = CreateMutex(NULL, FALSE, NULL);

    if (ReadMPDDefault("usejobhost"))
    {
	DWORD length = MAX_HOST_LENGTH;
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

    CHARFORMAT cf;

    cf.dwMask = CFM_FACE;
    cf.dwEffects = 0;
    //strcpy(cf.szFaceName, "Courier");
    strcpy(cf.szFaceName, "Lucida Console");
    m_output.SetDefaultCharFormat(cf);

    ReadMRU();

    ::SendMessage(m_output.m_hWnd, EM_SETEVENTMASK, 0, ENM_KEYEVENTS);
}

/////////////////////////////////////////////////////////////////////////////
// CGuiMPIRunView diagnostics

#ifdef _DEBUG
void CGuiMPIRunView::AssertValid() const
{
    CFormView::AssertValid();
}

void CGuiMPIRunView::Dump(CDumpContext& dc) const
{
    CFormView::Dump(dc);
}

CGuiMPIRunDoc* CGuiMPIRunView::GetDocument() // non-debug version is inline
{
    ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CGuiMPIRunDoc)));
    return (CGuiMPIRunDoc*)m_pDocument;
}
#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CGuiMPIRunView message handlers

void CGuiMPIRunView::OnSize(UINT nType, int cx, int cy) 
{
    CFormView::OnSize(nType, cx, cy);
    
    if (m_nMinWidth < cx || m_nMinHeight < cy)
    {
	rOutput.Resize(cx, cy);
	rHostList.Resize(cx, cy);
	rAppCombo.Resize(cx, cy);
	rAppBrowse.Resize(cx, cy);
	rAnyHost.Resize(cx, cy);
	rHost.Resize(cx, cy);
	rHostEdit.Resize(cx, cy);
	rAdd.Resize(cx, cy);
	rAdvanced.Resize(cx, cy);
	rReset.Resize(cx, cy);
    }
}

void CGuiMPIRunView::OnDeltaposNprocSpin(NMHDR* pNMHDR, LRESULT* pResult) 
{
    NM_UPDOWN* pNMUpDown = (NM_UPDOWN*)pNMHDR;
    
    UpdateData();
    
    if (pNMUpDown->iDelta < 0)
	m_nproc++;
    else
	m_nproc--;
    
    if (m_nproc < 1)
	m_nproc = 1;
    if (m_nproc > 1024)
	m_nproc = 1024;
    
    UpdateData(FALSE);
    
    *pResult = 0;
}

void CGuiMPIRunView::OnAppBrowseBtn() 
{
    UpdateData();
    CFileDialog f(
	TRUE, "*.exe", m_app, 
	OFN_HIDEREADONLY | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST,
	"Executables (*.exe)|*.exe|All files (*.*)|*.*||"
	);
    if (f.DoModal() == IDOK)
    {
	POSITION p;
	p = f.GetStartPosition();
	m_app = f.GetNextPathName(p);
	UpdateData(FALSE);
    }
}

void CGuiMPIRunView::OnHostsRadio() 
{
    m_host_list.EnableWindow();
    m_host_edit.EnableWindow();
    m_bAnyHosts = false;
}

void CGuiMPIRunView::OnAnyHostsRadio() 
{
    m_host_list.EnableWindow(FALSE);
    m_host_edit.EnableWindow(FALSE);
    m_bAnyHosts = true;
}

void CGuiMPIRunView::OnEditCopy() 
{
    CString str = m_output.GetSelText();
    if (str.GetLength() == 0)
    {
	m_output.SetSel(0, -1);
    }
    m_output.Copy();
}

void CGuiMPIRunView::OnClose() 
{
    // Stop any running jobs
    Abort();
    WaitForSingleObject(m_hJobFinished, 5000);

    // Signal the IO redirection thread to stop
    if (m_sockStopIOSignalSocket != INVALID_SOCKET)
	easy_send(m_sockStopIOSignalSocket, "x", 1);

    if (m_hRedirectIOListenThread != NULL)
    {
	// Wait for the redirection thread to complete.  Kill it if it takes too long.
	if (WaitForSingleObject(m_hRedirectIOListenThread, 1000) != WAIT_OBJECT_0)
	{
	    //printf("Terminating the IO redirection control thread\n");
	    TerminateThread(m_hRedirectIOListenThread, 0);
	}
	CloseHandle(m_hRedirectIOListenThread);
	m_hRedirectIOListenThread = NULL;
    }

    if (m_hRedirectRicheditThread != NULL)
    {
	if (WaitForSingleObject(m_hRedirectRicheditThread, 1000) != WAIT_OBJECT_0)
	{
	    TerminateThread(m_hRedirectRicheditThread, 0);
	}
	CloseHandle(m_hRedirectRicheditThread);
	m_hRedirectRicheditThread = NULL;
    }

    CloseHandle(m_hConsoleOutputMutex);
    CloseHandle(m_hAbortEvent);
    CloseHandle(m_hJobFinished);
    m_hJobFinished = NULL;
    m_hAbortEvent = NULL;
    m_hConsoleOutputMutex = NULL;

    CloseHandle(m_hRedirectStdinEvent);
    CloseHandle(m_hRedirectStdinMutex);
    while (m_pRedirectStdinList)
    {
	RedirectStdinStruct *p = m_pRedirectStdinList;
	m_pRedirectStdinList = m_pRedirectStdinList->pNext;
	delete p;
    }

    CloseHandle(m_hBreakReadyEvent);

    easy_socket_finalize();
    CFormView::OnClose();
}

void CGuiMPIRunView::OnAddHostBtn() 
{
    // Add hostname to host list
    UpdateData();
    
    if (m_host.GetLength() != 0)
    {
	
	CString str;
	int n = m_host_list.GetCount();
	if (n != LB_ERR)
	{
	    bool bFound = false;
	    for (int i=0; i<n; i++)
	    {
		m_host_list.GetText(i, str);
		if (str.CompareNoCase(m_host) == 0)
		{
		    bFound = true;
		    break;
		}
	    }
	    if (!bFound)
	    {
		m_host_list.InsertString(-1, m_host);
	    }
	}
    }
}

void CGuiMPIRunView::ReadMRU()
{
    HKEY tkey;
    DWORD result, len;
    DWORD nCount;
    char name[20];
    char value[1024];
    DWORD i;

    m_app_combo.ResetContent();

    // Open the root key
    if (RegCreateKeyEx(HKEY_CURRENT_USER, MPICHKEY,
	0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ, NULL, &tkey, &result) != ERROR_SUCCESS)
    {
	return;
    }

    // Read the count of items
    len = sizeof(DWORD);
    result = RegQueryValueEx(tkey, "mru", 0, NULL, (unsigned char *)&nCount, &len);
    if (result != ERROR_SUCCESS)
    {
	RegCloseKey(tkey);
	return;
    }

    // Read each item into the combo box
    for (i=1; i<=nCount; i++)
    {
	sprintf(name, "mru%d", i);
	len = 1024;
	if (RegQueryValueEx(tkey, name, 0, NULL, (unsigned char *)value, &len) != ERROR_SUCCESS)
	    break;
	m_app_combo.AddString(value);
    }

    RegCloseKey(tkey);
}

void CGuiMPIRunView::SaveAppToMRU()
{
    HKEY tkey;
    DWORD result, len;
    DWORD nCount;
    char value[1024];
    char name[20];
    DWORD i;

    if (m_app_combo.FindString(-1, m_app) != CB_ERR)
	return;
    
    m_app_combo.AddString(m_app);

    // Open the root key
    if (RegCreateKeyEx(HKEY_CURRENT_USER, MPICHKEY,
	0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &tkey, &result) != ERROR_SUCCESS)
    {
	return;
    }

    // Read the number of mru entries
    len = sizeof(DWORD);
    result = RegQueryValueEx(tkey, "mru", 0, NULL, (unsigned char *)&nCount, &len);
    if (result != ERROR_SUCCESS)
    {
	// If there are none
	nCount = 1;
	RegSetValueEx(tkey, "mru", 0, REG_DWORD, (unsigned char *)&nCount, sizeof(DWORD));
	RegSetValueEx(tkey, "mru1", 0, REG_SZ, (unsigned char *)m_app.GetBuffer(0), m_app.GetLength()+1);
	m_app.ReleaseBuffer();
	RegCloseKey(tkey);
	return;
    }

    if (nCount < (DWORD)m_nMaxMRU)
    {
	nCount++;
	sprintf(name, "mru%d", nCount);
	RegSetValueEx(tkey, name, 0, REG_SZ, (unsigned char *)m_app.GetBuffer(0), m_app.GetLength()+1);
	RegSetValueEx(tkey, "mru", 0, REG_DWORD, (unsigned char *)&nCount, sizeof(DWORD));
	m_app.ReleaseBuffer();
	RegCloseKey(tkey);
	return;
    }

    for (i=1; i<nCount; i++)
    {
	sprintf(name, "mru%d", i+1);
	len = 1024;
	RegQueryValueEx(tkey, name, 0, NULL, (unsigned char *)value, &len);
	sprintf(name, "mru%d", i);
	RegSetValueEx(tkey, name, 0, REG_SZ, (unsigned char *)value, strlen(value)+1);
    }
    sprintf(name, "mru%d", nCount);
    RegSetValueEx(tkey, name, 0, REG_SZ, (unsigned char *)m_app.GetBuffer(0), m_app.GetLength()+1);
    m_app.ReleaseBuffer();

    RegCloseKey(tkey);
}

void CGuiMPIRunView::ClearMRU()
{
    HKEY tkey;
    DWORD result, len;
    DWORD nCount;
    char name[20];
    DWORD i;

    m_app_combo.ResetContent();

    // Open the root key
    if (RegCreateKeyEx(HKEY_CURRENT_USER, MPICHKEY,
	0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &tkey, &result) != ERROR_SUCCESS)
    {
	return;
    }

    // Read the count of items
    len = sizeof(DWORD);
    result = RegQueryValueEx(tkey, "mru", 0, NULL, (unsigned char *)&nCount, &len);
    if (result != ERROR_SUCCESS)
    {
	nCount = 0;
	RegSetValueEx(tkey, "mru", 0, REG_DWORD, (unsigned char *)&nCount, sizeof(DWORD));
	RegCloseKey(tkey);
	return;
    }

    // Delete each item
    for (i=1; i<=nCount; i++)
    {
	sprintf(name, "mru%d", i);
	RegDeleteValue(tkey, name);
    }

    RegCloseKey(tkey);
}

void CGuiMPIRunView::OnAdvancedBtn() 
{
    CAdvancedOptionsDlg dlg;

    UpdateData();

    dlg.m_bDir = m_bUseWorkingDirectory;
    if (m_bUseWorkingDirectory)
	dlg.m_directory = m_WorkingDirectory;
    dlg.m_bEnv = m_bUseCommonEnvironment;
    dlg.m_environment = m_CommonEnvironment;
    dlg.m_bSlave = m_bUseSlaveProcess;
    dlg.m_slave = m_SlaveProcess;
    dlg.m_bNoClear = m_bNoClear;
    dlg.m_bNoMPI = m_bNoMPI;
    dlg.m_bPassword = m_bForceLogon;
    dlg.m_bConfig = m_bUseConfigFile;
    dlg.m_config_filename = m_ConfigFileName;
    dlg.m_bRedirect = m_redirect;
    dlg.m_output_filename = m_output_filename;
    dlg.m_bNoColor = m_bNoColor;
    dlg.m_bMap = m_bUseMapping;
    dlg.m_map = m_Mappings;
    dlg.m_bCatch = m_bCatch;
    dlg.m_bUseJobHost = g_bUseJobHost;
    if (g_bUseJobHost)
    {
	dlg.m_jobhost = g_pszJobHost;
    }

    if (dlg.DoModal() == IDOK)
    {
	m_bNoColor = dlg.m_bNoColor == TRUE;
	m_bNoClear = dlg.m_bNoClear == TRUE;
	m_bNoMPI = dlg.m_bNoMPI == TRUE;
	m_bForceLogon = dlg.m_bPassword == TRUE;
	m_bCatch = dlg.m_bCatch == TRUE;
	if (dlg.m_bUseJobHost == TRUE && (dlg.m_jobhost.GetLength() > 0))
	{
	    g_bUseJobHost = true;
	    strcpy(g_pszJobHost, dlg.m_jobhost);
	}
	else
	    g_bUseJobHost = false;
	if (dlg.m_bRedirect)
	{
	    m_output_filename = dlg.m_output_filename;
	    m_redirect = true;
	}
	else
	    m_redirect = false;
	if (dlg.m_bConfig)
	{
	    m_bUseCommonEnvironment = false;
	    m_bUseSlaveProcess = false;
	    m_bUseMapping = false;
	    m_bUseWorkingDirectory = false;
	    m_bUseConfigFile = true;
	    m_ConfigFileName = dlg.m_config_filename;
	    m_app_combo.EnableWindow(FALSE);
	    m_app_browse_btn.EnableWindow(FALSE);
	    m_nproc_edit.EnableWindow(FALSE);
	    m_nproc_spin.EnableWindow(FALSE);
	}
	else
	{
	    m_bUseConfigFile = false;
	    m_app_combo.EnableWindow();
	    m_app_browse_btn.EnableWindow();
	    m_nproc_edit.EnableWindow();
	    m_nproc_spin.EnableWindow();
	    if (dlg.m_bEnv)
	    {
		m_CommonEnvironment = dlg.m_environment;
		m_bUseCommonEnvironment = true;
	    }
	    else
		m_bUseCommonEnvironment = false;
	    if (dlg.m_bSlave)
	    {
		m_SlaveProcess = dlg.m_slave;
		m_bUseSlaveProcess = true;
	    }
	    else
		m_bUseSlaveProcess = false;
	    if (dlg.m_bDir)
	    {
		m_WorkingDirectory = dlg.m_directory;
		m_bUseWorkingDirectory = true;
	    }
	    else
		m_bUseWorkingDirectory = false;
	    if (dlg.m_bMap)
	    {
		m_Mappings = dlg.m_map;
		m_bUseMapping = true;
	    }
	    else
		m_bUseMapping = false;
	}
	UpdateData(FALSE);
    }
}

void CGuiMPIRunView::Abort()
{
    SetEvent(m_hAbortEvent);
    if (m_sockBreak != INVALID_SOCKET)
	easy_send(m_sockBreak, "x", 1);
    easy_send(m_sockStopIOSignalSocket, "x", 1);
}

struct ProcessWaitAbortThreadArg
{
    SOCKET sockAbort;
    SOCKET sockStop;
    int n;
    int *pSocket;
};

void ProcessWaitAbort(ProcessWaitAbortThreadArg *pArg)
{
    int n, i;
    fd_set readset;

    FD_ZERO(&readset);
    FD_SET(pArg->sockAbort, &readset);
    FD_SET(pArg->sockStop, &readset);

    n = select(0, &readset, NULL, NULL, NULL);

    if (n == SOCKET_ERROR)
    {
	char str[100];
	sprintf(str, "bselect failed, error %d\n", WSAGetLastError());
	MessageBox(NULL, str, "ProcessWaitAbort", MB_OK);
	for (i=0; i<pArg->n; i++)
	{
	    easy_closesocket(pArg->pSocket[i]);
	}
	easy_closesocket(pArg->sockAbort);
	easy_closesocket(pArg->sockStop);
	return;
    }
    if (n == 0)
    {
	MessageBox(NULL, "bselect returned zero sockets available\n", "ProcessWaitAbort", MB_OK);
	for (i=0; i<pArg->n; i++)
	{
	    easy_closesocket(pArg->pSocket[i]);
	}
	easy_closesocket(pArg->sockAbort);
	easy_closesocket(pArg->sockStop);
	return;
    }
    if (FD_ISSET(pArg->sockAbort, &readset))
    {
	for (i=0; i<pArg->n; i++)
	{
	    easy_send(pArg->pSocket[i], "x", 1);
	}
	if (g_bUseJobHost)
	    UpdateJobState("ABORTED");
    }
    for (i=0; i<pArg->n; i++)
    {
	easy_closesocket(pArg->pSocket[i]);
    }
    easy_closesocket(pArg->sockAbort);
    easy_closesocket(pArg->sockStop);
}

struct ProcessWaitThreadArg
{
    int n;
    SOCKET *pSocket;
    int *pId;
    int *pRank;
    SOCKET sockAbort;
    CGuiMPIRunView *pDlg;
};

void ProcessWait(ProcessWaitThreadArg *pArg)
{
    int i, j, n;
    fd_set totalset, readset;
    char str[256];
    
    FD_ZERO(&totalset);
    
    FD_SET(pArg->sockAbort, &totalset);
    for (i=0; i<pArg->n; i++)
    {
	FD_SET(pArg->pSocket[i], &totalset);
    }
    
    while (pArg->n)
    {
	readset = totalset;
	n = select(0, &readset, NULL, NULL, NULL);
	if (n == SOCKET_ERROR)
	{
	    sprintf(str, "bselect failed, error %d\n", WSAGetLastError());
	    MessageBox(NULL, str, "WaitForExitCommands", MB_OK);
	    for (i=0, j=0; i<pArg->n; i++, j++)
	    {
		while (pArg->pSocket[j] == INVALID_SOCKET)
		    j++;
		easy_closesocket(pArg->pSocket[j]);
	    }
	    return;
	}
	if (n == 0)
	{
	    MessageBox(NULL, "bselect returned zero sockets available", "WaitForExitCommands", MB_OK);
	    for (i=0, j=0; i<pArg->n; i++, j++)
	    {
		while (pArg->pSocket[j] == INVALID_SOCKET)
		    j++;
		easy_closesocket(pArg->pSocket[j]);
	    }
	    return;
	}

	if (FD_ISSET(pArg->sockAbort, &readset))
	{
	    for (i=0; pArg->n > 0; i++)
	    {
		while (pArg->pSocket[i] == INVALID_SOCKET)
		    i++;
		sprintf(str, "kill %d", pArg->pId[i]);
		WriteString(pArg->pSocket[i], str);

		int nRank = pArg->pRank[i];
		if (pArg->pDlg->m_nproc > FORWARD_NPROC_THRESHOLD)
		{
		    if (nRank > 0 && (pArg->pDlg->m_nproc/2) > nRank)
		    {
			//printf("rank %d(%d) stopping forwarder\n", nRank, g_pProcessLaunchId[i]);fflush(stdout);
			sprintf(str, "stopforwarder port=%d abort=yes", pArg->pDlg->m_pForwardHost[nRank].nPort);
			WriteString(pArg->pSocket[i], str);
		    }
		}

		//UnmapDrives(pArg->pSocket[i], pArg->pDlg->m_pDriveMapList);

		sprintf(str, "freeprocess %d", pArg->pId[i]);
		WriteString(pArg->pSocket[i], str);
		ReadString(pArg->pSocket[i], str);
		WriteString(pArg->pSocket[i], "done");
		easy_closesocket(pArg->pSocket[i]);
		pArg->pSocket[i] = INVALID_SOCKET;
		pArg->n--;
	    }
	    return;
	}
	for (i=0; n>0; i++)
	{
	    while (pArg->pSocket[i] == INVALID_SOCKET)
		i++;
	    if (FD_ISSET(pArg->pSocket[i], &readset))
	    {
		if (!ReadString(pArg->pSocket[i], str))
		{
		    sprintf(str, "Unable to read the result of the getexitcodewait command for process %d, error %d", i, WSAGetLastError());
		    MessageBox(NULL, str, "Critical Error", MB_OK);
		    return;
		}
		
		int nRank = pArg->pRank[i];

		if (strnicmp(str, "FAIL", 4) == 0)
		{
		    sprintf(str, "geterror %d", pArg->pId[i]);
		    WriteString(pArg->pSocket[i], str);
		    ReadString(pArg->pSocket[i], str);
		    printf("getexitcode(rank %d) failed: %s\n", nRank, str);fflush(stdout);
		    if (g_bUseJobHost)
		    {
			UpdateJobKeyValue(nRank, "error", str);
		    
			// get the time the process exited
			sprintf(str, "getexittime %d", pArg->pId[i]);
			WriteString(pArg->pSocket[i], str);
			ReadString(pArg->pSocket[i], str);
			UpdateJobKeyValue(nRank, "exittime", str);
		    }
		    
		    if (easy_send(pArg->sockAbort, "x", 1) == SOCKET_ERROR)
		    {
			printf("Hard abort.\n");fflush(stdout);
			//ExitProcess(-1);
		    }
		}
		else
		{
		    if (g_bUseJobHost)
		    {
			strtok(str, ":"); // strip the extra data from the string
			UpdateJobKeyValue(nRank, "exitcode", str);

			// get the time the process exited
			sprintf(str, "getexittime %d", pArg->pId[i]);
			WriteString(pArg->pSocket[i], str);
			ReadString(pArg->pSocket[i], str);
			UpdateJobKeyValue(nRank, "exittime", str);
		    }
		}

		if (pArg->pDlg->m_nproc > FORWARD_NPROC_THRESHOLD)
		{
		    if (nRank > 0 && (pArg->pDlg->m_nproc/2) > nRank)
		    {
			//printf("rank %d(%d) stopping forwarder\n", nRank, g_pProcessLaunchId[i]);fflush(stdout);
			sprintf(str, "stopforwarder port=%d abort=yes", pArg->pDlg->m_pForwardHost[nRank].nPort);
			WriteString(pArg->pSocket[i], str);
		    }
		}

		// UnmapDrives?

		sprintf(str, "freeprocess %d", pArg->pId[i]);
		WriteString(pArg->pSocket[i], str);
		ReadString(pArg->pSocket[i], str);
		
		WriteString(pArg->pSocket[i], "done");
		easy_closesocket(pArg->pSocket[i]);
		FD_CLR(pArg->pSocket[i], &totalset);
		pArg->pSocket[i] = INVALID_SOCKET;
		n--;
		pArg->n--;
	    }
	}
    }
}

void CGuiMPIRunView::WaitForExitCommands()
{
    int iter;
    if (m_nNumProcessSockets < FD_SETSIZE)
    {
	int i, j, n;
	fd_set totalset, readset;
	char str[256];
	SOCKET break_sock;
	
	if (m_sockBreak != INVALID_SOCKET)
	    easy_closesocket(m_sockBreak);
	MakeLoop(&break_sock, &m_sockBreak);
	SetEvent(m_hBreakReadyEvent);

	FD_ZERO(&totalset);
	
	FD_SET(break_sock, &totalset);
	for (i=0; i<m_nNumProcessSockets; i++)
	{
	    FD_SET(m_pProcessSocket[i], &totalset);
	}
	
	while (m_nNumProcessSockets)
	{
	    readset = totalset;
	    n = select(0, &readset, NULL, NULL, NULL);
	    if (n == SOCKET_ERROR)
	    {
		sprintf(str, "WaitForExitCommands: bselect failed, error %d\n", WSAGetLastError());
		MessageBox(str);
		for (i=0; m_nNumProcessSockets > 0; i++)
		{
		    while (m_pProcessSocket[i] == INVALID_SOCKET)
			i++;
		    easy_closesocket(m_pProcessSocket[i]);
		    m_nNumProcessSockets--;
		}
		return;
	    }
	    if (n == 0)
	    {
		MessageBox("WaitForExitCommands: bselect returned zero sockets available\n");
		for (i=0; m_nNumProcessSockets > 0; i++)
		{
		    while (m_pProcessSocket[i] == INVALID_SOCKET)
			i++;
		    easy_closesocket(m_pProcessSocket[i]);
		}
		return;
	    }
	    else
	    {
		if (FD_ISSET(break_sock, &readset))
		{
		    int num_read = easy_receive(break_sock, str, 1);
		    if (num_read == 0 || num_read == SOCKET_ERROR)
		    {
			FD_CLR(break_sock, &totalset);
		    }
		    else
		    {
			for (i=0, j=0; i < m_nNumProcessSockets; i++, j++)
			{
			    while (m_pProcessSocket[j] == INVALID_SOCKET)
				j++;
			    sprintf(str, "kill %d", m_pProcessLaunchId[j]);
			    WriteString(m_pProcessSocket[j], str);
			}
		    }
		    n--;
		}
		for (i=0; n>0; i++)
		{
		    while (m_pProcessSocket[i] == INVALID_SOCKET)
			i++;
		    if (FD_ISSET(m_pProcessSocket[i], &readset))
		    {
			if (!ReadString(m_pProcessSocket[i], str))
			{
			    sprintf(str, "Unable to read the result of the getexitcodewait command for process %d, error %d", i, WSAGetLastError());
			    MessageBox(str, "Critical Error", MB_OK);
			    return;
			}
			
			int nRank = m_pLaunchIdToRank[i];

			if (strnicmp(str, "FAIL", 4) == 0)
			{
			    sprintf(str, "geterror %d", m_pProcessLaunchId[i]);
			    WriteString(m_pProcessSocket[i], str);
			    ReadString(m_pProcessSocket[i], str);
			    printf("getexitcode(rank %d) failed: %s\n", nRank, str);fflush(stdout);
			    if (g_bUseJobHost)
			    {
				UpdateJobKeyValue(nRank, "error", str);
			    
				// get the time the process exited
				sprintf(str, "getexittime %d", m_pProcessLaunchId[i]);
				WriteString(m_pProcessSocket[i], str);
				ReadString(m_pProcessSocket[i], str);
				UpdateJobKeyValue(nRank, "exittime", str);
			    }

			    if (easy_send(m_sockBreak, "x", 1) == SOCKET_ERROR)
			    {
				printf("Hard abort.\n");fflush(stdout);
				//ExitProcess(-1);
			    }
			}
			else
			{
			    if (g_bUseJobHost)
			    {
				//printf("ExitProcess: %s\n", str);fflush(stdout);
				strtok(str, ":"); // strip the extra data from the string
				UpdateJobKeyValue(nRank, "exitcode", str);

				// get the time the process exited
				sprintf(str, "getexittime %d", m_pProcessLaunchId[i]);
				WriteString(m_pProcessSocket[i], str);
				ReadString(m_pProcessSocket[i], str);
				UpdateJobKeyValue(nRank, "exittime", str);
			    }
			}

			if (m_nproc > FORWARD_NPROC_THRESHOLD)
			{
			    if (nRank > 0 && (m_nproc/2) > nRank)
			    {
				sprintf(str, "stopforwarder port=%d abort=no", m_pForwardHost[nRank].nPort);
				WriteString(m_pProcessSocket[i], str);
			    }
			}

			//UnmapDrives(m_pProcessSocket[i], m_pDriveMapList);

			sprintf(str, "freeprocess %d", m_pProcessLaunchId[i]);
			WriteString(m_pProcessSocket[i], str);
			ReadString(m_pProcessSocket[i], str);
			
			WriteString(m_pProcessSocket[i], "done");
			easy_closesocket(m_pProcessSocket[i]);
			FD_CLR(m_pProcessSocket[i], &totalset);
			m_pProcessSocket[i] = INVALID_SOCKET;
			n--;
			m_nNumProcessSockets--;
		    }
		}
	    }
	}
	
	easy_closesocket(m_sockBreak);
	m_sockBreak = INVALID_SOCKET;
	delete m_pProcessSocket;
	delete m_pProcessLaunchId;
	delete m_pLaunchIdToRank;
	m_pProcessSocket = NULL;
	m_pProcessLaunchId = NULL;
	m_pLaunchIdToRank = NULL;
    }
    else
    {
	DWORD dwThreadID;
	int num = (m_nNumProcessSockets / (FD_SETSIZE-1)) + 1;
	HANDLE *hThread = new HANDLE[num];
	SOCKET *pAbortsock = new SOCKET[num];
	SOCKET sockStop;
	ProcessWaitThreadArg *arg = new ProcessWaitThreadArg[num];
	ProcessWaitAbortThreadArg *arg2 = new ProcessWaitAbortThreadArg;
	for (int i=0; i<num; i++)
	{
	    if (i == num-1)
		arg[i].n = m_nNumProcessSockets % (FD_SETSIZE-1);
	    else
		arg[i].n = (FD_SETSIZE-1);
	    arg[i].pSocket = &m_pProcessSocket[i*(FD_SETSIZE-1)];
	    arg[i].pId = &m_pProcessLaunchId[i*(FD_SETSIZE-1)];
	    arg[i].pRank = &m_pLaunchIdToRank[i*(FD_SETSIZE-1)];
	    arg[i].pDlg = this;
	    MakeLoop(&arg[i].sockAbort, &pAbortsock[i]);
	}
	for (i=0; i<num; i++)
	{
	    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
	    {
		hThread[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ProcessWait, &arg[i], 0, &dwThreadID);
		if (hThread[i] != NULL)
		    break;
		Sleep(CREATE_THREAD_SLEEP_TIME);
	    }
	}
	if (m_sockBreak != INVALID_SOCKET)
	    easy_closesocket(m_sockBreak);
	MakeLoop(&arg2->sockAbort, &m_sockBreak);
	MakeLoop(&arg2->sockStop, &sockStop);

	HANDLE hWaitAbortThread;
	for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
	{
	    hWaitAbortThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ProcessWaitAbort, arg2, 0, &dwThreadID);
	    if (hWaitAbortThread != NULL)
		break;
	    Sleep(CREATE_THREAD_SLEEP_TIME);
	}

	SetEvent(m_hBreakReadyEvent);

	WaitForMultipleObjects(num, hThread, TRUE, INFINITE);
	for (i=0; i<num; i++)
	    CloseHandle(hThread[i]);
	delete hThread;
	delete arg;

	easy_send(sockStop, "x", 1);
	easy_closesocket(sockStop);
	WaitForSingleObject(hWaitAbortThread, 10000);
	delete pAbortsock;
	delete arg2;
	CloseHandle(hWaitAbortThread);

	easy_closesocket(m_sockBreak);
	m_sockBreak = INVALID_SOCKET;
	delete m_pProcessSocket;
	delete m_pProcessLaunchId;
	delete m_pLaunchIdToRank;
	m_pProcessSocket = NULL;
	m_pProcessLaunchId = NULL;
	m_pLaunchIdToRank = NULL;
    }
}

static void ProcessInputString(CString &str)
{
    int index;
    while (true)
    {
	index = str.Find('\b');
	if (index == -1)
	    return;
	str.Delete(index);
	if (index > 0)
	    str.Delete(index-1);
    }
}

void CGuiMPIRunView::OnMsgfilterOutput(NMHDR* pNMHDR, LRESULT* pResult) 
{
    MSGFILTER *pMsgFilter = reinterpret_cast<MSGFILTER *>(pNMHDR);

    if (pMsgFilter->msg == WM_CHAR)
    {
	char ch = (char)pMsgFilter->wParam;
	if (ch == VK_RETURN)
	{
	    ProcessInputString(m_curoutput);
	    m_curoutput += "\r\n";
	    if (WaitForSingleObject(m_hRedirectStdinMutex, 10000) == WAIT_OBJECT_0)
	    {
		if (m_pRedirectStdinList == NULL)
		{
		    m_pRedirectStdinList = new RedirectStdinStruct;
		    m_pRedirectStdinList->str = m_curoutput;
		    m_pRedirectStdinList->pNext = NULL;
		}
		else
		{
		    RedirectStdinStruct *pNode = m_pRedirectStdinList;
		    while (pNode->pNext != NULL)
			pNode = pNode->pNext;
		    pNode->pNext = new RedirectStdinStruct;
		    pNode = pNode->pNext;
		    pNode->pNext = NULL;
		    pNode->str = m_curoutput;
		}
		m_curoutput = "";
		SetEvent(m_hRedirectStdinEvent);
		ReleaseMutex(m_hRedirectStdinMutex);
	    }
	}
	else
	    m_curoutput += ch;
    }

    *pResult = 0;
}

void CGuiMPIRunView::OnResetHostsBtn() 
{
    m_host_list.ResetContent();

    // Get hosts from registry
    char pszHosts[4096];
    DWORD nLength = 4096;
    if (ReadMPDRegistry("hosts", pszHosts, &nLength))
    {
	/*
	char *token = NULL;
	token = strtok(pszHosts, "|");
	if (token != NULL)
	{
	    m_host_list.InsertString(-1, token);
	    while ((token = strtok(NULL, "|")) != NULL)
	    {
		m_host_list.InsertString(-1, token);
	    }
	}
	*/
	QVS_Container *phosts;
	phosts = new QVS_Container(pszHosts);
	if (phosts->first(pszHosts, 4096))
	{
	    m_host_list.InsertString(-1, pszHosts);
	    while (phosts->next(pszHosts, 4096))
	    {
		m_host_list.InsertString(-1, pszHosts);
	    }
	}
	delete phosts;
    }
    else
    {
	char temp[100];
	gethostname(temp, 100);
	m_host_list.InsertString(-1, temp);
    }
}

int CGuiMPIRunView::OnVKeyToItem(UINT nKey, CListBox* pListBox, UINT nIndex) 
{
    if (pListBox == &m_host_list)
    {
	if (nKey == VK_DELETE)
	{
	    int pIndices[1024];
	    int n;
	    n = m_host_list.GetSelItems(1024, pIndices);
	    if (n > 0)
	    {
		for (int i=n-1; i>=0; i--)
		    m_host_list.DeleteString(pIndices[i]);
		if (m_host_list.SetCurSel(pIndices[0]) == LB_ERR)
		    m_host_list.SetCurSel(pIndices[0]-1);
	    }
	    else
	    {
		if (m_host_list.GetCount() > 0)
		{
		    m_host_list.DeleteString(0);
		    m_host_list.SetCurSel(0);
		}
	    }
	}
    }
    
    return CFormView::OnVKeyToItem(nKey, pListBox, nIndex);
}
