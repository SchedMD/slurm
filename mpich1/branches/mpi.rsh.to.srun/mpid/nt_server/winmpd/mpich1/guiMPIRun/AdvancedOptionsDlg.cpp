// AdvancedOptionsDlg.cpp : implementation file
//

#include "stdafx.h"
#include "guiMPIRun.h"
#include "AdvancedOptionsDlg.h"
#include "DirDialog.h"
#include "HelpDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CAdvancedOptionsDlg dialog


CAdvancedOptionsDlg::CAdvancedOptionsDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CAdvancedOptionsDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CAdvancedOptionsDlg)
	m_bDir = FALSE;
	m_directory = _T("");
	m_bEnv = FALSE;
	m_environment = _T("");
	m_bNoClear = FALSE;
	m_bNoMPI = FALSE;
	m_slave = _T("");
	m_bSlave = FALSE;
	m_bPassword = FALSE;
	m_config_filename = _T("");
	m_bConfig = FALSE;
	m_output_filename = _T("");
	m_bRedirect = FALSE;
	m_bNoColor = FALSE;
	m_map = _T("");
	m_bMap = FALSE;
	m_bCatch = FALSE;
	m_jobhost = _T("");
	m_bUseJobHost = FALSE;
	//}}AFX_DATA_INIT
}


void CAdvancedOptionsDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAdvancedOptionsDlg)
	DDX_Control(pDX, IDC_JOBHOST, m_jobhost_edit);
	DDX_Control(pDX, IDC_MAP_CHK, m_map_chk);
	DDX_Control(pDX, IDC_DIR_CHK, m_dir_chk);
	DDX_Control(pDX, IDC_DRIVEMAPPINGS, m_map_edit);
	DDX_Control(pDX, IDC_REDIRECT_BROWSE_BTN, m_redirect_browse_btn);
	DDX_Control(pDX, IDC_OUTPUT_FILENAME, m_output_edit);
	DDX_Control(pDX, IDC_ENV_CHK, m_env_chk);
	DDX_Control(pDX, IDC_SLAVE_CHK, m_slave_chk);
	DDX_Control(pDX, IDC_CONFIG, m_config_edit);
	DDX_Control(pDX, IDC_CONFIG_BROWSE_BTN, m_config_browse_btn);
	DDX_Control(pDX, IDC_DIR_BROWSE_BTN, m_dir_browse_btn);
	DDX_Control(pDX, IDC_SLAVE, m_slave_edit);
	DDX_Control(pDX, IDC_SLAVE_BROWSE_BTN, m_slave_browse_btn);
	DDX_Control(pDX, IDC_ENVIRONMENT, m_env_edit);
	DDX_Control(pDX, IDC_DIRECTORY, m_dir_edit);
	DDX_Check(pDX, IDC_DIR_CHK, m_bDir);
	DDX_Text(pDX, IDC_DIRECTORY, m_directory);
	DDX_Check(pDX, IDC_ENV_CHK, m_bEnv);
	DDX_Text(pDX, IDC_ENVIRONMENT, m_environment);
	DDX_Check(pDX, IDC_NOCLEAR_CHK, m_bNoClear);
	DDX_Check(pDX, IDC_NOMPI_CHK, m_bNoMPI);
	DDX_Text(pDX, IDC_SLAVE, m_slave);
	DDX_Check(pDX, IDC_SLAVE_CHK, m_bSlave);
	DDX_Check(pDX, IDC_PASSWORD_CHK, m_bPassword);
	DDX_Text(pDX, IDC_CONFIG, m_config_filename);
	DDX_Check(pDX, IDC_CONFIG_CHK, m_bConfig);
	DDX_Text(pDX, IDC_OUTPUT_FILENAME, m_output_filename);
	DDX_Check(pDX, IDC_REDIRECT_CHK, m_bRedirect);
	DDX_Check(pDX, IDC_NOCOLOR_CHK, m_bNoColor);
	DDX_Text(pDX, IDC_DRIVEMAPPINGS, m_map);
	DDX_Check(pDX, IDC_MAP_CHK, m_bMap);
	DDX_Check(pDX, IDC_CATCH_CHK, m_bCatch);
	DDX_Text(pDX, IDC_JOBHOST, m_jobhost);
	DDX_Check(pDX, IDC_JOBHOST_CHK, m_bUseJobHost);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CAdvancedOptionsDlg, CDialog)
	//{{AFX_MSG_MAP(CAdvancedOptionsDlg)
	ON_BN_CLICKED(IDC_SLAVE_BROWSE_BTN, OnSlaveBrowseBtn)
	ON_BN_CLICKED(IDC_SLAVE_CHK, OnSlaveChk)
	ON_BN_CLICKED(IDC_DIR_CHK, OnDirChk)
	ON_BN_CLICKED(IDC_ENV_CHK, OnEnvChk)
	ON_BN_CLICKED(IDC_DIR_BROWSE_BTN, OnDirBrowseBtn)
	ON_BN_CLICKED(IDC_CONFIG_BROWSE_BTN, OnConfigBrowseBtn)
	ON_BN_CLICKED(IDC_CONFIG_CHK, OnConfigChk)
	ON_BN_CLICKED(IDC_REDIRECT_CHK, OnRedirectChk)
	ON_BN_CLICKED(IDC_REDIRECT_BROWSE_BTN, OnRedirectBrowseBtn)
	ON_BN_CLICKED(IDC_MAP_CHK, OnMapChk)
	ON_BN_CLICKED(IDC_HELP_BTN, OnHelpBtn)
	ON_BN_CLICKED(IDC_JOBHOST_CHK, OnJobhostChk)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CAdvancedOptionsDlg message handlers

