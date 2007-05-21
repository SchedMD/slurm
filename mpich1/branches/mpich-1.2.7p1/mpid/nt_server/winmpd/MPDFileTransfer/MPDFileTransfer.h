/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
// MPDFileTransfer.h : main header file for the MPDFILETRANSFER application
//

#if !defined(AFX_MPDFILETRANSFER_H__C148333F_9D61_4F01_81AB_8CD0CFB0FE69__INCLUDED_)
#define AFX_MPDFILETRANSFER_H__C148333F_9D61_4F01_81AB_8CD0CFB0FE69__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"		// main symbols

/////////////////////////////////////////////////////////////////////////////
// CMPDFileTransferApp:
// See MPDFileTransfer.cpp for the implementation of this class
//

class CMPDFileTransferApp : public CWinApp
{
public:
	CMPDFileTransferApp();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMPDFileTransferApp)
	public:
	virtual BOOL InitInstance();
	//}}AFX_VIRTUAL

// Implementation

	//{{AFX_MSG(CMPDFileTransferApp)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MPDFILETRANSFER_H__C148333F_9D61_4F01_81AB_8CD0CFB0FE69__INCLUDED_)
