// MPIRingView.h : interface of the CMPIRingView class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_MPIRINGVIEW_H__F016B10D_C928_4799_ADCD_FBE28AB29983__INCLUDED_)
#define AFX_MPIRINGVIEW_H__F016B10D_C928_4799_ADCD_FBE28AB29983__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


class CMPIRingView : public CView
{
protected: // create from serialization only
	CMPIRingView();
	DECLARE_DYNCREATE(CMPIRingView)

// Attributes
public:
	CMPIRingDoc* GetDocument();

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMPIRingView)
	public:
	virtual void OnDraw(CDC* pDC);  // overridden to draw this view
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	protected:
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CMPIRingView();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:

// Generated message map functions
protected:
	//{{AFX_MSG(CMPIRingView)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

#ifndef _DEBUG  // debug version in MPIRingView.cpp
inline CMPIRingDoc* CMPIRingView::GetDocument()
   { return (CMPIRingDoc*)m_pDocument; }
#endif

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPIRINGVIEW_H__F016B10D_C928_4799_ADCD_FBE28AB29983__INCLUDED_)
