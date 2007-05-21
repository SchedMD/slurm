// mpichshDlg.h : header file
//

#if !defined(AFX_MPICHSHDLG_H__E308CD23_99FC_11D3_A5F0_C2038F6E14D5__INCLUDED_)
#define AFX_MPICHSHDLG_H__E308CD23_99FC_11D3_A5F0_C2038F6E14D5__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

/////////////////////////////////////////////////////////////////////////////
// CMpichshDlg dialog

class CMpichshDlg : public CDialog
{
// Construction
public:
	CMpichshDlg(CWnd* pParent = NULL);	// standard constructor

// Dialog Data
	//{{AFX_DATA(CMpichshDlg)
	enum { IDD = IDD_MPICHSH_DIALOG };
	CListBox	m_list;
	int		m_nPort;
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMpichshDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	//}}AFX_VIRTUAL

// Implementation
protected:
	HICON m_hIcon;
	HANDLE m_hSocketServerThread;

	// Generated message map functions
	//{{AFX_MSG(CMpichshDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnDestroy();
	afx_msg void OnClearBtn();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPICHSHDLG_H__E308CD23_99FC_11D3_A5F0_C2038F6E14D5__INCLUDED_)
