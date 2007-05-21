// guiMPIRunDoc.cpp : implementation of the CGuiMPIRunDoc class
//

#include "stdafx.h"
#include "guiMPIRun.h"

#include "guiMPIRunDoc.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CGuiMPIRunDoc

IMPLEMENT_DYNCREATE(CGuiMPIRunDoc, CDocument)

BEGIN_MESSAGE_MAP(CGuiMPIRunDoc, CDocument)
	//{{AFX_MSG_MAP(CGuiMPIRunDoc)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CGuiMPIRunDoc construction/destruction

CGuiMPIRunDoc::CGuiMPIRunDoc()
{
}

CGuiMPIRunDoc::~CGuiMPIRunDoc()
{
}

BOOL CGuiMPIRunDoc::OnNewDocument()
{
	if (!CDocument::OnNewDocument())
		return FALSE;

	return TRUE;
}



/////////////////////////////////////////////////////////////////////////////
// CGuiMPIRunDoc serialization

void CGuiMPIRunDoc::Serialize(CArchive& ar)
{
	if (ar.IsStoring())
	{
	}
	else
	{
	}
}

/////////////////////////////////////////////////////////////////////////////
// CGuiMPIRunDoc diagnostics

#ifdef _DEBUG
void CGuiMPIRunDoc::AssertValid() const
{
	CDocument::AssertValid();
}

void CGuiMPIRunDoc::Dump(CDumpContext& dc) const
{
	CDocument::Dump(dc);
}
#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CGuiMPIRunDoc commands
