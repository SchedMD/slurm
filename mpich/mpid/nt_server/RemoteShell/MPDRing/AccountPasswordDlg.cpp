// AccountPasswordDlg.cpp : implementation file
//

#include "stdafx.h"
#include "MPDRing.h"
#include "AccountPasswordDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CAccountPasswordDlg dialog


CAccountPasswordDlg::CAccountPasswordDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CAccountPasswordDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CAccountPasswordDlg)
	m_account = _T("");
	m_password = _T("");
	//}}AFX_DATA_INIT
}


void CAccountPasswordDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAccountPasswordDlg)
	DDX_Text(pDX, IDC_ACCOUNT, m_account);
	DDX_Text(pDX, IDC_PASSWORD, m_password);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CAccountPasswordDlg, CDialog)
	//{{AFX_MSG_MAP(CAccountPasswordDlg)
		// NOTE: the ClassWizard will add message map macros here
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CAccountPasswordDlg message handlers
