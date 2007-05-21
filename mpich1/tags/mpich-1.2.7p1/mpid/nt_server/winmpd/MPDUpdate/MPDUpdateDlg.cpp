// MPDUpdateDlg.cpp : implementation file
//

#include "stdafx.h"
#include "MPDUpdate.h"
#include "MPDUpdateDlg.h"
#include "mpd.h"
#include "crypt.h"
#include <tchar.h>
#include "PwdDialog.h"
#include <afxinet.h>
#include "mpdutil.h"
#include "launchprocess.h"
#include "FindHostsDlg.h"
#include "ConnectToHost.h"
#include "Translate_Error.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

bool GetLocalVersion(const char *filename, unsigned int &version);
bool GetLocalMPICHVersion(const char *filename, unsigned int &version);

/////////////////////////////////////////////////////////////////////////////
// CMPDUpdateDlg dialog

CMPDUpdateDlg::CMPDUpdateDlg(CWnd* pParent /*=NULL*/)
: CDialog(CMPDUpdateDlg::IDD, pParent)
{
    m_bNeedPassword = false;
    //{{AFX_DATA_INIT(CMPDUpdateDlg)
	m_bShowHostConfig = FALSE;
	m_filename = _T("");
	m_urlname = _T("ftp://ftp.mcs.anl.gov/pub/mpi/nt/binaries/mpd.exe");
	m_hostname = _T("");
	m_cred_account = _T("");
	m_cred_password = _T("");
	m_bForceUpdate = FALSE;
	m_results = _T("");
	m_mpd_pwd = _T("");
	m_mpd_port = MPD_DEFAULT_PORT;
	m_mpich_filename = _T("");
	m_mpich_url = _T("ftp://ftp.mcs.anl.gov/pub/mpi/nt/binaries/mpich.dll");
	m_bUpdateMPD = TRUE;
	m_bUpdateMPICH = FALSE;
	m_bMPDPassphraseChecked = FALSE;
	m_bMPDPortChecked = FALSE;
	m_mpd_version = _T("");
	m_mpich_version = _T("");
	m_config_host = _T("");
	m_config_mpich_version = _T("");
	m_config_mpd_version = _T("");
	//}}AFX_DATA_INIT
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
    m_hUpdateBtnThread = NULL;
    m_nMinWidth = -1;
    m_nMinHeight = -1;
}

void CMPDUpdateDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    //{{AFX_DATA_MAP(CMPDUpdateDlg)
	DDX_Control(pDX, IDC_MPICH_VERSION_BTN, m_mpich_version_btn);
	DDX_Control(pDX, IDC_MPD_VERSION_BTN, m_mpd_version_btn);
	DDX_Control(pDX, IDC_MPICH_SOURCE_STATIC, m_mpich_source_static);
	DDX_Control(pDX, IDC_CRED_ACCOUNT_EDIT, m_cred_account_edit);
	DDX_Control(pDX, IDC_SELECT_HOSTS_BTN, m_select_btn);
	DDX_Control(pDX, IDC_MPICH_URL_EDIT, m_mpich_url_edit);
	DDX_Control(pDX, IDC_MPICH_URL_RADIO, m_mpich_url_radio);
	DDX_Control(pDX, IDC_MPICH_FILE_RADIO, m_mpich_filename_radio);
	DDX_Control(pDX, IDC_MPICH_FILE_EDIT, m_mpich_filename_edit);
	DDX_Control(pDX, IDC_MPICH_FILE_BROWSE_BTN, m_mpich_file_browse_btn);
	DDX_Control(pDX, IDC_MPICH_ANL_BTN, m_mpich_anl_btn);
	DDX_Control(pDX, IDC_MPD_PORT_STATIC, m_mpd_port_static);
	DDX_Control(pDX, IDC_MPD_PORT_EDIT, m_mpd_port_edit);
	DDX_Control(pDX, IDC_MPD_PASSPHRASE_STATIC, m_mpd_pwd_static);
	DDX_Control(pDX, IDC_MPD_PASSPHRASE, m_mpd_pwd_edit);
	DDX_Control(pDX, IDC_RESULTS, m_results_edit);
	DDX_Control(pDX, IDC_UPDATE_STATIC, m_update_static);
	DDX_Control(pDX, IDC_UPDATE_ONE_STATIC, m_update_one_static);
	DDX_Control(pDX, IDC_UPDATE_ONE_BTN, m_update_one_btn);
	DDX_Control(pDX, IDC_UPDATE_BTN, m_update_btn);
	DDX_Control(pDX, IDC_SOURCE_STATIC, m_source_static);
	DDX_Control(pDX, IDC_URL_EDIT, m_url_edit);
	DDX_Control(pDX, IDC_FILE_EDIT, m_file_edit);
	DDX_Control(pDX, IDC_FILE_BROWSE_BTN, m_file_browse_btn);
	DDX_Control(pDX, IDC_ANL_BTN, m_anl_btn);
	DDX_Control(pDX, IDC_SHOW_HOST_CHK, m_show_host_chk);
	DDX_Control(pDX, IDOK, m_ok_btn);
	DDX_Control(pDX, IDCANCEL, m_cancel_btn);
	DDX_Control(pDX, IDC_EDIT_ADD_BTN, m_edit_add_btn);
    DDX_Control(pDX, IDC_HOST_LIST, m_host_list);
	DDX_Check(pDX, IDC_SHOW_HOST_CHK, m_bShowHostConfig);
	DDX_Text(pDX, IDC_FILE_EDIT, m_filename);
	DDX_Text(pDX, IDC_URL_EDIT, m_urlname);
	DDX_Text(pDX, IDC_HOSTNAME, m_hostname);
	DDX_Control(pDX, IDC_FILE_RADIO, m_file_radio);
	DDX_Control(pDX, IDC_URL_RADIO, m_url_radio);
	DDX_Text(pDX, IDC_CRED_ACCOUNT_EDIT, m_cred_account);
	DDX_Text(pDX, IDC_CRED_PWD_EDIT, m_cred_password);
	DDX_Check(pDX, IDC_FORCE_UPDATE_CHK, m_bForceUpdate);
	DDX_Text(pDX, IDC_RESULTS, m_results);
	DDX_Text(pDX, IDC_MPD_PASSPHRASE, m_mpd_pwd);
	DDX_Text(pDX, IDC_MPD_PORT_EDIT, m_mpd_port);
	DDV_MinMaxInt(pDX, m_mpd_port, 1, 65000);
	DDX_Text(pDX, IDC_MPICH_FILE_EDIT, m_mpich_filename);
	DDX_Text(pDX, IDC_MPICH_URL_EDIT, m_mpich_url);
	DDX_Check(pDX, IDC_UPDATE_MPD_CHECK, m_bUpdateMPD);
	DDX_Check(pDX, IDC_UPDATE_MPICH_CHECK, m_bUpdateMPICH);
	DDX_Check(pDX, IDC_MPD_PASSPHRASE_CHK, m_bMPDPassphraseChecked);
	DDX_Check(pDX, IDC_MPD_PORT_CHK, m_bMPDPortChecked);
	DDX_Text(pDX, IDC_MPD_VERSION_STATIC, m_mpd_version);
	DDX_Text(pDX, IDC_MPICH_VERSION_STATIC, m_mpich_version);
	DDX_Text(pDX, IDC_HOST_STATIC, m_config_host);
	DDX_Text(pDX, IDC_HOST_MPICH_VERSION_STATIC, m_config_mpich_version);
	DDX_Text(pDX, IDC_HOST_MPD_VERSION_STATIC, m_config_mpd_version);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CMPDUpdateDlg, CDialog)
