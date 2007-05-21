/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
// MPDUpdateDlg.h : header file
//

#if !defined(AFX_MPDUPDATEDLG_H__0095789A_A062_11D3_95FB_009027106653__INCLUDED_)
#define AFX_MPDUPDATEDLG_H__0095789A_A062_11D3_95FB_009027106653__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "resizer.h"

/////////////////////////////////////////////////////////////////////////////
// CMPDUpdateDlg dialog

class CMPDUpdateDlg : public CDialog
{
    // Construction
public:
	void GetHostConfig(const char *host);
	bool GetTmpMpdFromURL();
	bool GetTmpMPICHFromURL();
    CMPDUpdateDlg(CWnd* pParent = NULL);	// standard constructor
    
    HANDLE m_hUpdateBtnThread;
    DWORD m_num_threads;
    char m_pszHost[100];
    char m_pszPhrase[100];
    bool m_bNeedPassword;
    bool m_bUseDefault;
    CString m_localfile;
    CString m_mpich_localfile, m_mpich_localfiled;
    CString m_mpich_filenamed;
    void ParseRegistry();
    
    Resizer rList;
    Resizer rResults;

    int m_nMinWidth, m_nMinHeight;

    // Dialog Data
    //{{AFX_DATA(CMPDUpdateDlg)
	enum { IDD = IDD_MPDUPDATE_DIALOG };
	CButton	m_mpich_version_btn;
	CButton	m_mpd_version_btn;
	CButton	m_mpich_source_static;
	CEdit	m_cred_account_edit;
	CButton	m_select_btn;
	CEdit	m_mpich_url_edit;
	CButton m_mpich_url_radio;
	CButton m_mpich_filename_radio;
	CEdit	m_mpich_filename_edit;
	CButton	m_mpich_file_browse_btn;
	CButton	m_mpich_anl_btn;
	CStatic	m_mpd_port_static;
	CEdit	m_mpd_port_edit;
	CStatic	m_mpd_pwd_static;
	CEdit	m_mpd_pwd_edit;
	CStatic	m_static_edit;
	CEdit	m_results_edit;
	CStatic	m_update_static;
	CStatic	m_update_one_static;
	CButton	m_update_one_btn;
	CButton	m_update_btn;
	CButton	m_source_static;
	CEdit	m_url_edit;
	CEdit	m_file_edit;
	CButton	m_file_browse_btn;
	CButton	m_anl_btn;
	CButton	m_show_host_chk;
    CButton	m_ok_btn;
    CButton	m_cancel_btn;
    CButton	m_edit_add_btn;
    CListBox	m_host_list;
	BOOL	m_bShowHostConfig;
	CString	m_filename;
	CString	m_urlname;
	CString	m_hostname;
	CButton m_file_radio;
	CButton m_url_radio;
	CString	m_cred_account;
	CString	m_cred_password;
	BOOL	m_bForceUpdate;
	CString	m_results;
	CString	m_mpd_pwd;
	int		m_mpd_port;
	CString	m_mpich_filename;
	CString	m_mpich_url;
	BOOL	m_bUpdateMPD;
	BOOL	m_bUpdateMPICH;
	BOOL	m_bMPDPassphraseChecked;
	BOOL	m_bMPDPortChecked;
	CString	m_mpd_version;
	CString	m_mpich_version;
	CString	m_config_host;
	CString	m_config_mpich_version;
	CString	m_config_mpd_version;
	//}}AFX_DATA
    
    // ClassWizard generated virtual function overrides
    //{{AFX_VIRTUAL(CMPDUpdateDlg)
protected:
    virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
    virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
    //}}AFX_VIRTUAL
    
    // Implementation
protected:
    HICON m_hIcon;
    
    // Generated message map functions
    //{{AFX_MSG(CMPDUpdateDlg)
    virtual BOOL OnInitDialog();
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    afx_msg void OnUpdateBtn();
    afx_msg void OnEditAddBtn();
    afx_msg int OnVKeyToItem(UINT nKey, CListBox* pListBox, UINT nIndex);
    afx_msg void OnClose();
    afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnShowHostChk();
	afx_msg void OnSelchangeHostList();
	afx_msg void OnUpdateOneBtn();
	afx_msg void OnAnlBtn();
	afx_msg void OnFileBrowseBtn();
	afx_msg void OnURLRadio();
	afx_msg void OnFileRadio();
	afx_msg void OnSelectHostsBtn();
	afx_msg void OnUpdateMpichCheck();
	afx_msg void OnUpdateMpdCheck();
	afx_msg void OnMpdPortChk();
	afx_msg void OnMpdPassphraseChk();
	afx_msg void OnMpichUrlRadio();
	afx_msg void OnMpichFileRadio();
	afx_msg void OnMpichFileBrowseBtn();
	afx_msg void OnMpichAnlBtn();
	afx_msg void OnMpichVersionBtn();
	afx_msg void OnMpdVersionBtn();
	//}}AFX_MSG
    DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPDUPDATEDLG_H__0095789A_A062_11D3_95FB_009027106653__INCLUDED_)
