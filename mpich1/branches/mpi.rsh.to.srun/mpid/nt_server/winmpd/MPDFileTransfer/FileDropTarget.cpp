// FileDropTarget.cpp: implementation of the CFileDropTarget class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "MPDFileTransfer.h"
#include "FileDropTarget.h"
#include "MPDFileTransferDlg.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

#define RECT_BORDER 10

DROPEFFECT CFileDropTarget::OnDragEnter( CWnd* pWnd, COleDataObject* pDataObject, DWORD dwKeyState, CPoint point )
{
    return OnDragOver(pWnd, pDataObject, dwKeyState, point);
}

//*
//CMPDFileTransferDlg *g_pDlg;
CTreeCtrl *g_pTimerTree = NULL;
UINT g_nTimerId = 0;
HTREEITEM g_hTimerItem = NULL;
VOID CALLBACK MyTimerProc(
  HWND hwnd,         // handle to window
  UINT uMsg,         // WM_TIMER message
  UINT_PTR idEvent,  // timer identifier
  DWORD dwTime       // current system time
)
{
    //g_pDlg->m_counter = idEvent;
    //g_pDlg->UpdateData(FALSE);
    //*
    if (idEvent == g_nTimerId)
    //if (idEvent == 2)
    {
	//KillTimer(NULL, idEvent);
	KillTimer(NULL, g_nTimerId);
	g_nTimerId = 0;
	if (g_pTimerTree != NULL)
	{
	    /*
	    if (g_pTimerTree->GetItemState(g_hTimerItem, TVIS_EXPANDED) == TVIS_EXPANDED)
		g_pTimerTree->Expand(g_hTimerItem, TVE_COLLAPSE);
	    else
		g_pTimerTree->Expand(g_hTimerItem, TVE_EXPAND);
	    */
	    g_pTimerTree->Expand(g_hTimerItem, TVE_TOGGLE);
	    g_pTimerTree->SelectDropTarget(g_hTimerItem);
	}
	g_pTimerTree = NULL;
	//g_hTimerItem = NULL;
    }
    //*/
    /*
    else
    {
	CString str;
	str.Format("MyTimerProc called, idEvent = %d", idEvent);
	MessageBox(NULL, str, "timer", MB_OK);
	//KillTimer(hwnd, idEvent);
	KillTimer(NULL, g_nTimerId);
    }
    */
}
//*/

