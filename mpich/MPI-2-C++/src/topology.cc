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

#include "mpi++.h"


MPI::Cartcomm::Cartcomm(const MPI_Comm& data) : MPI::Intracomm(data), pmpi_comm(data)  { }


//
// Groups, Contexts, and Communicators
//

MPI::Cartcomm
MPI::Cartcomm::Dup() const
{
  return pmpi_comm.Dup();
}

#if MPI2CPP_VIRTUAL_FUNC_RET
MPI::Cartcomm& MPI::Cartcomm::Clone() const
{
  return (MPI::Cartcomm&)pmpi_comm.Clone();
}
#else
MPI::Comm& MPI::Cartcomm::Clone() const
{
  PMPI::Cartcomm& pmpiCart = (PMPI::Cartcomm&)pmpi_comm.Clone();
  MPI::Cartcomm* cartclone = new MPI::Cartcomm(pmpiCart);
  delete &pmpiCart;
  return *cartclone;
}
#endif


//
//  Process Topologies
//

int MPI::Cartcomm::Get_dim() const
{
  return pmpi_comm.Get_dim();
}

void MPI::Cartcomm::Get_topo(int maxdims, int dims[], MPI2CPP_BOOL_T periods[],
			     int coords[]) const
{
  pmpi_comm.Get_topo(maxdims, dims, periods, coords);
}

int MPI::Cartcomm::Get_cart_rank(const int coords[]) const
{
  return pmpi_comm.Get_cart_rank(coords);
}

void MPI::Cartcomm::Get_coords(int rank, int maxdims, int coords[]) const
{
  pmpi_comm.Get_coords(rank, maxdims, coords);
}

void MPI::Cartcomm::Shift(int direction, int disp,
			  int &rank_source, int &rank_dest) const
{
  pmpi_comm.Shift(direction, disp, rank_source, rank_dest);
}
  
MPI::Cartcomm MPI::Cartcomm::Sub(const MPI2CPP_BOOL_T remain_dims[])
{
  return pmpi_comm.Sub(remain_dims);
}

int MPI::Cartcomm::Map(int ndims, const int dims[], const MPI2CPP_BOOL_T periods[]) const
{
  return pmpi_comm.Map(ndims, dims, periods);
}

//
//   ========   Graphcomm member functions  ========
//

MPI::Graphcomm::Graphcomm(const MPI_Comm& data) : MPI::Intracomm(data), pmpi_comm(data)  { }

//
// Groups, Contexts, and Communicators
//

MPI::Graphcomm
MPI::Graphcomm::Dup() const
{
  return pmpi_comm.Dup();
}

#if MPI2CPP_VIRTUAL_FUNC_RET
MPI::Graphcomm& MPI::Graphcomm::Clone() const
{
  return (MPI::Graphcomm&)pmpi_comm.Clone();
}
#else
MPI::Comm& MPI::Graphcomm::Clone() const
{
  PMPI::Graphcomm& pmpigraph = (PMPI::Graphcomm&)pmpi_comm.Clone();
  MPI::Graphcomm* graphclone = new MPI::Graphcomm(pmpigraph);
  delete &pmpigraph;
  return *graphclone;
}
#endif

//
//  Process Topologies
//

void
MPI::Graphcomm::Get_dims(int nnodes[], int nedges[]) const 
{
  pmpi_comm.Get_dims(nnodes, nedges);
}

void
MPI::Graphcomm::Get_topo(int maxindex, int maxedges, int index[], 
	 int edges[]) const
{
  pmpi_comm.Get_topo(maxindex, maxedges, index, edges);
}

int
MPI::Graphcomm::Get_neighbors_count(int rank) const 
{
  return pmpi_comm.Get_neighbors_count(rank);
}

void
MPI::Graphcomm::Get_neighbors(int rank, int maxneighbors, 
	      int neighbors[]) const 
{
  pmpi_comm.Get_neighbors(rank, maxneighbors, neighbors);
}

int
MPI::Graphcomm::Map(int nnodes, const int index[], 
    const int edges[]) const 
{
  return pmpi_comm.Map(nnodes, index, edges);
}
