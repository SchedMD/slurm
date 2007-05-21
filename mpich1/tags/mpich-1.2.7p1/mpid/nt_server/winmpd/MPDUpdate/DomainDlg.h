/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#if !defined(AFX_DOMAINDLG_H__B65937BE_17C0_4B61_A46A_5129537EF46C__INCLUDED_)
#define AFX_DOMAINDLG_H__B65937BE_17C0_4B61_A46A_5129537EF46C__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// DomainDlg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CDomainDlg dialog

class CDomainDlg : public CDialog
{
// Construction
public:
	CDomainDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CDomainDlg)
	enum { IDD = IDD_DOMAIN_DLG };
	CString	m_domain;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CDomainDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CDomainDlg)
		// NOTE: the ClassWizard will add member functions here
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_DOMAINDLG_H__B65937BE_17C0_4B61_A46A_5129537EF46C__INCLUDED_)
