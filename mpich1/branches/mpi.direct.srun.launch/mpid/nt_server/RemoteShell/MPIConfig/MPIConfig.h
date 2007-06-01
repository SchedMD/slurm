// MPIConfig.h : main header file for the MPICONFIG application
//

#if !defined(AFX_MPICONFIG_H__00957898_A062_11D3_95FB_009027106653__INCLUDED_)
#define AFX_MPICONFIG_H__00957898_A062_11D3_95FB_009027106653__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"		// main symbols

/////////////////////////////////////////////////////////////////////////////
// CMPIConfigApp:
// See MPIConfig.cpp for the implementation of this class
//

class CMPIConfigApp : public CWinApp
{
public:
	CMPIConfigApp();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMPIConfigApp)
	public:
	virtual BOOL InitInstance();
	//}}AFX_VIRTUAL

// Implementation

	//{{AFX_MSG(CMPIConfigApp)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPICONFIG_H__00957898_A062_11D3_95FB_009027106653__INCLUDED_)
