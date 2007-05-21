// MPDRingDoc.h : interface of the CMPDRingDoc class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_MPDRINGDOC_H__37E958C2_15CB_461A_B5DE_BD1B1047635B__INCLUDED_)
#define AFX_MPDRINGDOC_H__37E958C2_15CB_461A_B5DE_BD1B1047635B__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


class CMPDRingDoc : public CDocument
{
protected: // create from serialization only
	CMPDRingDoc();
	DECLARE_DYNCREATE(CMPDRingDoc)

// Attributes
public:

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMPDRingDoc)
	public:
	virtual BOOL OnNewDocument();
	virtual void Serialize(CArchive& ar);
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CMPDRingDoc();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:

// Generated message map functions
protected:
	//{{AFX_MSG(CMPDRingDoc)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPDRINGDOC_H__37E958C2_15CB_461A_B5DE_BD1B1047635B__INCLUDED_)
