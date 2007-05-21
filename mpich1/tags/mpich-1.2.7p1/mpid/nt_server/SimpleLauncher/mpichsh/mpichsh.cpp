// mpichsh.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "mpichsh.h"
#include "mpichshDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CMpichshApp

BEGIN_MESSAGE_MAP(CMpichshApp, CWinApp)
	//{{AFX_MSG_MAP(CMpichshApp)
	//}}AFX_MSG
	ON_COMMAND(ID_HELP, CWinApp::OnHelp)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMpichshApp construction

CMpichshApp::CMpichshApp()
{
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CMpichshApp object

CMpichshApp theApp;

/////////////////////////////////////////////////////////////////////////////
// CMpichshApp initialization

BOOL CMpichshApp::InitInstance()
{
	/*
	if (!AfxSocketInit())
	{
		AfxMessageBox(IDP_SOCKETS_INIT_FAILED);
		return FALSE;
	}
	//*/

	AfxEnableControlContainer();

	// Standard initialization

#ifdef _AFXDLL
	Enable3dControls();			// Call this when using MFC in a shared DLL
#else
	Enable3dControlsStatic();	// Call this when linking to MFC statically
#endif

	CMpichshDlg dlg;
	m_pMainWnd = &dlg;
	if (__argc > 1)
		dlg.m_nPort = atoi(__argv[1]);
	int nResponse = dlg.DoModal();
	if (nResponse == IDOK)
	{
	}
	else if (nResponse == IDCANCEL)
	{
	}

	// Since the dialog has been closed, return FALSE so that we exit the
	//  application, rather than start the application's message pump.
	return FALSE;
}
