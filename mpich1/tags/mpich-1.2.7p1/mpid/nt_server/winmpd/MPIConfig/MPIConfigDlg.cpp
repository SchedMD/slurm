// MPIConfigDlg.cpp : implementation file
//

#include "stdafx.h"
#include "MPIConfig.h"
#include "MPIConfigDlg.h"
#include "mpd.h"
#include "crypt.h"
#include <tchar.h>
#include "RegistrySettingsDialog.h"
#include "PwdDialog.h"
#include "qvs.h"
#include "FindHostsDlg.h"
#include "mpdutil.h"
#include "ConnectToHost.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define DEFAULT_LAUNCH_TIMEOUT 7

/////////////////////////////////////////////////////////////////////////////
// CMPIConfigDlg dialog

CMPIConfigDlg::CMPIConfigDlg(CWnd* pParent /*=NULL*/)
: CDialog(CMPIConfigDlg::IDD, pParent)
{
    m_bNeedPassword = false;
    //{{AFX_DATA_INIT(CMPIConfigDlg)
    m_hostname = _T("");
	m_password = _T("");
	m_bHostsChk = TRUE;
	m_nLaunchTimeout = DEFAULT_LAUNCH_TIMEOUT;
	m_bTempChk = FALSE;
	m_pszTempDir = _T("C:\\");
	m_bLaunchTimeoutChk = FALSE;
	m_host_config = _T("");
	m_bShowHostConfig = FALSE;
	//}}AFX_DATA_INIT
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
    m_hSetBtnThread = NULL;
    m_nMinWidth = -1;
    m_nMinHeight = -1;
}

void CMPIConfigDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    //{{AFX_DATA_MAP(CMPIConfigDlg)
	DDX_Control(pDX, IDC_SET_ONE_BTN, m_set_one_btn);
	DDX_Control(pDX, IDC_APPLY_ONE_STATIC, m_apply_one_static);
	DDX_Control(pDX, IDC_SHOW_HOST_CHK, m_show_host_chk);
	DDX_Control(pDX, IDC_HOST_CONFIG, m_host_config_edit);
	DDX_Control(pDX, IDC_STOPLIGHT_YELLOW, m_stoplight_yellow);
	DDX_Control(pDX, IDC_STOPLIGHT_GREEN, m_stoplight_green);
	DDX_Control(pDX, IDC_STOPLIGHT_RED, m_stoplight_red);
	DDX_Control(pDX, IDC_TIMEOUT_CHK, m_launch_chk);
	DDX_Control(pDX, IDC_TEMP_CHK, m_temp_chk);
	DDX_Control(pDX, IDC_HOSTS_CHK, m_hosts_chk);
	DDX_Control(pDX, IDC_TWO_STATIC, m_two_static);
	DDX_Control(pDX, IDC_THREE_STATIC, m_three_static);
	DDX_Control(pDX, IDC_TEMP_STATIC, m_temp_static);
	DDX_Control(pDX, IDC_PWD_STATIC, m_pwd_static);
	DDX_Control(pDX, IDC_PHRASE_STATIC, m_phrase_static);
	DDX_Control(pDX, IDC_ONE_STATIC, m_one_static);
	DDX_Control(pDX, IDC_APPLY_STATIC, m_apply_static);
	DDX_Control(pDX, IDC_TEMP_EDIT, m_TempEdit);
	DDX_Control(pDX, IDC_LAUNCH_TIMEOUT, m_LaunchTimeoutEdit);
	DDX_Control(pDX, IDC_PASSWORD, m_pwd_ctrl);
	DDX_Control(pDX, IDOK, m_ok_btn);
	DDX_Control(pDX, IDCANCEL, m_cancel_btn);
	DDX_Control(pDX, IDC_EDIT_ADD_BTN, m_edit_add_btn);
    DDX_Control(pDX, IDC_HOST_LIST, m_host_list);
    DDX_Control(pDX, IDC_SET_BTN, m_set_btn);
    DDX_Text(pDX, IDC_HOSTNAME, m_hostname);
	DDX_Text(pDX, IDC_PASSWORD, m_password);
	DDX_Check(pDX, IDC_HOSTS_CHK, m_bHostsChk);
	DDX_Text(pDX, IDC_LAUNCH_TIMEOUT, m_nLaunchTimeout);
	DDV_MinMaxInt(pDX, m_nLaunchTimeout, 1, 1000);
	DDX_Check(pDX, IDC_TEMP_CHK, m_bTempChk);
	DDX_Text(pDX, IDC_TEMP_EDIT, m_pszTempDir);
	DDX_Check(pDX, IDC_TIMEOUT_CHK, m_bLaunchTimeoutChk);
	DDX_Text(pDX, IDC_HOST_CONFIG, m_host_config);
	DDX_Check(pDX, IDC_SHOW_HOST_CHK, m_bShowHostConfig);
	DDX_Control(pDX, IDC_DEFAULT_RADIO, m_default_radio);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CMPIConfigDlg, CDialog)
