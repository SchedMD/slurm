/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#if !defined(AFX_ADVANCEDCONNECTDIALOG_H__D8529482_B991_4833_99EF_8D5DA7A13F6E__INCLUDED_)
#define AFX_ADVANCEDCONNECTDIALOG_H__D8529482_B991_4833_99EF_8D5DA7A13F6E__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// AdvancedConnectDialog.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CAdvancedConnectDialog dialog

class CAdvancedConnectDialog : public CDialog
{
// Construction
public:
	CAdvancedConnectDialog(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CAdvancedConnectDialog)
	enum { IDD = IDD_CONNECT_DLG };
	CString	m_account1;
	CString	m_account2;
	CString	m_host1;
	CString	m_host2;
	CString	m_password1;
	CString	m_password2;
	CString	m_phrase1;
	CString	m_phrase2;
	int		m_port1;
	int		m_port2;
	CString	m_root1;
	CString	m_root2;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAdvancedConnectDialog)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CAdvancedConnectDialog)
		// NOTE: the ClassWizard will add member functions here
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_ADVANCEDCONNECTDIALOG_H__D8529482_B991_4833_99EF_8D5DA7A13F6E__INCLUDED_)
