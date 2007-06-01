#if !defined(AFX_ACCOUNTPASSWORDDLG_H__0A3F6274_583F_4AF0_9F6A_FCC958F8B100__INCLUDED_)
#define AFX_ACCOUNTPASSWORDDLG_H__0A3F6274_583F_4AF0_9F6A_FCC958F8B100__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// AccountPasswordDlg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CAccountPasswordDlg dialog

class CAccountPasswordDlg : public CDialog
{
// Construction
public:
	CAccountPasswordDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CAccountPasswordDlg)
	enum { IDD = IDD_ACCOUNT_PASSWORD_DLG };
	CString	m_account;
	CString	m_password;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAccountPasswordDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CAccountPasswordDlg)
		// NOTE: the ClassWizard will add member functions here
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_ACCOUNTPASSWORDDLG_H__0A3F6274_583F_4AF0_9F6A_FCC958F8B100__INCLUDED_)
