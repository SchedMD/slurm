/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#if !defined(AFX_MPDCONNECTDLG_H__4927C5A8_98BF_4B12_8C22_E01F2ACF0A9C__INCLUDED_)
#define AFX_MPDCONNECTDLG_H__4927C5A8_98BF_4B12_8C22_E01F2ACF0A9C__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// MPDConnectDlg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CMPDConnectDlg dialog

class CMPDConnectDlg : public CDialog
{
// Construction
public:
	CMPDConnectDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CMPDConnectDlg)
	enum { IDD = IDD_MPD_CONNECT_DIALOG };
	CEdit	m_port_edit;
	CEdit	m_phrase_edit;
	CString	m_host;
	CString	m_phrase;
	BOOL	m_bPhraseChecked;
	int		m_port;
	BOOL	m_bPortChecked;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMPDConnectDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CMPDConnectDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnPortChk();
	afx_msg void OnPhraseChk();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPDCONNECTDLG_H__4927C5A8_98BF_4B12_8C22_E01F2ACF0A9C__INCLUDED_)
