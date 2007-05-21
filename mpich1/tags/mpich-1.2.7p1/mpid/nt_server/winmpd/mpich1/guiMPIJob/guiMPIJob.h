/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
// guiMPIJob.h : main header file for the GUIMPIJOB application
//

#if !defined(AFX_GUIMPIJOB_H__34C836AA_62D5_451A_8D78_25794BD74E1E__INCLUDED_)
#define AFX_GUIMPIJOB_H__34C836AA_62D5_451A_8D78_25794BD74E1E__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"		// main symbols

/////////////////////////////////////////////////////////////////////////////
// CGuiMPIJobApp:
// See guiMPIJob.cpp for the implementation of this class
//

class CGuiMPIJobApp : public CWinApp
{
public:
	CGuiMPIJobApp();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CGuiMPIJobApp)
	public:
	virtual BOOL InitInstance();
	//}}AFX_VIRTUAL

// Implementation

	//{{AFX_MSG(CGuiMPIJobApp)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_GUIMPIJOB_H__34C836AA_62D5_451A_8D78_25794BD74E1E__INCLUDED_)