//{{AFX_MSG_MAP(CMPDUpdateDlg)
ON_WM_PAINT()
ON_WM_QUERYDRAGICON()
ON_BN_CLICKED(IDC_UPDATE_BTN, OnUpdateBtn)
ON_BN_CLICKED(IDC_EDIT_ADD_BTN, OnEditAddBtn)
	ON_WM_VKEYTOITEM()
	ON_WM_CLOSE()
	ON_WM_SIZE()
	ON_BN_CLICKED(IDC_SHOW_HOST_CHK, OnShowHostChk)
	ON_LBN_SELCHANGE(IDC_HOST_LIST, OnSelchangeHostList)
	ON_BN_CLICKED(IDC_UPDATE_ONE_BTN, OnUpdateOneBtn)
	ON_BN_CLICKED(IDC_ANL_BTN, OnAnlBtn)
	ON_BN_CLICKED(IDC_FILE_BROWSE_BTN, OnFileBrowseBtn)
	ON_BN_CLICKED(IDC_URL_RADIO, OnURLRadio)
	ON_BN_CLICKED(IDC_FILE_RADIO, OnFileRadio)
	ON_BN_CLICKED(IDC_SELECT_HOSTS_BTN, OnSelectHostsBtn)
	ON_BN_CLICKED(IDC_UPDATE_MPICH_CHECK, OnUpdateMpichCheck)
	ON_BN_CLICKED(IDC_UPDATE_MPD_CHECK, OnUpdateMpdCheck)
	ON_BN_CLICKED(IDC_MPD_PORT_CHK, OnMpdPortChk)
	ON_BN_CLICKED(IDC_MPD_PASSPHRASE_CHK, OnMpdPassphraseChk)
	ON_BN_CLICKED(IDC_MPICH_URL_RADIO, OnMpichUrlRadio)
	ON_BN_CLICKED(IDC_MPICH_FILE_RADIO, OnMpichFileRadio)
	ON_BN_CLICKED(IDC_MPICH_FILE_BROWSE_BTN, OnMpichFileBrowseBtn)
	ON_BN_CLICKED(IDC_MPICH_ANL_BTN, OnMpichAnlBtn)
	ON_BN_CLICKED(IDC_MPICH_VERSION_BTN, OnMpichVersionBtn)
	ON_BN_CLICKED(IDC_MPD_VERSION_BTN, OnMpdVersionBtn)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMPDUpdateDlg message handlers

void CMPDUpdateDlg::ParseRegistry()
{
    HKEY tkey;
    DWORD result, len;
    char path[MAX_PATH];
    char mpich_path[MAX_PATH], *filename;
    DWORD length;
    
    // Set the defaults.
    m_mpd_port = MPD_DEFAULT_PORT;
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
    result = RegQueryValueEx(tkey, "port", 0, NULL, (unsigned char *)&m_mpd_port, &len);
    
    // Read the passphrase
    len = 100;
    result = RegQueryValueEx(tkey, "phrase", 0, NULL, (unsigned char *)m_pszPhrase, &len);
    if (result == ERROR_SUCCESS)
	m_bNeedPassword = false;
    
    // Read the port
    len = MAX_PATH;
    result = RegQueryValueEx(tkey, "path", 0, NULL, (unsigned char *)path, &len);
    if (result == ERROR_SUCCESS)
    {
	m_filename = path;
    }

    RegCloseKey(tkey);

    // Find the mpich.dll
    length = SearchPath(NULL, "mpich.dll", NULL, MAX_PATH, mpich_path, &filename);
    if (length > 0 && length < MAX_PATH)
    {
	// save the mpich.dll filename
	m_mpich_filename = mpich_path;
	// save the mpichd.dll filename
	m_mpich_filenamed = mpich_path;
	m_mpich_filenamed.TrimRight(".dll");
	m_mpich_filenamed += "d.dll";
    }
}

static bool bOnInitDialogFinished = false;

BOOL CMPDUpdateDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    
    SetIcon(m_hIcon, TRUE);  // Set big icon
    SetIcon(m_hIcon, FALSE); // Set small icon
    
    easy_socket_init();
    ParseRegistry();

    RECT r;
    GetClientRect(&r);
    m_nMinWidth = r.right;
    m_nMinHeight = r.bottom;

    rList.SetInitialPosition(m_host_list.m_hWnd, RSR_STRETCH_BOTTOM);
    rResults.SetInitialPosition(m_results_edit.m_hWnd, RSR_STRETCH);

    // mpd default - choose url
    m_source_static.EnableWindow();
    m_bUpdateMPD = TRUE;
    m_file_radio.SetCheck(0);
    m_url_radio.SetCheck(1);
    m_url_edit.EnableWindow();
    m_anl_btn.EnableWindow();
    m_file_edit.EnableWindow(FALSE);
    m_file_browse_btn.EnableWindow(FALSE);

    // mpich defaults - choose url
    m_mpich_source_static.EnableWindow();
    m_bUpdateMPICH = TRUE;
    m_mpich_filename_radio.SetCheck(0);
    m_mpich_url_radio.SetCheck(1);
    m_mpich_url_edit.EnableWindow();
    m_mpich_anl_btn.EnableWindow();
    m_mpich_filename_edit.EnableWindow(FALSE);
    m_mpich_file_browse_btn.EnableWindow(FALSE);

    // default - use default passphrase
    m_mpd_pwd_edit.EnableWindow(FALSE);
    m_mpd_port_edit.EnableWindow(FALSE);

    char host[100] = "";
    gethostname(host, 100);
    m_hostname = host;
    UpdateData(FALSE);

    bOnInitDialogFinished = true;
    return TRUE;  // return TRUE  unless you set the focus to a control
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CMPDUpdateDlg::OnPaint() 
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

HCURSOR CMPDUpdateDlg::OnQueryDragIcon()
{
    return (HCURSOR) m_hIcon;
}

