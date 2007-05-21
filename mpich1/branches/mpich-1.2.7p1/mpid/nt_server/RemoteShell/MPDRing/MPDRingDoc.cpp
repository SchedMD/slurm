// MPDRingDoc.cpp : implementation of the CMPDRingDoc class
//

#include "stdafx.h"
#include "MPDRing.h"

#include "MPDRingDoc.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CMPDRingDoc

IMPLEMENT_DYNCREATE(CMPDRingDoc, CDocument)

BEGIN_MESSAGE_MAP(CMPDRingDoc, CDocument)
	//{{AFX_MSG_MAP(CMPDRingDoc)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMPDRingDoc construction/destruction

CMPDRingDoc::CMPDRingDoc()
{
}

CMPDRingDoc::~CMPDRingDoc()
{
}

BOOL CMPDRingDoc::OnNewDocument()
{
	if (!CDocument::OnNewDocument())
		return FALSE;

	SetTitle("Disconnected");

	return TRUE;
}



/////////////////////////////////////////////////////////////////////////////
// CMPDRingDoc serialization

void CMPDRingDoc::Serialize(CArchive& ar)
{
	if (ar.IsStoring())
	{
	}
	else
	{
	}
}

/////////////////////////////////////////////////////////////////////////////
// CMPDRingDoc diagnostics

#ifdef _DEBUG
void CMPDRingDoc::AssertValid() const
{
	CDocument::AssertValid();
}

void CMPDRingDoc::Dump(CDumpContext& dc) const
{
	CDocument::Dump(dc);
}
#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CMPDRingDoc commands
