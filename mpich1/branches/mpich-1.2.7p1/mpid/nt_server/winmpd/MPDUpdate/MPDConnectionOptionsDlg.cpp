// MPDConnectionOptionsDlg.cpp : implementation file
//

#include "stdafx.h"
#include "mpdupdate.h"
#include "MPDConnectionOptionsDlg.h"
#include "mpd.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CMPDConnectionOptionsDlg dialog


CMPDConnectionOptionsDlg::CMPDConnectionOptionsDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CMPDConnectionOptionsDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CMPDConnectionOptionsDlg)
	m_phrase = _T("");
	m_port = MPD_DEFAULT_PORT;
	m_bFastConnect = FALSE;
	//}}AFX_DATA_INIT
    m_bPhrase = false;
    m_bPort = false;
}


void CMPDConnectionOptionsDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CMPDConnectionOptionsDlg)
	DDX_Control(pDX, IDC_PORT, m_port_edit);
	DDX_Control(pDX, IDC_PASSPHRASE, m_phrase_edit);
	DDX_Text(pDX, IDC_PASSPHRASE, m_phrase);
	DDX_Text(pDX, IDC_PORT, m_port);
	DDV_MinMaxInt(pDX, m_port, 1, 65000);
	DDX_Control(pDX, IDC_DEFAULT_PASSPHRASE_RADIO, m_def_phrase_radio);
	DDX_Control(pDX, IDC_PASSPHRASE_RADIO, m_phrase_radio);
	DDX_Control(pDX, IDC_DEFAULT_PORT_RADIO, m_def_port_radio);
	DDX_Control(pDX, IDC_PORT_RADIO, m_port_radio);
	DDX_Check(pDX, IDC_FAST_CHECK, m_bFastConnect);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CMPDConnectionOptionsDlg, CDialog)
	//{{AFX_MSG_MAP(CMPDConnectionOptionsDlg)
	ON_BN_CLICKED(IDC_PASSPHRASE_RADIO, OnPassphraseRadio)
	ON_BN_CLICKED(IDC_DEFAULT_PASSPHRASE_RADIO, OnDefaultPassphraseRadio)
	ON_BN_CLICKED(IDC_DEFAULT_PORT_RADIO, OnDefaultPortRadio)
	ON_BN_CLICKED(IDC_PORT_RADIO, OnPortRadio)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMPDConnectionOptionsDlg message handlers

BOOL CMPDConnectionOptionsDlg::OnInitDialog() 
{
    CDialog::OnInitDialog();
    
    if (m_phrase == MPD_DEFAULT_PASSPHRASE)
    {
	m_phrase_radio.SetCheck(0);
	m_def_phrase_radio.SetCheck(1);
	m_phrase_edit.EnableWindow(FALSE);
	m_bPhrase = false;
    }
    else
    {
	m_def_phrase_radio.SetCheck(0);
	m_phrase_radio.SetCheck(1);
	m_phrase_edit.EnableWindow();
	m_bPhrase = true;
    }

    if (m_port == MPD_DEFAULT_PORT)
    {
	m_port_radio.SetCheck(0);
	m_def_port_radio.SetCheck(1);
	m_port_edit.EnableWindow(FALSE);
	m_bPort = false;
    }
    else
    {
	m_def_port_radio.SetCheck(0);
	m_port_radio.SetCheck(1);
	m_port_edit.EnableWindow();
	m_bPort = true;
    }
    
    return TRUE;  // return TRUE unless you set the focus to a control
}

void CMPDConnectionOptionsDlg::OnDefaultPassphraseRadio() 
{
    m_phrase_edit.EnableWindow(FALSE);
    m_bPhrase = false;
}

void CMPDConnectionOptionsDlg::OnPassphraseRadio() 
{
    m_phrase_edit.EnableWindow();
    m_bPhrase = true;
}

void CMPDConnectionOptionsDlg::OnDefaultPortRadio() 
{
    m_port_edit.EnableWindow(FALSE);
    m_bPort = false;
}

void CMPDConnectionOptionsDlg::OnPortRadio() 
{
    m_port_edit.EnableWindow();
    m_bPort = true;
}
