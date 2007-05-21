/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
// guiMPIRunDoc.h : interface of the CGuiMPIRunDoc class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_GUIMPIRUNDOC_H__60FB549B_DFA8_401E_9A78_6C32AC550F86__INCLUDED_)
#define AFX_GUIMPIRUNDOC_H__60FB549B_DFA8_401E_9A78_6C32AC550F86__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


class CGuiMPIRunDoc : public CDocument
{
protected: // create from serialization only
	CGuiMPIRunDoc();
	DECLARE_DYNCREATE(CGuiMPIRunDoc)

// Attributes
public:

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CGuiMPIRunDoc)
	public:
	virtual BOOL OnNewDocument();
	virtual void Serialize(CArchive& ar);
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CGuiMPIRunDoc();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:

// Generated message map functions
protected:
	//{{AFX_MSG(CGuiMPIRunDoc)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_GUIMPIRUNDOC_H__60FB549B_DFA8_401E_9A78_6C32AC550F86__INCLUDED_)
