// PwdDialog.cpp : implementation file
//

#include "stdafx.h"
#include "MPIConfig.h"
#include "PwdDialog.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CPwdDialog dialog


CPwdDialog::CPwdDialog(CWnd* pParent /*=NULL*/)
	: CDialog(CPwdDialog::IDD, pParent)
{
    m_bUseDefault = false;

	//{{AFX_DATA_INIT(CPwdDialog)
	m_password = _T("");
	//}}AFX_DATA_INIT
}


void CPwdDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CPwdDialog)
	DDX_Control(pDX, IDC_PASSWORD, m_pwd_ctrl);
	DDX_Text(pDX, IDC_PASSWORD, m_password);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CPwdDialog, CDialog)
	//{{AFX_MSG_MAP(CPwdDialog)
	ON_BN_CLICKED(IDC_PHRASE_RADIO, OnPhraseRadio)
	ON_BN_CLICKED(IDC_DEFAULT_RADIO, OnDefaultRadio)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CPwdDialog message handlers

void CPwdDialog::OnPhraseRadio() 
{
    m_bUseDefault = false;
    m_pwd_ctrl.EnableWindow();
}

void CPwdDialog::OnDefaultRadio() 
{
    m_bUseDefault = true;
    m_pwd_ctrl.EnableWindow(FALSE);
}
