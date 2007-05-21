/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
// guiMPIJobDlg.h : header file
//

#if !defined(AFX_GUIMPIJOBDLG_H__C622E57E_A659_44C4_81F8_93A6EDC9057B__INCLUDED_)
#define AFX_GUIMPIJOBDLG_H__C622E57E_A659_44C4_81F8_93A6EDC9057B__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "resizer.h"

/////////////////////////////////////////////////////////////////////////////
// CGuiMPIJobDlg dialog

class CGuiMPIJobDlg : public CDialog
{
// Construction
public:
	CGuiMPIJobDlg(CWnd* pParent = NULL);	// standard constructor

	SOCKET m_sock;
	CString m_host;
	int m_port;
	CString m_passphrase;

	Resizer rOk, rCancel, rJobs, rDetails;

// Dialog Data
	//{{AFX_DATA(CGuiMPIJobDlg)
	enum { IDD = IDD_GUIMPIJOB_DIALOG };
	CButton	m_ok_btn;
	CButton	m_cancel_btn;
	CButton	m_remove_btn;
	CButton	m_refresh_btn;
	CButton	m_kill_btn;
	CListBox	m_job_list;
	CEdit	m_job_edit;
	CButton	m_full_chk;
	CString	m_job_details;
	CString	m_job;
	BOOL	m_bFullChecked;
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CGuiMPIJobDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	//{{AFX_MSG(CGuiMPIJobDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnConnectBtn();
	afx_msg void OnRefreshBtn();
	afx_msg void OnRemoveBtn();
	afx_msg void OnKillBtn();
	afx_msg void OnFullChk();
	afx_msg void OnSelchangeJobsList();
	afx_msg void OnClose();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg int OnVKeyToItem(UINT nKey, CListBox* pListBox, UINT nIndex);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
private:
	void GetJobDetails();
	void Disconnect();
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_GUIMPIJOBDLG_H__C622E57E_A659_44C4_81F8_93A6EDC9057B__INCLUDED_)
