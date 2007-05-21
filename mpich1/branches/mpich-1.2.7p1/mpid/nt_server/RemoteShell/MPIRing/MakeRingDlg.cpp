// MakeRingDlg.cpp : implementation file
//

#include "stdafx.h"
#include "MPIRing.h"
#include "MakeRingDlg.h"
#include "..\common\mpijobdefs.h"
//#include "MPDRingDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CMakeRingDlg dialog


CMakeRingDlg::CMakeRingDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CMakeRingDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CMakeRingDlg)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}


void CMakeRingDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CMakeRingDlg)
	DDX_Control(pDX, IDC_LIST, m_list);
	DDX_Control(pDX, IDOK, m_make_ring_btn);
	DDX_Control(pDX, IDC_REFRESH_BTN, m_refresh_btn);
	DDX_Control(pDX, IDC_FIND_BTN, m_find_btn);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CMakeRingDlg, CDialog)
	//{{AFX_MSG_MAP(CMakeRingDlg)
	ON_BN_CLICKED(IDC_REFRESH_BTN, OnRefreshBtn)
	ON_BN_CLICKED(IDC_FIND_BTN, OnFindBtn)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMakeRingDlg message handlers

struct HostNode
{
	TCHAR host[100];
	TCHAR exe[MAX_PATH];
	long nSMPProcs;
	HostNode *next;
};

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
			0, KEY_ALL_ACCESS, &hKey)) != ERROR_SUCCESS)
	{
		return false;
	}

	// Read the hosts entry
	//TCHAR pszHosts[1024];
	TCHAR *pszHosts;
	DWORD type, num_bytes=0;//1024*sizeof(TCHAR);
	ret_val = RegQueryValueEx(hKey, _T("Hosts"), 0, &type, NULL, &num_bytes);
	if (ret_val != ERROR_SUCCESS)
		return false;
	pszHosts = new TCHAR[num_bytes];
	ret_val = RegQueryValueEx(hKey, _T("Hosts"), 0, &type, (BYTE *)pszHosts, &num_bytes);
	RegCloseKey(hKey);
	if (ret_val != ERROR_SUCCESS)
	{
		delete pszHosts;
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
		//_tcscpy(n->host, g_pHosts->host);
		DWORD length = 100;
		GetComputerName(n->host, &length);
		n->nSMPProcs = 1;

		*list = l;

		delete pszHosts;
		return true;
	}

	delete pszHosts;
	return false;
}

BOOL CMakeRingDlg::OnInitDialog() 
{
	CDialog::OnInitDialog();
	
	HostNode *pList;

	OnRefreshBtn();
	if (GetHostsFromRegistry(&pList))
	{
		CString str;
		while (pList)
		{
			for (int i=0; i<m_list.GetCount(); i++)
			{
				m_list.GetText(i, str);
				if (str.CompareNoCase(pList->host) == 0)
					m_list.SetSel(i);
			}
			HostNode *p = pList;
			pList = pList->next;
			delete p;
		}
	}
	
	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

#include <lmerr.h>
#include <lmcons.h>
#include <lmapibuf.h>
#include <lmserver.h>

void CMakeRingDlg::OnRefreshBtn() 
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

void CMakeRingDlg::OnFindBtn() 
{
	HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );

	m_find_btn.EnableWindow(FALSE);
	m_refresh_btn.EnableWindow(FALSE);

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

LRESULT CMakeRingDlg::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) 
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
			}
		}
	}
	return CDialog::WindowProc(message, wParam, lParam);
}

void CMakeRingDlg::OnOK() 
{
	//CMPDRingDlg mpdDialog;
	int i;
	int *iHosts;
	int num_hosts = m_list.GetSelCount();
	char hoststring[4096] = "";
	char host[100];
	
	m_pszHosts = "";

	if (num_hosts == 0)
		return;
	
	HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );
	
	// Create the host list
	iHosts = new int[num_hosts];
	if (m_list.GetSelItems(num_hosts, iHosts) == LB_ERR)
	{
		SetCursor(hOldCursor);
		MessageBox("GetSelItems failed", "Error", MB_OK);
		delete iHosts;
		return;
	}
	for (i=0; i<num_hosts; i++)
	{
		if (m_list.GetText(iHosts[i], host) == LB_ERR)
		{
			SetCursor(hOldCursor);
			MessageBox("GetText failed", "Error", MB_OK);
			delete iHosts;
			return;
		}
		//strcat(hoststring, host);
		//strcat(hoststring, " 1");
		m_pszHosts += host;
		m_pszHosts += " 1";
		if (i<num_hosts-1)
			//strcat(hoststring, " ");
			m_pszHosts += " ";
	}
	delete iHosts;
	
	SetCursor(hOldCursor);

	//mpdDialog.m_input = hoststring;
	//mpdDialog.DoModal();
	
	CDialog::OnOK();
}
