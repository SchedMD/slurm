/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
// MPDFileTransferDlg.h : header file
//

#if !defined(AFX_MPDFILETRANSFERDLG_H__051ED747_059C_4227_8230_12815379EDF0__INCLUDED_)
#define AFX_MPDFILETRANSFERDLG_H__051ED747_059C_4227_8230_12815379EDF0__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#define TREE_FOLDER_UNOPENED ((DWORD)-1)
#define TREE_FOLDER_OPENED   ((DWORD)-2)
#define TREE_FILE            ((DWORD)-3)

#include "resizer.h"
#include "FileDropTarget.h"

/////////////////////////////////////////////////////////////////////////////
// CMPDFileTransferDlg dialog

class CMPDFileTransferDlg : public CDialog
{
    // Construction
public:
    CMPDFileTransferDlg(CWnd* pParent = NULL);	// standard constructor
    
    char m_pszHost[100];
    int m_nPort1, m_nPort2;
    char m_pszPhrase1[100], m_pszPhrase2[100];
    char m_pszAccount1[100], m_pszAccount2[100];
    char m_pszPassword1[100], m_pszPassword2[100];
    char m_pszRoot1[MAX_PATH], m_pszRoot2[MAX_PATH];
    bool m_bNeedPassword1, m_bNeedPassword2;
    bool m_bNeedAccount1, m_bNeedAccount2;
    int m_bfd1, m_bfd2;
    void ParseRegistry();
    
    Resizer m_rsrHost1, m_rsrConnect1, m_rsrTree1;
    Resizer m_rsrHost2, m_rsrConnect2, m_rsrTree2;
    Resizer m_rsrHostB;
    Resizer m_rsrFileProgress1, m_rsrFolderProgress1;
    Resizer m_rsrFileProgress2, m_rsrFolderProgress2;
    
    CFileDropTarget m_DropTarget1, m_DropTarget2;

    enum DRAG_STATE { DRAGGING_NOT, DRAGGING_LEFT_FILE, DRAGGING_LEFT_FOLDER, DRAGGING_RIGHT_FILE, DRAGGING_RIGHT_FOLDER };
    DRAG_STATE m_dragState;
    HTREEITEM m_hDragItem;

    UINT m_nTimerId;
    CTreeCtrl *m_pTimerTree;
    HTREEITEM m_hTimerItem;

    // Dialog Data
    //{{AFX_DATA(CMPDFileTransferDlg)
	enum { IDD = IDD_MPDFILETRANSFER_DIALOG };
    CProgressCtrl	m_folder_progress2;
    CProgressCtrl	m_folder_progress1;
    CProgressCtrl	m_file_progress2;
    CProgressCtrl	m_file_progress1;
    CEdit	m_hostb_edit;
    CTreeCtrl	m_tree2;
    CTreeCtrl	m_tree1;
    CEdit	m_host2_edit;
    CEdit	m_host1_edit;
    CButton	m_connect2_btn;
    CButton	m_connect1_btn;
    CString	m_host1;
    CString	m_host2;
    CString	m_hostb;
	//}}AFX_DATA
    
    // ClassWizard generated virtual function overrides
    //{{AFX_VIRTUAL(CMPDFileTransferDlg)
	protected:
    virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	//}}AFX_VIRTUAL
    
    // Implementation
protected:
    HICON m_hIcon;
    
    // Generated message map functions
    //{{AFX_MSG(CMPDFileTransferDlg)
    virtual BOOL OnInitDialog();
    afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    afx_msg void OnClose();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnConnect1Btn();
    afx_msg void OnConnect2Btn();
    afx_msg void OnFileExit();
    afx_msg void OnItemexpandingTree1(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnItemexpandingTree2(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnFileConnect();
    afx_msg void OnBegindragTree2(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnBegindragTree1(NMHDR* pNMHDR, LRESULT* pResult);
	//}}AFX_MSG
    DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPDFILETRANSFERDLG_H__051ED747_059C_4227_8230_12815379EDF0__INCLUDED_)
