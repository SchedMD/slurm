// MPIRing.h : main header file for the MPIRING application
//

#if !defined(AFX_MPIRING_H__16C968C9_B354_40C5_B294_6C77A687D0A0__INCLUDED_)
#define AFX_MPIRING_H__16C968C9_B354_40C5_B294_6C77A687D0A0__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"       // main symbols

/////////////////////////////////////////////////////////////////////////////
// CMPIRingApp:
// See MPIRing.cpp for the implementation of this class
//

class CMPIRingApp : public CWinApp
{
public:
	CMPIRingApp();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMPIRingApp)
	public:
	virtual BOOL InitInstance();
	//}}AFX_VIRTUAL

// Implementation
	//{{AFX_MSG(CMPIRingApp)
	afx_msg void OnAppAbout();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPIRING_H__16C968C9_B354_40C5_B294_6C77A687D0A0__INCLUDED_)
