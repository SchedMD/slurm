// -*- c++ -*-
//
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
//

#include <iostream.h> 
#include <mpi++.h>


int
main(int argc, char *argv[])
{
  int count = 5;
  MPI::Init(argc, argv);
  int i = 0;
  int msg = 123;

  int rank = MPI::COMM_WORLD.Get_rank();
  int size = MPI::COMM_WORLD.Get_size();
  int to   = (rank + 1) % size;
  int from = (size + rank - 1) % size;
  
  cout << "I am node " << rank << " of " << size << endl;
  cout << "Sending to " << to << " and receiving from " << from << endl;
  
  if (rank == size - 1)
    MPI::COMM_WORLD.Send(&msg, 1, MPI::INT, to, 4);
  
  for (i = 0; i < count; i++) {
    MPI::COMM_WORLD.Recv(&msg, 1, MPI::INT, from, MPI::ANY_TAG);
    cout << "Node " << rank << " received " << msg << endl;
    MPI::COMM_WORLD.Send(&msg, 1, MPI::INT, to, 4);
  }

  if (rank == 0) {
    MPI::COMM_WORLD.Recv(&msg, 1, MPI::INT, from, MPI::ANY_TAG);
    cout << "Node " << rank << " received " << msg << endl;
  }

  cout << "All done!" << endl;
  MPI::Finalize();
  
  return 0;
}
