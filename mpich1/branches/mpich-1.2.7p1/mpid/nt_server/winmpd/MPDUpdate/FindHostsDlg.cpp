// FindHostsDlg.cpp : implementation file
//

#include "stdafx.h"
#include "MPDUpdate.h"
#include "FindHostsDlg.h"
#include "DomainDlg.h"
#include "mpd.h"
#include "PwdDialog.h"
#include "mpdutil.h"
#include "qvs.h"
#include "MPDConnectionOptionsDlg.h"
#include "WildStrDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CFindHostsDlg dialog


CFindHostsDlg::CFindHostsDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CFindHostsDlg::IDD, pParent)
{
    //{{AFX_DATA_INIT(CFindHostsDlg)
    m_encoded_hosts = _T("");
	m_nofm = _T("");
	//}}AFX_DATA_INIT
    m_domain = "";
    m_num_threads = 0;
    m_hFindThread = NULL;
    m_bNeedPassword = false;
    m_bFastConnect = false;
    m_bInitDialogCalled = false;
    m_pszPhrase[0] = '\0';
    m_nPort = MPD_DEFAULT_PORT;
    m_num_items = 0;
    m_pImageList = NULL;
    m_bWildcard = false;
    m_wildstr = "*";
}


void CFindHostsDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CFindHostsDlg)
	DDX_Control(pDX, IDC_N_OF_M_STATIC, m_nofm_static);
	DDX_Control(pDX, IDC_PROGRESS, m_progress);
	DDX_Control(pDX, IDOK, m_ok_btn);
	DDX_Control(pDX, IDCANCEL, m_cancel_btn);
	DDX_Control(pDX, IDC_ENCODED_HOSTS, m_encoded_hosts_edit);
	DDX_Control(pDX, IDC_DOMAIN_HOST_LIST, m_list);
	DDX_Text(pDX, IDC_ENCODED_HOSTS, m_encoded_hosts);
	DDX_Text(pDX, IDC_N_OF_M_STATIC, m_nofm);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CFindHostsDlg, CDialog)
	//{{AFX_MSG_MAP(CFindHostsDlg)
	ON_COMMAND(ID_FILE_CHANGEDOMAIN, OnChangedomain)
	ON_COMMAND(ID_FILE_EXIT, OnFileExit)
	ON_COMMAND(ID_FILE_FINDHOSTS, OnFindhosts)
	ON_COMMAND(ID_FILE_LOADLIST, OnLoadlist)
	ON_COMMAND(ID_FILE_SAVELIST, OnSavelist)
	ON_WM_SIZE()
	ON_COMMAND(ID_FILE_VERIFY, OnVerify)
	ON_NOTIFY(NM_CLICK, IDC_DOMAIN_HOST_LIST, OnClickDomainHostList)
	ON_COMMAND(ID_FILE_CONNECTIONOPTIONS, OnConnectionOptions)
	ON_COMMAND(ID_ACTION_WILDCARDSCANHOSTS, OnActionWildcardScanHosts)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CFindHostsDlg message handlers

void CFindHostsDlg::ParseRegistry()
{
    HKEY tkey;
    DWORD result, len;
    char path[MAX_PATH];
    
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
    
    // Read the passphrase
    len = 100;
    result = RegQueryValueEx(tkey, "phrase", 0, NULL, (unsigned char *)m_pszPhrase, &len);
    if (result == ERROR_SUCCESS)
	m_bNeedPassword = false;
    
    // Read the path
    len = MAX_PATH;
    result = RegQueryValueEx(tkey, "path", 0, NULL, (unsigned char *)path, &len);
    if (result == ERROR_SUCCESS)
    {
	m_filename = path;
	m_filename.TrimRight(".exe");
	m_filename += "_hosts.txt";
    }

    RegCloseKey(tkey);
}

void CFindHostsDlg::OnChangedomain() 
{
    CDomainDlg dlg;

    UpdateData();

    dlg.m_domain = m_domain;
    if (dlg.DoModal() == IDOK)
    {
	m_domain = dlg.m_domain;
	UpdateData(FALSE);
	Refresh();
    }
}

void CFindHostsDlg::OnFileExit() 
{
    EndDialog(IDOK);
}

