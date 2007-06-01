// mpichsh.h : main header file for the MPICHSH application
//

#if !defined(AFX_MPICHSH_H__E308CD21_99FC_11D3_A5F0_C2038F6E14D5__INCLUDED_)
#define AFX_MPICHSH_H__E308CD21_99FC_11D3_A5F0_C2038F6E14D5__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"		// main symbols

/////////////////////////////////////////////////////////////////////////////
// CMpichshApp:
// See mpichsh.cpp for the implementation of this class
//

class CMpichshApp : public CWinApp
{
public:
	CMpichshApp();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMpichshApp)
	public:
	virtual BOOL InitInstance();
	//}}AFX_VIRTUAL

// Implementation

	//{{AFX_MSG(CMpichshApp)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPICHSH_H__E308CD21_99FC_11D3_A5F0_C2038F6E14D5__INCLUDED_)
