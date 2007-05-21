/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
// guiMPIRunView.h : interface of the CGuiMPIRunView class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_GUIMPIRUNVIEW_H__C02DB9BF_C4B2_4EC5_8C65_9E2C0725DFB9__INCLUDED_)
#define AFX_GUIMPIRUNVIEW_H__C02DB9BF_C4B2_4EC5_8C65_9E2C0725DFB9__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "resizer.h"
#include "global.h"

#define PARSE_ERR_NO_FILE  -1
#define PARSE_SUCCESS       0

class CGuiMPIRunView : public CFormView
{
protected: // create from serialization only
    CGuiMPIRunView();
    DECLARE_DYNCREATE(CGuiMPIRunView)
	
public:
    //{{AFX_DATA(CGuiMPIRunView)
	enum { IDD = IDD_GUIMPIRUN_FORM };
	CButton	m_reset_btn;
	CButton m_any_hosts_btn;
    CButton	m_advanced_btn;
    CEdit	m_nproc_edit;
    CButton	m_run_btn;
    CSpinButtonCtrl	m_nproc_spin;
    CButton	m_break_btn;
    CButton	m_add_btn;
    CEdit	m_host_edit;
    CRichEditCtrl	m_output;
    CListBox	m_host_list;
    CComboBox	m_app_combo;
    CButton	m_app_browse_btn;
    int		m_nproc;
    CString	m_app;
    CString	m_host;
	//}}AFX_DATA
    
    // Attributes
public:
    CGuiMPIRunDoc* GetDocument();

    CString m_output_filename;
    bool m_redirect;
    CString m_Account, m_Password, m_Phrase;
    Resizer rOutput, rHostList, rAppCombo;
    Resizer rAppBrowse, rAnyHost, rHost, rHostEdit, rAdd, rAdvanced, rReset;

    FILE *m_fout;

    bool m_bAnyHosts;
    HostNode *m_pHosts;
    void GetHosts();
    HANDLE m_hJobThread;
    
    HANDLE m_hRedirectIOListenThread;
    SOCKET m_sockStopIOSignalSocket;
    char m_pszIOHost[100];
    int m_nIOPort;
    bool m_bLogon;
    bool m_bFirstBreak;
    
    bool m_bForceLogon;
    void EnableRunning();
    void DisableRunning();
    
    HANDLE m_hAbortEvent;
    bool m_bNormalExit;
    bool m_bNoMPI;
    bool m_bNoColor;
    HANDLE m_hConsoleOutputMutex;
    long m_nRootPort;
    bool m_bCatch;

    bool m_bUseWorkingDirectory;
    CString m_WorkingDirectory;
    bool m_bUseCommonEnvironment;
    CString m_CommonEnvironment;
    bool m_bUseSlaveProcess;
    CString m_SlaveProcess;
    bool m_bNoClear;
    CString m_Mappings;
    bool m_bUseMapping;

    void Abort();

    void ReadMRU();
    void SaveAppToMRU();
    void ClearMRU();
    int m_nMaxMRU;

    HANDLE m_hJobFinished;

    struct RedirectStdinStruct
    {
	CString str;
	RedirectStdinStruct *pNext;
    };
    HANDLE m_hRedirectStdinEvent;
    HANDLE m_hRedirectStdinMutex;
    RedirectStdinStruct *m_pRedirectStdinList;
    CString m_curoutput;

    int ParseConfigFile();
    CString m_ConfigFileName;
    bool m_bUseConfigFile;

    HANDLE *m_pProcessThread;
    int m_nNumProcessThreads;

    SOCKET *m_pProcessSocket;
    int *m_pProcessLaunchId;
    LONG m_nNumProcessSockets;
    void WaitForExitCommands();

    SOCKET m_sockBreak;
    HANDLE m_hBreakReadyEvent;

    ForwardHostStruct *m_pForwardHost;
    int *m_pLaunchIdToRank;

    HANDLE m_hRedirectRicheditThread;

    //MapDriveNode *m_pDriveMapList;

    int m_nMinWidth, m_nMinHeight;
    // Operations
public:
    
    // Overrides
    // ClassWizard generated virtual function overrides
    //{{AFX_VIRTUAL(CGuiMPIRunView)
public:
    virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
protected:
    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
    virtual void OnInitialUpdate(); // called first time after construct
    //}}AFX_VIRTUAL
    
    // Implementation
public:
    virtual ~CGuiMPIRunView();
#ifdef _DEBUG
    virtual void AssertValid() const;
    virtual void Dump(CDumpContext& dc) const;
#endif
    
protected:
    
    // Generated message map functions
protected:
    //{{AFX_MSG(CGuiMPIRunView)
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnDeltaposNprocSpin(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnAppBrowseBtn();
    afx_msg void OnRunBtn();
    afx_msg void OnHostsRadio();
    afx_msg void OnAnyHostsRadio();
    afx_msg void OnEditCopy();
    afx_msg void OnClose();
    afx_msg void OnAddHostBtn();
    afx_msg void OnBreakBtn();
    afx_msg void OnAdvancedBtn();
	afx_msg void OnMsgfilterOutput(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnResetHostsBtn();
	afx_msg int OnVKeyToItem(UINT nKey, CListBox* pListBox, UINT nIndex);
	//}}AFX_MSG
    DECLARE_MESSAGE_MAP()
};

#ifndef _DEBUG  // debug version in guiMPIRunView.cpp
inline CGuiMPIRunDoc* CGuiMPIRunView::GetDocument()
{ return (CGuiMPIRunDoc*)m_pDocument; }
#endif

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_GUIMPIRUNVIEW_H__C02DB9BF_C4B2_4EC5_8C65_9E2C0725DFB9__INCLUDED_)