#include <lmerr.h>
#include <lmcons.h>
#include <lmapibuf.h>
#include <lmserver.h>

#ifndef LMCSTR
#define LMCSTR LPCWSTR
#endif

void CFindHostsDlg::Refresh()
{
    DWORD num_read=0, total=0, size;
    int index;
    SERVER_INFO_100 *pBuf = NULL;
    char tBuf[100], tLocalHost[100];
    DWORD ret_val;

    UpdateData();

    HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );
    
    if (m_domain == "")
    {
	ret_val = NetServerEnum(
	    NULL, 
	    100,
	    (LPBYTE*)&pBuf,
	    MAX_PREFERRED_LENGTH,
	    &num_read,
	    &total,
	    SV_TYPE_NT, 
	    NULL,
	    0);
    }
    else
    {
	WCHAR wDomain[100];
	mbstowcs(wDomain, m_domain, 100);
	ret_val = NetServerEnum(
	    NULL, 
	    100,
	    (LPBYTE*)&pBuf,
	    MAX_PREFERRED_LENGTH,
	    &num_read,
	    &total,
	    SV_TYPE_NT, 
	    (LMCSTR)wDomain,
	    0);
    }
    
    if (ret_val == NERR_Success)
    {
	size = 100;
	GetComputerName(tLocalHost, &size);
	m_list.DeleteAllItems();
	if (num_read == 0)
	{
	    //m_list.InsertItem(0, tLocalHost, 0);
	    //m_list.SetItemState(0, LVIS_SELECTED, LVIS_SELECTED);
	    InsertHost(tLocalHost);
	    SelectHost(tLocalHost);
	}
	else
	{
	    index = -1;
	    for (unsigned int i=0; i<num_read; i++)
	    {
		wcstombs(tBuf, (WCHAR*)pBuf[i].sv100_name, wcslen((WCHAR*)pBuf[i].sv100_name)+1);
		InsertHost(tBuf);
		/*
		index = m_list.InsertItem(0, tBuf, 0);
		if (stricmp(tBuf, tLocalHost) == 0)
		    index = ret_val;
		else
		    index = -1;
		*/
	    }
	    /*
	    if (index != -1)
	    {
		m_list.SetItemState(index, LVIS_SELECTED, LVIS_SELECTED);
	    }
	    */
	    SelectHost(tLocalHost);
	}
	NetApiBufferFree(pBuf);
    }
    else
    {
	sprintf(tBuf, "error: %d", ret_val);
	MessageBox(tBuf, "Unable to retrieve network host names");
    }
    
    SetCursor(hOldCursor);
}

static int wildcmp(LPCTSTR wild, LPCTSTR string)
{
    LPCTSTR cp;
    LPCTSTR mp;
    
    while ((*string) && (*wild != '*'))
    {
	if ((*wild != *string) && (*wild != '?'))
	{
	    return 0;
	}
	wild++;
	string++;
    }
    
    while (*string)
    {
	if (*wild == '*')
	{
	    if (!*++wild)
	    {
		return 1;
	    }
	    mp = wild;
	    cp = string+1;
	}
	else if ((*wild == *string) || (*wild == '?'))
	{
	    wild++;
	    string++;
	}
	else
	{
	    wild = mp;
	    string = cp++;
	}
    }
    
    while (*wild == '*')
    {
	wild++;
    }
    return !*wild;
}

struct FindThreadSingleArg
{
    CListCtrl *list;
    int i;
    HWND hWnd;
    int port;
    char phrase[100];
    bool fast;
    bool wildcard;
    CString wildstr;
};

