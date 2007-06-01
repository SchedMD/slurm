// MPDRing.h : main header file for the MPDRING application
//

#if !defined(AFX_MPDRING_H__D8358071_37E3_4F15_9C5A_56635B7612F6__INCLUDED_)
#define AFX_MPDRING_H__D8358071_37E3_4F15_9C5A_56635B7612F6__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"       // main symbols

/////////////////////////////////////////////////////////////////////////////
// CMPDRingApp:
// See MPDRing.cpp for the implementation of this class
//

class CMPDRingApp : public CWinApp
{
public:
	CMPDRingApp();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMPDRingApp)
	public:
	virtual BOOL InitInstance();
	//}}AFX_VIRTUAL

// Implementation
	//{{AFX_MSG(CMPDRingApp)
	afx_msg void OnAppAbout();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPDRING_H__D8358071_37E3_4F15_9C5A_56635B7612F6__INCLUDED_)
