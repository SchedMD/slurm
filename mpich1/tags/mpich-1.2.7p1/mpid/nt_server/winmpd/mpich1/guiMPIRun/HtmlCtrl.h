/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef HTML_CONTROL_H
#define HTML_CONTROL_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CHtmlCtrl : public CWnd
{
public: 

    CHtmlCtrl();
    ~CHtmlCtrl();

    virtual bool Create(CWnd* pParent, const RECT& rc, 
                LPCTSTR pszHomeURL = NULL, bool fBtnText = true);

    virtual bool ReplaceControl(CWnd* pDlg, UINT idCtrl, 
                LPCTSTR pszHomeURL = NULL, bool fBtnText = true);

    DECLARE_DYNAMIC(CHtmlCtrl)
    DECLARE_EVENTSINK_MAP()

public:
    BOOL LoadFromResource(UINT nRes);
    HRESULT Navigate(
	LPCTSTR lpszURL, 
	DWORD dwFlags = 0,
	LPCTSTR lpszTargetFrameName = NULL,
	LPCTSTR lpszHeaders = NULL, 
	LPVOID lpvPostData = NULL,
	DWORD dwPostDataLen = 0);
public:
#ifdef _DEBUG
    virtual void AssertValid() const;
    virtual void Dump(CDumpContext& dc) const;
#endif

public:
    //{{AFX_MSG(CHtmlCtrl)
    //}}AFX_MSG
    DECLARE_MESSAGE_MAP()

protected:
    IWebBrowser2* m_pBrowser;
};

#endif
