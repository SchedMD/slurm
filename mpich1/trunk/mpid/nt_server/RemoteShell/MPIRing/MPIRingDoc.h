// MPIRingDoc.h : interface of the CMPIRingDoc class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_MPIRINGDOC_H__B86577E0_DEFE_41AE_8027_A5F438A64625__INCLUDED_)
#define AFX_MPIRINGDOC_H__B86577E0_DEFE_41AE_8027_A5F438A64625__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


class CMPIRingDoc : public CDocument
{
protected: // create from serialization only
	CMPIRingDoc();
	DECLARE_DYNCREATE(CMPIRingDoc)

// Attributes
public:

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMPIRingDoc)
	public:
	virtual BOOL OnNewDocument();
	virtual void Serialize(CArchive& ar);
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CMPIRingDoc();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:

// Generated message map functions
protected:
	//{{AFX_MSG(CMPIRingDoc)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPIRINGDOC_H__B86577E0_DEFE_41AE_8027_A5F438A64625__INCLUDED_)