void UpdateBtnThread(CMPDUpdateDlg *pDlg)
{
    int i;
    int num_hosts;
    char host[100];
    SOCKET sock;
    bool bDeleteTmpMpd = false;
    bool bDeleteTmpMPICH = false;
    bool bFailure = false;
    unsigned int version_new, version_old;
    CString results;

    num_hosts = pDlg->m_host_list.GetCount();
    if (num_hosts == 0)
    {
	CloseHandle(pDlg->m_hUpdateBtnThread);
	pDlg->m_hUpdateBtnThread = NULL;
	return;
    }
    
    if (pDlg->m_bNeedPassword)
    {
	if (pDlg->m_bUseDefault)
	    strcpy(pDlg->m_pszPhrase, MPD_DEFAULT_PASSPHRASE);
	else
	    strcpy(pDlg->m_pszPhrase, pDlg->m_mpd_pwd);
    }

    if (pDlg->m_bUpdateMPD)
    {
	results = "Updating mpd\r\n";
	pDlg->m_results += results;
	PostMessage(pDlg->m_hWnd, WM_USER + 5, 0, 0);

	if (pDlg->m_url_radio.GetCheck())
	{
	    if (!pDlg->GetTmpMpdFromURL())
	    {
		if (pDlg->m_bShowHostConfig)
		    pDlg->GetHostConfig(NULL);
		CloseHandle(pDlg->m_hUpdateBtnThread);
		pDlg->m_hUpdateBtnThread = NULL;
		return;
	    }
	    bDeleteTmpMpd = true;
	}
	
	if (!pDlg->m_bForceUpdate)
	{
	    if (!GetLocalVersion(pDlg->m_url_radio.GetCheck() ? pDlg->m_localfile : pDlg->m_filename, version_new))
	    {
		if (bDeleteTmpMpd)
		    DeleteFile(pDlg->m_localfile);
		MessageBox(NULL, "Unable to get the version of the new mpd", "Update aborted", MB_OK);
		CloseHandle(pDlg->m_hUpdateBtnThread);
		pDlg->m_hUpdateBtnThread = NULL;
		return;
	    }
	}
	
	// gray out the buttons
	PostMessage(pDlg->m_hWnd, WM_USER + 3, 0, 0);
	
	for (i=0; i<num_hosts; i++)
	{
	    if (pDlg->m_host_list.GetText(i, host) == LB_ERR)
		continue;
	    
	    if (ConnectToHost(host, pDlg->m_mpd_port, pDlg->m_pszPhrase, &sock))
	    {
		char pszError[256];
		char str[256], str2[256];
		
		if (!pDlg->m_bForceUpdate)
		{
		    WriteString(sock, "version");
		    ReadString(sock, str);
		}
		WriteString(sock, "done");
		easy_closesocket(sock);
		
		if (!pDlg->m_bForceUpdate)
		{
		    version_old = mpd_version_string_to_int(str);
		    mpd_version_int_to_string(version_old, str);
		    mpd_version_int_to_string(version_new, str2);
		}
		if (pDlg->m_bForceUpdate || version_old < version_new)
		{
		    if (UpdateMPD(host, pDlg->m_cred_account, pDlg->m_cred_password, pDlg->m_mpd_port, pDlg->m_pszPhrase, 
			bDeleteTmpMpd ? pDlg->m_localfile : pDlg->m_filename, pszError, 256))
		    {
			results.Format("%s: success\r\n", host);
			pDlg->m_results += results;
			PostMessage(pDlg->m_hWnd, WM_USER + 5, 0, 0);
		    }
		    else
		    {
			bFailure = true;
			results.Format("%s: mpd failure, %s\r\n", host, pszError);
			pDlg->m_results += results;
			PostMessage(pDlg->m_hWnd, WM_USER + 5, 0, 0);
			//MessageBox(NULL, pszError, "Unable to update mpd", MB_OK);
		    }
		}
		else
		{
		    CString s;
		    if (version_old == version_new)
		    {
			s.Format("MPD version <%s> is already installed on %s", str, host);
			results.Format("%s: no action, <%s> is already installed\r\n", host, str);
			pDlg->m_results += results;
			PostMessage(pDlg->m_hWnd, WM_USER + 5, 0, 0);
			//MessageBox(NULL, s, "Update aborted", MB_OK);
		    }
		    else
		    {
			s.Format("MPD version <%s> on %s is newer than version <%s>", str, host, str2);
			results.Format("%s: no action, <%s> is newer than <%s>\r\n", host, str, str2);
			pDlg->m_results += results;
			PostMessage(pDlg->m_hWnd, WM_USER + 5, 0, 0);
			//MessageBox(NULL, s, "Update aborted", MB_OK);
		    }
		}
	    }
	    else
	    {
		CString str;
		bFailure = true;
		str.Format("Connect to mpd on %s failed", host);
		results.Format("%s: failure, connect to mpd failed\r\n", host);
		pDlg->m_results += results;
		PostMessage(pDlg->m_hWnd, WM_USER + 5, 0, 0);
		//MessageBox(NULL, str, "Unable to update mpd", MB_OK);
	    }
	}
	
	PostMessage(pDlg->m_hWnd, WM_USER + 4, 0, 0);
	
	if (bDeleteTmpMpd)
	    DeleteFile(pDlg->m_localfile);
	
	if (pDlg->m_bShowHostConfig)
	{
	    PostMessage(pDlg->m_hWnd, WM_USER+2, 0, 0);
	}
    }

    if (pDlg->m_bUpdateMPICH)
    {
	results = "Updating mpich dlls\r\n";
	pDlg->m_results += results;
	PostMessage(pDlg->m_hWnd, WM_USER + 5, 0, 0);

	if (pDlg->m_mpich_url_radio.GetCheck())
	{
	    if (!pDlg->GetTmpMPICHFromURL())
	    {
		if (pDlg->m_bShowHostConfig)
		    pDlg->GetHostConfig(NULL);
		CloseHandle(pDlg->m_hUpdateBtnThread);
		pDlg->m_hUpdateBtnThread = NULL;
		PostMessage(pDlg->m_hWnd, WM_USER + 4, 0, 0); // re-enable the buttons
		return;
	    }
	    bDeleteTmpMPICH = true;
	}
	
	if (!pDlg->m_bForceUpdate)
	{
	    if (!GetLocalMPICHVersion(pDlg->m_mpich_url_radio.GetCheck() ? pDlg->m_mpich_localfile : pDlg->m_mpich_filename, version_new))
	    {
		if (bDeleteTmpMPICH)
		{
		    DeleteFile(pDlg->m_mpich_localfile);
		    DeleteFile(pDlg->m_mpich_localfiled);
		}
		MessageBox(NULL, "Unable to get the version of the new mpich dlls", "Update aborted", MB_OK);
		CloseHandle(pDlg->m_hUpdateBtnThread);
		pDlg->m_hUpdateBtnThread = NULL;
		PostMessage(pDlg->m_hWnd, WM_USER + 4, 0, 0); // re-enable the buttons
		return;
	    }
	}
	
	// gray out the buttons
	PostMessage(pDlg->m_hWnd, WM_USER + 3, 0, 0);
	
	for (i=0; i<num_hosts; i++)
	{
	    if (pDlg->m_host_list.GetText(i, host) == LB_ERR)
		continue;
	    
	    if (ConnectToHost(host, pDlg->m_mpd_port, pDlg->m_pszPhrase, &sock))
	    {
		char pszError[256];
		char str[256], str2[256];
		
		if (!pDlg->m_bForceUpdate)
		{
		    WriteString(sock, "mpich version");
		    if (!ReadStringTimeout(sock, str, MPD_SHORT_TIMEOUT))
		    {
			MessageBox(NULL, "MPD is unable to update the mpich dlls, please update mpd before attempting to update the mpich dlls", "Error", MB_OK);
			WriteString(sock, "done");
			easy_closesocket(sock);
			CloseHandle(pDlg->m_hUpdateBtnThread);
			pDlg->m_hUpdateBtnThread = NULL;
			PostMessage(pDlg->m_hWnd, WM_USER + 4, 0, 0); // re-enable the buttons
			return;
		    }
		}
		WriteString(sock, "done");
		easy_closesocket(sock);
		
		if (!pDlg->m_bForceUpdate)
		{
		    version_old = mpd_version_string_to_int(str);
		    mpd_version_int_to_string(version_old, str);
		    mpd_version_int_to_string(version_new, str2);
		}
		if (pDlg->m_bForceUpdate || version_old < version_new)
		{
		    if (UpdateMPICH(host, pDlg->m_cred_account, pDlg->m_cred_password, pDlg->m_mpd_port, pDlg->m_pszPhrase, 
			bDeleteTmpMPICH ? pDlg->m_mpich_localfile : pDlg->m_mpich_filename, 
			bDeleteTmpMPICH ? pDlg->m_mpich_localfiled : pDlg->m_mpich_filenamed, 
			pszError, 256))
		    {
			results.Format("%s: success\r\n", host);
			pDlg->m_results += results;
			PostMessage(pDlg->m_hWnd, WM_USER + 5, 0, 0);
		    }
		    else
		    {
			bFailure = true;
			results.Format("%s: mpich failure, %s\r\n", host, pszError);
			pDlg->m_results += results;
			PostMessage(pDlg->m_hWnd, WM_USER + 5, 0, 0);
			//MessageBox(NULL, pszError, "Unable to update mpd", MB_OK);
		    }
		}
		else
		{
		    CString s;
		    if (version_old == version_new)
		    {
			s.Format("MPICH version <%s> is already installed on %s", str, host);
			results.Format("%s: no action, mpich <%s> is already installed\r\n", host, str);
			pDlg->m_results += results;
			PostMessage(pDlg->m_hWnd, WM_USER + 5, 0, 0);
			//MessageBox(NULL, s, "Update aborted", MB_OK);
		    }
		    else
		    {
			s.Format("MPICH version <%s> on %s is newer than version <%s>", str, host, str2);
			results.Format("%s: no action, mpich <%s> is newer than <%s>\r\n", host, str, str2);
			pDlg->m_results += results;
			PostMessage(pDlg->m_hWnd, WM_USER + 5, 0, 0);
			//MessageBox(NULL, s, "Update aborted", MB_OK);
		    }
		}
	    }
	    else
	    {
		CString str;
		bFailure = true;
		str.Format("Connect to mpd on %s failed", host);
		results.Format("%s: failure, connect to mpd failed\r\n", host);
		pDlg->m_results += results;
		PostMessage(pDlg->m_hWnd, WM_USER + 5, 0, 0);
		//MessageBox(NULL, str, "Unable to update mpich", MB_OK);
	    }
	}
	
	PostMessage(pDlg->m_hWnd, WM_USER + 4, 0, 0); // re-enable the buttons
	
	if (bDeleteTmpMPICH)
	{
	    DeleteFile(pDlg->m_mpich_localfile);
	    DeleteFile(pDlg->m_mpich_localfiled);
	}
	
	if (pDlg->m_bShowHostConfig)
	{
	    PostMessage(pDlg->m_hWnd, WM_USER+2, 0, 0);
	}
    }

    CloseHandle(pDlg->m_hUpdateBtnThread);
    pDlg->m_hUpdateBtnThread = NULL;
}

