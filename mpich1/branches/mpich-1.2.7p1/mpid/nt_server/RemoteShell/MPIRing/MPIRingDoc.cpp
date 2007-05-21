// MPIRingDoc.cpp : implementation of the CMPIRingDoc class
//

#include "stdafx.h"
#include "MPIRing.h"

#include "MPIRingDoc.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CMPIRingDoc

IMPLEMENT_DYNCREATE(CMPIRingDoc, CDocument)

BEGIN_MESSAGE_MAP(CMPIRingDoc, CDocument)
	//{{AFX_MSG_MAP(CMPIRingDoc)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMPIRingDoc construction/destruction

CMPIRingDoc::CMPIRingDoc()
{
}

CMPIRingDoc::~CMPIRingDoc()
{
}

BOOL CMPIRingDoc::OnNewDocument()
{
	if (!CDocument::OnNewDocument())
		return FALSE;

	return TRUE;
}



/////////////////////////////////////////////////////////////////////////////
// CMPIRingDoc serialization

void CMPIRingDoc::Serialize(CArchive& ar)
{
	if (ar.IsStoring())
	{
	}
	else
	{
	}
}

/////////////////////////////////////////////////////////////////////////////
// CMPIRingDoc diagnostics

#ifdef _DEBUG
void CMPIRingDoc::AssertValid() const
{
	CDocument::AssertValid();
}

void CMPIRingDoc::Dump(CDumpContext& dc) const
{
	CDocument::Dump(dc);
}
#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CMPIRingDoc commands
