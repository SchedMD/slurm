// MPIConfigDlg.cpp : implementation file
//

#include "stdafx.h"
#include "MPIConfig.h"
#include "MPIConfigDlg.h"
#include "..\common\mpijobdefs.h"
#include <tchar.h>
#include "RegistrySettingsDialog.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CMPIConfigDlg dialog

CMPIConfigDlg::CMPIConfigDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CMPIConfigDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CMPIConfigDlg)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_hFindThread = NULL;
}

void CMPIConfigDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CMPIConfigDlg)
	DDX_Control(pDX, IDC_VERIFY_BTN, m_verify_btn);
	DDX_Control(pDX, IDC_SET_BTN, m_set_btn);
	DDX_Control(pDX, IDC_REFRESH_BTN, m_refresh_btn);
	DDX_Control(pDX, IDC_FIND_BTN, m_find_btn);
	DDX_Control(pDX, IDC_LIST, m_list);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CMPIConfigDlg, CDialog)
	//{{AFX_MSG_MAP(CMPIConfigDlg)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_FIND_BTN, OnFindBtn)
	ON_BN_CLICKED(IDC_REFRESH_BTN, OnRefreshBtn)
	ON_BN_CLICKED(IDC_SET_BTN, OnSetBtn)
	ON_BN_CLICKED(IDC_VERIFY_BTN, OnVerifyBtn)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMPIConfigDlg message handlers

BOOL CMPIConfigDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon
	
	OnRefreshBtn();
	m_verify_btn.EnableWindow(FALSE);

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

struct FindThreadArg
{
	CListBox *list;
	HWND hWnd;
	HANDLE *phThread;
};

void FindThread(FindThreadArg *arg)
{
	for (int i=0; i<arg->list->GetCount(); i++)
	{
		TCHAR host[100];
		if (arg->list->GetText(i, host) == LB_ERR)
			continue;
		
		SendMessage(arg->hWnd, WM_USER+1, i, TRUE);

		// Open registry
		HKEY hKeyRoot, hKey;
		DWORD type, ret_val;
		TCHAR sValue[100] = TEXT("");
		DWORD dwSize = 100*sizeof(TCHAR);
		
		ret_val = RegConnectRegistry(host, HKEY_LOCAL_MACHINE, &hKeyRoot);
		if (ret_val != ERROR_SUCCESS)
		{
			// Unable to connect to remote host
			SendMessage(arg->hWnd, WM_USER+1, i, FALSE);
			continue;
		}
		ret_val = RegOpenKeyEx(hKeyRoot, MPICHKEY, 0, KEY_READ, &hKey);
		if (ret_val != ERROR_SUCCESS)
		{
			// Unable to open MPICH registry key
			RegCloseKey(hKeyRoot);
			SendMessage(arg->hWnd, WM_USER+1, i, FALSE);
			continue;
		}
		RegQueryValueEx(hKey, NULL, 0, &type, (LPBYTE)sValue, &dwSize);
		if (ret_val != ERROR_SUCCESS)
		{
			// Unable to read Hosts value from MPICH registry key
			RegCloseKey(hKeyRoot);
			RegCloseKey(hKey);
			SendMessage(arg->hWnd, WM_USER+1, i, FALSE);
			continue;
		}
		RegCloseKey(hKey);
		RegCloseKey(hKeyRoot);
		
		if (_tcsicmp(sValue, TEXT("Installed")))
			SendMessage(arg->hWnd, WM_USER+1, i, FALSE);
	}
	*arg->phThread = NULL;
	delete arg;
}

struct FindThreadSingleArg
{
	CListBox *list;
	int i;
	HWND hWnd;
};

void FindThreadSingle(FindThreadSingleArg *arg)
{
	TCHAR host[100];
	if (arg->list->GetText(arg->i, host) == LB_ERR)
		return;
	
	SendMessage(arg->hWnd, WM_USER+1, arg->i, TRUE);
	
	// Open registry
	HKEY hKeyRoot, hKey;
	DWORD type, ret_val;
	TCHAR sValue[100] = TEXT("");
	DWORD dwSize = 100*sizeof(TCHAR);
	
	ret_val = RegConnectRegistry(host, HKEY_LOCAL_MACHINE, &hKeyRoot);
	if (ret_val != ERROR_SUCCESS)
	{
		// Unable to connect to remote host
		SendMessage(arg->hWnd, WM_USER+1, arg->i, FALSE);
		return;
	}
	ret_val = RegOpenKeyEx(hKeyRoot, MPICHKEY, 0, KEY_READ, &hKey);
	if (ret_val != ERROR_SUCCESS)
	{
		// Unable to open MPICH registry key
		RegCloseKey(hKeyRoot);
		SendMessage(arg->hWnd, WM_USER+1, arg->i, FALSE);
		return;
	}
	RegQueryValueEx(hKey, NULL, 0, &type, (LPBYTE)sValue, &dwSize);
	if (ret_val != ERROR_SUCCESS)
	{
		// Unable to read Hosts value from MPICH registry key
		RegCloseKey(hKeyRoot);
		RegCloseKey(hKey);
		SendMessage(arg->hWnd, WM_USER+1, arg->i, FALSE);
		return;
	}
	RegCloseKey(hKey);
	RegCloseKey(hKeyRoot);
	
	if (_tcsicmp(sValue, TEXT("Installed")))
		SendMessage(arg->hWnd, WM_USER+1, arg->i, FALSE);
	else
		SendMessage(arg->hWnd, WM_USER+1, -1, FALSE);
	delete arg;
}

