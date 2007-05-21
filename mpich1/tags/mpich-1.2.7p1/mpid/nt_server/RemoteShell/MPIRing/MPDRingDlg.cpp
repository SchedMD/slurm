// MPDRingDlg.cpp : implementation file
//

#include "stdafx.h"
#include "MPIRing.h"
#include "MPDRingDlg.h"
#include "AccountPasswordDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CMPDRingDlg dialog


CMPDRingDlg::CMPDRingDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CMPDRingDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CMPDRingDlg)
	m_input = _T("");
	//}}AFX_DATA_INIT
}


void CMPDRingDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CMPDRingDlg)
	DDX_Control(pDX, IDCANCEL, m_quit_btn);
	DDX_Control(pDX, IDC_INPUT_BOX, m_input_box);
	DDX_Control(pDX, IDC_ENTER_BTN, m_enter_btn);
	DDX_Control(pDX, IDC_OUTPUT_LIST, m_list);
	DDX_Text(pDX, IDC_INPUT_BOX, m_input);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CMPDRingDlg, CDialog)
	//{{AFX_MSG_MAP(CMPDRingDlg)
	ON_BN_CLICKED(IDC_ENTER_BTN, OnEnterBtn)
	ON_WM_SIZE()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMPDRingDlg message handlers

void CMPDRingDlg::OnEnterBtn() 
{
	DWORD dwNumWritten;
	bool bQuit = false;

	UpdateData();

	if (m_input.CompareNoCase("quit") == 0 || m_input.CompareNoCase("exit") == 0)
		bQuit = true;

	m_list.InsertString(-1, m_input);
	m_list.SetCurSel(m_list.GetCount()-1);

	//m_output += m_input + '\r' + '\n';
	m_input += '\n';

	UpdateData(FALSE);

	WriteFile(m_hStdinPipeW, m_input.GetBuffer(0), m_input.GetLength(), &dwNumWritten, NULL);

	m_input = "";
	UpdateData(FALSE);

	if (bQuit)
		PostMessage(WM_QUIT);
}

