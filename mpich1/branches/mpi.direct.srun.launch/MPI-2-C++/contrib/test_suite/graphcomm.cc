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
/****************************************************************************

 MESSAGE PASSING INTERFACE TEST CASE SUITE

 Copyright IBM Corp. 1995

 IBM Corp. hereby grants a non-exclusive license to use, copy, modify, and
 distribute this software for any purpose and without fee provided that the
 above copyright notice and the following paragraphs appear in all copies.

 IBM Corp. makes no representation that the test cases comprising this
 suite are correct or are an accurate representation of any standard.

 In no event shall IBM be liable to any party for direct, indirect, special
 incidental, or consequential damage arising out of the use of this software
 even if IBM Corp. has been advised of the possibility of such damage.

 IBM CORP. SPECIFICALLY DISCLAIMS ANY WARRANTIES INCLUDING, BUT NOT LIMITED
 TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS AND IBM
 CORP. HAS NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES,
 ENHANCEMENTS, OR MODIFICATIONS.

****************************************************************************

 These test cases reflect an interpretation of the MPI Standard.  They are
 are, in most cases, unit tests of specific MPI behaviors.  If a user of any
 test case from this set believes that the MPI Standard requires behavior
 different than that implied by the test case we would appreciate feedback.

 Comments may be sent to:
    Richard Treumann
    treumann@kgn.ibm.com

****************************************************************************
*/
#include "mpi2c++_test.h"