void FindThreadSingle(FindThreadSingleArg *arg)
{
    TCHAR host[100];
    char str[100];
    SOCKET sock;
    
    if (arg->list->GetItemText(arg->i, 0, host, 100) == 0)
    {
	::PostMessage(arg->hWnd, WM_USER+1, arg->i, FALSE);
	delete arg;
	return;
    }
    
    if (arg->wildcard && (!wildcmp(arg->wildstr, host)))
    {
	::PostMessage(arg->hWnd, WM_USER+1, arg->i, FALSE);
	delete arg;
	return;
    }
    
    ::PostMessage(arg->hWnd, WM_USER+1, arg->i, TRUE);

    if (arg->fast)
    {
	if (ConnectToMPDquick(host, arg->port, arg->phrase, &sock) != 0)
	{
	    ::PostMessage(arg->hWnd, WM_USER+1, arg->i, FALSE);
	    delete arg;
	    return;
	}
    }
    else
    {
	if (ConnectToMPD(host, arg->port, arg->phrase, &sock) != 0)
	{
	    ::PostMessage(arg->hWnd, WM_USER+1, arg->i, FALSE);
	    delete arg;
	    return;
	}
    }
    
    if (WriteString(sock, "version") == SOCKET_ERROR)
    {
	printf("WriteString failed after attempting passphrase authentication: %d\n", WSAGetLastError());fflush(stdout);
	easy_closesocket(sock);
	::PostMessage(arg->hWnd, WM_USER+1, arg->i, FALSE);
	delete arg;
	return;
    }
    if (!ReadString(sock, str))
    {
	::PostMessage(arg->hWnd, WM_USER+1, arg->i, FALSE);
	delete arg;
	return;
    }
    WriteString(sock, "done");
    easy_closesocket(sock);
    
    if (mpd_version_string_to_int(str) == 0)
	::PostMessage(arg->hWnd, WM_USER+1, arg->i, FALSE);
    else
	::PostMessage(arg->hWnd, WM_USER+2, arg->i, TRUE);
    delete arg;
}

#define FIND_NUM_PER_THREAD MAXIMUM_WAIT_OBJECTS

void OnFindBtnThread(CFindHostsDlg *pDlg)
{
    int n;
    DWORD count = pDlg->m_list.GetItemCount();
    pDlg->m_num_threads = count;

    if (count < 1)
    {
	CloseHandle(pDlg->m_hFindThread);
	pDlg->m_hFindThread = NULL;
	return;
    }

    if (pDlg->m_bNeedPassword)
    {
	CPwdDialog dlg;
	dlg.DoModal();
	if (dlg.m_bUseDefault)
	    strcpy(pDlg->m_pszPhrase, MPD_DEFAULT_PASSPHRASE);
	else
	    strcpy(pDlg->m_pszPhrase, dlg.m_password);
    }

    DWORD dwThreadID;
    HANDLE hThread[FIND_NUM_PER_THREAD];
    int index;
    for (DWORD i=0; i<count; i++)
    {
	FindThreadSingleArg *arg = new FindThreadSingleArg;
	arg->wildcard = pDlg->m_bWildcard;
	arg->wildstr = pDlg->m_wildstr;
	arg->hWnd = pDlg->m_hWnd;
	arg->list = &pDlg->m_list;
	arg->i = i;
	arg->port = pDlg->m_nPort;
	arg->fast = pDlg->m_bFastConnect;
	if (strlen(pDlg->m_pszPhrase) < 100)
	    strcpy(arg->phrase, pDlg->m_pszPhrase);
	else
	    arg->phrase[0] = '\0';
	index = i % FIND_NUM_PER_THREAD;
	hThread[index] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)FindThreadSingle, arg, 0, &dwThreadID);
	if (hThread[index] == NULL)
	{
	    pDlg->m_num_threads--;
	    ::PostMessage(arg->hWnd, WM_USER+1, arg->i, FALSE);
	    delete arg;
	}
	if (index == FIND_NUM_PER_THREAD-1)
	{
	    WaitForMultipleObjects(FIND_NUM_PER_THREAD, hThread, TRUE, 120000);
	    for (n=0; n<FIND_NUM_PER_THREAD; n++)
		CloseHandle(hThread[n]);
	}
    }

    CloseHandle(pDlg->m_hFindThread);
    pDlg->m_hFindThread = NULL;
}

