// MPIRingView.cpp : implementation of the CMPIRingView class
//

#include "stdafx.h"
#include "MPIRing.h"

#include "MPIRingDoc.h"
#include "MPIRingView.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CMPIRingView

IMPLEMENT_DYNCREATE(CMPIRingView, CView)

BEGIN_MESSAGE_MAP(CMPIRingView, CView)
	//{{AFX_MSG_MAP(CMPIRingView)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMPIRingView construction/destruction

CMPIRingView::CMPIRingView()
{
}

CMPIRingView::~CMPIRingView()
{
}

BOOL CMPIRingView::PreCreateWindow(CREATESTRUCT& cs)
{
	return CView::PreCreateWindow(cs);
}

/////////////////////////////////////////////////////////////////////////////
// CMPIRingView drawing

void CMPIRingView::OnDraw(CDC* pDC)
{
	CMPIRingDoc* pDoc = GetDocument();
	ASSERT_VALID(pDoc);
}

/////////////////////////////////////////////////////////////////////////////
// CMPIRingView diagnostics

#ifdef _DEBUG
void CMPIRingView::AssertValid() const
{
	CView::AssertValid();
}

void CMPIRingView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}

CMPIRingDoc* CMPIRingView::GetDocument() // non-debug version is inline
{
	ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CMPIRingDoc)));
	return (CMPIRingDoc*)m_pDocument;
}
#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CMPIRingView message handlers
