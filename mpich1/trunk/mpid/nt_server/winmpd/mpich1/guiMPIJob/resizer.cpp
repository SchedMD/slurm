#include "stdafx.h"
#include "Resizer.h"

Resizer::Resizer()
{
    m_hWnd = NULL;
    m_Type = RSR_ALL_ANCHOR;
    m_rRect.top = 0;
    m_rRect.bottom = 0;
    m_rRect.left = 0;
    m_rRect.right = 0;
}

Resizer::Resizer(HWND hWnd, int t)
{
    SetInitialPosition(hWnd, t);
}

Resizer::~Resizer()
{
    m_hWnd = NULL;
    m_Type = RSR_ALL_ANCHOR;
    m_rRect.top = 0;
    m_rRect.bottom = 0;
    m_rRect.left = 0;
    m_rRect.right = 0;
}

void Resizer::SetInitialPosition(HWND hWnd, int t)
{
    HWND hParent;
    RECT r1, r2;
    POINT point;

    m_hWnd = hWnd;
    m_Type = t;

    hParent = GetParent(hWnd);
    GetClientRect(hParent, &r1);
    
    GetWindowRect(hWnd, &r2);

    point.x = r2.left;
    point.y = r2.top;
    ScreenToClient(hParent, &point);
    r2.left = point.x;
    r2.top = point.y;

    point.x = r2.right;
    point.y = r2.bottom;
    ScreenToClient(hParent, &point);
    r2.right = point.x;
    r2.bottom = point.y;

    if (t & RSR_LEFT_ANCHOR)
	m_rRect.left = r2.left;
    if (t & RSR_LEFT_MOVE)
	m_rRect.left = r1.right - r2.left;
    if (t & RSR_LEFT_PROPORTIONAL)
	m_rRect.left = (r2.left * 100) / r1.right;
    if (t & RSR_RIGHT_ANCHOR)
	m_rRect.right = r2.right;
    if (t & RSR_RIGHT_MOVE)
	m_rRect.right = r1.right - r2.right;
    if (t & RSR_RIGHT_PROPORTIONAL)
	m_rRect.right = (r2.right * 100) / r1.right;
    if (t & RSR_TOP_ANCHOR)
	m_rRect.top = r2.top;
    if (t & RSR_TOP_MOVE)
	m_rRect.top = r1.bottom - r2.top;
    if (t & RSR_TOP_PROPORTIONAL)
	m_rRect.top = (r2.top * 100) / r1.bottom;
    if (t & RSR_BOTTOM_ANCHOR)
	m_rRect.bottom = r2.bottom;
    if (t & RSR_BOTTOM_MOVE)
	m_rRect.bottom = r1.bottom - r2.bottom;
    if (t & RSR_BOTTOM_PROPORTIONAL)
	m_rRect.bottom = (r2.bottom * 100) / r1.bottom;
}

void Resizer::Resize(int cx, int cy)
{
    int x,y,w,h;

    if (m_Type & RSR_LEFT_ANCHOR)
	x = m_rRect.left;
    if (m_Type & RSR_LEFT_MOVE)
	x = cx - m_rRect.left;
    if (m_Type & RSR_LEFT_PROPORTIONAL)
	x = (m_rRect.left * cx) / 100;

    if (m_Type & RSR_TOP_ANCHOR)
	y = m_rRect.top;
    if (m_Type & RSR_TOP_MOVE)
	y = cy - m_rRect.top;
    if (m_Type & RSR_TOP_PROPORTIONAL)
	y = (m_rRect.top * cy) / 100;

    if (m_Type & RSR_RIGHT_ANCHOR)
	w = (m_rRect.right) - x;
    if (m_Type & RSR_RIGHT_MOVE)
	w = (cx - m_rRect.right) - x;
    if (m_Type & RSR_RIGHT_PROPORTIONAL)
	w = ((m_rRect.right * cx) / 100) - x;

    if (m_Type & RSR_BOTTOM_ANCHOR)
	h = (m_rRect.bottom) - y;
    if (m_Type & RSR_BOTTOM_MOVE)
	h = (cy - m_rRect.bottom) - y;
    if (m_Type & RSR_BOTTOM_PROPORTIONAL)
	h = ((m_rRect.bottom * cy) / 100) - y;

    if (m_hWnd != NULL)
    {
	MoveWindow(m_hWnd, x, y, w, h, TRUE);
	InvalidateRect(m_hWnd, NULL, TRUE);
	UpdateWindow(m_hWnd);
    }
}