//{{AFX_MSG_MAP(CMPIConfigDlg)
ON_WM_PAINT()
ON_WM_QUERYDRAGICON()
ON_BN_CLICKED(IDC_SET_BTN, OnSetBtn)
ON_BN_CLICKED(IDC_EDIT_ADD_BTN, OnEditAddBtn)
	ON_WM_VKEYTOITEM()
	ON_WM_CLOSE()
	ON_WM_SIZE()
	ON_BN_CLICKED(IDC_PHRASE_RADIO, OnPhraseRadio)
	ON_BN_CLICKED(IDC_DEFAULT_RADIO, OnDefaultPwdRadio)
	ON_BN_CLICKED(IDC_TEMP_CHK, OnTempChk)
	ON_BN_CLICKED(IDC_TIMEOUT_CHK, OnTimeoutChk)
	ON_BN_CLICKED(IDC_SHOW_HOST_CHK, OnShowHostChk)
	ON_LBN_SELCHANGE(IDC_HOST_LIST, OnSelchangeHostList)
	ON_BN_CLICKED(IDC_SET_ONE_BTN, OnSetOneBtn)
	ON_BN_CLICKED(IDC_SELECT_BTN, OnSelectBtn)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMPIConfigDlg message handlers

void CMPIConfigDlg::ParseRegistry()
{
    HKEY tkey;
    DWORD result, len;
    
    // Set the defaults.
    m_nPort = MPD_DEFAULT_PORT;
    gethostname(m_pszHost, 100);
    
    m_bNeedPassword = true;

    // Open the root key
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, MPD_REGISTRY_KEY,
	0, KEY_ALL_ACCESS, &tkey) != ERROR_SUCCESS)
    {
	printf("Unable to open SOFTWARE\\MPICH\\MPD registry key, error %d\n", GetLastError());
	return;
    }
    
    // Read the port
    len = sizeof(int);
    result = RegQueryValueEx(tkey, "port", 0, NULL, (unsigned char *)&m_nPort, &len);
    
    // Check to see if a passphrase has been set and set it to the default if necessary.
    len = 100;
    result = RegQueryValueEx(tkey, "phrase", 0, NULL, (unsigned char *)m_pszPhrase, &len);
    if (result == ERROR_SUCCESS)
	m_bNeedPassword = false;
    
    RegCloseKey(tkey);
}