void CFindHostsDlg::OnActionWildcardScanHosts() 
{
    DWORD dwThreadID;
    CWildStrDlg dlg;

    dlg.m_wildstr = m_wildstr;
    if (dlg.DoModal() == IDOK)
    {
	m_bWildcard = true;
	m_wildstr = dlg.m_wildstr;
	
	UpdateData();
	
	m_nofm = "";
	UpdateData(FALSE);
	
	m_num_items = m_list.GetItemCount();
	if (m_num_items < 1)
	    return;
	
	m_ok_btn.EnableWindow(FALSE);
	m_cancel_btn.EnableWindow(FALSE);
	
	m_progress.SetRange(0, m_num_items);
	m_progress.SetStep(1);
	m_progress.SetPos(0);
	
	m_bWildcard = true;
	
	m_hFindThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)OnFindBtnThread, this, 0, &dwThreadID);
	if (m_hFindThread == NULL)
	{
	    MessageBox("Unable to create a Find thread", "Error", MB_OK);
	}
    }
}

void CFindHostsDlg::OnFindhosts() 
{
    DWORD dwThreadID;

    UpdateData();

    m_nofm = "";
    UpdateData(FALSE);

    m_num_items = m_list.GetItemCount();
    if (m_num_items < 1)
	return;

    m_ok_btn.EnableWindow(FALSE);
    m_cancel_btn.EnableWindow(FALSE);

    m_progress.SetRange(0, m_num_items);
    m_progress.SetStep(1);
    m_progress.SetPos(0);

    m_bWildcard = false;

    m_hFindThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)OnFindBtnThread, this, 0, &dwThreadID);
    if (m_hFindThread == NULL)
    {
	MessageBox("Unable to create a Find thread", "Error", MB_OK);
    }
}

void CFindHostsDlg::OnLoadlist() 
{
    CFileDialog f(
	TRUE, "*.txt", m_filename, 
	OFN_HIDEREADONLY | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST,
	"Text (*.txt)|*.txt|All files (*.*)|*.*||"
	);
    if (f.DoModal() == IDOK)
    {
	POSITION p;
	QVS_Container qvs;
	CStdioFile fin;
	CString line;

	p = f.GetStartPosition();
	m_filename = f.GetNextPathName(p);
	
	if (fin.Open(m_filename, CFile::modeRead))
	{
	    while (fin.ReadString(line))
	    {
		qvs.decode_string((char*)(LPCTSTR)line);
	    }
	    fin.Close();

	    char host[100];
	    if (qvs.first(host, 100))
	    {
		InsertHost(host);
		while (qvs.next(host, 100))
		{
		    InsertHost(host);
		}
	    }

	    qvs.output_encoded_string(m_encoded_hosts.GetBuffer(8192), 8192);
	    m_encoded_hosts.ReleaseBuffer();
	    UpdateData(FALSE);
	}
	else
	{
	    MessageBox(m_filename, "Unable to open file");
	}
    }
}

void CFindHostsDlg::OnSavelist() 
{
    CFileDialog f(
	FALSE, "*.txt", m_filename, 
	OFN_HIDEREADONLY | OFN_EXPLORER | OFN_PATHMUSTEXIST,
	"Text (*.txt)|*.txt|All files (*.*)|*.*||"
	);
    if (f.DoModal() == IDOK)
    {
	POSITION p;
	QVS_Container qvs;
	CStdioFile fout;
	CString str;

	p = f.GetStartPosition();
	m_filename = f.GetNextPathName(p);

	if (fout.Open(m_filename, CFile::modeWrite | CFile::modeCreate))
	{
	    qvs.decode_string((char*)(LPCTSTR)m_encoded_hosts);
	    if (qvs.first(str.GetBuffer(100), 100))
	    {
		str.ReleaseBuffer();
		fout.WriteString(str + "\n");
		while (qvs.next(str.GetBuffer(100), 100))
		{
		    str.ReleaseBuffer();
		    fout.WriteString(str + "\n");
		}
	    }
	    fout.Close();
	}
	else
	{
	    MessageBox(m_filename, "Unable to open file");
	}
    }
}

