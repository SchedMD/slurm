// MPIConfigDlg.h : header file
//

#if !defined(AFX_MPICONFIGDLG_H__0095789A_A062_11D3_95FB_009027106653__INCLUDED_)
#define AFX_MPICONFIGDLG_H__0095789A_A062_11D3_95FB_009027106653__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

/////////////////////////////////////////////////////////////////////////////
// CMPIConfigDlg dialog

class CMPIConfigDlg : public CDialog
{
// Construction
public:
	CMPIConfigDlg(CWnd* pParent = NULL);	// standard constructor

	HANDLE m_hFindThread;
	DWORD m_num_threads;
// Dialog Data
	//{{AFX_DATA(CMPIConfigDlg)
	enum { IDD = IDD_MPICONFIG_DIALOG };
	CButton	m_verify_btn;
	CButton	m_set_btn;
	CButton	m_refresh_btn;
	CButton	m_find_btn;
	CListBox	m_list;
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMPIConfigDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	//}}AFX_VIRTUAL

// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	//{{AFX_MSG(CMPIConfigDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnFindBtn();
	afx_msg void OnRefreshBtn();
	afx_msg void OnSetBtn();
	afx_msg void OnVerifyBtn();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPICONFIGDLG_H__0095789A_A062_11D3_95FB_009027106653__INCLUDED_)
