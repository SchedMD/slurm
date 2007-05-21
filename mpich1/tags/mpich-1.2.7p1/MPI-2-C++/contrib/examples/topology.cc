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
#include <assert.h>

void cartesian();
void graph();

int my_rank;


int
main(int argc, char *argv[])
{
  MPI::Init(argc, argv);
  my_rank = MPI::COMM_WORLD.Get_rank();

  if (MPI::COMM_WORLD.Get_size() != 4) {
    if (my_rank == 0)
      cerr << "This program must be invoked with 4 ranks." << endl;
  } else {
    cartesian();
    graph();
  }

  MPI::Finalize();
  return 0;
}
  
void 
cartesian()
{
  if (my_rank == 0)
    cout << endl << "CARTESIAN TOPOLOGY" << endl << endl;

  //  Create a 2-D mesh which is periodic in both dimensions.  Dims_create()
  //  will be used to set the number of nodes per dimension.  Periodic is
  //  set true, and Reorder is set false.  Note: if we wanted one
  //  dimension to be periodic and the other not, then an array of
  //  booleans would have to be created and passed in.  When all the
  //  dimensions have the same periodicity, then a single boolean can be
  //  used.

  int dims[2] = {0,0};

  MPI::Compute_dims(4, 2, dims);
  MPI2CPP_BOOL_T periods[2] = { MPI2CPP_TRUE, MPI2CPP_TRUE };
  MPI::Cartcomm cart = MPI::COMM_WORLD.Create_cart(2, dims, periods, 
						   MPI2CPP_FALSE);

  //  OOMPI_Cart_comm cart(OOMPI_COMM_WORLD, 2, 
  //		       OOMPI_COMM_WORLD.Dims_create(2,dims), MPI2CPP_TRUE, 
  //                   MPI2CPP_TRUE);

  cout << "cart is a " << dims[0] << "x" << dims[1] << " 2D mesh" << endl;

  // Find my left and right neighbors
  int right, left, me;
  cart.Shift(1, 1, me, right);
  cart.Shift(1, -1, me, left);

  // Find my coordinates in the mesh
  int coords[2];
  cart.Get_coords(my_rank, 2, coords);
  cout << "Process " << my_rank << " is at coords: " << coords[0] << " "
    << coords[1] << "." << " To the left is node " 
      << left << ". to the right is node " << right << "." << endl;
}

void
graph()
{
  int size = MPI::COMM_WORLD.Get_size();

  if (my_rank == 0)
    cout << endl << "GRAPH TOPOLOGY" << endl << endl;

  // This needs to be run with at least four processes.
  assert(size >= 4);

  // Create a graph with these connections 

  // process   | neighbors
  // ---------------------
  //    0      | 1,3
  //    1      | 0
  //    2      | 3
  //    3      | 0,2

  int edges[] = {1,3,0,3,0,2};
  int index[] = {2,3,4,6};

  MPI::Graphcomm graph = MPI::COMM_WORLD.Create_graph(4, index, edges, 
						      MPI2CPP_FALSE);

  for (int i=0; i<graph.Get_size(); i++) {
    int neighbors[2]; 
    graph.Get_neighbors(i,2,neighbors);
    cout << "Node " << i << " is connected to : ";
    int numneighbors = graph.Get_neighbors_count(i);
    for (int j=0; j< numneighbors; j++)
      cout << neighbors[j] << " ";
    cout << endl;
  }
}