BOOL CFindHostsDlg::OnInitDialog() 
{
    CDialog::OnInitDialog();
    
    ParseRegistry();
    
    r_domain.SetInitialPosition(m_encoded_hosts_edit.m_hWnd, RSR_STRETCH_RIGHT);
    r_hosts.SetInitialPosition(m_list.m_hWnd, RSR_STRETCH);
    r_ok.SetInitialPosition(m_ok_btn.m_hWnd, RSR_MOVE);
    r_cancel.SetInitialPosition(m_cancel_btn.m_hWnd, RSR_MOVE);
    r_progress.SetInitialPosition(m_progress.m_hWnd, RSR_MOVE);
    r_nofm.SetInitialPosition(m_nofm_static.m_hWnd, RSR_MOVE);
    
    m_pImageList = new CImageList();
    m_pImageList->Create(16, 16, ILC_COLOR8 | ILC_MASK, 2, 1);
    m_pImageList->Add(AfxGetApp()->LoadIcon(IDI_ICON_YES));
    m_pImageList->Add(AfxGetApp()->LoadIcon(IDI_ICON_NO));
    m_list.SetImageList(m_pImageList, LVSIL_STATE);

    Refresh();
    UpdateSelectedHosts();
    
    m_bInitDialogCalled = true;
    return TRUE;  // return TRUE unless you set the focus to a control
}

void CFindHostsDlg::OnSize(UINT nType, int cx, int cy) 
{
    CDialog::OnSize(nType, cx, cy);
    
    r_domain.Resize(cx, cy);
    r_hosts.Resize(cx, cy);
    r_ok.Resize(cx, cy);
    r_cancel.Resize(cx, cy);
    r_progress.Resize(cx, cy);
    r_nofm.Resize(cx, cy);
    
    if (m_bInitDialogCalled)
	m_list.Arrange(LVA_DEFAULT );
}

LRESULT CFindHostsDlg::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) 
{
    switch (message)
    {
    case WM_USER + 1:
	if (lParam)
	{
	    m_list.SetItemState((int)wParam, LVIS_SELECTED, LVIS_SELECTED);
	}
	else
	{
	    if ((int)wParam != -1)
	    {
		m_list.SetItemState((int)wParam, 0, LVIS_SELECTED);
		m_list.SetItemState((int)wParam, INDEXTOSTATEIMAGEMASK(0), LVIS_STATEIMAGEMASK);
	    }
	    m_num_threads--;
	    if (m_num_threads == 0)
	    {
		m_ok_btn.EnableWindow();
		m_cancel_btn.EnableWindow();
		UpdateSelectedHosts();
	    }
	    m_progress.StepIt();
	    m_nofm.Format("%d of %d", m_progress.GetPos(), m_num_items);
	    UpdateData(FALSE);
	}
	break;
    case WM_USER + 2:
	m_list.SetItemState((int)wParam, LVIS_SELECTED, LVIS_SELECTED);
	m_list.SetItemState((int)wParam, INDEXTOSTATEIMAGEMASK(1), LVIS_STATEIMAGEMASK);
	m_num_threads--;
	if (m_num_threads == 0)
	{
	    m_ok_btn.EnableWindow();
	    m_cancel_btn.EnableWindow();
	    UpdateSelectedHosts();
	}
	m_progress.StepIt();
	m_nofm.Format("%d of %d", m_progress.GetPos(), m_num_items);
	UpdateData(FALSE);
	break;
    }
    
    return CDialog::WindowProc(message, wParam, lParam);
}