BOOL CMPIConfigDlg::OnInitDialog()
{
    //HWND hWnd;
    CDialog::OnInitDialog();
    
    SetIcon(m_hIcon, TRUE);			// Set big icon
    SetIcon(m_hIcon, FALSE);		// Set small icon
    
    easy_socket_init();
    ParseRegistry();

    RECT r;
    GetClientRect(&r);
    m_nMinWidth = r.right;
    m_nMinHeight = r.bottom;

    r1Static.SetInitialPosition(m_one_static.m_hWnd, RSR_STRETCH_BOTTOM);
    rList.SetInitialPosition(m_host_list.m_hWnd, RSR_STRETCH_BOTTOM);
    rOk.SetInitialPosition(m_ok_btn.m_hWnd, RSR_MOVE);
    rCancel.SetInitialPosition(m_cancel_btn.m_hWnd, RSR_MOVE);
    rHostConfig.SetInitialPosition(m_host_config_edit.m_hWnd, RSR_STRETCH);

    m_TempEdit.EnableWindow(FALSE);
    m_LaunchTimeoutEdit.EnableWindow(FALSE);
    m_host_config_edit.EnableWindow(FALSE);

    m_default_radio.SetCheck(1);
    m_pwd_ctrl.EnableWindow(FALSE);

    SetRedLight();

    char host[100] = "";
    gethostname(host, 100);
    m_hostname = host;
    UpdateData(FALSE);

    return TRUE;  // return TRUE  unless you set the focus to a control
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CMPIConfigDlg::OnPaint() 
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

HCURSOR CMPIConfigDlg::OnQueryDragIcon()
{
    return (HCURSOR) m_hIcon;
}

void SetBtnThread(CMPIConfigDlg *pDlg)
{
    int i;
    int num_hosts;
    char pszStr[4096];
    char hoststring[4096] = "";
    char host[100];
    SOCKET sock;

    num_hosts = pDlg->m_host_list.GetCount();
    if (num_hosts == 0)
    {
	CloseHandle(pDlg->m_hSetBtnThread);
	pDlg->m_hSetBtnThread = NULL;
	return;
    }
    
    if (pDlg->m_bHostsChk == FALSE && pDlg->m_bTempChk == FALSE && pDlg->m_bLaunchTimeoutChk == FALSE)
    {
	CloseHandle(pDlg->m_hSetBtnThread);
	pDlg->m_hSetBtnThread = NULL;
	return;
    }
    
    if (pDlg->m_bNeedPassword)
    {
	if (pDlg->m_bUseDefault)
	    strcpy(pDlg->m_pszPhrase, MPD_DEFAULT_PASSPHRASE);
	else
	    strcpy(pDlg->m_pszPhrase, pDlg->m_password);
    }

    pDlg->SetYellowLight();

    HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );
    PostMessage(pDlg->m_hWnd, WM_USER + 3, 0, 0);
    
    // Create the host list
    QVS_Container qvs;
    for (i=0; i<num_hosts; i++)
    {
	if (pDlg->m_host_list.GetText(i, host) == LB_ERR)
	{
	    pDlg->SetRedLight();
	    SetCursor(hOldCursor);
	    PostMessage(pDlg->m_hWnd, WM_USER + 4, 0, 0);
	    CloseHandle(pDlg->m_hSetBtnThread);
	    pDlg->m_hSetBtnThread = NULL;
	    MessageBox(NULL, "GetText failed", "Error", MB_OK);
	    return;
	}
	/*
	strncat(hoststring, host, 4095 - strlen(hoststring));
	if (i<num_hosts-1)
	    strncat(hoststring, "|", 4095 - strlen(hoststring));
	    */
	qvs.encode_string(host);
    }
    qvs.output_encoded_string(hoststring, 4096);
    
    for (i=0; i<num_hosts; i++)
    {
	if (pDlg->m_host_list.GetText(i, host) == LB_ERR)
	    continue;
	
	if (!ConnectToHost(host, pDlg->m_nPort, pDlg->m_pszPhrase, &sock))
	    continue;
	
	if (pDlg->m_bHostsChk)
	{
	    sprintf(pszStr, "lset hosts=%s", hoststring);
	    WriteString(sock, pszStr);
	}
	if (pDlg->m_bTempChk)
	{
	    sprintf(pszStr, "lset temp=%s", pDlg->m_pszTempDir);
	    WriteString(sock, pszStr);
	}
	if (pDlg->m_bLaunchTimeoutChk)
	{
	    sprintf(pszStr, "lset timeout=%d", pDlg->m_nLaunchTimeout);
	    WriteString(sock, pszStr);
	}
	WriteString(sock, "done");
	easy_closesocket(sock);
    }

    pDlg->SetGreenLight();

    SetCursor(hOldCursor);
    PostMessage(pDlg->m_hWnd, WM_USER + 4, 0, 0);

    if (pDlg->m_bShowHostConfig)
    {
	PostMessage(pDlg->m_hWnd, WM_USER+2, 0, 0);
    }

    CloseHandle(pDlg->m_hSetBtnThread);
    pDlg->m_hSetBtnThread = NULL;
}