DROPEFFECT CFileDropTarget::OnDragOver( CWnd* pWnd, COleDataObject* pDataObject, DWORD dwKeyState, CPoint point )
{
    //::MessageBox(NULL, "DragOver", "", MB_OK);
    //DROPEFFECT dropeffectRet = DROPEFFECT_COPY;
    //if ( (dwKeyState & MK_SHIFT) == MK_SHIFT)	dropeffectRet = DROPEFFECT_MOVE;

    if (m_pDlg == NULL)
	return DROPEFFECT_NONE;

    // Don't allow dragging from other applications or other instances of this application
    if (m_pDlg->m_dragState == CMPDFileTransferDlg::DRAGGING_NOT)
	return DROPEFFECT_NONE;
    // Don't allow dragging to one's self
    if (&m_pDlg->m_tree1 == pWnd)
    {
	if ((m_pDlg->m_dragState == CMPDFileTransferDlg::DRAGGING_LEFT_FILE) ||
	    (m_pDlg->m_dragState == CMPDFileTransferDlg::DRAGGING_LEFT_FOLDER))
	return DROPEFFECT_NONE;
    }
    if (&m_pDlg->m_tree2 == pWnd)
    {
	if ((m_pDlg->m_dragState == CMPDFileTransferDlg::DRAGGING_RIGHT_FILE) ||
	    (m_pDlg->m_dragState == CMPDFileTransferDlg::DRAGGING_RIGHT_FOLDER))
	return DROPEFFECT_NONE;
    }

    // Expand and highlight the item under the mouse and 
    CTreeCtrl *pDestTreeCtrl = (CTreeCtrl *)pWnd;
    HTREEITEM hTItem = pDestTreeCtrl->HitTest(point);
    if ( hTItem != NULL ) 
    {
	/*
	//if (m_pDlg->m_nTimerId != 0) KillTimer(m_pDlg->m_hWnd, m_pDlg->m_nTimerId);
	m_pDlg->m_pTimerTree = pDestTreeCtrl;
	m_pDlg->m_hTimerItem = hTItem;
	m_pDlg->m_nTimerId = SetTimer(m_pDlg->m_hWnd, 123, 750, NULL);
	if (m_pDlg->m_nTimerId == 0)
	/*/
	if (hTItem != g_hTimerItem)
	{
	    if (g_nTimerId != 0)
		KillTimer(NULL, g_nTimerId);
	    //g_pDlg = m_pDlg;
	    g_pTimerTree = pDestTreeCtrl;
	    g_hTimerItem = hTItem;
	    g_nTimerId = SetTimer(NULL, 123, 750, (TIMERPROC)MyTimerProc);
	    if (g_nTimerId == 0)
		//*/
	    {
		int error = GetLastError();
		CString str;
		str.Format("SetTimer failed, error %d", error);
		MessageBox(NULL, str, "error", MB_OK);
	    }
	    /*
	    else
	    {
		m_pDlg->m_timerid = g_nTimerId;
		m_pDlg->UpdateData(FALSE);
	    }
	    */
	}
	//pDestTreeCtrl->Expand(hTItem, TVE_EXPAND);
	pDestTreeCtrl->SelectDropTarget(hTItem);
    }	
    
    // Scroll Tree control depending on mouse position
    CRect rectClient;
    pWnd->GetClientRect(&rectClient);
    pWnd->ClientToScreen(rectClient);
    pWnd->ClientToScreen(&point);
    int nScrollDir = -1;
    if ( point.y >= rectClient.bottom - RECT_BORDER)
	nScrollDir = SB_LINEDOWN;
    else
    {
	if ( (point.y <= rectClient.top + RECT_BORDER) )
	    nScrollDir = SB_LINEUP;
    }
    
    
    if ( nScrollDir != -1 ) 
    {
	int nScrollPos = pWnd->GetScrollPos(SB_VERT);
	WPARAM wParam = MAKELONG(nScrollDir, nScrollPos);
	pWnd->SendMessage(WM_VSCROLL, wParam);
    }
    
    nScrollDir = -1;
    if ( point.x <= rectClient.left + RECT_BORDER )
	nScrollDir = SB_LINELEFT;
    else
    {
	if ( point.x >= rectClient.right - RECT_BORDER)
	    nScrollDir = SB_LINERIGHT;
    }
    
    if ( nScrollDir != -1 ) 
    {
	int nScrollPos = pWnd->GetScrollPos(SB_VERT);
	WPARAM wParam = MAKELONG(nScrollDir, nScrollPos);
	pWnd->SendMessage(WM_HSCROLL, wParam);
    }
    
    //return dropeffectRet;
    return DROPEFFECT_COPY;
}

void CFileDropTarget::OnDragLeave( CWnd* pWnd )
{
    /*
    if (m_pDlg->m_nTimerId != 0)
    {
	KillTimer(m_pDlg->m_hWnd, 123);
	m_pDlg->m_nTimerId = 0;
    }
    /*/
    if (g_nTimerId != 0)
    {
	KillTimer(NULL, g_nTimerId);
	g_nTimerId = 0;
	//g_hTimerItem = NULL;
    }
    g_hTimerItem = NULL;    
    //*/
}

CString GetPathFromItem(CTreeCtrl &tree, HTREEITEM hItem)
{
    CString str;
    HTREEITEM hParent;

    if (hItem == NULL)
	return CString("");

    str = tree.GetItemText(hItem);
    hParent = tree.GetNextItem(hItem, TVGN_PARENT);

    return (GetPathFromItem(tree, hParent) + str + "\\");
}

CString GetFileFromItem(CTreeCtrl &tree, HTREEITEM hItem, int *pnLength)
{
    CString str, strLength;
    HTREEITEM hParent;
    int index;

    if (hItem == NULL)
	return CString("");

    str = tree.GetItemText(hItem);
    hParent = tree.GetNextItem(hItem, TVGN_PARENT);

    index = str.ReverseFind(' ');
    strLength = str.Right(str.GetLength() - index - 1);
    str = str.Left(index);

    *pnLength = atoi(strLength);

    return (GetPathFromItem(tree, hParent) + str);
}

CString GetFilePathFromItem(CTreeCtrl &tree, HTREEITEM hItem)
{
    HTREEITEM hParent;

    if (hItem == NULL)
	return CString("");
    hParent = tree.GetNextItem(hItem, TVGN_PARENT);
    return (GetPathFromItem(tree, hParent));
}

