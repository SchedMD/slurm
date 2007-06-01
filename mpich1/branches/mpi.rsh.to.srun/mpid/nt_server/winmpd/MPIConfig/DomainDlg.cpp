// DomainDlg.cpp : implementation file
//

#include "stdafx.h"
#include "MPIConfig.h"
#include "DomainDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CDomainDlg dialog


CDomainDlg::CDomainDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CDomainDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CDomainDlg)
	m_domain = _T("");
	//}}AFX_DATA_INIT
}


void CDomainDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CDomainDlg)
	DDX_Text(pDX, IDC_DOMAIN_EDIT, m_domain);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CDomainDlg, CDialog)
	//{{AFX_MSG_MAP(CDomainDlg)
		// NOTE: the ClassWizard will add message map macros here
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CDomainDlg message handlers
