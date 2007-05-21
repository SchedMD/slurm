// guiMPIJob.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "guiMPIJob.h"
#include "guiMPIJobDlg.h"
#include "mpdutil.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CGuiMPIJobApp

BEGIN_MESSAGE_MAP(CGuiMPIJobApp, CWinApp)
	//{{AFX_MSG_MAP(CGuiMPIJobApp)
	//}}AFX_MSG
	ON_COMMAND(ID_HELP, CWinApp::OnHelp)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CGuiMPIJobApp construction

CGuiMPIJobApp::CGuiMPIJobApp()
{
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CGuiMPIJobApp object

CGuiMPIJobApp theApp;

/////////////////////////////////////////////////////////////////////////////
// CGuiMPIJobApp initialization

BOOL CGuiMPIJobApp::InitInstance()
{
	AfxEnableControlContainer();

	// Standard initialization

#ifdef _AFXDLL
	Enable3dControls();			// Call this when using MFC in a shared DLL
#else
	Enable3dControlsStatic();	// Call this when linking to MFC statically
#endif

	CGuiMPIJobDlg dlg;

	easy_socket_init();

	m_pMainWnd = &dlg;
	int nResponse = dlg.DoModal();
	if (nResponse == IDOK)
	{
	}
	else if (nResponse == IDCANCEL)
	{
	}

	easy_socket_finalize();

	// Since the dialog has been closed, return FALSE so that we exit the
	//  application, rather than start the application's message pump.
	return FALSE;
}