BOOL CFileDropTarget::OnDrop(CWnd* pWnd, COleDataObject* pDataObject, DROPEFFECT dropEffect, CPoint point)
{
    if (m_pDlg == NULL)
	return FALSE;
    /*
    if (m_pDlg->m_nTimerId != 0)
    {
	//KillTimer(m_pDlg->m_hWnd, m_pDlg->m_nTimerId);
	KillTimer(m_pDlg->m_hWnd, 123);
	m_pDlg->m_nTimerId = 0;
    }
    /*/
    if (g_nTimerId != 0)
    {
	KillTimer(NULL, g_nTimerId);
	g_nTimerId = 0;
	g_hTimerItem = NULL;
    }
    //*/
    if (m_pDlg->m_dragState == CMPDFileTransferDlg::DRAGGING_NOT)
	return FALSE;
    // Don't allow dragging to one's self
    if (&m_pDlg->m_tree1 == pWnd)
    {
	if ((m_pDlg->m_dragState == CMPDFileTransferDlg::DRAGGING_LEFT_FILE) ||
	    (m_pDlg->m_dragState == CMPDFileTransferDlg::DRAGGING_LEFT_FOLDER))
	return FALSE;
    }
    if (&m_pDlg->m_tree2 == pWnd)
    {
	if ((m_pDlg->m_dragState == CMPDFileTransferDlg::DRAGGING_RIGHT_FILE) ||
	    (m_pDlg->m_dragState == CMPDFileTransferDlg::DRAGGING_RIGHT_FOLDER))
	return FALSE;
    }

    // Find the destination item
    CTreeCtrl *pDestTreeCtrl = (CTreeCtrl *)pWnd;
    HTREEITEM hDropItem = pDestTreeCtrl->HitTest(point);
    DWORD dwDestState = TREE_FOLDER_UNOPENED;
    int nLength = 0;
    CString strSource, strDest;
    if ( hDropItem != NULL ) 
    {
	dwDestState = pDestTreeCtrl->GetItemData(hDropItem);
	switch (m_pDlg->m_dragState)
	{
	case CMPDFileTransferDlg::DRAGGING_LEFT_FILE:
	    strSource = GetFileFromItem(m_pDlg->m_tree1, m_pDlg->m_hDragItem, &nLength);
	    if (dwDestState == TREE_FILE)
	    {
		strDest = GetFilePathFromItem(m_pDlg->m_tree2, hDropItem);
	    }
	    else
	    {
		strDest = GetPathFromItem(m_pDlg->m_tree2, hDropItem);
	    }
	    break;
	case CMPDFileTransferDlg::DRAGGING_LEFT_FOLDER:
	    strSource = GetPathFromItem(m_pDlg->m_tree1, m_pDlg->m_hDragItem);
	    if (dwDestState == TREE_FILE)
	    {
		strDest = GetFilePathFromItem(m_pDlg->m_tree2, hDropItem);
	    }
	    else
	    {
		strDest = GetPathFromItem(m_pDlg->m_tree2, hDropItem);
	    }
	    m_pDlg->m_DropTarget1.m_pDlg = NULL;
	    break;
	case CMPDFileTransferDlg::DRAGGING_RIGHT_FILE:
	    strSource = GetFileFromItem(m_pDlg->m_tree2, m_pDlg->m_hDragItem, &nLength);
	    if (dwDestState == TREE_FILE)
	    {
		strDest = GetFilePathFromItem(m_pDlg->m_tree1, hDropItem);
	    }
	    else
	    {
		strDest = GetPathFromItem(m_pDlg->m_tree1, hDropItem);
	    }
	    break;
	case CMPDFileTransferDlg::DRAGGING_RIGHT_FOLDER:
	    strSource = GetPathFromItem(m_pDlg->m_tree2, m_pDlg->m_hDragItem);
	    if (dwDestState == TREE_FILE)
	    {
		strDest = GetFilePathFromItem(m_pDlg->m_tree1, hDropItem);
	    }
	    else
	    {
		strDest = GetPathFromItem(m_pDlg->m_tree1, hDropItem);
	    }
	    m_pDlg->m_DropTarget2.m_pDlg = NULL;
	    break;
	default:
	    MessageBox(NULL, "Invalid dragState", "Error", MB_OK);
	    break;
	}
    }
    /*
    CString msg;
    msg.Format("Dragging\r\n'%s'\r\nto\r\n'%s'", strSource, strDest);
    MessageBox(NULL, msg, "Drop", MB_OK);
    */

    m_pDlg->m_dragState = CMPDFileTransferDlg::DRAGGING_NOT;
    m_pDlg = NULL;

    return TRUE;
}
