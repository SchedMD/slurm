#if !defined(AFX_MPDRINGDLG_H__175DBF8D_BD4B_439B_A8A1_A9D5EC6D8E0D__INCLUDED_)
#define AFX_MPDRINGDLG_H__175DBF8D_BD4B_439B_A8A1_A9D5EC6D8E0D__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// MPDRingDlg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CMPDRingDlg dialog

class CMPDRingDlg : public CDialog
{
// Construction
public:
	void StartMPDs(LPCTSTR pszAccount, LPCTSTR pszPassword);
	CMPDRingDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CMPDRingDlg)
	enum { IDD = IDD_MPD_RING_DLG };
	CButton	m_quit_btn;
	CEdit	m_input_box;
	CButton	m_enter_btn;
	CListBox	m_list;
	CString	m_input;
	//}}AFX_DATA

	HANDLE m_hThread;
	HANDLE m_hStdinPipeW;
	//HANDLE m_hStdoutPipeR;
	HANDLE m_hProcess;

	RECT m_rList, m_rInput, m_rEnter, m_rQuit, m_rDialog;

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMPDRingDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CMPDRingDlg)
	afx_msg void OnEnterBtn();
	virtual BOOL OnInitDialog();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	virtual void OnCancel();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPDRINGDLG_H__175DBF8D_BD4B_439B_A8A1_A9D5EC6D8E0D__INCLUDED_)