/*
Resizer::Resizer()
{
    m_hWnd = NULL;
    m_Type = RSR_ANCHORED;
    m_rRect.top = 0;
    m_rRect.bottom = 0;
    m_rRect.left = 0;
    m_rRect.right = 0;
}

Resizer::Resizer(HWND hWnd, ResizerType t)
{
    SetInitialPosition(hWnd, t);
}

Resizer::~Resizer()
{
    m_hWnd = NULL;
    m_Type = RSR_ANCHORED;
    m_rRect.top = 0;
    m_rRect.bottom = 0;
    m_rRect.left = 0;
    m_rRect.right = 0;
}

void Resizer::SetInitialPosition(HWND hWnd, ResizerType t)
{
    HWND hParent;
    RECT r1, r2;
    POINT point;

    m_hWnd = hWnd;
    m_Type = t;

    hParent = GetParent(hWnd);
    GetClientRect(hParent, &r1);
    GetWindowRect(hWnd, &r2);
    
    // This function would be easier to user but it doesn't return client coordinates
    //MapWindowPoints(NULL, hParent, (LPPOINT)&r2, 2);

    point.x = r2.left;
    point.y = r2.top;
    ScreenToClient(hParent, &point);
    r2.left = point.x;
    r2.top = point.y;

    point.x = r2.right;
    point.y = r2.bottom;
    ScreenToClient(hParent, &point);
    r2.right = point.x;
    r2.bottom = point.y;

    // Save the values necessary to compute the left,top position
    switch (t)
    {
    case RSR_ANCHORED:
    case RSR_STRETCH:
    case RSR_STRETCH_RIGHT:
    case RSR_STRETCH_BOTTOM:
	m_rRect.left = r2.left;
	m_rRect.top = r2.top;
	break;
    case RSR_ANCHOR_RIGHT:
	m_rRect.left = r1.right - r2.left;
	m_rRect.top = r2.top;
	break;
    case RSR_ANCHOR_BOTTOM:
	m_rRect.left = r2.left;
	m_rRect.top = r1.bottom - r2.top;
	break;
    case RSR_MOVE:
	m_rRect.left = r1.right - r2.left;
	m_rRect.top = r1.bottom - r2.top;
	break;
    case RSR_PROPORTIONAL:
	m_rRect.left = (r2.left * 100) / r1.right;
	m_rRect.top = (r2.top * 100) / r1.bottom;
	break;
    default:
	m_rRect.left = 0;
	m_rRect.top = 0;
	break;
    }

    // Save the values necessary to compute the right,bottom position
    switch (t)
    {
    case RSR_ANCHORED:
    case RSR_ANCHOR_RIGHT:
    case RSR_ANCHOR_BOTTOM:
    case RSR_MOVE:
	m_rRect.right = r2.right - r2.left; // width
	m_rRect.bottom = r2.bottom - r2.top; // height
	break;
    case RSR_STRETCH:
	m_rRect.right = r1.right - r2.right;
	m_rRect.bottom = r1.bottom - r2.bottom;
	break;
    case RSR_STRETCH_RIGHT:
	m_rRect.right = r1.right - r2.right;
	m_rRect.bottom = r2.bottom - r2.top; // height
	break;
    case RSR_STRETCH_BOTTOM:
	m_rRect.right = r2.right - r2.left; // width
	m_rRect.bottom = r1.bottom - r2.bottom;
	break;
    case RSR_PROPORTIONAL:
	m_rRect.right = (r2.right * 100) / r1.right;
	m_rRect.bottom = (r2.bottom * 100) / r1.bottom;
	break;
    default:
	m_rRect.right = 0;
	m_rRect.bottom = 0;
	break;
    }
}

void Resizer::Resize(int cx, int cy)
{
    int x,y,w,h;

    switch (m_Type)
    {
    case RSR_ANCHORED:
    case RSR_STRETCH:
    case RSR_STRETCH_RIGHT:
    case RSR_STRETCH_BOTTOM:
	x = m_rRect.left;
	y = m_rRect.top;
	break;
    case RSR_ANCHOR_RIGHT:
	x = cx - m_rRect.left;
	y = m_rRect.top;
	break;
    case RSR_ANCHOR_BOTTOM:
	x = m_rRect.left;
	y = cy - m_rRect.top;
	break;
    case RSR_MOVE:
	x = cx - m_rRect.left;
	y = cy - m_rRect.top;
	break;
    case RSR_PROPORTIONAL:
	x = (m_rRect.left * cx) / 100;
	y = (m_rRect.top * cy) / 100;
	break;
    default:
	x = 0;
	y = 0;
	break;
    }

    switch (m_Type)
    {
    case RSR_ANCHORED:
    case RSR_ANCHOR_RIGHT:
    case RSR_ANCHOR_BOTTOM:
    case RSR_MOVE:
	w = m_rRect.right;
	h = m_rRect.bottom;
	break;
    case RSR_STRETCH:
	w = cx - x - m_rRect.right;
	h = cy - y - m_rRect.bottom;
	break;
    case RSR_STRETCH_RIGHT:
	w = cx - x - m_rRect.right;
	h = m_rRect.bottom;
	break;
    case RSR_STRETCH_BOTTOM:
	w = m_rRect.right;
	h = cy - y - m_rRect.bottom;
	break;
    case RSR_PROPORTIONAL:
	w = ((m_rRect.right * cx) / 100) - x;
	h = ((m_rRect.bottom * cy) / 100) - y;
	break;
    default:
	w = 0;
	h = 0;
	break;
    }

    MoveWindow(m_hWnd, x, y, w, h, TRUE);
}
*/