/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#if !defined(AFX_PWDDIALOG_H__570578F6_CB25_43C4_AB81_447A3858D2E4__INCLUDED_)
#define AFX_PWDDIALOG_H__570578F6_CB25_43C4_AB81_447A3858D2E4__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// PwdDialog.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CPwdDialog dialog

class CPwdDialog : public CDialog
{
// Construction
public:
	CPwdDialog(CWnd* pParent = NULL);   // standard constructor
    
	bool m_bUseDefault;

// Dialog Data
	//{{AFX_DATA(CPwdDialog)
	enum { IDD = IDD_PHRASE_DLG };
	CEdit	m_pwd_ctrl;
	CString	m_password;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CPwdDialog)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CPwdDialog)
	afx_msg void OnPhraseRadio();
	afx_msg void OnDefaultRadio();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PWDDIALOG_H__570578F6_CB25_43C4_AB81_447A3858D2E4__INCLUDED_)
