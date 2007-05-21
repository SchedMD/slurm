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

//
//   ========   Cartcomm member functions  ========
//

inline
_REAL_MPI_::Cartcomm::Cartcomm(const MPI_Comm& data) {
  int status;
  if (MPI::Is_initialized() && (data != MPI_COMM_NULL)) {
    (void)MPI_Topo_test(data, &status) ;
    if (status == MPI_CART)
      mpi_comm = data;
    else
      mpi_comm = MPI_COMM_NULL;
  }
  else {
    mpi_comm = data;
  }
}

//
// Groups, Contexts, and Communicators
//

inline _REAL_MPI_::Cartcomm
_REAL_MPI_::Cartcomm::Dup() const
{
  MPI_Comm newcomm;
  (void)MPI_Comm_dup(mpi_comm, &newcomm);
  return newcomm;
}

//
//  Process Topologies
//

inline int
_REAL_MPI_::Cartcomm::Get_dim() const 
{
  int ndims;
  (void)MPI_Cartdim_get(mpi_comm, &ndims);
  return ndims;
}

inline void
_REAL_MPI_::Cartcomm::Get_topo(int maxdims, int dims[], MPI2CPP_BOOL_T periods[],
			       int coords[]) const
{
  int *int_periods = new int [maxdims];
  int i;
  for (i=0; i<maxdims; i++) {
    int_periods[i] = (int)periods[i];
  }
  (void)MPI_Cart_get(mpi_comm, maxdims, dims, int_periods, coords);
  for (i=0; i<maxdims; i++) {
    periods[i] = (MPI2CPP_BOOL_T)int_periods[i];
  }
  delete [] int_periods;
}

inline int
_REAL_MPI_::Cartcomm::Get_cart_rank(const int coords[]) const 
{
  int rank;
  (void)MPI_Cart_rank(mpi_comm, (int*)coords, &rank);
  return rank;
}
  
inline void
_REAL_MPI_::Cartcomm::Get_coords(int rank, int maxdims, int coords[]) const 
{
  (void)MPI_Cart_coords(mpi_comm, rank, maxdims, coords);
} 

inline void
_REAL_MPI_::Cartcomm::Shift(int direction, int disp,
			    int &rank_source, int &rank_dest) const 
{
  (void)MPI_Cart_shift(mpi_comm, direction, disp, &rank_source, &rank_dest);
}

inline _REAL_MPI_::Cartcomm
_REAL_MPI_::Cartcomm::Sub(const MPI2CPP_BOOL_T remain_dims[]) 
{
  int ndims;
  MPI_Cartdim_get(mpi_comm, &ndims);
  int* int_remain_dims = new int[ndims];
  for (int i=0; i<ndims; i++) {
    int_remain_dims[i] = (int)remain_dims[i];
  }
  MPI_Comm newcomm;
  (void)MPI_Cart_sub(mpi_comm, int_remain_dims, &newcomm);
  delete [] int_remain_dims;
  return newcomm;
}

inline int
_REAL_MPI_::Cartcomm::Map(int ndims, const int dims[], const MPI2CPP_BOOL_T periods[]) const 
{
  int *int_periods = new int [ndims];
  for (int i=0; i<ndims; i++) {
    int_periods[i] = (int) periods[i];
  }
  int newrank;
  (void)MPI_Cart_map(mpi_comm, ndims, (int*)dims, int_periods, &newrank);
  delete [] int_periods;
  return newrank;
}


#if MPI2CPP_VIRTUAL_FUNC_RET
inline _REAL_MPI_::Cartcomm&
_REAL_MPI_::Cartcomm::Clone() const
{
  MPI_Comm newcomm;
  (void)MPI_Comm_dup(mpi_comm, &newcomm);
  _REAL_MPI_::Cartcomm* dup = new _REAL_MPI_::Cartcomm(newcomm);
  return *dup;
}
#else
inline _REAL_MPI_::Comm&
_REAL_MPI_::Cartcomm::Clone() const
{
  MPI_Comm newcomm;
  (void)MPI_Comm_dup(mpi_comm, &newcomm);
  _REAL_MPI_::Cartcomm* dup = new _REAL_MPI_::Cartcomm(newcomm);
  return *dup;
}
#endif

//
//   ========   Graphcomm member functions  ========
//

inline
_REAL_MPI_::Graphcomm::Graphcomm(const MPI_Comm& data) {
  int status;
  if (MPI::Is_initialized() && (data != MPI_COMM_NULL)) {
    (void)MPI_Topo_test(data, &status) ;
    if (status == MPI_GRAPH)
      mpi_comm = data;
    else
      mpi_comm = MPI_COMM_NULL;
  }
  else {
    mpi_comm = data;
  }
}

//
// Groups, Contexts, and Communicators
//

inline _REAL_MPI_::Graphcomm
_REAL_MPI_::Graphcomm::Dup() const
{
  MPI_Comm newcomm;
  (void)MPI_Comm_dup(mpi_comm, &newcomm);
  return newcomm;
}

//
//  Process Topologies
//

inline void
_REAL_MPI_::Graphcomm::Get_dims(int nnodes[], int nedges[]) const 
{
  (void)MPI_Graphdims_get(mpi_comm, nnodes, nedges);
}

inline void
_REAL_MPI_::Graphcomm::Get_topo(int maxindex, int maxedges, int index[], 
	 int edges[]) const
{
  (void)MPI_Graph_get(mpi_comm, maxindex, maxedges, index, edges);
}

inline int
_REAL_MPI_::Graphcomm::Get_neighbors_count(int rank) const 
{
  int nneighbors;
  (void)MPI_Graph_neighbors_count(mpi_comm, rank, &nneighbors);
  return nneighbors;
}

inline void
_REAL_MPI_::Graphcomm::Get_neighbors(int rank, int maxneighbors, 
	      int neighbors[]) const 
{
  (void)MPI_Graph_neighbors(mpi_comm, rank, maxneighbors, neighbors);
}

inline int
_REAL_MPI_::Graphcomm::Map(int nnodes, const int index[], 
    const int edges[]) const 
{
  int newrank;
  (void)MPI_Graph_map(mpi_comm, nnodes, (int*)index, (int*)edges, &newrank);
  return newrank;
}

#if MPI2CPP_VIRTUAL_FUNC_RET
inline _REAL_MPI_::Graphcomm&
_REAL_MPI_::Graphcomm::Clone() const
{
  MPI_Comm newcomm;
  (void)MPI_Comm_dup(mpi_comm, &newcomm);
  _REAL_MPI_::Graphcomm* dup = new _REAL_MPI_::Graphcomm(newcomm);
  return *dup;
}
#else
inline _REAL_MPI_::Comm&
_REAL_MPI_::Graphcomm::Clone() const
{
  MPI_Comm newcomm;
  (void)MPI_Comm_dup(mpi_comm, &newcomm);
  _REAL_MPI_::Graphcomm* dup = new _REAL_MPI_::Graphcomm(newcomm);
  return *dup;
}
#endif
