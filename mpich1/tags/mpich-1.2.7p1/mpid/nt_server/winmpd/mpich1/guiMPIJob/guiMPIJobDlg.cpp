// guiMPIJobDlg.cpp : implementation file
//

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

/////////////////////////////////////////////////////////////////////////////
// CAboutDlg dialog used for App About

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// Dialog Data
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAboutDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	//{{AFX_MSG(CAboutDlg)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
	//{{AFX_MSG_MAP(CAboutDlg)
		// No message handlers
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CGuiMPIJobDlg dialog

CGuiMPIJobDlg::CGuiMPIJobDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CGuiMPIJobDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CGuiMPIJobDlg)
	m_job_details = _T("");
	m_job = _T("");
	m_bFullChecked = FALSE;
	//}}AFX_DATA_INIT
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_sock = INVALID_SOCKET;
	m_host = "";
	m_port = MPD_DEFAULT_PORT;
	m_passphrase = MPD_DEFAULT_PASSPHRASE;
}

void CGuiMPIJobDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CGuiMPIJobDlg)
	DDX_Control(pDX, IDOK, m_ok_btn);
	DDX_Control(pDX, IDCANCEL, m_cancel_btn);
	DDX_Control(pDX, IDC_REMOVE_BTN, m_remove_btn);
	DDX_Control(pDX, IDC_REFRESH_BTN, m_refresh_btn);
	DDX_Control(pDX, IDC_KILL_BTN, m_kill_btn);
	DDX_Control(pDX, IDC_JOBS_LIST, m_job_list);
	DDX_Control(pDX, IDC_JOB_EDIT, m_job_edit);
	DDX_Control(pDX, IDC_FULL_CHK, m_full_chk);
	DDX_Text(pDX, IDC_JOB_EDIT, m_job_details);
	DDX_LBString(pDX, IDC_JOBS_LIST, m_job);
	DDX_Check(pDX, IDC_FULL_CHK, m_bFullChecked);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CGuiMPIJobDlg, CDialog)
	//{{AFX_MSG_MAP(CGuiMPIJobDlg)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_CONNECT_BTN, OnConnectBtn)
	ON_BN_CLICKED(IDC_REFRESH_BTN, OnRefreshBtn)
	ON_BN_CLICKED(IDC_REMOVE_BTN, OnRemoveBtn)
	ON_BN_CLICKED(IDC_KILL_BTN, OnKillBtn)
	ON_BN_CLICKED(IDC_FULL_CHK, OnFullChk)
	ON_LBN_SELCHANGE(IDC_JOBS_LIST, OnSelchangeJobsList)
	ON_WM_CLOSE()
	ON_WM_SIZE()
	ON_WM_VKEYTOITEM()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CGuiMPIJobDlg message handlers

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

BOOL CGuiMPIJobDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	DWORD length = 100;
	char host[100];

	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon
	
	if (ReadMPDRegistry("jobhost", host, &length))
	{
	    bool bUseLocalHost = true;
	    m_host = host;
	    length = 100;
	    bUseLocalHost = !ReadMPDRegistry("usejobhost", host, &length);
	    if (!bUseLocalHost)
	    {
		bUseLocalHost = (stricmp(host, "yes") != 0);
	    }
	    if (bUseLocalHost)
	    {
		length = 100;
		GetComputerName(m_host.GetBuffer(100), &length);
		m_host.ReleaseBuffer();
	    }
	}
	else
	{
	    length = 100;
	    GetComputerName(m_host.GetBuffer(100), &length);
	    m_host.ReleaseBuffer();
	}

	m_refresh_btn.EnableWindow(FALSE);
	m_remove_btn.EnableWindow(FALSE);
	m_kill_btn.EnableWindow(FALSE);
	m_job_list.EnableWindow(FALSE);
	m_full_chk.EnableWindow(FALSE);

	rOk.SetInitialPosition(m_ok_btn.m_hWnd, RSR_ANCHOR_RIGHT);
	rCancel.SetInitialPosition(m_cancel_btn.m_hWnd, RSR_ANCHOR_RIGHT);
	rJobs.SetInitialPosition(m_job_list.m_hWnd, RSR_STRETCH_RIGHT);
	rDetails.SetInitialPosition(m_job_edit.m_hWnd, RSR_STRETCH);

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CGuiMPIJobDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CGuiMPIJobDlg::OnPaint() 
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, (WPARAM) dc.GetSafeHdc(), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

HCURSOR CGuiMPIJobDlg::OnQueryDragIcon()
{
	return (HCURSOR) m_hIcon;
}

void CGuiMPIJobDlg::OnConnectBtn() 
{
    CMPDConnectDlg dlg;

    dlg.m_port = m_port;
    dlg.m_phrase = m_passphrase;
    dlg.m_host = m_host;
    if (dlg.DoModal() == IDOK)
    {
	if (m_sock != INVALID_SOCKET)
	{
	    WriteString(m_sock, "done");
	    easy_closesocket(m_sock);
	    m_sock = INVALID_SOCKET;
	}

	if (dlg.m_bPortChecked)
	    m_port = dlg.m_port;
	if (dlg.m_bPhraseChecked)
	    m_passphrase = dlg.m_phrase;
	m_host = dlg.m_host;

	HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );
	if (ConnectToMPD(m_host, m_port, m_passphrase, &m_sock) != 0)
	{
	    CString str;
	    str.Format("Unable to connect to %s on port %d with the given passphrase", m_host, m_port);
	    MessageBox(str, "Connect failed", MB_OK);
	    m_sock = INVALID_SOCKET;
	    m_refresh_btn.EnableWindow(FALSE);
	    m_remove_btn.EnableWindow(FALSE);
	    m_kill_btn.EnableWindow(FALSE);
	    m_job_list.EnableWindow(FALSE);
	    m_full_chk.EnableWindow(FALSE);
	}
	else
	{
	    m_refresh_btn.EnableWindow();
	    m_remove_btn.EnableWindow();
	    m_kill_btn.EnableWindow();
	    m_job_list.EnableWindow();
	    m_full_chk.EnableWindow();
	    OnRefreshBtn();
	}
	SetCursor(hOldCursor);
    }
}