void CMPDUpdateDlg::OnUpdateBtn()
{
    DWORD dwThreadID;

    UpdateData();
    m_results = "";
    UpdateData(FALSE);

    if (m_host_list.GetCount() < 1)
    {
	MessageBox("Please add hosts to the list before selecting Update");
	return;
    }

    if (!m_bUpdateMPD && !m_bUpdateMPICH)
    {
	MessageBox("Please check at least one of the update boxes before selecting Update");
	return;
    }

    if (m_bNeedPassword)
    {
	if (m_bUseDefault)
	    strcpy(m_pszPhrase, MPD_DEFAULT_PASSPHRASE);
	else
	    strcpy(m_pszPhrase, m_mpd_pwd);
    }

    if (m_cred_account.GetLength() < 1)
    {
	MessageBox("Please enter the account information before selecting Update");
	m_cred_account_edit.SetFocus();
	return;
    }

    m_hUpdateBtnThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)UpdateBtnThread, this, 0, &dwThreadID);
}

#define BUFSIZE 1024*1024

bool CMPDUpdateDlg::GetTmpMpdFromURL()
{
    char *buffer;
    DWORD num_read;
    char path[MAX_PATH];
    CInternetSession session("MPDUpdate");
    CStdioFile *fin, fout;
    CString str;
    static bool bFirst = true;
    
    if (GetTempPath(MAX_PATH, path))
    {
	if (GetTempFileName(path, "mpd", 0, m_localfile.GetBuffer(MAX_PATH)))
	{
	    m_localfile.ReleaseBuffer();
	    buffer = new char[BUFSIZE];

	    // open session
	    fin = NULL;
	    try {
	    if (bFirst)
	    {
		fin = session.OpenURL(m_urlname, 1, INTERNET_FLAG_TRANSFER_BINARY | INTERNET_FLAG_RELOAD);
		bFirst = false;
	    }
	    else
		fin = session.OpenURL(m_urlname, 1, INTERNET_FLAG_TRANSFER_BINARY);
	    } catch(...)
	    {
		if (fin == NULL)
		{
		    str.Format("OpenURL(%s) failed, error %d", m_urlname, GetLastError());
		    MessageBox(str, "Error");
		    delete buffer;
		    return false;
		}
		str.Format("OpenURL(%s) failed", m_urlname);
		MessageBox(str, "Error");
		delete buffer;
		return false;
	    }

	    // open local file
	    if (!fout.Open(m_localfile, CFile::modeWrite | CFile::typeBinary))
	    {
		str.Format("Open(%s) failed, error %d", m_localfile, GetLastError());
		fin->Close();
		MessageBox(str, "Error");
		delete buffer;
		return false;
	    }

	    while (true)
	    {
		num_read = fin->Read(buffer, BUFSIZE);
		if (num_read > 0)
		{
		    try {
		    fout.Write(buffer, num_read);
		    } catch(...)
		    {
			str.Format("Write failed, error %d", m_localfile, GetLastError());
			fin->Close();
			MessageBox(str, "Error");
			delete buffer;
			return false;
		    }
		}
		else
		{
		    if (num_read == 0)
		    {
			fin->Close();
			fout.Close();
			delete buffer;
			//MessageBox(m_localfile, "File retrieved");
			return true;
		    }
		    str.Format("Read failed, error %d", GetLastError());
		    MessageBox(str, "Error");
		    fin->Close();
		    CString fname = fout.GetFilePath();
		    fout.Close();
		    try {
		    CFile::Remove(fname);
		    } catch(...) {}
		    delete buffer;
		    return false;
		}
	    }
	}
	str.Format("GetTempFileName failed, error %d", GetLastError());
	MessageBox(str, "Error");
	return false;
    }
    str.Format("GetTempPath failed, error %d", GetLastError());
    MessageBox(str, "Error");
    return false;
}

bool CMPDUpdateDlg::GetTmpMPICHFromURL()
{
    char *buffer;
    DWORD num_read;
    char path[MAX_PATH];
    CInternetSession session("MPDUpdate");
    CStdioFile *fin, fout;
    CString str;
    static bool bFirst = true;
    static bool bFirstd = true;

    // Create two urls for mpich.dll and mpichd.dll
    CString str_url, str_urld;
    str_url = m_mpich_url;
    if (str_url.Find("mpich.dll") == -1)
    {
	str_url.TrimRight("/\\");
	str_urld = str_url + "/mpichd.dll";
	str_url += "/mpich.dll";
    }
    else
    {
	str_urld = str_url;
	str_urld.Replace("mpich.dll", NULL);
	str_urld.TrimRight();
	str_urld.TrimRight("/\\");
	str_urld += "/mpichd.dll";
    }

    // Get mpich.dll
    if (GetTempPath(MAX_PATH, path))
    {
	if (GetTempFileName(path, "mpich", 0, m_mpich_localfile.GetBuffer(MAX_PATH)))
	{
	    m_mpich_localfile.ReleaseBuffer();
	    buffer = new char[BUFSIZE];

	    // open session
	    fin = NULL;
	    try {
	    if (bFirst)
	    {
		fin = session.OpenURL(str_url, 1, INTERNET_FLAG_TRANSFER_BINARY | INTERNET_FLAG_RELOAD);
		bFirst = false;
	    }
	    else
		fin = session.OpenURL(str_url, 1, INTERNET_FLAG_TRANSFER_BINARY);
	    } catch(...)
	    {
		if (fin == NULL)
		{
		    str.Format("OpenURL(%s) failed, error %d", str_url, GetLastError());
		    MessageBox(str, "Error");
		    delete buffer;
		    return false;
		}
		str.Format("OpenURL(%s) failed", str_url);
		MessageBox(str, "Error");
		delete buffer;
		return false;
	    }

	    // open local file
	    if (!fout.Open(m_mpich_localfile, CFile::modeWrite | CFile::typeBinary))
	    {
		str.Format("Open(%s) failed, error %d", m_mpich_localfile, GetLastError());
		fin->Close();
		MessageBox(str, "Error");
		delete buffer;
		return false;
	    }

	    while (true)
	    {
		num_read = fin->Read(buffer, BUFSIZE);
		if (num_read > 0)
		{
		    try {
		    fout.Write(buffer, num_read);
		    } catch(...)
		    {
			str.Format("Write failed, error %d", m_mpich_localfile, GetLastError());
			fin->Close();
			MessageBox(str, "Error");
			delete buffer;
			return false;
		    }
		}
		else
		{
		    if (num_read == 0)
		    {
			fin->Close();
			fout.Close();
			delete buffer;
			//MessageBox(m_localfile, "File retrieved");
			//return true;
			break;
		    }
		    str.Format("Read failed, error %d", GetLastError());
		    MessageBox(str, "Error");
		    fin->Close();
		    CString fname = fout.GetFilePath();
		    fout.Close();
		    try {
		    CFile::Remove(fname);
		    } catch(...) {}
		    delete buffer;
		    return false;
		}
	    }
	}
	else
	{
	    str.Format("GetTempFileName failed, error %d", GetLastError());
	    MessageBox(str, "Error");
	    return false;
	}
    }
    else
    {
	str.Format("GetTempPath failed, error %d", GetLastError());
	MessageBox(str, "Error");
	return false;
    }

    // Get mpichd.dll
    if (GetTempPath(MAX_PATH, path))
    {
	if (GetTempFileName(path, "mpich", 0, m_mpich_localfiled.GetBuffer(MAX_PATH)))
	{
	    m_mpich_localfiled.ReleaseBuffer();
	    buffer = new char[BUFSIZE];

	    // open session
	    fin = NULL;
	    try {
	    if (bFirstd)
	    {
		fin = session.OpenURL(str_urld, 1, INTERNET_FLAG_TRANSFER_BINARY | INTERNET_FLAG_RELOAD);
		bFirstd = false;
	    }
	    else
		fin = session.OpenURL(str_urld, 1, INTERNET_FLAG_TRANSFER_BINARY);
	    } catch(...)
	    {
		if (fin == NULL)
		{
		    str.Format("OpenURL(%s) failed, error %d", str_urld, GetLastError());
		    MessageBox(str, "Error");
		    delete buffer;
		    return false;
		}
		str.Format("OpenURL(%s) failed", str_urld);
		MessageBox(str, "Error");
		delete buffer;
		return false;
	    }

	    // open local file
	    if (!fout.Open(m_mpich_localfiled, CFile::modeWrite | CFile::typeBinary))
	    {
		str.Format("Open(%s) failed, error %d", m_mpich_localfiled, GetLastError());
		fin->Close();
		MessageBox(str, "Error");
		delete buffer;
		return false;
	    }

	    while (true)
	    {
		num_read = fin->Read(buffer, BUFSIZE);
		if (num_read > 0)
		{
		    try {
		    fout.Write(buffer, num_read);
		    } catch(...)
		    {
			str.Format("Write failed, error %d", m_mpich_localfiled, GetLastError());
			fin->Close();
			MessageBox(str, "Error");
			delete buffer;
			return false;
		    }
		}
		else
		{
		    if (num_read == 0)
		    {
			fin->Close();
			fout.Close();
			delete buffer;
			//MessageBox(m_localfile, "File retrieved");
			return true;
		    }
		    str.Format("Read failed, error %d", GetLastError());
		    MessageBox(str, "Error");
		    fin->Close();
		    CString fname = fout.GetFilePath();
		    fout.Close();
		    try {
		    CFile::Remove(fname);
		    } catch(...) {}
		    delete buffer;
		    return false;
		}
	    }
	}
	str.Format("GetTempFileName failed, error %d", GetLastError());
	MessageBox(str, "Error");
	return false;
    }

    str.Format("GetTempPath failed, error %d", GetLastError());
    MessageBox(str, "Error");
    return false;
}