void
graphcomm()
{
  MPI2CPP_BOOL_T reorder;
  char msg[150];
  int *cindex;
  int *cedges;
  int creorder;
  int ctype;
  int *edges;
  int i;
  int *dogindex;
  int nedges;
  int neighbors[2];
  int nnodes;
  int rank;
  int *tindex;
  int *tedges;
  int type;
  MPI::Graphcomm comm;
  MPI::Graphcomm dupcomm;
  MPI_Comm ccomm;

  comm = MPI::COMM_NULL;
  dupcomm = MPI::COMM_NULL;
  ccomm = MPI_COMM_NULL;

  neighbors[0] = -1;
  neighbors[1] = -1;

  dogindex = new int[comm_size];

  if (comm_size == 2) {
    // comm_size == 2 is a special case

    dogindex[0] = 1;
    dogindex[1] = 2;

    edges = new int[2];
    
    edges[0] = 1;
    edges[1] = 0;

    Testing("Create_graph");

    reorder = MPI2CPP_FALSE;
    
    comm = MPI::COMM_WORLD.Create_graph(comm_size, dogindex, edges, reorder);
    
    type = -1;
    
    type = comm.Get_topology();
    if (type != MPI::GRAPH) {
      sprintf(msg, "NODE %d - 1) ERROR in MPI::Create_graph, comm.Get_topology returned %d, which is not %d (MPI::GRAPH)", my_rank, type, MPI::GRAPH);
      Fail(msg);
    }
    
    Pass(); // Create_graph

    Testing("Get_dim");
    
    nnodes = 0;
    nedges = 0;
    
    comm.Get_dims(&nnodes, &nedges);
    if (nnodes != 2 || nedges != 2) {
      sprintf(msg, "NODE %d - 2) ERROR in MPI::Get_dim, nnodes, nedges = %d, %d, should be 2, 2", my_rank, nnodes, nedges);
      Fail(msg);
    }
    
    Pass(); // Get_dim

    Testing("Get_topo");

    if (flags[SKIP_IBM21014])
      Done("Skipped (IBM 2.1.0.14)");
    else if (flags[SKIP_IBM21015])
      Done("Skipped (IBM 2.1.0.15)");
    else if (flags[SKIP_IBM21016])
      Done("Skipped (IBM 2.1.0.16)");
    else if (flags[SKIP_IBM21017])
      Done("Skipped (IBM 2.1.0.17)");
    else {
      dogindex[0] = -1;
      dogindex[1] = -1;
      edges[0] = -1;
      edges[1] = -1;
      
      comm.Get_topo(2, 2, dogindex, edges);
      if (dogindex[0] != 1 || dogindex[1] != 2) {
	sprintf(msg, "NODE %d - 3) ERROR in MPI::Get_topo, dogindex[0] = %d, dogindex[1] = %d, should be 1, 2", my_rank, dogindex[0], dogindex[1]);
	Fail(msg);
      }
      if (edges[0] != 1 || edges[1] != 0) {
	sprintf(msg, "NODE %d - 4) ERROR in MPI::Get_topo, edges[0] = %d, edges[1] = %d, should be 1, 0", my_rank, edges[0], edges[1]);
	Fail(msg);
      }
      Pass(); // Get_topo
    }

    Testing("Get_neighbors_count");

    nnodes = 0;

    nnodes = comm.Get_neighbors_count(my_rank);
    if (nnodes != 1) {
      sprintf(msg, "NODE %d - 5) ERROR in MPI::Get_neighbors_count, nnodes = %d, should be 1", my_rank, nnodes);
      Fail(msg);
    }

    Pass(); // Get_neighbors_count

    Testing("Get_neighbors");

    comm.Get_neighbors(my_rank, 1, neighbors);  
    if (my_rank == 0)
      if (neighbors[0] != 1 || neighbors[1] != -1) {
	sprintf(msg, "NODE %d - 6) ERROR in MPI::Get_neighbors, neighbors[0] = %d, neighbors[1] = %d, should be 1, -1 (-1 is the default, comm_size == 2 only has one neighbor)", my_rank, neighbors[0], neighbors[1]);
	Fail(msg);
      }

    Pass(); // Get_neighbors

    Testing("Map");
    
    rank = comm.Map(2, dogindex, edges);
    if(rank < 0 || rank > comm_size) {
	sprintf(msg, "NODE %d - 7) ERROR in comm.Map, rank = %d, should be between 0 and %d", my_rank, rank, comm_size);
	Fail(msg);
    }

    Pass(); // Map
    Testing("Dup");

    dupcomm = comm.Dup();
    
    dogindex[0] = -1;
    dogindex[1] = -1;
    edges[0] = -1;
    edges[1] = -1;

    if (flags[SKIP_IBM21014])
      Done("Skipped (IBM 2.1.0.14)");
    else if (flags[SKIP_IBM21015])
      Done("Skipped (IBM 2.1.0.15)");
    else if (flags[SKIP_IBM21016])
      Done("Skipped (IBM 2.1.0.16)");
    else if (flags[SKIP_IBM21017])
      Done("Skipped (IBM 2.1.0.17)");
    else {
      dupcomm.Get_topo(2, 2, dogindex, edges);
      if (dogindex[0] != 1 || dogindex[1] != 2) {
	sprintf(msg, "NODE %d - 3) ERROR in dupcomm.Get_topo, dogindex[0] = %d, dogindex[1] = %d, should be 1, 2", my_rank, dogindex[0], dogindex[1]);
	Fail(msg);
      }
      if (edges[0] != 1 || edges[1] != 0) {
	sprintf(msg, "NODE %d - 4) ERROR in dupcomm.Get_topo, edges[0] = %d, edges[1] = %d, should be 1, 0", my_rank, edges[0], edges[1]);
	Fail(msg);
      }
    }

    Pass(); // Dup

    Testing("Clone");

    MPI::Graphcomm& clonecomm = (MPI::Graphcomm&)comm.Clone();

    dogindex[0] = -1;
    dogindex[1] = -1;
    edges[0] = -1;
    edges[1] = -1;

    if (flags[SKIP_IBM21014])
      Done("Skipped (IBM 2.1.0.14)");
    else if (flags[SKIP_IBM21015])
      Done("Skipped (IBM 2.1.0.15)");
    else if (flags[SKIP_IBM21016])
      Done("Skipped (IBM 2.1.0.16)");
    else if (flags[SKIP_IBM21017])
      Done("Skipped (IBM 2.1.0.17)");
    else {
      clonecomm.Get_topo(2, 2, dogindex, edges);
      if (dogindex[0] != 1 || dogindex[1] != 2) {
	sprintf(msg, "NODE %d - 3) ERROR in clonecomm.Get_topo, dogindex[0] = %d, dogindex[1] = %d, should be 1, 2", my_rank, dogindex[0], dogindex[1]);
	Fail(msg);
      }
      if (edges[0] != 1 || edges[1] != 0) {
	sprintf(msg, "NODE %d - 4) ERROR in clonecomm.Get_topo, edges[0] = %d, edges[1] = %d, should be 1, 0", my_rank, edges[0], edges[1]);
	Fail(msg);
      }
      if (clonecomm != MPI::COMM_NULL && clonecomm != MPI::COMM_WORLD) {
	clonecomm.Free();
	delete &clonecomm;
      }
    }

    Pass(); // Clone

    delete[] edges;
    // End of comm_size == 2 special case
  } else {
    // General test for comm_size > 2

    tindex = new int[comm_size];
    cindex = new int[comm_size];

    cindex[0] = dogindex[0] = 2;
    tindex[0] = 0;
    for (i = 1; i < comm_size; i++) {
      cindex[i] = dogindex[i] = dogindex[i - 1] + 2;
      tindex[i] = 0;
    }
    
    edges = new int[2 * comm_size];
    tedges = new int[2 * comm_size];
    cedges = new int[2 * comm_size];

#if 0
    cedges[0] = edges[0] = 1;
    cedges[1] = edges[1] = comm_size -1;
    cedges[2] = edges[2] = 0;
    cedges[3] = edges[3] = 2;
    cedges[4] = edges[4] = 1;
    cedges[5] = edges[5] = 3;
    for (i = 6; i < 2 * comm_size - 2; i += 2) {
      cedges[i] = edges[i] = edges[i - 4] + 2;
      cedges[i + 1] = edges[i + 1] = edges[i - 4] + 2;
    }
    cedges[i] = edges[i] = 0;
    cedges[i + 1] = edges[i + 1] = comm_size - 2;
#else
    for (i = 0; i < comm_size; i++) {
      cedges[i * 2] = edges[i * 2] = (i + comm_size - 1) % comm_size;
      cedges[i * 2 + 1] = edges[i * 2 + 1] = (i + 1) % comm_size;
    }
#endif

    for (i = 0; i < 2 * comm_size; i++)
      tedges[i] = -1;

    Testing("Create_graph"); {
      reorder = MPI2CPP_FALSE;
      comm = MPI::COMM_WORLD.Create_graph(comm_size, dogindex, edges, reorder);
      
      creorder = 0;
      MPI_Graph_create(MPI_COMM_WORLD, comm_size, dogindex, edges, 
		       creorder, &ccomm);
      
      type = ctype = -1;
      type = comm.Get_topology();
      MPI_Topo_test(ccomm, &ctype);
      if (type != ctype) {
	sprintf(msg, "NODE %d - 7) ERROR in MPI::Create_graph, comm.Get_topology returned %d, which is not %d (MPI::GRAPH)", my_rank, type, ctype);
	Fail(msg);
      }
    }
    Pass(); // Create_graph

    Testing("Get_dim");
    
    nnodes = 0;
    nedges = 0;
    
    comm.Get_dims(&nnodes, &nedges);
    if (nnodes != comm_size || nedges != 2 * comm_size) {
      sprintf(msg, "NODE %d - 8) ERROR in MPI::Get_dim, nnodes, nedges = %d, %d, should be %d, %d", my_rank, nnodes, nedges, comm_size, 2 * comm_size);
      Fail(msg);
    }
       
    Pass(); // Get_dim

    Testing("Get_topo"); 
    
    if (flags[SKIP_IBM21014])
      Done("Skipped (IBM 2.1.0.14)");
    else if (flags[SKIP_IBM21015])
      Done("Skipped (IBM 2.1.0.15)");
    else if (flags[SKIP_IBM21016])
      Done("Skipped (IBM 2.1.0.16)");
    else if (flags[SKIP_IBM21017])
      Done("Skipped (IBM 2.1.0.17)");
    else {
      comm.Get_topo(comm_size, 2 * comm_size, tindex, tedges);
      for (i = 0; i < comm_size; i++)
	if (tindex[i] != dogindex[i]) {
	  sprintf(msg, "NODE %d - 9) ERROR in comm.Get_topo, dogindex[%d] = %d, should be %d", my_rank, i, tindex[i], dogindex[i]);
	  Fail(msg);
	}
      for (i = 0; i < 2 * comm_size; i++)
	if (tedges[i] != edges[i]) {
	  sprintf(msg, "NODE %d - 10) ERROR in comm.Get_topo, edges[%d] = %d, should be %d", my_rank, i, tedges[i], edges[i]);
	  Fail(msg);
	}
      Pass(); // Get_topo
    }

    Testing("Get_neighbors_count");
    
    nnodes = 0;
    
    nnodes = comm.Get_neighbors_count(my_rank);
    if (nnodes != 2) {
      sprintf(msg, "NODE %d - 11) ERROR in MPI::Get_neighbors_count, nnodes = %d, should be 2", my_rank, nnodes);
      Fail(msg);
    }
    
    Pass(); // Get_neighbors_count

    Testing("Get_neighbors");
    
    comm.Get_neighbors(my_rank, 2, neighbors);  
    if (neighbors[0] != edges[dogindex[my_rank] - 2] || 
	neighbors[1] != edges[dogindex[my_rank] - 1]) {
      sprintf(msg, "NODE %d - 12) ERROR in MPI::Get_neighbors, neighbors[0] = %d, neighbors[1] = %d, should be %d, %d", my_rank, neighbors[0], neighbors[1], edges[dogindex[my_rank] - 2], edges[dogindex[my_rank] - 1]);
      Fail(msg);
    }

    Pass(); // Get_neighbors

    Testing("Map");
    
    rank = comm.Map(comm_size, dogindex, edges);
    if (rank != my_rank) 
      if (rank != MPI::UNDEFINED) 
	if(rank < 0 || rank > comm_size) {
	sprintf(msg, "NODE %d - 13) ERROR in comm.Map, rank = %d, should be between 0 and %d", my_rank, rank, comm_size);
	Fail(msg);
      }
    
    Pass(); // Map

    Testing("Dup");

    dupcomm = comm.Dup();

    if (flags[SKIP_IBM21014])
      Done("Skipped (IBM 2.1.0.14)");
    else if (flags[SKIP_IBM21015])
      Done("Skipped (IBM 2.1.0.15)");
    else if (flags[SKIP_IBM21016])
      Done("Skipped (IBM 2.1.0.16)");
    else if (flags[SKIP_IBM21017])
      Done("Skipped (IBM 2.1.0.17)");
    else {
      comm.Get_topo(comm_size, 2 * comm_size, tindex, tedges);
      for (i = 0; i < comm_size; i++)
	if (tindex[i] != dogindex[i]) {
	  sprintf(msg, "NODE %d - 14) ERROR in comm.Get_topo, dogindex[%d] incorrect", my_rank, i);
	  Fail(msg);
	}
      for (i = 0; i < 2 * comm_size; i++)
	if (tedges[i] != edges[i]) {
	  sprintf(msg, "NODE %d - 15) ERROR in comm.Get_topo, edges[%d] incorrect", my_rank, i);
	  Fail(msg);
	}
    }
    
    Pass(); // Dup

    Testing("Clone");
    
    MPI::Graphcomm& clonecomm1 = (MPI::Graphcomm&)comm.Clone();
    
    if (flags[SKIP_IBM21014])
      Done("Skipped (IBM 2.1.0.14)");
    else if (flags[SKIP_IBM21015])
      Done("Skipped (IBM 2.1.0.15)");
    else if (flags[SKIP_IBM21016])
      Done("Skipped (IBM 2.1.0.16)");
    else if (flags[SKIP_IBM21017])
      Done("Skipped (IBM 2.1.0.17)");
    else {
      clonecomm1.Get_topo(comm_size, 2 * comm_size, tindex, tedges);
      for (i = 0; i < comm_size; i++)
	if (tindex[i] != dogindex[i]) {
	  sprintf(msg, "NODE %d - 16) ERROR in comm.Get_topo, dogindex[%d] = %d, should be %d", my_rank, i, tindex[i], dogindex[i]);
	  Fail(msg);
	}
      for (i = 0; i < 2 * comm_size; i++)
	if (tedges[i] != edges[i]) {
	  sprintf(msg, "NODE %d - 17) ERROR in comm.Get_topo, edges[%d] incorrect", my_rank, i);
	  Fail(msg);
	}
    }
    if (clonecomm1 != MPI::COMM_NULL) {
      clonecomm1.Free();
      delete &clonecomm1;
    }
    Pass(); // Clone

    delete[] tindex;
    delete[] cindex;
    delete[] edges;
    delete[] tedges;
    delete[] cedges;
    // End of general test for comm_size > 2
  }
  
  delete[] dogindex;

  if (comm != MPI::COMM_NULL && comm != MPI::COMM_WORLD)
    comm.Free();
  if (dupcomm != MPI::COMM_NULL && dupcomm != MPI::COMM_WORLD)
    dupcomm.Free();
  if (ccomm != MPI_COMM_NULL && ccomm != MPI_COMM_WORLD)
    MPI_Comm_free(&ccomm);
}