void CFindHostsDlg::OnVerify() 
{
    char host[100];
    SOCKET sock;
    int index;
    char str[100];
    HCURSOR hOldCursor;
    POSITION pos;
    
    UpdateData();
    
    m_nofm = "";
    UpdateData(FALSE);

    m_num_threads = m_list.GetSelectedCount();
    if (m_num_threads == 0)
	return;

    pos = m_list.GetFirstSelectedItemPosition();
    if (pos == NULL)
    {
	return;
    }

    hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );

    m_ok_btn.EnableWindow(FALSE);
    m_cancel_btn.EnableWindow(FALSE);

    m_num_items = m_list.GetSelectedCount();
    m_progress.SetRange(0, m_num_items);
    m_progress.SetPos(0);
    m_progress.SetStep(1);
    m_nofm.Format("0 of %d", m_num_items);
    UpdateData(FALSE);


    if (m_bNeedPassword)
    {
	CPwdDialog dlg;
	dlg.DoModal();
	if (dlg.m_bUseDefault)
	    strcpy(m_pszPhrase, MPD_DEFAULT_PASSPHRASE);
	else
	    strcpy(m_pszPhrase, dlg.m_password);
    }
    while (pos)
    {
	index = m_list.GetNextSelectedItem(pos);
	if (m_list.GetItemText(index, 0, host, 100) == 0)
	{
	    SetCursor(hOldCursor);
	    MessageBox("GetItemText failed", "Error", MB_OK);
	    return;
	}

	if (ConnectToMPD(host, m_nPort, m_pszPhrase, &sock) != 0)
	{
	    easy_closesocket(sock);
	    ::PostMessage(m_hWnd, WM_USER+1, index, FALSE);
	    continue;
	}
	
	if (WriteString(sock, "version") == SOCKET_ERROR)
	{
	    easy_closesocket(sock);
	    ::PostMessage(m_hWnd, WM_USER+1, index, FALSE);
	    continue;
	}
	if (!ReadString(sock, str))
	{
	    easy_closesocket(sock);
	    ::PostMessage(m_hWnd, WM_USER+1, index, FALSE);
	    continue;
	}
	WriteString(sock, "done");
	easy_closesocket(sock);
	
	::PostMessage(m_hWnd, WM_USER+2, index, FALSE);
    }
    SetCursor(hOldCursor);
}

void CFindHostsDlg::UpdateSelectedHosts()
{
    QVS_Container qvs;
    POSITION pos;
    char host[100];
    int index;

    UpdateData();

    pos = m_list.GetFirstSelectedItemPosition();
    while (pos)
    {
	index = m_list.GetNextSelectedItem(pos);
	if (m_list.GetItemText(index, 0, host, 100) == 0)
	{
	    MessageBox("GetItemText failed", "Error", MB_OK);
	    return;
	}

	qvs.encode_string(host);
    }

    qvs.output_encoded_string(m_encoded_hosts.GetBuffer(8192), 8192);
    m_encoded_hosts.ReleaseBuffer();

    UpdateData(FALSE);
}

void CFindHostsDlg::OnClickDomainHostList(NMHDR* pNMHDR, LRESULT* pResult) 
{
    UpdateSelectedHosts();
    *pResult = 0;
}

void CFindHostsDlg::OnConnectionOptions() 
{
    CMPDConnectionOptionsDlg dlg;

    dlg.m_bFastConnect = m_bFastConnect;
    dlg.m_phrase = m_pszPhrase;
    dlg.m_port = m_nPort;

    if (dlg.DoModal() == IDOK)
    {
	if (dlg.m_bPhrase)
	{
	    strcpy(m_pszPhrase, dlg.m_phrase);
	}
	else
	{
	    strcpy(m_pszPhrase, MPD_DEFAULT_PASSPHRASE);
	}
	m_bNeedPassword = false;
	m_bUseDefault = false;
	
	if (dlg.m_bPort)
	{
	    m_nPort = dlg.m_port;
	}
	else
	{
	    m_nPort = MPD_DEFAULT_PORT;
	}

	m_bFastConnect = (dlg.m_bFastConnect == TRUE);
    }
}

void CFindHostsDlg::InsertHost(char *host)
{
    LVFINDINFO info;
    int i;
    char pszHost[100];

    if (host == NULL || *host == '\0')
	return;

    strncpy(pszHost, host, 100);
    for (i=0; i<100, pszHost[i] != '\0'; i++)
	pszHost[i] = toupper(pszHost[i]);

    info.flags = LVFI_STRING;
    info.psz = pszHost;
    
    if (m_list.FindItem(&info) == -1)
    {
	m_list.InsertItem(0, pszHost, 0);
    }
}

void CFindHostsDlg::SelectHost(char *host)
{
    LVFINDINFO info;
    int i;
    char pszHost[100];

    if (host == NULL || *host == '\0')
	return;

    strncpy(pszHost, host, 100);
    for (i=0; i<100, pszHost[i] != '\0'; i++)
	pszHost[i] = toupper(pszHost[i]);

    info.flags = LVFI_STRING;
    info.psz = pszHost;
    
    i = m_list.FindItem(&info);
    if (i != -1)
    {
	m_list.SetItemState(i, LVIS_SELECTED, LVIS_SELECTED);
    }
}
