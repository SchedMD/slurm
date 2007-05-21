/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#if !defined(AFX_HELPDLG_H__84140383_4A54_4012_8F9D_8C7B158E4F62__INCLUDED_)
#define AFX_HELPDLG_H__84140383_4A54_4012_8F9D_8C7B158E4F62__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// HelpDlg.h : header file
//

#include "HtmlCtrl.h"

/////////////////////////////////////////////////////////////////////////////
// CHelpDlg dialog

class CHelpDlg : public CDialog
{
// Construction
public:
	CHelpDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CHelpDlg)
	enum { IDD = IDD_HELP_DLG };
	CStatic	m_html_frame;
	//}}AFX_DATA

	CHtmlCtrl m_html_ctrl;
	void OnOK();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CHelpDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CHelpDlg)
	virtual BOOL OnInitDialog();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_HELPDLG_H__84140383_4A54_4012_8F9D_8C7B158E4F62__INCLUDED_)
