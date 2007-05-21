// WildStrDlg.cpp : implementation file
//

#include "stdafx.h"
#include "mpdupdate.h"
#include "WildStrDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CWildStrDlg dialog


CWildStrDlg::CWildStrDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CWildStrDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CWildStrDlg)
	m_wildstr = _T("");
	//}}AFX_DATA_INIT
}


void CWildStrDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CWildStrDlg)
	DDX_Text(pDX, IDC_WILDSTR_EDIT, m_wildstr);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CWildStrDlg, CDialog)
	//{{AFX_MSG_MAP(CWildStrDlg)
		// NOTE: the ClassWizard will add message map macros here
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CWildStrDlg message handlers
