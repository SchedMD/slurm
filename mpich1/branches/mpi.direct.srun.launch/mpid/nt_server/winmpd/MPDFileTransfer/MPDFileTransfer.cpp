// MPDFileTransfer.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "MPDFileTransfer.h"
#include "MPDFileTransferDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CMPDFileTransferApp

BEGIN_MESSAGE_MAP(CMPDFileTransferApp, CWinApp)
	//{{AFX_MSG_MAP(CMPDFileTransferApp)
	//}}AFX_MSG
	ON_COMMAND(ID_HELP, CWinApp::OnHelp)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMPDFileTransferApp construction

CMPDFileTransferApp::CMPDFileTransferApp()
{
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CMPDFileTransferApp object

CMPDFileTransferApp theApp;

/////////////////////////////////////////////////////////////////////////////
// CMPDFileTransferApp initialization

BOOL CMPDFileTransferApp::InitInstance()
{
    AfxEnableControlContainer();
    
    // Standard initialization
    
#ifdef _AFXDLL
    Enable3dControls();			// Call this when using MFC in a shared DLL
#else
    Enable3dControlsStatic();	// Call this when linking to MFC statically
#endif
    if (!AfxOleInit())
	AfxMessageBox("Error");
    
    CMPDFileTransferDlg dlg;
    m_pMainWnd = &dlg;
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
