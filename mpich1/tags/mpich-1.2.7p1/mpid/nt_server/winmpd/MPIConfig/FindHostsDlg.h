/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#if !defined(AFX_FINDHOSTSDLG_H__277FC5E7_3564_4688_8216_7AD467F3C8D6__INCLUDED_)
#define AFX_FINDHOSTSDLG_H__277FC5E7_3564_4688_8216_7AD467F3C8D6__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// FindHostsDlg.h : header file
//

#include "resizer.h"

/////////////////////////////////////////////////////////////////////////////
// CFindHostsDlg dialog

class CFindHostsDlg : public CDialog
{
// Construction
public:
	void SelectHost(char *host);
	void InsertHost(char *host);
	CFindHostsDlg(CWnd* pParent = NULL);   // standard constructor

	void Refresh();

	CString m_domain;
	Resizer r_domain, r_hosts, r_ok, r_cancel, r_progress, r_nofm;

    //HANDLE m_hFindThread, m_hUpdateBtnThread;
    //DWORD m_num_threads;
    int m_nPort;
    char m_pszHost[100];
    char m_pszPhrase[100];
    bool m_bNeedPassword;
    bool m_bUseDefault;
    void ParseRegistry();
    CString m_filename;
    int m_num_threads;
    HANDLE m_hFindThread;
    bool m_bFastConnect;
    bool m_bInitDialogCalled;
    int m_num_items;
    CImageList *m_pImageList;
    bool m_bWildcard;
    CString m_wildstr;

// Dialog Data
	//{{AFX_DATA(CFindHostsDlg)
	enum { IDD = IDD_FIND_HOSTS_DLG };
	CStatic	m_nofm_static;
	CProgressCtrl	m_progress;
	CButton	m_ok_btn;
	CButton	m_cancel_btn;
	CEdit	m_encoded_hosts_edit;
	CListCtrl	m_list;
	CString	m_encoded_hosts;
	CString	m_nofm;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CFindHostsDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CFindHostsDlg)
	afx_msg void OnChangedomain();
	afx_msg void OnFileExit();
	afx_msg void OnFindhosts();
	afx_msg void OnLoadlist();
	afx_msg void OnSavelist();
	virtual BOOL OnInitDialog();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnVerify();
	afx_msg void OnClickDomainHostList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnConnectionOptions();
	afx_msg void OnActionWildcardScanHosts();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
private:
	void UpdateSelectedHosts();
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_FINDHOSTSDLG_H__277FC5E7_3564_4688_8216_7AD467F3C8D6__INCLUDED_)