void GetKeyAndValue(char *str, char *key, char *value)
{
    char *token;

    token = strstr(str, "value=");
    *token = '\0';
    strcpy(value, token+6);
    strcpy(key, str+4);

    // strip any whitespace off the end of the key
    token = &key[strlen(key)-1];
    while (isspace(*token))
	token--;
    token++;
    *token = '\0';
}

bool GetState(SOCKET sock, char *dbname, char *state)
{
    char str[256];
    int error;

    sprintf(str, "dbget %s:state", dbname);
    if (WriteString(sock, str) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	Translate_Error(error, str);
	return false;
    }
    if (ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
    {
	if (strcmp(str, "DBS_FAIL") == 0)
	{
	    strcpy(state, "CORRUPTED");
	    return true;
	}
	strcpy(state, str);
	return true;
    }

    return false;
}

void CGuiMPIJobDlg::OnRefreshBtn() 
{
    char str[CONSOLE_STR_LENGTH+1];
    int error;
    char key[100];
    char value[CONSOLE_STR_LENGTH];
    char state[100];

    UpdateData();
    m_job_list.ResetContent();
    m_job_details = "";
    UpdateData(FALSE);

    m_job_list.EnableWindow();

    strcpy(str, "dbfirst jobs");
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
	    m_job_list.InsertString(-1, "no jobs");
	    m_job_list.EnableWindow(FALSE);
	    return;
	}
	if (strcmp(str, "DBS_END") == 0)
	{
	    m_job_list.InsertString(-1, "no jobs");
	    m_job_list.EnableWindow(FALSE);
	    return;
	}
	GetKeyAndValue(str, key, value);
	if (!GetState(m_sock, strstr(value, "@")+1, state))
	{
	    sprintf(str, "Unable to read the state of job %s", strstr(value, "@")+1);
	    MessageBox(str, "Error");
	    Disconnect();
	    return;
	}
	sprintf(str, "%s : %s : %s", key, value, state);
	m_job_list.InsertString(-1, str);
    }
    else
    {
	MessageBox("Unable to read the jobs", "Connection Error");
	//m_job_list.InsertString(-1, "Unable to read the jobs");
	Disconnect();
	return;
    }

    while (true)
    {
	strcpy(str, "dbnext jobs");
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
		return;
	    }
	    if (strcmp(str, "DBS_END") == 0)
	    {
		break;
	    }
	    GetKeyAndValue(str, key, value);
	    if (!GetState(m_sock, strstr(value, "@")+1, state))
	    {
		sprintf(str, "Unable to read the state of job %s", strstr(value, "@")+1);
		MessageBox(str, "Error");
		Disconnect();
		return;
	    }
	    sprintf(str, "%s : %s : %s", key, value, state);
	    m_job_list.InsertString(-1, str);
	}
	else
	{
	    MessageBox("Unable to read the jobs", "Connection Error");
	    //m_job_list.InsertString(-1, "Unable to read the jobs");
	    Disconnect();
	    return;
	}
    }
}

void CGuiMPIJobDlg::OnFullChk() 
{
    UpdateData();
    if (m_job.GetLength())
    {
	GetJobDetails();
    }
}

void CGuiMPIJobDlg::OnSelchangeJobsList() 
{
    UpdateData();
    if (m_job.GetLength())
    {
	GetJobDetails();
    }
}

void CGuiMPIJobDlg::OnClose() 
{
    if (m_sock != INVALID_SOCKET)
    {
	WriteString(m_sock, "done");
	easy_closesocket(m_sock);
	m_sock = INVALID_SOCKET;
    }
    
    CDialog::OnClose();
}

void CGuiMPIJobDlg::Disconnect()
{
    if (m_sock != INVALID_SOCKET)
    {
	WriteString(m_sock, "done");
	easy_closesocket(m_sock);
	m_sock = INVALID_SOCKET;
    }

    m_refresh_btn.EnableWindow(FALSE);
    m_remove_btn.EnableWindow(FALSE);
    m_kill_btn.EnableWindow(FALSE);
    m_job_list.EnableWindow(FALSE);
    m_full_chk.EnableWindow(FALSE);
}

void CGuiMPIJobDlg::OnSize(UINT nType, int cx, int cy) 
{
	CDialog::OnSize(nType, cx, cy);

	rOk.Resize(cx, cy);
	rCancel.Resize(cx, cy);
	rJobs.Resize(cx, cy);
	rDetails.Resize(cx, cy);
}

int CGuiMPIJobDlg::OnVKeyToItem(UINT nKey, CListBox* pListBox, UINT nIndex) 
{
    if (nKey == VK_DELETE)
    {
	if (pListBox == &m_job_list)
	{
	    OnRemoveBtn();
	}
    }
    return CDialog::OnVKeyToItem(nKey, pListBox, nIndex);
}
