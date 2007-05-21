// PasswordDialog.cpp : implementation file
//

#include "stdafx.h"
#include "MPDFileTransfer.h"
#include "PasswordDialog.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CPasswordDialog dialog


CPasswordDialog::CPasswordDialog(CWnd* pParent /*=NULL*/)
	: CDialog(CPasswordDialog::IDD, pParent)
{
	//{{AFX_DATA_INIT(CPasswordDialog)
	m_phrase = _T("");
	//}}AFX_DATA_INIT
	m_bUseDefault = false;
}


void CPasswordDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CPasswordDialog)
	DDX_Control(pDX, IDC_PHRASE_EDIT, m_phrase_edit);
	DDX_Text(pDX, IDC_PHRASE_EDIT, m_phrase);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CPasswordDialog, CDialog)
	//{{AFX_MSG_MAP(CPasswordDialog)
	ON_BN_CLICKED(IDC_DEFAULT_RADIO, OnDefaultRadio)
	ON_BN_CLICKED(IDC_PWD_RADIO, OnPwdRadio)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CPasswordDialog message handlers

void CPasswordDialog::OnDefaultRadio() 
{
    m_bUseDefault = true;
    m_phrase_edit.EnableWindow(FALSE);
}

void CPasswordDialog::OnPwdRadio() 
{
    m_bUseDefault = false;
    m_phrase_edit.EnableWindow();
}
