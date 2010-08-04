//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Filename:  hilbert.c
//
// Purpose:   Hilbert and Linked-list utility procedures for BayeSys3.
//
// History:   TreeSys.c   17 Apr 1996 - 31 Dec 2002
//            Peano.c     10 Apr 2001 - 11 Jan 2003
//            merged       1 Feb 2003
//            Arith debug 28 Aug 2003
//            Hilbert.c   14 Oct 2003
//                         2 Dec 2003
//-----------------------------------------------------------------------------
/*
    Copyright (c) 1996-2003 Maximum Entropy Data Consultants Ltd,
                            114c Milton Road, Cambridge CB4 1XE, England

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

#include "license.txt"
*/

#include "src/plugins/topology/3d_torus/hilbert.h"

extern void TransposetoAxes(
coord_t* X,            // I O  position   [n]
int      b,            // I    # bits
int      n)            // I    dimension
{
    coord_t  M, P, Q, t;
    int      i;

// Gray decode by  H ^ (H/2)
    t = X[n-1] >> 1;
    for( i = n-1; i; i-- )
        X[i] ^= X[i-1];
    X[0] ^= t;

// Undo excess work
    M = 2 << (b - 1);
    for( Q = 2; Q != M; Q <<= 1 )
    {
        P = Q - 1;
        for( i = n-1; i; i-- )
            if( X[i] & Q ) X[0] ^= P;                              // invert
            else{ t = (X[0] ^ X[i]) & P;  X[0] ^= t;  X[i] ^= t; } // exchange
        if( X[0] & Q ) X[0] ^= P;                                  // invert
    }
}
extern void AxestoTranspose(
coord_t* X,            // I O  position   [n]
int      b,            // I    # bits
int      n)            // I    dimension
{
    coord_t  P, Q, t;
    int      i;

// Inverse undo
    for( Q = 1 << (b - 1); Q > 1; Q >>= 1 )
    {
        P = Q - 1;
        if( X[0] & Q ) X[0] ^= P;                                  // invert
        for( i = 1; i < n; i++ )
            if( X[i] & Q ) X[0] ^= P;                              // invert
            else{ t = (X[0] ^ X[i]) & P;  X[0] ^= t;  X[i] ^= t; } // exchange
    }

// Gray encode (inverse of decode)
    for( i = 1; i < n; i++ )
        X[i] ^= X[i-1];
    t = X[n-1];
    for( i = 1; i < b; i <<= 1 )
        X[n-1] ^= X[n-1] >> i;
    t ^= X[n-1];
    for( i = n-2; i >= 0; i-- )
        X[i] ^= t;
}
