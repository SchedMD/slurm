// UserPwdDialog.cpp : implementation file
//

#include "stdafx.h"
#include "guiMPIRun.h"
#include "UserPwdDialog.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CUserPwdDialog dialog


CUserPwdDialog::CUserPwdDialog(CWnd* pParent /*=NULL*/)
	: CDialog(CUserPwdDialog::IDD, pParent)
{
	//{{AFX_DATA_INIT(CUserPwdDialog)
	m_account = _T("");
	m_password = _T("");
	m_remember = FALSE;
	//}}AFX_DATA_INIT
}


void CUserPwdDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CUserPwdDialog)
	DDX_Text(pDX, IDC_ACCOUNT, m_account);
	DDX_Text(pDX, IDC_PASSWORD, m_password);
	DDX_Check(pDX, IDC_REMEMBER_CHK, m_remember);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CUserPwdDialog, CDialog)
	//{{AFX_MSG_MAP(CUserPwdDialog)
		// NOTE: the ClassWizard will add message map macros here
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CUserPwdDialog message handlers
