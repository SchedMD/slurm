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
topo()
{
  char msg[150];
  int topology;
  MPI::Intracomm comm1;

  comm1 = MPI::COMM_WORLD;

  Testing("Get_topology");

  topology = comm1.Get_topology();

  if(topology != MPI::UNDEFINED) {
    sprintf(msg, "NODE %d - 1) ERROR in comm1.Get_topology, topology = %d, should be %d (MPI::UNDEFINED)", my_rank, topology, MPI::UNDEFINED);
    Fail(msg);
  }

  Pass(); // Get_topology

  if(comm1 != MPI::COMM_NULL && comm1 != MPI::COMM_WORLD)
    comm1.Free();
}
