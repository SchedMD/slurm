/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#if !defined(AFX_ADVANCEDOPTIONSDLG_H__B39EC945_72E1_4D77_B70A_E46D39A2780E__INCLUDED_)
#define AFX_ADVANCEDOPTIONSDLG_H__B39EC945_72E1_4D77_B70A_E46D39A2780E__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// AdvancedOptionsDlg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CAdvancedOptionsDlg dialog

class CAdvancedOptionsDlg : public CDialog
{
// Construction
public:
	CAdvancedOptionsDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CAdvancedOptionsDlg)
	enum { IDD = IDD_ADV_OPTIONS_DLG };
	CEdit	m_jobhost_edit;
	CButton	m_map_chk;
	CButton	m_dir_chk;
	CEdit	m_map_edit;
	CButton	m_redirect_browse_btn;
	CEdit	m_output_edit;
	CButton	m_env_chk;
	CButton	m_slave_chk;
	CEdit	m_config_edit;
	CButton	m_config_browse_btn;
	CButton	m_dir_browse_btn;
	CEdit	m_slave_edit;
	CButton	m_slave_browse_btn;
	CEdit	m_env_edit;
	CEdit	m_dir_edit;
	BOOL	m_bDir;
	CString	m_directory;
	BOOL	m_bEnv;
	CString	m_environment;
	BOOL	m_bNoClear;
	BOOL	m_bNoMPI;
	CString	m_slave;
	BOOL	m_bSlave;
	BOOL	m_bPassword;
	CString	m_config_filename;
	BOOL	m_bConfig;
	CString	m_output_filename;
	BOOL	m_bRedirect;
	BOOL	m_bNoColor;
	CString	m_map;
	BOOL	m_bMap;
	BOOL	m_bCatch;
	CString	m_jobhost;
	BOOL	m_bUseJobHost;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAdvancedOptionsDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CAdvancedOptionsDlg)
	afx_msg void OnSlaveBrowseBtn();
	afx_msg void OnSlaveChk();
	afx_msg void OnDirChk();
	afx_msg void OnEnvChk();
	virtual BOOL OnInitDialog();
	afx_msg void OnDirBrowseBtn();
	afx_msg void OnConfigBrowseBtn();
	afx_msg void OnConfigChk();
	afx_msg void OnRedirectChk();
	afx_msg void OnRedirectBrowseBtn();
	afx_msg void OnMapChk();
	afx_msg void OnHelpBtn();
	afx_msg void OnJobhostChk();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_ADVANCEDOPTIONSDLG_H__B39EC945_72E1_4D77_B70A_E46D39A2780E__INCLUDED_)
