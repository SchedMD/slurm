/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#if !defined(AFX_MPDCONNECTIONOPTIONSDLG_H__0A9AFE02_4201_45C9_A83B_CC1065BE4A10__INCLUDED_)
#define AFX_MPDCONNECTIONOPTIONSDLG_H__0A9AFE02_4201_45C9_A83B_CC1065BE4A10__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// MPDConnectionOptionsDlg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CMPDConnectionOptionsDlg dialog

class CMPDConnectionOptionsDlg : public CDialog
{
// Construction
public:
	CMPDConnectionOptionsDlg(CWnd* pParent = NULL);   // standard constructor

	bool m_bPhrase, m_bPort;
// Dialog Data
	//{{AFX_DATA(CMPDConnectionOptionsDlg)
	enum { IDD = IDD_CONNECTION_OPTIONS_DLG };
	CEdit	m_port_edit;
	CEdit	m_phrase_edit;
	CString	m_phrase;
	int	m_port;
	CButton m_def_phrase_radio;
	CButton m_phrase_radio;
	CButton m_def_port_radio;
	CButton m_port_radio;
	BOOL	m_bFastConnect;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMPDConnectionOptionsDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CMPDConnectionOptionsDlg)
	afx_msg void OnPassphraseRadio();
	virtual BOOL OnInitDialog();
	afx_msg void OnDefaultPassphraseRadio();
	afx_msg void OnDefaultPortRadio();
	afx_msg void OnPortRadio();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPDCONNECTIONOPTIONSDLG_H__0A9AFE02_4201_45C9_A83B_CC1065BE4A10__INCLUDED_)