#ifdef USE_SINGLE_THREADED_SET
void CMPIConfigDlg::OnSetBtn()
{
    int i;
    int num_hosts;
    char pszStr[4096];
    char hoststring[4096] = "";
    char host[100];
    SOCKET sock;

    UpdateData();

    num_hosts = m_host_list.GetCount();
    if (num_hosts == 0)
	return;
    
    if (m_bHostsChk == FALSE && m_bTempChk == FALSE && m_bLaunchTimeoutChk == FALSE)
	return;
    
    if (m_bNeedPassword)
    {
	if (m_bUseDefault)
	    strcpy(m_pszPhrase, MPD_DEFAULT_PASSPHRASE);
	else
	    strcpy(m_pszPhrase, m_password);
    }

    SetYellowLight();

    HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );
    
    // Create the host list
    QVS_Container qvs;
    for (i=0; i<num_hosts; i++)
    {
	if (m_host_list.GetText(i, host) == LB_ERR)
	{
	    SetRedLight();
	    SetCursor(hOldCursor);
	    MessageBox("GetText failed", "Error", MB_OK);
	    return;
	}
	/*
	strncat(hoststring, host, 4095 - strlen(hoststring));
	if (i<num_hosts-1)
	    strncat(hoststring, "|", 4095 - strlen(hoststring));
	    */
	qvs.encode_string(host);
    }
    qvs.output_encoded_string(hoststring, 4096);
    
    for (i=0; i<num_hosts; i++)
    {
	if (m_host_list.GetText(i, host) == LB_ERR)
	    continue;
	
	if (!ConnectToHost(host, m_nPort, m_pszPhrase, &sock))
	    continue;
	
	if (m_bHostsChk)
	{
	    sprintf(pszStr, "lset hosts=%s", hoststring);
	    WriteString(sock, pszStr);
	}
	if (m_bTempChk)
	{
	    sprintf(pszStr, "lset temp=%s", m_pszTempDir);
	    WriteString(sock, pszStr);
	}
	if (m_bLaunchTimeoutChk)
	{
	    sprintf(pszStr, "lset timeout=%d", m_nLaunchTimeout);
	    WriteString(sock, pszStr);
	}
	WriteString(sock, "done");
	easy_closesocket(sock);
    }

    SetGreenLight();

    SetCursor(hOldCursor);

    if (m_bShowHostConfig)
	GetHostConfig(NULL);
}
#else
void CMPIConfigDlg::OnSetBtn()
{
    DWORD dwThreadID;

    UpdateData();
    m_hSetBtnThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)SetBtnThread, this, 0, &dwThreadID);
}
#endif

