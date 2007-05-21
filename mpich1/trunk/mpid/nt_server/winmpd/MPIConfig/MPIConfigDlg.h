/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
// MPIConfigDlg.h : header file
//

#if !defined(AFX_MPICONFIGDLG_H__0095789A_A062_11D3_95FB_009027106653__INCLUDED_)
#define AFX_MPICONFIGDLG_H__0095789A_A062_11D3_95FB_009027106653__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "resizer.h"

/////////////////////////////////////////////////////////////////////////////
// CMPIConfigDlg dialog

class CMPIConfigDlg : public CDialog
{
    // Construction
public:
	void GetHostConfig(const char *host);
	void SetYellowLight();
	void SetGreenLight();
	void SetRedLight();
    CMPIConfigDlg(CWnd* pParent = NULL);	// standard constructor
    
    HANDLE m_hSetBtnThread;
    DWORD m_num_threads;
    int m_nPort;
    char m_pszHost[100];
    char m_pszPhrase[100];
    bool m_bNeedPassword;
    bool m_bUseDefault;
    void ParseRegistry();
    
    /*
    Resizer rOk, rCancel;
    Resizer r1Static, rList;
    Resizer r2Static, rHostsChk, rTempChk, rTempEdit, rTempStatic, rLaunchChk, rLaunchEdit;
    Resizer r3Static, rPwdStatic, rPhraseStatic, rPwdRadio, rPhrase, rDefaultRadio, rApply, rApplyStatic, rApplyOne, rApplyOneStatic, rStopLightRed, rStopLightYellow, rStopLightGreen;
    Resizer rShowHostChk, rHostConfig;
    */
    Resizer rOk, rCancel;
    Resizer r1Static, rList;
    Resizer rHostConfig;

    int m_nMinWidth, m_nMinHeight;

    // Dialog Data
    //{{AFX_DATA(CMPIConfigDlg)
	enum { IDD = IDD_MPICONFIG_DIALOG };
	CButton	m_set_one_btn;
	CStatic	m_apply_one_static;
	CButton	m_show_host_chk;
	CEdit	m_host_config_edit;
	CStatic	m_stoplight_yellow;
	CStatic	m_stoplight_green;
	CStatic	m_stoplight_red;
	CButton	m_launch_chk;
	CButton	m_temp_chk;
	CButton	m_hosts_chk;
	CButton	m_two_static;
	CButton	m_three_static;
	CStatic	m_temp_static;
	CStatic	m_pwd_static;
	CButton	m_phrase_static;
	CButton	m_one_static;
	CStatic	m_apply_static;
	CEdit	m_TempEdit;
	CEdit	m_LaunchTimeoutEdit;
	CEdit	m_pwd_ctrl;
    CButton	m_ok_btn;
    CButton	m_cancel_btn;
    CButton	m_edit_add_btn;
    CListBox	m_host_list;
    CButton	m_set_btn;
    CString	m_hostname;
	CString	m_password;
	BOOL	m_bHostsChk;
	int		m_nLaunchTimeout;
	BOOL	m_bTempChk;
	CString	m_pszTempDir;
	BOOL	m_bLaunchTimeoutChk;
	CString	m_host_config;
	BOOL	m_bShowHostConfig;
	CButton m_default_radio;
	//}}AFX_DATA
    
    // ClassWizard generated virtual function overrides
    //{{AFX_VIRTUAL(CMPIConfigDlg)
protected:
    virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
    virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
    //}}AFX_VIRTUAL
    
    // Implementation
protected:
    HICON m_hIcon;
    
    // Generated message map functions
    //{{AFX_MSG(CMPIConfigDlg)
    virtual BOOL OnInitDialog();
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    afx_msg void OnSetBtn();
    afx_msg void OnEditAddBtn();
    afx_msg int OnVKeyToItem(UINT nKey, CListBox* pListBox, UINT nIndex);
    afx_msg void OnClose();
    afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnPhraseRadio();
	afx_msg void OnDefaultPwdRadio();
	afx_msg void OnTempChk();
	afx_msg void OnTimeoutChk();
	afx_msg void OnShowHostChk();
	afx_msg void OnSelchangeHostList();
	afx_msg void OnSetOneBtn();
	afx_msg void OnSelectBtn();
	//}}AFX_MSG
    DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPICONFIGDLG_H__0095789A_A062_11D3_95FB_009027106653__INCLUDED_)