void CMPIConfigDlg::OnFindBtn() 
{
	HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );

	m_find_btn.EnableWindow(FALSE);
	m_refresh_btn.EnableWindow(FALSE);
	m_set_btn.EnableWindow(FALSE);
	m_verify_btn.EnableWindow(FALSE);

	DWORD count = m_list.GetCount();
	m_num_threads = count;
	for (DWORD i=0; i<count; i++)
	{
		DWORD dwThreadID;
		FindThreadSingleArg *arg = new FindThreadSingleArg;
		arg->hWnd = m_hWnd;
		arg->list = &m_list;
		arg->i = i;
		CloseHandle(CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)FindThreadSingle, arg, 0, &dwThreadID));
	}
/*
	m_find_btn.EnableWindow();
	m_refresh_btn.EnableWindow();
	m_set_btn.EnableWindow();
	m_verify_btn.EnableWindow();
	/*
	if (m_hFindThread == NULL)
	{
		DWORD dwThreadID;
		FindThreadArg *arg = new FindThreadArg;
		arg->hWnd = m_hWnd;
		arg->list = &m_list;
		arg->phThread = &m_hFindThread;
		m_hFindThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)FindThread, arg, 0, &dwThreadID);
	}
	//*/
	/*
	for (int i=0; i<m_list.GetCount(); i++)
	{
		TCHAR host[100];
		if (m_list.GetText(i, host) == LB_ERR)
			continue;
		
		// Open registry
		HKEY hKeyRoot, hKey;
		DWORD type, ret_val;
		TCHAR sValue[100] = TEXT("");
		DWORD dwSize = 100*sizeof(TCHAR);
		
		ret_val = RegConnectRegistry(host, HKEY_LOCAL_MACHINE, &hKeyRoot);
		if (ret_val != ERROR_SUCCESS)
		{
			// Unable to connect to remote host
			continue;
		}
		ret_val = RegOpenKeyEx(hKeyRoot, MPICHKEY, 0, KEY_READ, &hKey);
		if (ret_val != ERROR_SUCCESS)
		{
			// Unable to open MPICH registry key
			RegCloseKey(hKeyRoot);
			continue;
		}
		RegQueryValueEx(hKey, NULL, 0, &type, (LPBYTE)sValue, &dwSize);
		if (ret_val != ERROR_SUCCESS)
		{
			// Unable to read Hosts value from MPICH registry key
			RegCloseKey(hKeyRoot);
			RegCloseKey(hKey);
			continue;
		}
		RegCloseKey(hKey);
		RegCloseKey(hKeyRoot);
		
		if (_tcsicmp(sValue, TEXT("Installed")) == 0)
			m_list.SetSel(i, TRUE);
	}
	//*/

	SetCursor(hOldCursor);
}

#include <lmerr.h>
#include <lmcons.h>
#include <lmapibuf.h>
#include <lmserver.h>