void CMPIConfigDlg::OnSetOneBtn() 
{
    int i;
    char pszStr[4096];
    char hoststring[4096] = "";
    char host[100];
    SOCKET sock;
    CString sHost;
    int num_hosts;

    UpdateData();

    num_hosts = m_host_list.GetCount();
    if (num_hosts == 0)
	return;

    if (m_bHostsChk == FALSE && m_bTempChk == FALSE && m_bLaunchTimeoutChk == FALSE)
	return;
    
    int index = m_host_list.GetCurSel();
    if (index == LB_ERR)
	return;
    m_host_list.GetText(index, sHost);

    if (m_bNeedPassword)
    {
	if (m_bUseDefault)
	    strcpy(m_pszPhrase, MPD_DEFAULT_PASSPHRASE);
	else
	    strcpy(m_pszPhrase, m_password);
    }

    SetYellowLight();

    HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );
    
    // Create the host list
    QVS_Container qvs;
    for (i=0; i<num_hosts; i++)
    {
	if (m_host_list.GetText(i, host) == LB_ERR)
	{
	    SetRedLight();
	    SetCursor(hOldCursor);
	    MessageBox("GetText failed", "Error", MB_OK);
	    return;
	}
	qvs.encode_string(host);
    }
    qvs.output_encoded_string(hoststring, 4096);
    
    if (ConnectToHost(sHost, m_nPort, m_pszPhrase, &sock))
    {
	if (m_bHostsChk)
	{
	    sprintf(pszStr, "lset hosts=%s", hoststring);
	    WriteString(sock, pszStr);
	}
	if (m_bTempChk)
	{
	    sprintf(pszStr, "lset temp=%s", m_pszTempDir);
	    WriteString(sock, pszStr);
	}
	if (m_bLaunchTimeoutChk)
	{
	    sprintf(pszStr, "lset timeout=%d", m_nLaunchTimeout);
	    WriteString(sock, pszStr);
	}
	WriteString(sock, "done");
	easy_closesocket(sock);
    }
    else
    {
	SetRedLight();
	SetCursor(hOldCursor);
	if (m_bShowHostConfig)
	    GetHostConfig(NULL);
	return;
    }

    SetGreenLight();

    SetCursor(hOldCursor);

    if (m_bShowHostConfig)
	GetHostConfig(NULL);
}

LRESULT CMPIConfigDlg::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) 
{
    if (message == WM_USER + 2)
    {
	GetHostConfig(NULL);
    }
    if (message == WM_USER + 3)
    {
	m_set_btn.EnableWindow(FALSE);
	m_set_one_btn.EnableWindow(FALSE);
	m_edit_add_btn.EnableWindow(FALSE);
    }
    if (message == WM_USER + 4)
    {
	m_set_btn.EnableWindow();
	m_set_one_btn.EnableWindow();
	m_edit_add_btn.EnableWindow();
    }
    return CDialog::WindowProc(message, wParam, lParam);
}

void CMPIConfigDlg::OnEditAddBtn() 
{
    UpdateData();
    
    if (m_hostname.GetLength() != 0)
    {
	
	CString str;
	int n = m_host_list.GetCount();
	if (n != LB_ERR)
	{
	    bool bFound = false;
	    for (int i=0; i<n; i++)
	    {
		m_host_list.GetText(i, str);
		if (str.CompareNoCase(m_hostname) == 0)
		{
		    bFound = true;
		    //break;
		}
	    }
	    if (!bFound)
	    {
		m_host_list.InsertString(-1, m_hostname);
	    }
	}
    }
}

int CMPIConfigDlg::OnVKeyToItem(UINT nKey, CListBox* pListBox, UINT nIndex) 
{
    if (*pListBox == m_host_list)
    {
	if (nKey == VK_DELETE)
	{
	    int index = m_host_list.GetCurSel();
	    if (index != LB_ERR)
	    {
		m_host_list.DeleteString(index);
		if (m_host_list.SetCurSel(index) == LB_ERR)
		    m_host_list.SetCurSel(index-1);
		SetRedLight();
	    }
	}
    }
	return CDialog::OnVKeyToItem(nKey, pListBox, nIndex);
}

void CMPIConfigDlg::OnClose() 
{
    if (m_hSetBtnThread)
    {
	TerminateThread(m_hSetBtnThread, -1);
	m_hSetBtnThread = NULL;
    }
    easy_socket_finalize();
	CDialog::OnClose();
}

void CMPIConfigDlg::OnSize(UINT nType, int cx, int cy) 
{
    CDialog::OnSize(nType, cx, cy);

    if (nType != SIZE_MINIMIZED)
    {
	if (m_nMinWidth <= cx || m_nMinHeight <= cy)
	{
	    if (cx < m_nMinWidth)
		cx = m_nMinWidth;
	    if (cy < m_nMinHeight)
		cy = m_nMinHeight;

	    r1Static.Resize(cx, cy);
	    rList.Resize(cx, cy);
	    rOk.Resize(cx, cy);
	    rCancel.Resize(cx, cy);
	    
	    rHostConfig.Resize(cx, cy);
	    
	    Invalidate();
	}
    }
}

