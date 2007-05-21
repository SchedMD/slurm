// AdvancedConnectDialog.cpp : implementation file
//

#include "stdafx.h"
#include "MPDFileTransfer.h"
#include "AdvancedConnectDialog.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CAdvancedConnectDialog dialog


CAdvancedConnectDialog::CAdvancedConnectDialog(CWnd* pParent /*=NULL*/)
	: CDialog(CAdvancedConnectDialog::IDD, pParent)
{
	//{{AFX_DATA_INIT(CAdvancedConnectDialog)
	m_account1 = _T("");
	m_account2 = _T("");
	m_host1 = _T("");
	m_host2 = _T("");
	m_password1 = _T("");
	m_password2 = _T("");
	m_phrase1 = _T("");
	m_phrase2 = _T("");
	m_port1 = 0;
	m_port2 = 0;
	m_root1 = _T("");
	m_root2 = _T("");
	//}}AFX_DATA_INIT
}


void CAdvancedConnectDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAdvancedConnectDialog)
	DDX_Text(pDX, IDC_ACCOUNT1, m_account1);
	DDX_Text(pDX, IDC_ACCOUNT2, m_account2);
	DDX_Text(pDX, IDC_HOST1, m_host1);
	DDX_Text(pDX, IDC_HOST2, m_host2);
	DDX_Text(pDX, IDC_PASSWORD1, m_password1);
	DDX_Text(pDX, IDC_PASSWORD2, m_password2);
	DDX_Text(pDX, IDC_PHRASE1, m_phrase1);
	DDX_Text(pDX, IDC_PHRASE2, m_phrase2);
	DDX_Text(pDX, IDC_PORT1, m_port1);
	DDX_Text(pDX, IDC_PORT2, m_port2);
	DDX_Text(pDX, IDC_ROOT1, m_root1);
	DDX_Text(pDX, IDC_ROOT2, m_root2);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CAdvancedConnectDialog, CDialog)
	//{{AFX_MSG_MAP(CAdvancedConnectDialog)
		// NOTE: the ClassWizard will add message map macros here
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CAdvancedConnectDialog message handlers