/*
void CMPDUpdateDlg::GetTmpMpdFromFtp()
{
    char path[MAX_PATH];
    CInternetSession session;
    CFtpSession *pFtp;
    
    if (GetTempPath(MAX_PATH, path))
    {
	if (GetTempFileName(path, "mpd", 0, m_localfile.GetBuffer(MAX_PATH)))
	{
	    m_localfile.ReleaseBuffer();
	    try {
		pFtp = session.GetFtpConnection("ftp.mcs.anl.gov");
		pFtp->GetFile("/pub/mpi/nt/binaries/mpd.exe");
		MessageBox(m_localfile, "File retrieved");
	    } catch (...)
	}
    }
}
*/

bool GetLocalMPICHVersion(const char *filename, unsigned int &version)
{
    void (*pGetMPICHVersion)(char *str, int length);
    HMODULE hModule;
    char err_str[256];
    char err_msg[1024];
    char version_str[100];

    hModule = LoadLibrary(filename);

    if (hModule == NULL)
    {
	Translate_Error(GetLastError(), err_str, NULL);
	sprintf(err_msg, "LoadLibrary(%s) failed, %s\n", filename, err_str);
	MessageBox(NULL, err_msg, "Error: GetMPICHVersion failed", MB_OK);
	return false;
    }

    pGetMPICHVersion = (void (*)(char *, int))GetProcAddress(hModule, "GetMPICHVersion");

    if (pGetMPICHVersion == NULL)
    {
	int error = GetLastError();
	FreeLibrary(hModule);
	if (error == ERROR_PROC_NOT_FOUND)
	{
	    version = 0;
	    return true;
	}
	Translate_Error(error, err_msg, "GetProcAddress(\"GetMPICHVersion\") failed, ");
	MessageBox(NULL, err_msg, "Error: GetMPICHVersion failed", MB_OK);
	return false;
    }

    pGetMPICHVersion(version_str, 100);
    version = mpd_version_string_to_int(version_str);

    FreeLibrary(hModule);
    return true;
}

bool GetLocalVersionFromRun(const char *filename, unsigned int &version)
//bool GetLocalVersion(const char *filename, unsigned int &version)
{
    char line[1024];
    HANDLE hIn, hOut, hErr;
    char err_str[256];
    char err_msg[256];
    int error;
    int pid;
    DWORD num_read;
    HANDLE hProcess;

    if (strlen(filename) > 1023)
	return false;

    sprintf(line, "%s -version", filename);
    
    hProcess = LaunchProcess(line, NULL, NULL, &hIn, &hOut, &hErr, &pid, &error, err_str);
    if (hProcess == INVALID_HANDLE_VALUE)
    {
	sprintf(err_msg, "%s%s", err_str, strerror(error));
	MessageBox(NULL, err_msg, "Unable to launch the new mpd", MB_OK);
	return false;
    }
    line[0] = '\0';

    while (true)
    {
	if (!ReadFile(hErr, line, 1024, &num_read, NULL))
	{
	    sprintf(err_str, "error %d", GetLastError());
	    TerminateProcess(hProcess, -1);
	    break;
	}
	if (num_read > 5)
	    break;
    }
    CloseHandle(hIn);
    CloseHandle(hOut);
    CloseHandle(hErr);
    if (WaitForSingleObject(hProcess, 1000) == WAIT_TIMEOUT)
	TerminateProcess(hProcess, -1);
    CloseHandle(hProcess);

    version = mpd_version_string_to_int(line);
    return (version != 0);
}

