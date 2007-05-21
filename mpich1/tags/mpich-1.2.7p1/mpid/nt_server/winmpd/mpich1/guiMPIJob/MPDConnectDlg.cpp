// MPDConnectDlg.cpp : implementation file
//

#include "stdafx.h"
#include "guiMPIJob.h"
#include "MPDConnectDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CMPDConnectDlg dialog


CMPDConnectDlg::CMPDConnectDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CMPDConnectDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CMPDConnectDlg)
	m_host = _T("");
	m_phrase = _T("");
	m_bPhraseChecked = FALSE;
	m_port = 0;
	m_bPortChecked = FALSE;
	//}}AFX_DATA_INIT
}


void CMPDConnectDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CMPDConnectDlg)
	DDX_Control(pDX, IDC_PORT, m_port_edit);
	DDX_Control(pDX, IDC_PHRASE, m_phrase_edit);
	DDX_Text(pDX, IDC_JOB_HOST, m_host);
	DDX_Text(pDX, IDC_PHRASE, m_phrase);
	DDX_Check(pDX, IDC_PHRASE_CHK, m_bPhraseChecked);
	DDX_Text(pDX, IDC_PORT, m_port);
	DDV_MinMaxInt(pDX, m_port, 1, 65000);
	DDX_Check(pDX, IDC_PORT_CHK, m_bPortChecked);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CMPDConnectDlg, CDialog)
	//{{AFX_MSG_MAP(CMPDConnectDlg)
	ON_BN_CLICKED(IDC_PORT_CHK, OnPortChk)
	ON_BN_CLICKED(IDC_PHRASE_CHK, OnPhraseChk)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMPDConnectDlg message handlers

BOOL CMPDConnectDlg::OnInitDialog() 
{
	CDialog::OnInitDialog();

	m_phrase_edit.EnableWindow(FALSE);
	m_port_edit.EnableWindow(FALSE);

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CMPDConnectDlg::OnPortChk() 
{
    UpdateData();
    m_port_edit.EnableWindow(m_bPortChecked);
}

void CMPDConnectDlg::OnPhraseChk() 
{
    UpdateData();
    m_phrase_edit.EnableWindow(m_bPhraseChecked);
}
