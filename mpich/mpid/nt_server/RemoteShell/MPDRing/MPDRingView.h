// MPDRingView.h : interface of the CMPDRingView class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_MPDRINGVIEW_H__C661AFFF_7466_48B5_A22D_22CBF7BD2D98__INCLUDED_)
#define AFX_MPDRINGVIEW_H__C661AFFF_7466_48B5_A22D_22CBF7BD2D98__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


class CMPDRingView : public CFormView
{
protected: // create from serialization only
	CMPDRingView();
	DECLARE_DYNCREATE(CMPDRingView)

public:
	//{{AFX_DATA(CMPDRingView)
	enum { IDD = IDD_MPDRING_FORM };
	CButton	m_quit_btn;
	CListBox	m_list;
	CEdit	m_input_box;
	CButton	m_enter_btn;
	CString	m_input;
	//}}AFX_DATA

	HANDLE m_hThread;
	HANDLE m_hStdinPipeW;
	HANDLE m_hProcess;

	RECT m_rList, m_rInput, m_rEnter, m_rQuit, m_rDialog;
	bool m_bRectsValid;

// Attributes
public:
	CMPDRingDoc* GetDocument();

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMPDRingView)
	public:
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	virtual void OnInitialUpdate(); // called first time after construct
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	//}}AFX_VIRTUAL

// Implementation
public:
	void StopMPDs();
	void StartMPDs(LPCTSTR pszAccount, LPCTSTR pszPassword);
	virtual ~CMPDRingView();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:

// Generated message map functions
protected:
	//{{AFX_MSG(CMPDRingView)
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnEnterBtn();
	afx_msg void OnQuitBtn();
	afx_msg void OnCreateRing();
	afx_msg void OnDestroyRing();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

#ifndef _DEBUG  // debug version in MPDRingView.cpp
inline CMPDRingDoc* CMPDRingView::GetDocument()
   { return (CMPDRingDoc*)m_pDocument; }
#endif

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPDRINGVIEW_H__C661AFFF_7466_48B5_A22D_22CBF7BD2D98__INCLUDED_)
