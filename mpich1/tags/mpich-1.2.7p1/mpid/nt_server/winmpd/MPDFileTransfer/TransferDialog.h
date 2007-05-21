/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#if !defined(AFX_TRANSFERDIALOG_H__E6F01D8B_4E46_4E28_B71C_FD11AFC6D1AD__INCLUDED_)
#define AFX_TRANSFERDIALOG_H__E6F01D8B_4E46_4E28_B71C_FD11AFC6D1AD__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// TransferDialog.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CTransferDialog dialog

class CTransferDialog : public CDialog
{
// Construction
public:
	CTransferDialog(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CTransferDialog)
	enum { IDD = IDD_TRANSFER_DLG };
		// NOTE: the ClassWizard will add data members here
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CTransferDialog)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CTransferDialog)
		// NOTE: the ClassWizard will add member functions here
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_TRANSFERDIALOG_H__E6F01D8B_4E46_4E28_B71C_FD11AFC6D1AD__INCLUDED_)
