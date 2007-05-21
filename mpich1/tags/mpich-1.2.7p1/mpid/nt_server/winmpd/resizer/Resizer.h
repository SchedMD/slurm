/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef RESIZER_H
#define RESIZER_H

#include <winsock2.h>
#include <windows.h>

#define RSR_LEFT_ANCHOR          (0x1 << 0)
#define RSR_LEFT_MOVE            (0x1 << 1)
#define RSR_LEFT_PROPORTIONAL    (0x1 << 2)
#define RSR_RIGHT_ANCHOR         (0x1 << 3)
#define RSR_RIGHT_MOVE           (0x1 << 4)
#define RSR_RIGHT_PROPORTIONAL   (0x1 << 5)
#define RSR_TOP_ANCHOR           (0x1 << 6)
#define RSR_TOP_MOVE             (0x1 << 7)
#define RSR_TOP_PROPORTIONAL     (0x1 << 8)
#define RSR_BOTTOM_ANCHOR        (0x1 << 9)
#define RSR_BOTTOM_MOVE          (0x1 << 10)
#define RSR_BOTTOM_PROPORTIONAL  (0x1 << 11)

#define RSR_ALL_ANCHOR       ( RSR_LEFT_ANCHOR | RSR_RIGHT_ANCHOR | RSR_TOP_ANCHOR | RSR_BOTTOM_ANCHOR )
#define RSR_ALL_MOVE         ( RSR_LEFT_MOVE | RSR_RIGHT_MOVE | RSR_TOP_MOVE | RSR_BOTTOM_MOVE )
#define RSR_ALL_PROPORTIONAL ( RSR_LEFT_PROPORTIONAL | RSR_RIGHT_PROPORTIONAL | RSR_TOP_PROPORTIONAL | RSR_BOTTOM_PROPORTIONAL )

#define RSR_ANCHORED              RSR_ALL_ANCHOR
#define RSR_STRETCH_RIGHT         ( RSR_LEFT_ANCHOR | RSR_RIGHT_MOVE | RSR_TOP_ANCHOR | RSR_BOTTOM_ANCHOR )
#define RSR_ANCHOR_RIGHT          ( RSR_LEFT_MOVE | RSR_RIGHT_MOVE | RSR_TOP_ANCHOR | RSR_BOTTOM_ANCHOR )
#define RSR_ANCHOR_RIGHT_STRETCH  ( RSR_LEFT_MOVE | RSR_RIGHT_MOVE | RSR_TOP_ANCHOR | RSR_BOTTOM_MOVE )
#define RSR_ANCHOR_RIGHT_BOTTOM   RSR_ALL_MOVE
#define RSR_ANCHOR_BOTTOM_RIGHT   RSR_ALL_MOVE
#define RSR_MOVE                  RSR_ALL_MOVE
#define RSR_ANCHOR_BOTTOM_STRETCH ( RSR_LEFT_ANCHOR | RSR_RIGHT_MOVE | RSR_TOP_MOVE | RSR_BOTTOM_MOVE )
#define RSR_ANCHOR_BOTTOM         ( RSR_LEFT_ANCHOR | RSR_RIGHT_ANCHOR | RSR_TOP_MOVE | RSR_BOTTOM_MOVE )
#define RSR_STRETCH_BOTTOM        ( RSR_LEFT_ANCHOR | RSR_TOP_ANCHOR | RSR_RIGHT_ANCHOR | RSR_BOTTOM_MOVE )
#define RSR_STRETCH               ( RSR_LEFT_ANCHOR | RSR_RIGHT_MOVE | RSR_TOP_ANCHOR | RSR_BOTTOM_MOVE )
#define RSR_PROPORTIONAL          RSR_ALL_PROPORTIONAL
#define RSR_UL_PROPORTIONAL       ( RSR_LEFT_ANCHOR | RSR_RIGHT_PROPORTIONAL | RSR_TOP_ANCHOR | RSR_BOTTOM_PROPORTIONAL )
#define RSR_UR_PROPORTIONAL       ( RSR_LEFT_PROPORTIONAL | RSR_RIGHT_MOVE | RSR_TOP_ANCHOR | RSR_BOTTOM_PROPORTIONAL )
#define RSR_LL_PROPORTIONAL       ( RSR_LEFT_ANCHOR | RSR_RIGHT_PROPORTIONAL | RSR_TOP_PROPORTIONAL | RSR_BOTTOM_MOVE )
#define RSR_LR_PROPORTIONAL       ( RSR_LEFT_PROPORTIONAL | RSR_RIGHT_MOVE | RSR_TOP_PROPORTIONAL | RSR_BOTTOM_MOVE )

class Resizer
{
public:
    Resizer();
    Resizer(HWND hWnd, int t);
    ~Resizer();

    void SetInitialPosition(HWND hWnd, int t);
    void Resize(int cx, int cy);

private:
    RECT m_rRect;
    HWND m_hWnd;
    int m_Type;
};

/*
enum ResizerType
{
    RSR_ANCHORED,
    RSR_ANCHOR_RIGHT,
    RSR_ANCHOR_BOTTOM,
    RSR_MOVE,
    RSR_STRETCH,
    RSR_STRETCH_RIGHT,
    RSR_STRETCH_BOTTOM,
    RSR_BOTTOM_STRETCH_RIGHT,
    RSR_RIGHT_STRETCH_BOTTOM,
    RSR_PROPORTIONAL
};

class Resizer
{
public:
    Resizer();
    Resizer(HWND hWnd, ResizerType t);
    ~Resizer();

    void SetInitialPosition(HWND hWnd, ResizerType t);
    void Resize(int cx, int cy);

private:
    RECT m_rRect;
    HWND m_hWnd;
    ResizerType m_Type;
};
*/

#endif
