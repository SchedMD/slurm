// Copyright 1997-2000, University of Notre Dame.
// Authors: Jeremy G. Siek, Jeffery M. Squyres, Michael P. McNally, and
//          Andrew Lumsdaine
// 
// This file is part of the Notre Dame C++ bindings for MPI.
// 
// You should have received a copy of the License Agreement for the Notre
// Dame C++ bindings for MPI along with the software; see the file
// LICENSE.  If not, contact Office of Research, University of Notre
// Dame, Notre Dame, IN 46556.
// 
// Permission to modify the code and to distribute modified code is
// granted, provided the text of this NOTICE is retained, a notice that
// the code was modified is included with the above COPYRIGHT NOTICE and
// with the COPYRIGHT NOTICE in the LICENSE file, and that the LICENSE
// file is distributed with the modified code.
// 
// LICENSOR MAKES NO REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED.
// By way of example, but not limitation, Licensor MAKES NO
// REPRESENTATIONS OR WARRANTIES OF MERCHANTABILITY OR FITNESS FOR ANY
// PARTICULAR PURPOSE OR THAT THE USE OF THE LICENSED SOFTWARE COMPONENTS
// OR DOCUMENTATION WILL NOT INFRINGE ANY PATENTS, COPYRIGHTS, TRADEMARKS
// OR OTHER RIGHTS.
// 
// Additional copyrights may follow.
// I made this.

#include "mpi2c++_test.h"

void
pack_test()
{
  char buffer[100];
  char msg[150];
  double din;
  double dout;
  int iin;
  int iout;
  int position;

  din = 66.6;
  dout = 0.0;
  iin = 69;
  iout = 0;
  position = 0;

  Testing("Pack / Unpack");

  MPI::INT.Pack(&iin, 1, buffer, sizeof(buffer), position, MPI::COMM_WORLD);
  MPI::DOUBLE.Pack(&din, 1, buffer, sizeof(buffer), position, MPI::COMM_WORLD);

  position = 0;

  MPI::INT.Unpack(buffer, sizeof(buffer), &iout, 1, position, MPI::COMM_WORLD);
  MPI::DOUBLE.Unpack(buffer, sizeof(buffer), &dout, 1, position, 
		     MPI::COMM_WORLD);

  if (iout != iin) {
    sprintf(msg, "NODE %d - 1) ERROR in pack/unpack, iout = %d, should be %d",
	    my_rank, iout, iin);
    Fail(msg);
  }

  if (dout != din) {
    sprintf(msg, "NODE %d - 2) ERROR in pack/unpack, dout = %f, should be %f",
	    my_rank, dout, din);
    Fail(msg);
  }

  Pass(); // Pack / Unpack
}
