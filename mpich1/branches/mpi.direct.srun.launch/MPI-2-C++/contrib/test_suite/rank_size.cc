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
rank_size()
{
  char msg[150];
  int rank;
  int size;

  Testing("Get_rank");

  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  if(my_rank != rank) {
    sprintf(msg, "NODE %d - 1) ERROR in MPI::Get_rank, rank = %d, should be %d", my_rank, my_rank, rank);
    Fail(msg);
  }

  Pass(); // Get_rank;

  Testing("Get_size");

  MPI_Comm_size(MPI_COMM_WORLD, &size);
  if(comm_size != size) {
    sprintf(msg, "NODE %d - 2) ERROR in MPI::Get_size, rank = %d, should be %d", my_rank, comm_size, size);
    Fail(msg);
  }

  Pass(); // Get_size
}
