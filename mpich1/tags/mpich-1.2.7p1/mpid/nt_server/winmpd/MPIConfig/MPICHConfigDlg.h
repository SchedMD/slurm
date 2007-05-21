/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#if !defined(AFX_MPICHCONFIGDLG_H__21835812_B3D5_4E84_B37C_665D378EA803__INCLUDED_)
#define AFX_MPICHCONFIGDLG_H__21835812_B3D5_4E84_B37C_665D378EA803__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// MPICHConfigDlg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CMPICHConfigDlg dialog

class CMPICHConfigDlg : public CDialog
{
// Construction
public:
	CMPICHConfigDlg(CWnd* pParent = NULL);   // standard constructor

	bool m_bToggle, m_bHostToggle, m_buse_default_passphrase;
    HANDLE m_hApplyBtnThread;
    //DWORD m_num_threads;
    DWORD m_num_outstanding;
    int m_nPort;
    //char m_pszHost[100];
    char m_pszPhrase[100];
    bool m_bNeedPassword;
    void ParseRegistry();

// Dialog Data
	//{{AFX_DATA(CMPICHConfigDlg)
	enum { IDD = IDD_CONFIG_DLG };
	CStatic	m_logfile_static;
	CButton	m_host_logfile_yes_btn;
	CButton	m_host_logfile_no_btn;
	CEdit	m_host_logfile_edit;
	CButton	m_host_logfile_chk;
	CButton	m_logfile_yes_btn;
	CButton	m_logfile_no_btn;
	CEdit	m_logfile_edit;
	CButton	m_host_codes_yes_btn;
	CButton	m_host_codes_no_btn;
	CButton	m_host_codes_chk;
	CButton	m_codes_yes_btn;
	CButton	m_codes_no_btn;
	CButton	m_host_localroot_yes_btn;
	CButton	m_host_localroot_no_btn;
	CButton	m_host_localroot_chk;
	CButton	m_localroot_yes_btn;
	CButton	m_localroot_no_btn;
	CEdit	m_config_host_msg_static;
	CButton	m_host_catch_yes_btn;
	CButton	m_host_catch_no_btn;
	CButton	m_host_catch_chk;
	CButton	m_catch_yes_btn;
	CButton	m_catch_no_btn;
	CStatic	m_config_host_static;
	CStatic	m_modify_static;
	CButton	m_use_jobhost_pwd_chk;
	CButton	m_toggle_btn;
	CProgressCtrl	m_progress;
	CButton	m_popup_debug_yes_btn;
	CButton	m_popup_debug_no_btn;
	CEdit	m_mpd_phrase_edit;
	CButton	m_modify_btn;
	CButton	m_mapping_yes_btn;
	CButton	m_mapping_no_btn;
	CEdit	m_launch_edit;
	CButton	m_jobhost_yes_btn;
	CStatic	m_jobhost_static;
	CEdit	m_jobhost_pwd_edit;
	CButton	m_jobhost_no_btn;
	CEdit	m_jobhost_edit;
	CEdit	m_hosts_edit;
	CButton	m_host_use_jobhost_pwd_chk;
	CButton	m_host_use_jobhost_chk;
	CButton	m_host_toggle_btn;
	CButton	m_host_popup_debug_yes_btn;
	CButton	m_host_popup_debug_no_btn;
	CButton	m_host_popup_debug_chk;
	CButton	m_host_mapping_yes_btn;
	CButton	m_host_mapping_no_btn;
	CButton	m_host_mapping_chk;
	CListCtrl	m_host_list;
	CEdit	m_host_launch_edit;
	CButton	m_host_launch_chk;
	CButton	m_host_hosts_chk;
	CButton	m_host_dots_chk;
	CButton	m_host_color_chk;
	CButton	m_host_jobhost_yes_btn;
	CEdit	m_host_jobhost_pwd_edit;
	CButton	m_host_jobhost_no_btn;
	CEdit	m_host_jobhost_edit;
	CEdit	m_host_hosts_edit;
	CButton	m_host_dots_yes_btn;
	CButton	m_host_dots_no_btn;
	CButton	m_host_color_yes_btn;
	CButton	m_host_color_no_btn;
	CButton	m_dots_yes_btn;
	CButton	m_dots_no_btn;
	CButton	m_color_yes_btn;
	CButton	m_color_no_btn;
	CButton	m_apply_single_btn;
	CButton	m_apply_btn;
	CButton	m_add_btn;
	CString	m_add_hostname;
	BOOL	m_color_no;
	BOOL	m_color_yes;
	BOOL	m_bdots;
	BOOL	m_bcolor;
	BOOL	m_dots_no;
	BOOL	m_dots_yes;
	BOOL	m_host_color_no;
	BOOL	m_bhost_color;
	BOOL	m_host_color_yes;
	BOOL	m_bhost_dots;
	BOOL	m_host_dots_no;
	BOOL	m_host_dots_yes;
	BOOL	m_bhost_hosts;
	CString	m_host_hosts;
	CString	m_host_jobhost;
	BOOL	m_host_jobhost_no;
	CString	m_host_jobhost_pwd;
	BOOL	m_host_jobhost_yes;
	BOOL	m_bhost_launch;
	int		m_host_launch;
	BOOL	m_bhost_mapping;
	BOOL	m_host_mapping_no;
	BOOL	m_host_mapping_yes;
	BOOL	m_bhost_popup_debug;
	BOOL	m_host_popup_debug_no;
	BOOL	m_host_popup_debug_yes;
	CString	m_config_host;
	BOOL	m_bhost_use_jobhost;
	BOOL	m_bhost_use_jobhost_pwd;
	BOOL	m_bhosts;
	CString	m_hosts;
	CString	m_jobhost;
	BOOL	m_jobhost_no;
	CString	m_jobhost_pwd;
	BOOL	m_jobhost_yes;
	BOOL	m_blaunch;
	int		m_launch;
	BOOL	m_bmapping;
	BOOL	m_mapping_no;
	BOOL	m_mapping_yes;
	CString	m_mpd_phrase;
	CString	m_nofm;
	BOOL	m_bpopup_debug;
	BOOL	m_popup_debug_no;
	BOOL	m_popup_debug_yes;
	BOOL	m_buse_jobhost;
	BOOL	m_buse_jobhost_pwd;
	BOOL	m_bshow_config;
	CButton m_mpd_default_radio;
	CString	m_config_host_msg;
	BOOL	m_bcatch;
	BOOL	m_catch_no;
	BOOL	m_catch_yes;
	BOOL	m_bhost_catch;
	BOOL	m_host_catch_no;
	BOOL	m_host_catch_yes;
	BOOL	m_bcodes;
	BOOL	m_codes_no;
	BOOL	m_codes_yes;
	BOOL	m_bhost_codes;
	BOOL	m_host_codes_no;
	BOOL	m_host_codes_yes;
	BOOL	m_blocalroot;
	BOOL	m_localroot_no;
	BOOL	m_localroot_yes;
	BOOL	m_bhost_localroot;
	BOOL	m_host_localroot_no;
	BOOL	m_host_localroot_yes;
	BOOL	m_blogfile;
	CString	m_logfile;
	BOOL	m_logfile_no;
	BOOL	m_logfile_yes;
	BOOL	m_bhost_logfile;
	CString	m_host_logfile;
	BOOL	m_host_logfile_no;
	BOOL	m_host_logfile_yes;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMPICHConfigDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CMPICHConfigDlg)
	afx_msg void OnAddBtn();
	afx_msg void OnSelectBtn();
	afx_msg void OnMpdPhraseRadio();
	afx_msg void OnMpdPhraseDefaultRadio();
	afx_msg void OnToggleBtn();
	afx_msg void OnHostsChk();
	afx_msg void OnLaunchChk();
	afx_msg void OnUseJobhostChk();
	afx_msg void OnJobhostYes();
	afx_msg void OnJobhostNo();
	afx_msg void OnUseJobhostPwdChk();
	afx_msg void OnColorChk();
	afx_msg void OnColorYes();
	afx_msg void OnColorNo();
	afx_msg void OnDotsChk();
	afx_msg void OnDotsYes();
	afx_msg void OnDotsNo();
	afx_msg void OnMappingChk();
	afx_msg void OnMappingYes();
	afx_msg void OnMappingNo();
	afx_msg void OnPopupDebugChk();
	afx_msg void OnPopupDebugYes();
	afx_msg void OnPopupDebugNo();
	afx_msg void OnApplyBtn();
	afx_msg void OnApplySingleBtn();
	afx_msg void OnHostToggleBtn();
	afx_msg void OnHostHostsChk();
	afx_msg void OnHostLaunchChk();
	afx_msg void OnHostUseJobhostChk();
	afx_msg void OnHostJobhostYes();
	afx_msg void OnHostJobhostNo();
	afx_msg void OnHostUseJobhostPwdChk();
	afx_msg void OnHostColorChk();
	afx_msg void OnHostColorYes();
	afx_msg void OnHostColorNo();
	afx_msg void OnHostDotsChk();
	afx_msg void OnHostDotsYes();
	afx_msg void OnHostDotsNo();
	afx_msg void OnHostMappingChk();
	afx_msg void OnHostMappingYes();
	afx_msg void OnHostMappingNo();
	afx_msg void OnHostPopupDebugChk();
	afx_msg void OnHostPopupDebugYes();
	afx_msg void OnHostPopupDebugNo();
	afx_msg void OnModifyBtn();
	afx_msg void OnShowConfigChk();
	virtual BOOL OnInitDialog();
	afx_msg void OnKeydownHostList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnClose();
	afx_msg void OnItemchangingHostList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnHostCatchChk();
	afx_msg void OnHostCatchYes();
	afx_msg void OnHostCatchNo();
	afx_msg void OnCatchChk();
	afx_msg void OnCatchYes();
	afx_msg void OnCatchNo();
	afx_msg void OnHostCodesYes();
	afx_msg void OnHostCodesNo();
	afx_msg void OnHostCodesChk();
	afx_msg void OnCodesChk();
	afx_msg void OnCodesYes();
	afx_msg void OnCodesNo();
	afx_msg void OnHostLocalRootYes();
	afx_msg void OnHostLocalRootNo();
	afx_msg void OnHostLocalRootChk();
	afx_msg void OnLocalRootChk();
	afx_msg void OnLocalRootYes();
	afx_msg void OnLocalRootNo();
	afx_msg void OnRedirectMpdChk();
	afx_msg void OnRedirectMpdNo();
	afx_msg void OnRedirectMpdYes();
	afx_msg void OnHostRedirectMpdChk();
	afx_msg void OnHostRedirectMpdNo();
	afx_msg void OnHostRedirectMpdYes();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
private:
	void GetHostsString();
	void GetHostConfig();
	void UpdateApplyButtonStates();
	void UpdateModifyButtonState();
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPICHCONFIGDLG_H__21835812_B3D5_4E84_B37C_665D378EA803__INCLUDED_)
