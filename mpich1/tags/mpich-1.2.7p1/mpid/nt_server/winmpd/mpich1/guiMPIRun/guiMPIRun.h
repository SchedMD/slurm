/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
// guiMPIRun.h : main header file for the GUIMPIRUN application
//

#if !defined(AFX_GUIMPIRUN_H__216ECC67_6990_4044_A70A_2812302EAC24__INCLUDED_)
#define AFX_GUIMPIRUN_H__216ECC67_6990_4044_A70A_2812302EAC24__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"       // main symbols

/////////////////////////////////////////////////////////////////////////////
// CGuiMPIRunApp:
// See guiMPIRun.cpp for the implementation of this class
//

class CGuiMPIRunApp : public CWinApp
{
public:
	CGuiMPIRunApp();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CGuiMPIRunApp)
	public:
	virtual BOOL InitInstance();
	//}}AFX_VIRTUAL

// Implementation
	//{{AFX_MSG(CGuiMPIRunApp)
	afx_msg void OnAppAbout();
	afx_msg void OnHelp();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_GUIMPIRUN_H__216ECC67_6990_4044_A70A_2812302EAC24__INCLUDED_)
