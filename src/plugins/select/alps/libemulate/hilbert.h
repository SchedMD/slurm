//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Filename:  hilbert.h
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

typedef unsigned int coord_t; // char,short,int for up to 8,16,32 bits per word

extern void TransposetoAxes(
coord_t* X,            // I O  position   [n]
int      b,            // I    # bits
int      n);           // I    dimension

extern void AxestoTranspose(
coord_t* X,            // I O  position   [n]
int      b,            // I    # bits
int      n);           // I    dimension