/*
bool GetLocalVersion(const char *filename, unsigned int &version)
{
    void (*pGetMPDVersion)(char *str, int length);
    PROCESS_INFORMATION pInfo;
    STARTUPINFO sInfo;
    int error;
    char err_msg[1024];
    char version_str[100];

    GetStartupInfo(&sInfo);

    if (!CreateProcess(filename, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &sInfo, &pInfo))
	return GetLocalVersionFromRun(filename, version);

    OpenProcess(all, no, id);
    pGetMPDVersion = (void (*)(char *, int))GetProcAddress((HINSTANCE)pInfo.hProcess, "GetMPDVersion");

    if (pGetMPDVersion == NULL)
    {
	error = GetLastError();
	// try to get the version by running "mpd -version"
	if (!GetLocalVersionFromRun(filename, version))
	{
	    Translate_Error(error, err_msg, "GetProcAddress(\"GetMPDVersion\") failed, ");
	    MessageBox(NULL, err_msg, "Error in GetLocalVersion", MB_OK);
	    version = 0;
	    return false;
	}
    }
    else
    {
	pGetMPDVersion(version_str, 100);
	//printf("%s\n", version);
	version = mpd_version_string_to_int(version_str);
    }

    TerminateProcess(pInfo.hProcess, 0);
    CloseHandle(pInfo.hProcess);
    CloseHandle(pInfo.hThread);

    return true;
}
*/
/*
bool GetLocalVersion(const char *filename, unsigned int &version)
{
    void (*pGetMPDVersion)(char *str, int length);
    HMODULE hModule;
    char err_msg[1024];
    int error;
    char version_str[100] = "";

    hModule = LoadLibrary(filename);

    if (hModule == NULL)
    {
	Translate_Error(GetLastError(), err_msg, "LoadLibrary(mpd.exe) failed, ");
	MessageBox(NULL, err_msg, "Error in GetLocalVersion", MB_OK);
	version = 0;
	return false;
    }

    pGetMPDVersion = (void (*)(char *, int))GetProcAddress(hModule, "GetMPDVersion");

    if (pGetMPDVersion == NULL)
    {
	error = GetLastError();
	FreeLibrary(hModule);
	// try to get the version by running "mpd -version"
	if (!GetLocalVersionFromRun(filename, version))
	{
	    Translate_Error(error, err_msg, "GetProcAddress(\"GetMPDVersion\") failed, ");
	    MessageBox(NULL, err_msg, "Error in GetLocalVersion", MB_OK);
	    version = 0;
	    return false;
	}
    }
    else
    {
	pGetMPDVersion(version_str, 100);
	//printf("%s\n", version);
	FreeLibrary(hModule);
	version = mpd_version_string_to_int(version_str);
    }

    return true;
}
*/
/*
bool GetLocalVersion(const char *filename, unsigned int &version)
{
    void (*pGetMPDVersionFoo)(int (* function)(char *, size_t, const char *, ...), char *str, int length);
    HMODULE hModule;
    char err_msg[1024];
    int error;
    char version_str[100] = "";

    hModule = LoadLibrary(filename);

    if (hModule == NULL)
    {
	Translate_Error(GetLastError(), err_msg, "LoadLibrary(mpd.exe) failed, ");
	MessageBox(NULL, err_msg, "Error in GetLocalVersion", MB_OK);
	version = 0;
	return false;
    }

    pGetMPDVersionFoo = (void (*)(int (*)(char *, size_t, const char *, ...), char *, int))GetProcAddress(hModule, "GetMPDVersionFoo");

    if (pGetMPDVersionFoo == NULL)
    {
	error = GetLastError();
	FreeLibrary(hModule);
	// try to get the version by running "mpd -version"
	if (!GetLocalVersionFromRun(filename, version))
	{
	    Translate_Error(error, err_msg, "GetProcAddress(\"GetMPDVersionFoo\") failed, ");
	    MessageBox(NULL, err_msg, "Error in GetLocalVersion", MB_OK);
	    version = 0;
	    return false;
	}
    }
    else
    {
	pGetMPDVersionFoo(_snprintf, version_str, 100);
	//printf("%s\n", version);
	FreeLibrary(hModule);
	version = mpd_version_string_to_int(version_str);
    }

    return true;
}
*/
bool GetLocalVersion(const char *filename, unsigned int &version)
{
    int *pRelease;
    int *pMajor;
    int *pMinor;
    char *pDate;
    HMODULE hModule;
    char err_msg[1024];
    int error;
    char version_str[100] = "";

    hModule = LoadLibrary(filename);

    if (hModule == NULL)
    {
	Translate_Error(GetLastError(), err_msg, "LoadLibrary(mpd.exe) failed, ");
	MessageBox(NULL, err_msg, "Error in GetLocalVersion", MB_OK);
	version = 0;
	return false;
    }

    pRelease = (int*)GetProcAddress(hModule, "mpdVersionRelease");
    pMajor = (int*)GetProcAddress(hModule, "mpdVersionMajor");
    pMinor = (int*)GetProcAddress(hModule, "mpdVersionMinor");
    pDate = (char*)GetProcAddress(hModule, "mpdVersionDate");

    if (pRelease == NULL || pMajor == NULL || pMinor == NULL || pDate == NULL)
    {
	error = GetLastError();
	FreeLibrary(hModule);
	// try to get the version by running "mpd -version"
	if (!GetLocalVersionFromRun(filename, version))
	{
	    Translate_Error(error, err_msg, "GetProcAddress(\"mpdVersion...\") failed, ");
	    MessageBox(NULL, err_msg, "Error in GetLocalVersion", MB_OK);
	    version = 0;
	    return false;
	}
    }
    else
    {
	_snprintf(version_str, 100, "%d.%d.%d %s", *pRelease, *pMajor, *pMinor, pDate);
	//printf("%s\n", version);
	FreeLibrary(hModule);
	version = mpd_version_string_to_int(version_str);
    }

    return true;
}

void CMPDUpdateDlg::OnUpdateOneBtn() 
{
    SOCKET sock;
    CString sHost;
    int num_hosts;
    bool bDeleteTmpMpd = false;
    bool bDeleteTmpMPICH = false;
    unsigned int version_new, version_old;

    UpdateData();
    m_results = "";
    UpdateData(FALSE);

    num_hosts = m_host_list.GetCount();
    if (num_hosts == 0)
    {
	MessageBox("Please add a host to the list before selecting Update");
	return;
    }

    int index = m_host_list.GetCurSel();
    if (index == LB_ERR)
    {
	MessageBox("Please select a host from the list before selecting Update single");
	return;
    }
    m_host_list.GetText(index, sHost);

    if (!m_bUpdateMPD && !m_bUpdateMPICH)
    {
	MessageBox("Please check at least one of the update boxes before selecting Update");
	return;
    }

    if (m_bNeedPassword)
    {
	if (m_bUseDefault)
	    strcpy(m_pszPhrase, MPD_DEFAULT_PASSPHRASE);
	else
	    strcpy(m_pszPhrase, m_mpd_pwd);
    }

    if (m_cred_account.GetLength() < 1)
    {
	MessageBox("Please enter the account information before selecting Update");
	m_cred_account_edit.SetFocus();
	return;
    }

    HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );
    
    if (m_bUpdateMPD)
    {
	if (m_url_radio.GetCheck())
	{
	    if (!GetTmpMpdFromURL())
	    {
		SetCursor(hOldCursor);
		if (m_bShowHostConfig)
		    GetHostConfig(NULL);
		m_results.Format("%s: failure", sHost);
		UpdateData(FALSE);
		return;
	    }
	    bDeleteTmpMpd = true;
	}
	
	if (!m_bForceUpdate)
	{
	    if (!GetLocalVersion(m_url_radio.GetCheck() ? m_localfile : m_filename, version_new))
	    {
		if (bDeleteTmpMpd)
		    DeleteFile(m_localfile);
		SetCursor(hOldCursor);
		m_results.Format("%s: failure", sHost);
		UpdateData(FALSE);
		MessageBox("Unable to get the version of the new mpd", "Update aborted");
		return;
	    }
	}
	
	if (ConnectToHost(sHost, m_mpd_port, m_pszPhrase, &sock))
	{
	    char pszError[256];
	    char str[256], str2[256];
	    
	    if (!m_bForceUpdate)
	    {
		WriteString(sock, "version");
		ReadString(sock, str);
	    }
	    WriteString(sock, "done");
	    easy_closesocket(sock);
	    
	    if (!m_bForceUpdate)
	    {
		version_old = mpd_version_string_to_int(str);
		mpd_version_int_to_string(version_old, str);
		mpd_version_int_to_string(version_new, str2);
	    }
	    if (m_bForceUpdate || (version_old < version_new))
	    {
		if (UpdateMPD(sHost, m_cred_account, m_cred_password, m_mpd_port, m_pszPhrase, 
		    m_url_radio.GetCheck() ? m_localfile : m_filename, pszError, 256))
		{
		    m_results.Format("%s: success", sHost);
		    UpdateData(FALSE);
		}
		else
		{
		    m_results.Format("%s: failure, %s", sHost, pszError);
		    UpdateData(FALSE);
		    MessageBox(pszError, "Unable to update mpd");
		}
	    }
	    else
	    {
		CString s;
		if (version_old == version_new)
		{
		    s.Format("Version <%s> is already installed on %s", str, sHost);
		    MessageBox(s, "Update aborted");
		}
		else
		{
		    s.Format("Version <%s> on %s is newer than version <%s>", str, sHost, str2);
		    MessageBox(s, "Update aborted");
		}
		m_results.Format("%s: no action\r\n%s", sHost, s);
		UpdateData(FALSE);
	    }
	}
	else
	{
	    CString str;
	    str.Format("Connect to mpd on %s failed", sHost);
	    m_results.Format("%s: failure", sHost);
	    UpdateData(FALSE);
	    MessageBox(str, "Unable to update mpd");
	}
	
	if (bDeleteTmpMpd)
	{
	    DeleteFile(m_localfile);
	}
    }

    if (m_bUpdateMPICH)
    {
	if (m_mpich_url_radio.GetCheck())
	{
	    if (!GetTmpMPICHFromURL())
	    {
		SetCursor(hOldCursor);
		if (m_bShowHostConfig)
		    GetHostConfig(NULL);
		m_results.Format("%s: failure", sHost);
		UpdateData(FALSE);
		return;
	    }
	    bDeleteTmpMPICH = true;
	}
	
	if (!m_bForceUpdate)
	{
	    if (!GetLocalMPICHVersion(m_mpich_url_radio.GetCheck() ? m_mpich_localfile : m_mpich_filename, version_new))
	    {
		if (bDeleteTmpMPICH)
		{
		    DeleteFile(m_mpich_localfile);
		    DeleteFile(m_mpich_localfiled);
		}
		SetCursor(hOldCursor);
		m_results.Format("%s: failure", sHost);
		UpdateData(FALSE);
		MessageBox("Unable to get the version of the new mpich dlls", "Update aborted");
		return;
	    }
	}
	
	if (ConnectToHost(sHost, m_mpd_port, m_pszPhrase, &sock))
	{
	    char pszError[256];
	    char str[256] = "", str2[256];
	    
	    if (!m_bForceUpdate)
	    {
		WriteString(sock, "mpich version");
		if (!ReadStringTimeout(sock, str, MPD_SHORT_TIMEOUT))
		{
		    MessageBox("MPD is unable to update the mpich dlls, please update mpd before attempting to update the mpich dlls", "Error");
		    WriteString(sock, "done");
		    easy_closesocket(sock);
		    return;
		}
	    }
	    WriteString(sock, "done");
	    easy_closesocket(sock);
	    
	    if (!m_bForceUpdate)
	    {
		version_old = mpd_version_string_to_int(str);
		mpd_version_int_to_string(version_old, str);
		mpd_version_int_to_string(version_new, str2);
	    }
	    if (m_bForceUpdate || (version_old < version_new))
	    {
		if (UpdateMPICH(sHost, m_cred_account, m_cred_password, m_mpd_port, m_pszPhrase, 
		    m_mpich_url_radio.GetCheck() ? m_mpich_localfile : m_mpich_filename, 
		    m_mpich_url_radio.GetCheck() ? m_mpich_localfiled : m_mpich_filenamed, 
		    pszError, 256))
		{
		    m_results.Format("%s: success", sHost);
		    UpdateData(FALSE);
		}
		else
		{
		    m_results.Format("%s: failure, %s", sHost, pszError);
		    UpdateData(FALSE);
		    MessageBox(pszError, "Unable to update the mpich dlls");
		}
	    }
	    else
	    {
		CString s;
		if (version_old == version_new)
		{
		    s.Format("Version <%s> is already installed on %s", str, sHost);
		    MessageBox(s, "Update aborted");
		}
		else
		{
		    s.Format("Version <%s> on %s is newer than version <%s>", str, sHost, str2);
		    MessageBox(s, "Update aborted");
		}
		m_results.Format("%s: no action\r\n%s", sHost, s);
		UpdateData(FALSE);
	    }
	}
	else
	{
	    CString str;
	    str.Format("Connect to mpd on %s failed", sHost);
	    m_results.Format("%s: failure", sHost);
	    UpdateData(FALSE);
	    MessageBox(str, "Unable to update the mpich dlls");
	}
	
	if (bDeleteTmpMPICH)
	{
	    DeleteFile(m_mpich_localfile);
	    DeleteFile(m_mpich_localfiled);
	}
    }

    SetCursor(hOldCursor);

    if (m_bShowHostConfig)
	GetHostConfig(NULL);
}