void CMPIConfigDlg::OnRefreshBtn() 
{
	DWORD num_read=0, total=0, size;
	int index;
	SERVER_INFO_100 *pBuf = NULL;
	WCHAR wBuffer[1024] = L"";
	char tBuf[100], tLocalHost[100];
	DWORD ret_val;

	HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );

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

	if (ret_val == NERR_Success)
	{
		size = 100;
		GetComputerName(tLocalHost, &size);
		m_list.ResetContent();
		if (num_read == 0)
		{
			m_list.InsertString(-1, tLocalHost);
			m_list.SetSel(0);
		}
		else
		{
			index = -1;
			for (unsigned int i=0; i<num_read; i++)
			{
				wcstombs(tBuf, (WCHAR*)pBuf[i].sv100_name, wcslen((WCHAR*)pBuf[i].sv100_name)+1);
				ret_val = m_list.InsertString(-1, tBuf);
				if (stricmp(tBuf, tLocalHost) == 0)
					index = ret_val;
			}
			if (index != -1)
				m_list.SetSel(index);
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

// Function name	: RemoveHostFromHostString
// Description	    : 
// Return type		: void 
// Argument         : LPCTSTR host
// Argument         : LPCTSTR hoststring
// Argument         : LPTSTR cuthoststring
void RemoveHostFromHostString(LPCTSTR host, LPCTSTR hoststring, LPTSTR cuthoststring)
{
	TCHAR buffer[4096];
	LPTSTR token = NULL;

	_tcscpy(buffer, hoststring);
	token = _tcstok(buffer, TEXT("|"));

	// Concatenate all the hosts in 'hostring' except those matching 'host'
	while (token != NULL)
	{
		if (_tcsicmp(host, token))
		{
			_tcscat(cuthoststring, token);
			_tcscat(cuthoststring, TEXT("|"));
		}
		token = _tcstok(NULL, TEXT("|"));
	}

	// Remove trailing '|' character
	if (_tcslen(cuthoststring)>0)
	{
		if (cuthoststring[_tcslen(cuthoststring)-1] == TEXT('|'))
			cuthoststring[_tcslen(cuthoststring)-1] = TEXT('\0');
	}
}

void CMPIConfigDlg::OnSetBtn()
{
	int i;
	int *iHosts;
	int num_hosts = m_list.GetSelCount();
	char hoststring[4096] = "", cuthoststring[4096] = "";
	char host[100];
	char msg[256];

	if (num_hosts == 0)
		return;

	CRegistrySettingsDialog dlg;
	if (dlg.DoModal() == IDCANCEL)
		return;

	if (dlg.m_bHostsChk == FALSE && dlg.m_bTempChk == FALSE && dlg.m_bLaunchTimeoutChk == FALSE)
		return;

	HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );

	// Create the host list
	iHosts = new int[num_hosts];
	if (m_list.GetSelItems(num_hosts, iHosts) == LB_ERR)
	{
		SetCursor(hOldCursor);
		MessageBox("GetSelItems failed", "Error", MB_OK);
		return;
	}
	for (i=0; i<num_hosts; i++)
	{
		if (m_list.GetText(iHosts[i], host) == LB_ERR)
		{
			SetCursor(hOldCursor);
			MessageBox("GetText failed", "Error", MB_OK);
			return;
		}
		strcat(hoststring, host);
		if (i<num_hosts-1)
			strcat(hoststring, "|");
	}

	for (i=0; i<num_hosts; i++)
	{
		HKEY hKeyRoot, hKey;
		DWORD ret_val;
		
		if (m_list.GetText(iHosts[i], host) == LB_ERR)
			continue;
		
		ret_val = RegConnectRegistry(host, HKEY_LOCAL_MACHINE, &hKeyRoot);
		if (ret_val != ERROR_SUCCESS)
		{
			sprintf(msg, "Unable to connect to the registry on %s", host);
			MessageBox(msg, "Error", MB_OK);
			continue;
		}
		if (RegCreateKeyEx(hKeyRoot, MPICHKEY, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL) != ERROR_SUCCESS)
		{
			RegCloseKey(hKeyRoot);
			sprintf(msg, "Unable to create the MPICH registry key on %s", host);
			MessageBox(msg, "Error", MB_OK);
			continue;
		}
		if (dlg.m_bHostsChk)
		{
			cuthoststring[0] = '\0';
			RemoveHostFromHostString(host, hoststring, cuthoststring);
			if (RegSetValueEx(hKey, "Hosts", 0, REG_SZ, (LPBYTE)cuthoststring, strlen(cuthoststring)+1) != ERROR_SUCCESS)
			{
				RegCloseKey(hKey);
				RegCloseKey(hKeyRoot);
				sprintf(msg, "Unable to set the 'Hosts' registry entry on %s", host);
				MessageBox(msg, "Error", MB_OK);
				continue;
			}
		}
		if (dlg.m_bTempChk)
		{
			if (RegSetValueEx(hKey, "Temp", 0, REG_SZ, (LPBYTE)(LPCTSTR)dlg.m_pszTempDir, dlg.m_pszTempDir.GetLength()+1) != ERROR_SUCCESS)
			{
				RegCloseKey(hKey);
				RegCloseKey(hKeyRoot);
				sprintf(msg, "Unable to set the 'Temp' registry entry on %s", host);
				MessageBox(msg, "Error", MB_OK);
				continue;
			}
		}
		if (dlg.m_bLaunchTimeoutChk)
		{
			if (RegSetValueEx(hKey, "LaunchTimeout", 0, REG_DWORD, (LPBYTE)&dlg.m_nLaunchTimeout, sizeof(DWORD)) != ERROR_SUCCESS)
			{
				RegCloseKey(hKey);
				RegCloseKey(hKeyRoot);
				sprintf(msg, "Unable to set the 'LaunchTimeout' registry entry on %s", host);
				MessageBox(msg, "Error", MB_OK);
				continue;
			}
		}
		RegCloseKey(hKey);
		RegCloseKey(hKeyRoot);
	}
	SetCursor(hOldCursor);
}

void CMPIConfigDlg::OnVerifyBtn() 
{
	MessageBox("Not implemented", "Note", MB_OK);
}

LRESULT CMPIConfigDlg::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) 
{
	if (message == WM_USER+1)
	{
		if (lParam)
			m_list.SetSel((int)wParam, TRUE);
		else
		{
			if ((int)wParam != -1)
				m_list.SetSel((int)wParam, FALSE);
			m_num_threads--;
			if (m_num_threads == 0)
			{
				m_find_btn.EnableWindow();
				m_refresh_btn.EnableWindow();
				m_set_btn.EnableWindow();
				//m_verify_btn.EnableWindow();
			}
		}
	}
	return CDialog::WindowProc(message, wParam, lParam);
}
