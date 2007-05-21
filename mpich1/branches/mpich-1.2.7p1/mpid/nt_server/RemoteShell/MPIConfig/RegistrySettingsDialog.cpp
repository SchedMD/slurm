// RegistrySettingsDialog.cpp : implementation file
//

#include "stdafx.h"
#include "MPIConfig.h"
#include "RegistrySettingsDialog.h"

#include "..\Common\MPIJobDefs.h"
#define DEFAULT_LAUNCH_TIMEOUT 15000

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CRegistrySettingsDialog dialog


CRegistrySettingsDialog::CRegistrySettingsDialog(CWnd* pParent /*=NULL*/)
	: CDialog(CRegistrySettingsDialog::IDD, pParent)
{
	//{{AFX_DATA_INIT(CRegistrySettingsDialog)
	m_bTempChk = FALSE;
	m_bHostsChk = TRUE;
	m_pszTempDir = _T("C:\\");
	m_nLaunchTimeout = DEFAULT_LAUNCH_TIMEOUT;
	m_bLaunchTimeoutChk = FALSE;
	//}}AFX_DATA_INIT
}


void CRegistrySettingsDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CRegistrySettingsDialog)
	DDX_Control(pDX, IDC_LAUNCH_TIMEOUT, m_LaunchTimeoutEdit);
	DDX_Control(pDX, IDC_TEMP_EDIT, m_TempEdit);
	DDX_Check(pDX, IDC_TEMP_CHK, m_bTempChk);
	DDX_Check(pDX, IDC_HOSTS_CHK, m_bHostsChk);
	DDX_Text(pDX, IDC_TEMP_EDIT, m_pszTempDir);
	DDX_Text(pDX, IDC_LAUNCH_TIMEOUT, m_nLaunchTimeout);
	DDV_MinMaxInt(pDX, m_nLaunchTimeout, 1000, 300000);
	DDX_Check(pDX, IDC_TIMEOUT_CHK, m_bLaunchTimeoutChk);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CRegistrySettingsDialog, CDialog)
	//{{AFX_MSG_MAP(CRegistrySettingsDialog)
	ON_BN_CLICKED(IDC_TEMP_CHK, OnTempChk)
	ON_BN_CLICKED(IDC_TIMEOUT_CHK, OnTimeoutChk)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CRegistrySettingsDialog message handlers

void CRegistrySettingsDialog::OnTempChk() 
{
	UpdateData();

	if (m_bTempChk)
		m_TempEdit.EnableWindow();
	else
		m_TempEdit.EnableWindow(FALSE);
}

BOOL CRegistrySettingsDialog::OnInitDialog() 
{
	CDialog::OnInitDialog();
	
	m_TempEdit.EnableWindow(FALSE);
	m_LaunchTimeoutEdit.EnableWindow(FALSE);

	// Get launch timeout from registry
	DWORD ret_val, type, num_bytes = sizeof(DWORD);
	HKEY hKey;

	// Open the MPICH root key
	if ((ret_val = RegOpenKeyEx(
			HKEY_LOCAL_MACHINE, 
			MPICHKEY,
			0, KEY_READ | KEY_WRITE, &hKey)) == ERROR_SUCCESS)
	{
		// Read the launch timeout number
		ret_val = RegQueryValueEx(hKey, TEXT("LaunchTimeout"), 0, &type, (BYTE *)&m_nLaunchTimeout, &num_bytes);

		if (m_nLaunchTimeout == 0)
			m_nLaunchTimeout = DEFAULT_LAUNCH_TIMEOUT;
		RegCloseKey(hKey);
	}

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CRegistrySettingsDialog::OnTimeoutChk() 
{
	UpdateData();

	if (m_bLaunchTimeoutChk)
		m_LaunchTimeoutEdit.EnableWindow();
	else
		m_LaunchTimeoutEdit.EnableWindow(FALSE);
}
