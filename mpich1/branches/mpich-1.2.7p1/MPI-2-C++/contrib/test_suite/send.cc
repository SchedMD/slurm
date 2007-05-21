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
send()
{
  char msg[150];
  int data;
  int i;
  int source;
  int tag;
  MPI::Status status;

  Testing("Send / Recv w/ Status");

  if(my_rank > 0) {
    data = my_rank;
    MPI::COMM_WORLD.Send(&data, 1, MPI::INT, 0, my_rank);
  } else {
    for(i = 1; i < comm_size; i++)  {
      data = -1;

      MPI::COMM_WORLD.Recv(&data, 1, MPI::INT, i, i, status);
      if(data != i) {
	sprintf(msg, "NODE %d - 1) ERROR in MPI::Recv, data = %d, should be %d",
		my_rank, data, i);
	Fail(msg);
      }

      source = status.Get_source();
      if(source != i) {
	sprintf(msg, "NODE %d - 2) ERROR in MPI::Recv, source = %d, should be %d",
		my_rank, source, i);
	Fail(msg);
      }
      
      tag = status.Get_tag();
      if(tag != i) {
	sprintf(msg, "NODE %d - 3) ERROR in MPI::Recv, tag = %d, should be %d",
		my_rank, tag, i);
	Fail(msg);
      }
    }
  }

  Pass(); // Send / Recv w/ Status

  MPI::COMM_WORLD.Barrier();

  Testing("Send / Recv w/o Status");

  if(my_rank) {
    data = my_rank;
    MPI::COMM_WORLD.Send(&data, 1, MPI::INT, 0, my_rank);
  } else {
    for(i = 1; i < comm_size; i++)  {
      data = -1;

      MPI::COMM_WORLD.Recv(&data, 1, MPI::INT, i, i);
      if(data != i) {
	sprintf(msg, "NODE %d - 5) ERROR in MPI::Recv, data = %d, should be %d",
		my_rank, data, i);
	Fail(msg);
      }
    }
  }
  
  Pass(); // Send / Recv w/o Status
}


