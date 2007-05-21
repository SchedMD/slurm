#if !defined(AFX_MAKERINGDLG_H__875D7013_9BE9_4D86_9504_CA2F39579BED__INCLUDED_)
#define AFX_MAKERINGDLG_H__875D7013_9BE9_4D86_9504_CA2F39579BED__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// MakeRingDlg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CMakeRingDlg dialog

class CMakeRingDlg : public CDialog
{
// Construction
public:
	CMakeRingDlg(CWnd* pParent = NULL);   // standard constructor

	HANDLE m_hFindThread;
	DWORD m_num_threads;
	CString m_pszHosts;

// Dialog Data
	//{{AFX_DATA(CMakeRingDlg)
	enum { IDD = IDD_MAKE_RING_DLG };
	CListBox	m_list;
	CButton	m_make_ring_btn;
	CButton	m_refresh_btn;
	CButton	m_find_btn;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMakeRingDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CMakeRingDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnRefreshBtn();
	afx_msg void OnFindBtn();
	virtual void OnOK();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MAKERINGDLG_H__875D7013_9BE9_4D86_9504_CA2F39579BED__INCLUDED_)
