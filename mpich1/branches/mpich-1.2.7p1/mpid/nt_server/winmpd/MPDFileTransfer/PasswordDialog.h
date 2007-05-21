/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#if !defined(AFX_PASSWORDDIALOG_H__59B32EE8_A450_4B68_B941_AD9BD009CDA2__INCLUDED_)
#define AFX_PASSWORDDIALOG_H__59B32EE8_A450_4B68_B941_AD9BD009CDA2__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// PasswordDialog.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CPasswordDialog dialog

class CPasswordDialog : public CDialog
{
// Construction
public:
	CPasswordDialog(CWnd* pParent = NULL);   // standard constructor

	bool m_bUseDefault;
// Dialog Data
	//{{AFX_DATA(CPasswordDialog)
	enum { IDD = IDD_PASSWORD_DLG };
	CEdit	m_phrase_edit;
	CString	m_phrase;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CPasswordDialog)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CPasswordDialog)
	afx_msg void OnDefaultRadio();
	afx_msg void OnPwdRadio();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PASSWORDDIALOG_H__59B32EE8_A450_4B68_B941_AD9BD009CDA2__INCLUDED_)
