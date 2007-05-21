/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#if !defined(AFX_USERPWDDIALOG_H__C65FFC4C_2B73_481A_8D0A_0CDD48B3BDDD__INCLUDED_)
#define AFX_USERPWDDIALOG_H__C65FFC4C_2B73_481A_8D0A_0CDD48B3BDDD__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// UserPwdDialog.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CUserPwdDialog dialog

class CUserPwdDialog : public CDialog
{
// Construction
public:
	CUserPwdDialog(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CUserPwdDialog)
	enum { IDD = IDD_USER_PWD_DLG };
	CString	m_account;
	CString	m_password;
	BOOL	m_remember;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CUserPwdDialog)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CUserPwdDialog)
		// NOTE: the ClassWizard will add member functions here
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_USERPWDDIALOG_H__C65FFC4C_2B73_481A_8D0A_0CDD48B3BDDD__INCLUDED_)
