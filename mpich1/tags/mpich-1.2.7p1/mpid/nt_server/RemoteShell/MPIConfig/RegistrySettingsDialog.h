#if !defined(AFX_REGISTRYSETTINGSDIALOG_H__1722FDD9_0600_48B5_9CA6_1916620C74CC__INCLUDED_)
#define AFX_REGISTRYSETTINGSDIALOG_H__1722FDD9_0600_48B5_9CA6_1916620C74CC__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// RegistrySettingsDialog.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CRegistrySettingsDialog dialog

class CRegistrySettingsDialog : public CDialog
{
// Construction
public:
	CRegistrySettingsDialog(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CRegistrySettingsDialog)
	enum { IDD = IDD_SET_DIALOG };
	CEdit	m_LaunchTimeoutEdit;
	CEdit	m_TempEdit;
	BOOL	m_bTempChk;
	BOOL	m_bHostsChk;
	CString	m_pszTempDir;
	int		m_nLaunchTimeout;
	BOOL	m_bLaunchTimeoutChk;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CRegistrySettingsDialog)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CRegistrySettingsDialog)
	afx_msg void OnTempChk();
	virtual BOOL OnInitDialog();
	afx_msg void OnTimeoutChk();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_REGISTRYSETTINGSDIALOG_H__1722FDD9_0600_48B5_9CA6_1916620C74CC__INCLUDED_)