BOOL CAdvancedOptionsDlg::OnInitDialog() 
{
	CDialog::OnInitDialog();
	
	if (!m_bSlave || m_bConfig)
	{
	    m_slave_edit.EnableWindow(FALSE);
	    m_slave_browse_btn.EnableWindow(FALSE);
	}
	m_dir_edit.EnableWindow(m_bDir);
	if (!m_bEnv || m_bConfig)
	{
	    m_env_edit.EnableWindow(FALSE);
	    m_dir_browse_btn.EnableWindow(FALSE);
	}
	if (m_bConfig)
	{
	    m_slave_chk.EnableWindow(FALSE);
	    m_slave_browse_btn.EnableWindow(FALSE);
	    m_env_chk.EnableWindow(FALSE);
	}
	else
	{
	    m_config_edit.EnableWindow(FALSE);
	    m_config_browse_btn.EnableWindow(FALSE);
	}
	if (!m_bRedirect)
	{
	    m_output_edit.EnableWindow(FALSE);
	    m_redirect_browse_btn.EnableWindow(FALSE);
	}
	m_map_edit.EnableWindow(m_bMap);
	m_jobhost_edit.EnableWindow(m_bUseJobHost);
	
	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CAdvancedOptionsDlg::OnSlaveBrowseBtn() 
{
    UpdateData();
    CFileDialog f(
	TRUE, "*.exe", m_slave, 
	OFN_HIDEREADONLY | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST,
	"Executables (*.exe)|*.exe|All files (*.*)|*.*||"
	);
    if (f.DoModal() == IDOK)
    {
	POSITION p;
	p = f.GetStartPosition();
	m_slave = f.GetNextPathName(p);
	UpdateData(FALSE);
    }
}

void CAdvancedOptionsDlg::OnSlaveChk() 
{
    UpdateData();
    if (m_bSlave)
    {
	m_slave_edit.EnableWindow();
	m_slave_browse_btn.EnableWindow();
    }
    else
    {
	m_slave_edit.EnableWindow(FALSE);
	m_slave_browse_btn.EnableWindow(FALSE);
    }
}

void CAdvancedOptionsDlg::OnDirChk() 
{
    UpdateData();
    if (m_bDir)
    {
	m_dir_edit.EnableWindow();
	m_dir_browse_btn.EnableWindow();
    }
    else
    {
	m_dir_edit.EnableWindow(FALSE);
	m_dir_browse_btn.EnableWindow(FALSE);
    }
}

void CAdvancedOptionsDlg::OnEnvChk() 
{
    UpdateData();
    if (m_bEnv)
    {
	m_env_edit.EnableWindow();
    }
    else
    {
	m_env_edit.EnableWindow(FALSE);
    }
}

void CAdvancedOptionsDlg::OnDirBrowseBtn() 
{
    CDirDialog dlg;

    UpdateData();
    
    dlg.m_strTitle = "Select the working directory for the application";
    dlg.m_strInitDir = "";
    dlg.m_strSelDir = m_directory;
    if (dlg.DoBrowse() == IDOK)
    {
	m_directory = dlg.m_strPath;
	UpdateData(FALSE);
    }
}

void CAdvancedOptionsDlg::OnConfigBrowseBtn() 
{
    UpdateData();
    CFileDialog f(
	TRUE, "*.cfg", m_config_filename, 
	OFN_HIDEREADONLY | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST,
	"Configuration files (*.cfg)|*.cfg|All files (*.*)|*.*||"
	);
    if (f.DoModal() == IDOK)
    {
	POSITION p;
	p = f.GetStartPosition();
	m_config_filename = f.GetNextPathName(p);
	UpdateData(FALSE);
    }
}

void CAdvancedOptionsDlg::OnConfigChk() 
{
    UpdateData();
    if (m_bConfig)
    {
	m_config_edit.EnableWindow();
	m_config_browse_btn.EnableWindow();

	m_env_chk.EnableWindow(FALSE);
	m_env_edit.EnableWindow(FALSE);
	m_slave_chk.EnableWindow(FALSE);
	m_slave_edit.EnableWindow(FALSE);
	m_slave_browse_btn.EnableWindow(FALSE);
	m_dir_chk.EnableWindow(FALSE);
	m_dir_edit.EnableWindow(FALSE);
	m_dir_browse_btn.EnableWindow(FALSE);
	m_map_chk.EnableWindow(FALSE);
	m_map_edit.EnableWindow(FALSE);
    }
    else
    {
	m_config_edit.EnableWindow(FALSE);
	m_config_browse_btn.EnableWindow(FALSE);

	m_env_chk.EnableWindow();
	if (m_bEnv)
	    m_env_edit.EnableWindow();
	m_slave_chk.EnableWindow();
	if (m_bSlave)
	{
	    m_slave_edit.EnableWindow();
	    m_slave_browse_btn.EnableWindow();
	}
	m_dir_chk.EnableWindow();
	if (m_bDir)
	{
	    m_dir_edit.EnableWindow();
	    m_dir_browse_btn.EnableWindow();
	}
	m_map_chk.EnableWindow();
	if (m_bMap)
	    m_map_edit.EnableWindow();
    }
}

void CAdvancedOptionsDlg::OnRedirectChk() 
{
    UpdateData();
    if (m_bRedirect)
    {
	m_output_edit.EnableWindow();
	m_redirect_browse_btn.EnableWindow();
    }
    else
    {
	m_output_edit.EnableWindow(FALSE);
	m_redirect_browse_btn.EnableWindow(FALSE);
    }
}

void CAdvancedOptionsDlg::OnRedirectBrowseBtn() 
{
    UpdateData();
    CFileDialog f(
	TRUE, "*.txt", m_output_filename, 
	OFN_HIDEREADONLY | OFN_EXPLORER | OFN_PATHMUSTEXIST,
	//"Text files (*.txt)|*.txt|All files (*.*)|*.*||"
	"Text files (*.txt)|*.txt|Output files (*.out)|*.out|All files (*.*)|*.*||"
	);
    if (f.DoModal() == IDOK)
    {
	POSITION p;
	p = f.GetStartPosition();
	m_output_filename = f.GetNextPathName(p);
	UpdateData(FALSE);
    }
}

void CAdvancedOptionsDlg::OnMapChk() 
{
    UpdateData();
    if (m_bMap)
    {
	m_map_edit.EnableWindow();
    }
    else
    {
	m_map_edit.EnableWindow(FALSE);
    }
}

void CAdvancedOptionsDlg::OnHelpBtn() 
{
    CHelpDlg dlg;
    dlg.DoModal();
}

void CAdvancedOptionsDlg::OnJobhostChk() 
{
    UpdateData();
    m_jobhost_edit.EnableWindow(m_bUseJobHost);
}
