// TransferDialog.cpp : implementation file
//

#include "stdafx.h"
#include "MPDFileTransfer.h"
#include "TransferDialog.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CTransferDialog dialog


CTransferDialog::CTransferDialog(CWnd* pParent /*=NULL*/)
	: CDialog(CTransferDialog::IDD, pParent)
{
	//{{AFX_DATA_INIT(CTransferDialog)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}


void CTransferDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CTransferDialog)
		// NOTE: the ClassWizard will add DDX and DDV calls here
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CTransferDialog, CDialog)
	//{{AFX_MSG_MAP(CTransferDialog)
		// NOTE: the ClassWizard will add message map macros here
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CTransferDialog message handlers
