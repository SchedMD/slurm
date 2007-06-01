/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
// FileDropTarget.h: interface for the CFileDropTarget class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_FILEDROPTARGET_H__5D8E0137_B56D_4971_8465_6A5557EC3876__INCLUDED_)
#define AFX_FILEDROPTARGET_H__5D8E0137_B56D_4971_8465_6A5557EC3876__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CMPDFileTransferDlg;
class CFileDropTarget : public COleDropTarget  
{
public:
    virtual DROPEFFECT OnDragEnter( CWnd* pWnd, COleDataObject* pDataObject, DWORD dwKeyState, CPoint point );
    virtual DROPEFFECT OnDragOver( CWnd* pWnd, COleDataObject* pDataObject, DWORD dwKeyState, CPoint point );
    virtual void OnDragLeave( CWnd* pWnd );
    virtual BOOL OnDrop(CWnd* pWnd, COleDataObject* pDataObject, DROPEFFECT dropEffect, CPoint point);

    CMPDFileTransferDlg *m_pDlg;
};

#endif // !defined(AFX_FILEDROPTARGET_H__5D8E0137_B56D_4971_8465_6A5557EC3876__INCLUDED_)