LRESULT CMPDUpdateDlg::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) 
{
    if (message == WM_USER + 2)
    {
	GetHostConfig(NULL);
    }
    if (message == WM_USER + 3)
    {
	m_update_btn.EnableWindow(FALSE);
	m_update_one_btn.EnableWindow(FALSE);
	m_edit_add_btn.EnableWindow(FALSE);
	m_select_btn.EnableWindow(FALSE);
    }
    if (message == WM_USER + 4)
    {
	m_update_btn.EnableWindow();
	m_update_one_btn.EnableWindow();
	m_edit_add_btn.EnableWindow();
	m_select_btn.EnableWindow();
    }
    if (message == WM_USER + 5)
    {
	UpdateData(FALSE);
    }
    return CDialog::WindowProc(message, wParam, lParam);
}

void CMPDUpdateDlg::OnEditAddBtn() 
{
    // Add hostname to host list
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

int CMPDUpdateDlg::OnVKeyToItem(UINT nKey, CListBox* pListBox, UINT nIndex) 
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
	    }
	}
    }
	return CDialog::OnVKeyToItem(nKey, pListBox, nIndex);
}

void CMPDUpdateDlg::OnClose() 
{
    if (m_hUpdateBtnThread)
    {
	TerminateThread(m_hUpdateBtnThread, -1);
	m_hUpdateBtnThread = NULL;
    }
    easy_socket_finalize();
	CDialog::OnClose();
}

void CMPDUpdateDlg::OnSize(UINT nType, int cx, int cy) 
{
    CDialog::OnSize(nType, cx, cy);

    if (nType != SIZE_MINIMIZED)
    {
	/*
	if (m_nMinWidth != -1)
	{
	    if (cx < m_nMinWidth || cy < m_nMinHeight)
	    {
		RECT r, r2;
		r.left = 0;
		r.top = 0;
		r.right = m_nMinWidth;
		r.bottom = m_nMinHeight;
		AdjustWindowRect(&r, WS_CHILD, FALSE);
		GetWindowRect(&r2);
		MoveWindow(r2.left, r2.top, r.right-r.left, r.bottom-r.top, TRUE);
	    }
	}
	*/

	if (m_nMinWidth <= cx || m_nMinHeight <= cy)
	{
	    if (cx < m_nMinWidth)
		cx = m_nMinWidth;
	    if (cy < m_nMinHeight)
		cy = m_nMinHeight;

	    rList.Resize(cx, cy);
	    rResults.Resize(cx, cy);

	    if (bOnInitDialogFinished)
		Invalidate();
	}
    }
}

void CMPDUpdateDlg::OnShowHostChk() 
{
    UpdateData();

    if (m_bShowHostConfig)
	GetHostConfig(NULL);
    else
    {
	m_config_host = "";
	m_config_mpich_version = "";
	m_config_mpd_version = "";
	UpdateData(FALSE);
    }
}

void CMPDUpdateDlg::OnSelchangeHostList() 
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

void CMPDUpdateDlg::GetHostConfig(const char *host)
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
	    strcpy(m_pszPhrase, m_mpd_pwd);
    }
    
    HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );
    
    if (ConnectToMPDquickReport(sHost, m_mpd_port, m_pszPhrase, &sock, pszStr) == 0)
    {
	// get the mpd version
	WriteString(sock, "version");
	if (ReadStringTimeout(sock, pszStr, MPD_SHORT_TIMEOUT))
	{
	    m_config_mpd_version = "mpd:\r\n";
	    m_config_mpd_version += pszStr;
	}
	else
	{
	    m_config_mpd_version = "mpd:\r\nunknown version";
	}

	// get the mpich version
	WriteString(sock, "mpich version");
	if (ReadStringTimeout(sock, pszStr, MPD_SHORT_TIMEOUT))
	{
	    m_config_mpich_version = "mpich:\r\n";
	    m_config_mpich_version += pszStr;
	}
	else
	{
	    m_config_mpich_version = "mpich:\r\nunknown version";
	}

	m_config_host = sHost;
	WriteString(sock, "done");
	easy_closesocket(sock);
    }
    else
    {
	if (strstr(pszStr, "10061"))
	    m_config_mpich_version = "mpd not installed";
	else if (strstr(pszStr, "11001"))
	    m_config_mpich_version = "unknown host";
	else
	    m_config_mpich_version = pszStr;
	m_config_mpd_version = "";
	m_config_host = sHost;
    }
    
    SetCursor(hOldCursor);
    UpdateData(FALSE);
}

void CMPDUpdateDlg::OnAnlBtn() 
{
    UpdateData();
    m_urlname = "ftp://ftp.mcs.anl.gov/pub/mpi/nt/binaries/mpd.exe";
    UpdateData(FALSE);
}

void CMPDUpdateDlg::OnFileBrowseBtn() 
{
    UpdateData();
    
    CFileDialog f(
	TRUE, "*.exe", m_filename, 
	OFN_HIDEREADONLY | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST,
	"Executables (*.exe)|*.exe|All files (*.*)|*.*||"
	);
    if (f.DoModal() == IDOK)
    {
	POSITION p;
	p = f.GetStartPosition();
	m_filename = f.GetNextPathName(p);
	UpdateData(FALSE);
    }
}