BOOL CMPDRingDlg::OnInitDialog() 
{
	CDialog::OnInitDialog();
	
	CAccountPasswordDlg dlg;
	dlg.m_account = "";
	while (dlg.m_account.GetLength() == 0)
	{
		if (dlg.DoModal() == IDOK)
		{
			if (dlg.m_account.GetLength() > 0)
			{
				if (dlg.m_password.GetLength() == 0)
				{
					if (MessageBox("Are you sure you want to enter a blank password?", "Empty field", MB_YESNO) == IDYES)
						StartMPDs(dlg.m_account, dlg.m_password);
					else
						dlg.m_account = "";
				}
				else
					StartMPDs(dlg.m_account, dlg.m_password);
			}
		}
		else
			ExitProcess(0);
	}

	GetClientRect(&m_rDialog);
	ClientToScreen(&m_rDialog);

	m_list.GetWindowRect(&m_rList);
	m_rList.left = m_rList.left - m_rDialog.left;
	m_rList.top = m_rList.top - m_rDialog.top;
	m_rList.right = m_rList.right - m_rDialog.right;
	m_rList.bottom = m_rList.bottom - m_rDialog.bottom;

	m_enter_btn.GetWindowRect(&m_rEnter);
	m_rEnter.left = m_rEnter.left - m_rDialog.right;
	m_rEnter.right = m_rEnter.right - m_rDialog.right;
	m_rEnter.top = m_rEnter.top - m_rDialog.bottom;
	m_rEnter.bottom = m_rEnter.bottom - m_rDialog.bottom;

	m_input_box.GetWindowRect(&m_rInput);
	m_rInput.left = m_rInput.left - m_rDialog.left;
	m_rInput.right = m_rInput.right - m_rDialog.right;
	m_rInput.top = m_rInput.top - m_rDialog.bottom;
	m_rInput.bottom = m_rInput.bottom - m_rDialog.bottom;

	m_quit_btn.GetWindowRect(&m_rQuit);
	m_rQuit.left = m_rQuit.left - m_rDialog.right;
	m_rQuit.right = m_rQuit.right - m_rDialog.right;
	m_rQuit.top = m_rQuit.top - m_rDialog.bottom;
	m_rQuit.bottom = m_rQuit.bottom - m_rDialog.bottom;

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

struct RedirectOutputThreadArg
{
	HWND hWnd;
	HANDLE hPipe;
};

void RedirectOutputThread(RedirectOutputThreadArg *pArg)
{
	DWORD dwNumRead;
	char buffer[1024];
	HANDLE hPipe;
	HWND hWnd;
	hPipe = pArg->hPipe;
	hWnd = pArg->hWnd;
	delete pArg;
	COPYDATASTRUCT copyData;

	while (ReadFile(hPipe, buffer, 1024, &dwNumRead, NULL))
	{
		copyData.dwData = 0;
		copyData.cbData = dwNumRead;
		copyData.lpData = buffer;
		SendMessage(hWnd, WM_COPYDATA, (WPARAM)hWnd, (LPARAM)&copyData);
	}
	PostMessage(hWnd, WM_USER+1, 0, 0);
}

void CMPDRingDlg::StartMPDs(LPCTSTR pszAccount, LPCTSTR pszPassword)
{
	BOOL bSuccess = FALSE;
	HANDLE hStdin, hStdout, hStderr;
	HANDLE hStdinPipeR=NULL;
	//HANDLE hStdinPipeW=NULL;
	HANDLE hStdoutPipeR=NULL;
	HANDLE hStdoutPipeW=NULL;
	HANDLE hTempPipe=NULL;
	STARTUPINFO saInfo;
	PROCESS_INFORMATION psInfo;
	int nError;
	char pszCmdLine[4096];
	
	sprintf(pszCmdLine, "mpd.exe -timeout %d -hosts %s", 10000, m_input);
	//strcat(pszCmdLine, m_input);
	m_input = "";
	UpdateData(FALSE);

	// Don't handle errors, just let the process die.
	// In the future this will be configurable to allow various debugging options.
	//SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

	// Save stdin, stdout, and stderr
	hStdin = GetStdHandle(STD_INPUT_HANDLE);
	hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	hStderr = GetStdHandle(STD_ERROR_HANDLE);
	if (hStdin == INVALID_HANDLE_VALUE || hStdout == INVALID_HANDLE_VALUE  || hStderr == INVALID_HANDLE_VALUE)
	{
		nError = GetLastError();
		return;
	}

	// Set the security attributes to allow handles to be inherited
	SECURITY_ATTRIBUTES saAttr;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.lpSecurityDescriptor = NULL;
	saAttr.bInheritHandle = TRUE;

	// Create pipes for stdin, stdout, and stderr

	// Stdout
	if (!CreatePipe(&hTempPipe, &hStdoutPipeW, &saAttr, 0))
	{
		nError = GetLastError();
		goto CLEANUP;
	}
	// Make the read end of the stdout pipe not inheritable
	if (!DuplicateHandle(GetCurrentProcess(), hTempPipe, 
		GetCurrentProcess(), &hStdoutPipeR, 
		0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
	{
		nError = GetLastError();
		goto CLEANUP;
	}

	// Stdin
	if (!CreatePipe(&hStdinPipeR, &hTempPipe, &saAttr, 0))
	{
		nError = GetLastError();
		goto CLEANUP;
	}
	// Make the write end of the stdin pipe not inheritable
	if (!DuplicateHandle(GetCurrentProcess(), hTempPipe, 
		GetCurrentProcess(), &m_hStdinPipeW, 
		0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
	{
		nError = GetLastError();
		goto CLEANUP;
	}

	// Set stdin, stdout, and stderr to the ends of the pipe the created process will use
	if (!SetStdHandle(STD_INPUT_HANDLE, hStdinPipeR))
	{
		nError = GetLastError();
		goto CLEANUP;
	}
	if (!SetStdHandle(STD_OUTPUT_HANDLE, hStdoutPipeW))
	{
		nError = GetLastError();
		goto RESTORE_CLEANUP;
	}
	if (!SetStdHandle(STD_ERROR_HANDLE, hStdoutPipeW))
	{
		nError = GetLastError();
		goto RESTORE_CLEANUP;
	}

	// Set up the STARTINFO structure
	memset(&saInfo, 0, sizeof(STARTUPINFO));
	saInfo.cb         = sizeof(STARTUPINFO);
	saInfo.hStdInput  = hStdinPipeR;
	saInfo.hStdOutput = hStdoutPipeW;
	saInfo.hStdError  = hStdoutPipeW;
	saInfo.dwFlags    = STARTF_USESTDHANDLES;

	// Create the mpd process

	if (CreateProcess(
		NULL,
		pszCmdLine,
		NULL, NULL, TRUE,
		//DETACHED_PROCESS | IDLE_PRIORITY_CLASS, 
		//CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS,
		CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS | CREATE_NEW_PROCESS_GROUP,
		//DETACHED_PROCESS | IDLE_PRIORITY_CLASS | CREATE_NEW_PROCESS_GROUP,
		//CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS | CREATE_SUSPENDED, 
		NULL,
		NULL,
		&saInfo, &psInfo))
	{
		CloseHandle(psInfo.hThread);
		bSuccess = TRUE;
	}
	else
	{
		nError = GetLastError();
		//Translate_Error(*nError, error_msg, L"LaunchProcess:CreateProcessAsUser failed: ");
	}

RESTORE_CLEANUP:
	// Restore stdin, stdout, stderr
	if (!SetStdHandle(STD_INPUT_HANDLE, hStdin))
	{
		nError = GetLastError();
	}
	if (!SetStdHandle(STD_OUTPUT_HANDLE, hStdout))
	{
		nError = GetLastError();
	}
	if (!SetStdHandle(STD_ERROR_HANDLE, hStderr))
	{
		nError = GetLastError();
	}

CLEANUP:
	CloseHandle(hStdoutPipeW);
	CloseHandle(hStdinPipeR);

	if (bSuccess)
	{
		RedirectOutputThreadArg *pArg = new RedirectOutputThreadArg;
		pArg->hPipe = hStdoutPipeR;
		pArg->hWnd = m_hWnd;
		DWORD dwThreadID;
		m_hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectOutputThread, pArg, 0, &dwThreadID);

		DWORD dwNumWritten;
		char pBuffer[256];
		sprintf(pBuffer, "%s\n%s\n", pszAccount, pszPassword);
		WriteFile(m_hStdinPipeW, pBuffer, strlen(pBuffer), &dwNumWritten, NULL);
	}

	m_hProcess = psInfo.hProcess;
}

LRESULT CMPDRingDlg::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) 
{
	if (message == WM_COPYDATA)
	{
		COPYDATASTRUCT *pData = (COPYDATASTRUCT *)lParam;
		if (lParam)
		{
			UpdateData();
			/*
			char c;
			CString str = "";
			for (unsigned int i=0; i<pData->cbData; i++)
			{
				c = ((char*)pData->lpData)[i];
				if (c == '\r' || c == '\n')
				{
					if (i < pData->cbData-1)
					{
						c = ((char*)pData->lpData)[i+1];
						if (c == '\r' || c == '\n')
							i++;
					}
					//MessageBox("adding LF CR");
					m_output += str + '\r' + '\n';
				}
				else
					m_output += c;
			}
			//*/
			char *pString = new char[pData->cbData];
			memcpy(pString, pData->lpData, pData->cbData);
			char c;
			CString str = "";
			for (unsigned int i=0; i<pData->cbData; i++)
			{
				c = ((char*)pData->lpData)[i];
				if (c == '\r' || c == '\n' || i == (pData->cbData-1) )
				{
					if (i < pData->cbData-1)
					{
						c = ((char*)pData->lpData)[i+1];
						if (c == '\r' || c == '\n')
							i++;
					}
					m_list.InsertString(-1, str);
					m_list.SetCurSel(m_list.GetCount()-1);
					str = "";
				}
				else
					str += c;
			}

			UpdateData(FALSE);
		}
	}
	if (message == WM_USER+1)
	{
		UpdateData();
		m_list.InsertString(-1, "The mpds have exited");
		m_list.SetCurSel(m_list.GetCount()-1);
		CloseHandle(m_hStdinPipeW);
		m_hStdinPipeW = NULL;
		UpdateData(FALSE);
	}
	return CDialog::WindowProc(message, wParam, lParam);
}


void CMPDRingDlg::OnSize(UINT nType, int cx, int cy) 
{
	CDialog::OnSize(nType, cx, cy);
	
	if (IsWindow(m_list.m_hWnd))
	{
		RECT r;
		GetClientRect(&m_rDialog);

		r.left = m_rDialog.left + m_rList.left;
		r.right = m_rDialog.right + m_rList.right;
		r.top = m_rDialog.top + m_rList.top;
		r.bottom = m_rDialog.bottom + m_rList.bottom;
		m_list.MoveWindow(&r);

		r.left = m_rDialog.right + m_rEnter.left;
		r.right = m_rDialog.right + m_rEnter.right;
		r.top = m_rDialog.bottom + m_rEnter.top;
		r.bottom = m_rDialog.bottom + m_rEnter.bottom;
		m_enter_btn.MoveWindow(&r);

		r.left = m_rDialog.left + m_rInput.left;
		r.right = m_rDialog.right + m_rInput.right;
		r.top = m_rDialog.bottom + m_rInput.top;
		r.bottom = m_rDialog.bottom + m_rInput.bottom;
		m_input_box.MoveWindow(&r);
	
		r.left = m_rDialog.right + m_rQuit.left;
		r.right = m_rDialog.right + m_rQuit.right;
		r.top = m_rDialog.bottom + m_rQuit.top;
		r.bottom = m_rDialog.bottom + m_rQuit.bottom;
		m_quit_btn.MoveWindow(&r);
	}
}


void CMPDRingDlg::OnCancel() 
{
	if (m_hStdinPipeW != NULL)
	{
		DWORD dwNumWritten;
		CString str;
		str = "quit\n";
		WriteFile(m_hStdinPipeW, str.GetBuffer(0), str.GetLength(), &dwNumWritten, NULL);
		FlushFileBuffers(m_hStdinPipeW);
	}
	CDialog::OnCancel();
}