void CMPIConfigDlg::OnPhraseRadio() 
{
    m_bUseDefault = false;
    m_pwd_ctrl.EnableWindow();
    SetRedLight();
}

void CMPIConfigDlg::OnDefaultPwdRadio() 
{
    m_bUseDefault = true;
    m_pwd_ctrl.EnableWindow(FALSE);
    SetRedLight();
}

void CMPIConfigDlg::OnTempChk() 
{
    UpdateData();
    
    if (m_bTempChk)
	m_TempEdit.EnableWindow();
    else
	m_TempEdit.EnableWindow(FALSE);

    SetRedLight();
}

void CMPIConfigDlg::OnTimeoutChk() 
{
    UpdateData();
    
    if (m_bLaunchTimeoutChk)
	m_LaunchTimeoutEdit.EnableWindow();
    else
	m_LaunchTimeoutEdit.EnableWindow(FALSE);

    SetRedLight();
}

void CMPIConfigDlg::SetRedLight()
{
    m_stoplight_red.ShowWindow(SW_SHOW);
    m_stoplight_yellow.ShowWindow(SW_HIDE);
    m_stoplight_green.ShowWindow(SW_HIDE);
}

void CMPIConfigDlg::SetGreenLight()
{
    m_stoplight_red.ShowWindow(SW_HIDE);
    m_stoplight_yellow.ShowWindow(SW_HIDE);
    m_stoplight_green.ShowWindow(SW_SHOW);
}

void CMPIConfigDlg::SetYellowLight()
{
    m_stoplight_red.ShowWindow(SW_HIDE);
    m_stoplight_yellow.ShowWindow(SW_SHOW);
    m_stoplight_green.ShowWindow(SW_HIDE);
}

void CMPIConfigDlg::OnShowHostChk() 
{
    UpdateData();

    m_host_config_edit.EnableWindow(m_bShowHostConfig);
    if (m_bShowHostConfig)
	GetHostConfig(NULL);
}

void CMPIConfigDlg::OnSelchangeHostList() 
{
    UpdateData();
    if (m_bShowHostConfig)
    {
	CString host;
	int index = m_host_list.GetCurSel();
	if (index != LB_ERR)
	{
	    m_host_list.GetText(index, host);
	    GetHostConfig(host);
	}
    }
}

void CMPIConfigDlg::GetHostConfig(const char *host)
{
    CString sHost;
    SOCKET sock;
    char pszStr[MAX_CMD_LENGTH] = "mpd not installed";

    UpdateData();

    if (host == NULL)
    {
	int index = m_host_list.GetCurSel();
	if (index == LB_ERR)
	    return;
	m_host_list.GetText(index, sHost);
    }
    else
	sHost = host;

    if (m_bNeedPassword)
    {
	if (m_bUseDefault)
	    strcpy(m_pszPhrase, MPD_DEFAULT_PASSPHRASE);
	else
	    strcpy(m_pszPhrase, m_password);
    }
    
    HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );
    
    if (ConnectToHost(sHost, m_nPort, m_pszPhrase, &sock))
    {
	WriteString(sock, "config");
	ReadString(sock, pszStr);
	WriteString(sock, "done");
	easy_closesocket(sock);
	
	m_host_config = sHost + ":\n";
	m_host_config += pszStr;
	m_host_config.Replace("\n", "\r\n");
    }
    else
    {
	m_host_config = sHost + ":\r\n" + pszStr;
    }
    
    SetCursor(hOldCursor);
    UpdateData(FALSE);
}

void CMPIConfigDlg::OnSelectBtn() 
{
    CFindHostsDlg dlg;
    if (dlg.DoModal() == IDOK)
    {
	QVS_Container qvs;
	char str[100];

	m_host_list.ResetContent();

	qvs.decode_string((char*)(LPCTSTR)dlg.m_encoded_hosts);
	if (qvs.first(str, 100))
	{
	    m_host_list.AddString(str);
	    while (qvs.next(str, 100))
	    {
		m_host_list.AddString(str);
	    }
	}
    }
}