void CMPDUpdateDlg::OnURLRadio() 
{
    UpdateData();
    
    if (m_url_radio.GetCheck())
    {
	m_url_edit.EnableWindow();
	m_anl_btn.EnableWindow();
	m_file_edit.EnableWindow(FALSE);
	m_file_browse_btn.EnableWindow(FALSE);
    }
    else
    {
	m_url_edit.EnableWindow(FALSE);
	m_anl_btn.EnableWindow(FALSE);
	m_file_edit.EnableWindow();
	m_file_browse_btn.EnableWindow();
    }
}

void CMPDUpdateDlg::OnFileRadio() 
{
    UpdateData();
    
    if (m_file_radio.GetCheck())
    {
	m_file_edit.EnableWindow();
	m_file_browse_btn.EnableWindow();
	m_url_edit.EnableWindow(FALSE);
	m_anl_btn.EnableWindow(FALSE);
    }
    else
    {
	m_file_edit.EnableWindow(FALSE);
	m_file_browse_btn.EnableWindow(FALSE);
	m_url_edit.EnableWindow();
	m_anl_btn.EnableWindow();
    }
}

void CMPDUpdateDlg::OnSelectHostsBtn() 
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

void CMPDUpdateDlg::OnUpdateMpichCheck() 
{
    UpdateData();
    if (m_bUpdateMPICH)
    {
	m_mpich_source_static.EnableWindow();
	m_mpich_filename_radio.EnableWindow();
	m_mpich_url_radio.EnableWindow();
	m_mpich_version_btn.EnableWindow();
	OnMpichUrlRadio();
    }
    else
    {
	m_mpich_source_static.EnableWindow(FALSE);
	m_mpich_filename_radio.EnableWindow(FALSE);
	m_mpich_url_radio.EnableWindow(FALSE);
	m_mpich_url_edit.EnableWindow(FALSE);
	m_mpich_anl_btn.EnableWindow(FALSE);
	m_mpich_filename_edit.EnableWindow(FALSE);
	m_mpich_file_browse_btn.EnableWindow(FALSE);
	m_mpich_version_btn.EnableWindow(FALSE);
    }
}

void CMPDUpdateDlg::OnUpdateMpdCheck() 
{
    UpdateData();
    if (m_bUpdateMPD)
    {
	m_source_static.EnableWindow();
	m_file_radio.EnableWindow();
	m_url_radio.EnableWindow();
	m_mpd_version_btn.EnableWindow();
	OnURLRadio();
    }
    else
    {
	m_source_static.EnableWindow(FALSE);
	m_file_radio.EnableWindow(FALSE);
	m_url_radio.EnableWindow(FALSE);
	m_url_edit.EnableWindow(FALSE);
	m_anl_btn.EnableWindow(FALSE);
	m_file_edit.EnableWindow(FALSE);
	m_file_browse_btn.EnableWindow(FALSE);
	m_mpd_version_btn.EnableWindow(FALSE);
    }
}

void CMPDUpdateDlg::OnMpdPortChk() 
{
    UpdateData();
    if (m_bMPDPortChecked)
    {
	m_mpd_port_edit.EnableWindow();
	m_mpd_port_static.EnableWindow(FALSE);
    }
    else
    {
	m_mpd_port_edit.EnableWindow(FALSE);
	m_mpd_port_static.EnableWindow();
    }
}

void CMPDUpdateDlg::OnMpdPassphraseChk() 
{
    UpdateData();
    if (m_bMPDPassphraseChecked)
    {
	m_mpd_pwd_edit.EnableWindow();
	m_mpd_pwd_static.EnableWindow(FALSE);
    }
    else
    {
	m_mpd_pwd_edit.EnableWindow(FALSE);
	m_mpd_pwd_static.EnableWindow();
    }
}

void CMPDUpdateDlg::OnMpichUrlRadio() 
{
    UpdateData();
    
    if (m_mpich_url_radio.GetCheck())
    {
	m_mpich_url_edit.EnableWindow();
	m_mpich_anl_btn.EnableWindow();
	m_mpich_filename_edit.EnableWindow(FALSE);
	m_mpich_file_browse_btn.EnableWindow(FALSE);
    }
    else
    {
	m_mpich_url_edit.EnableWindow(FALSE);
	m_mpich_anl_btn.EnableWindow(FALSE);
	m_mpich_filename_edit.EnableWindow();
	m_mpich_file_browse_btn.EnableWindow();
    }
}

void CMPDUpdateDlg::OnMpichFileRadio() 
{
    UpdateData();
    
    if (m_mpich_filename_radio.GetCheck())
    {
	m_mpich_filename_edit.EnableWindow();
	m_mpich_file_browse_btn.EnableWindow();
	m_mpich_url_edit.EnableWindow(FALSE);
	m_mpich_anl_btn.EnableWindow(FALSE);
    }
    else
    {
	m_mpich_filename_edit.EnableWindow(FALSE);
	m_mpich_file_browse_btn.EnableWindow(FALSE);
	m_mpich_url_edit.EnableWindow();
	m_mpich_anl_btn.EnableWindow();
    }
}

void CMPDUpdateDlg::OnMpichFileBrowseBtn() 
{
    UpdateData();
    
    CFileDialog f(
	TRUE, "*.dll", m_mpich_filename, 
	OFN_HIDEREADONLY | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST,
	"Dynamic libraries (*.dll)|*.dll|All files (*.*)|*.*||"
	);
    if (f.DoModal() == IDOK)
    {
	POSITION p;
	p = f.GetStartPosition();
	m_mpich_filename = f.GetNextPathName(p);
	UpdateData(FALSE);
    }
}

void CMPDUpdateDlg::OnMpichAnlBtn() 
{
    UpdateData();
    m_mpich_url = "ftp://ftp.mcs.anl.gov/pub/mpi/nt/binaries/mpich.dll";
    UpdateData(FALSE);
}

void CMPDUpdateDlg::OnMpichVersionBtn() 
{
    HCURSOR hOldCursor;
    bool bDeleteTmpMPICH = false;
    unsigned int version;
    char str[100];

    UpdateData();

    hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );

    if (m_mpich_url_radio.GetCheck())
    {
	if (!GetTmpMPICHFromURL())
	{
	    SetCursor(hOldCursor);
	    MessageBox("Unable to retrieve the new mpich dll from the specified url.", "Error");
	    return;
	}
	bDeleteTmpMPICH = true;
    }

    if (!GetLocalMPICHVersion(m_mpich_url_radio.GetCheck() ? m_mpich_localfile : m_mpich_filename, version))
    {
	if (bDeleteTmpMPICH)
	{
	    DeleteFile(m_mpich_localfile);
	    DeleteFile(m_mpich_localfiled);
	}
	SetCursor(hOldCursor);
	MessageBox("Unable to get the version of the new mpich dll", "Error");
	return;
    }

    if (bDeleteTmpMPICH)
    {
	DeleteFile(m_mpich_localfile);
	DeleteFile(m_mpich_localfiled);
    }

    if (version == 0)
    {
	m_mpich_version = "<not versioned>";
    }
    else
    {
	mpd_version_int_to_string(version, str);
	m_mpich_version = str;
    }

    SetCursor(hOldCursor);

    UpdateData(FALSE);
}

void CMPDUpdateDlg::OnMpdVersionBtn() 
{
    HCURSOR hOldCursor;
    bool bDeleteTmpMpd = false;
    unsigned int version;
    char str[100];

    UpdateData();

    hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );

    if (m_url_radio.GetCheck())
    {
	if (!GetTmpMpdFromURL())
	{
	    SetCursor(hOldCursor);
	    MessageBox("Unable to retrieve the new mpd from the specified url.", "Error");
	    return;
	}
	bDeleteTmpMpd = true;
    }
    
    if (!GetLocalVersion(m_url_radio.GetCheck() ? m_localfile : m_filename, version))
    {
	if (bDeleteTmpMpd)
	    DeleteFile(m_localfile);
	SetCursor(hOldCursor);
	MessageBox("Unable to get the version of the new mpd", "Error");
	return;
    }
    
    if (bDeleteTmpMpd)
    {
	DeleteFile(m_localfile);
    }

    mpd_version_int_to_string(version, str);
    m_mpd_version = str;

    SetCursor(hOldCursor);

    UpdateData(FALSE);
}
