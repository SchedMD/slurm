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
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA

#include "license.txt"
*/

#include <stdio.h>
typedef unsigned int coord_t; // char,short,int for up to 8,16,32 bits per word

static void TransposetoAxes(
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
static void AxestoTranspose(
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

/* This is an sample use of Skilling's functions above.
 * You will need to modify the code if the value of BITS or DIMS is changed.
 * The the output of this can be used to order the node name entries in slurm.conf */
#define BITS 5	/* number of bits used to store the axis values, size of Hilbert space */
#define DIMS 3	/* number of dimensions in the Hilbert space */
main(int argc, char **argv)
{
	int i, H;
	coord_t X[DIMS]; // any position in 32x32x32 cube for BITS=5
	if (argc != (DIMS + 1)) {
		printf("Usage %s X Y Z\n", argv[0]);
		exit(1);
	}
	for (i=0; i<DIMS; i++)
		X[i] = atoi(argv[i+1]);
	printf("Axis coordinates = %d %d %d\n", X[0], X[1], X[2]);

	AxestoTranspose(X, BITS, DIMS); // Hilbert transpose for 5 bits and 3 dimensions
	H = ((X[2]>>0 & 1) <<  0) + ((X[1]>>0 & 1) <<  1) + ((X[0]>>0 & 1) <<  2) +
		((X[2]>>1 & 1) <<  3) + ((X[1]>>1 & 1) <<  4) + ((X[0]>>1 & 1) <<  5) +
		((X[2]>>2 & 1) <<  6) + ((X[1]>>2 & 1) <<  7) + ((X[0]>>2 & 1) <<  8) +
		((X[2]>>3 & 1) <<  9) + ((X[1]>>3 & 1) << 10) + ((X[0]>>3 & 1) << 11) +
		((X[2]>>4 & 1) << 12) + ((X[1]>>4 & 1) << 13) + ((X[0]>>4 & 1) << 14);
	printf("Hilbert integer  = %d (%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d)\n", H,
	       X[0]>>4 & 1, X[1]>>4 & 1, X[2]>>4 & 1, X[0]>>3 & 1, X[1]>>3 & 1,
	       X[2]>>3 & 1, X[0]>>2 & 1, X[1]>>2 & 1, X[2]>>2 & 1, X[0]>>1 & 1,
	       X[1]>>1 & 1, X[2]>>1 & 1, X[0]>>0 & 1, X[1]>>0 & 1, X[2]>>0 & 1);

#if 0
	/* Used for validation purposes */
	TransposetoAxes(X, BITS, DIMS); // Hilbert transpose for 5 bits and 3 dimensions
	printf("Axis coordinates = %d %d %d\n", X[0], X[1], X[2]);
#endif
}
