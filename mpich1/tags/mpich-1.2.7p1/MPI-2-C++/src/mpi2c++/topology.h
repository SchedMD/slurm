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


class Cartcomm : public Intracomm {
public:

  // construction
  Cartcomm() { }
  // copy
  Cartcomm(const Comm_Null& data) : Intracomm(data) { }
  // inter-language operability
  inline Cartcomm(const MPI_Comm& data);
#if _MPIPP_PROFILING_
  Cartcomm(const Cartcomm& data) : Intracomm(data), pmpi_comm(data) { }
  Cartcomm(const PMPI::Cartcomm& d) : Intracomm((const PMPI::Intracomm&)d), pmpi_comm(d) { }
  
  // assignment
  Cartcomm& operator=(const Cartcomm& data) {
    Intracomm::operator=(data);
    pmpi_comm = data.pmpi_comm; return *this; }
  Cartcomm& operator=(const Comm_Null& data) {
    Intracomm::operator=(data);
    pmpi_comm = (PMPI::Cartcomm)data; return *this; }
  // inter-language operability
  Cartcomm& operator=(const MPI_Comm& data) {
    Intracomm::operator=(data);
    pmpi_comm = data; return *this; }
#else
  Cartcomm(const Cartcomm& data) : Intracomm(data.mpi_comm) { }
  // assignment
  Cartcomm& operator=(const Cartcomm& data) {
    mpi_comm = data.mpi_comm; return *this; }
  Cartcomm& operator=(const Comm_Null& data) {
    mpi_comm = data; return *this; }
  // inter-language operability
  Cartcomm& operator=(const MPI_Comm& data) {
    mpi_comm = data; return *this; } 
#endif
  //
  // Groups, Contexts, and Communicators
  //

  Cartcomm Dup() const;

  virtual
#if MPI2CPP_VIRTUAL_FUNC_RET
  Cartcomm&
#else
  Comm&
#endif
  Clone() const;


  //
  // Groups, Contexts, and Communicators
  //

  virtual int Get_dim() const;

  virtual void Get_topo(int maxdims, int dims[], MPI2CPP_BOOL_T periods[],
			int coords[]) const;

  virtual int Get_cart_rank(const int coords[]) const;

  virtual void Get_coords(int rank, int maxdims, int coords[]) const;

  virtual void Shift(int direction, int disp,
		     int &rank_source, int &rank_dest) const;
  
  virtual Cartcomm Sub(const MPI2CPP_BOOL_T remain_dims[]);

  virtual int Map(int ndims, const int dims[], const MPI2CPP_BOOL_T periods[]) const;

#if _MPIPP_PROFILING_
private:
  PMPI::Cartcomm pmpi_comm;
#endif
};


//===================================================================
//                    Class Graphcomm
//===================================================================

class Graphcomm : public Intracomm {
public:

  // construction
  Graphcomm() { }
  // copy
  Graphcomm(const Comm_Null& data) : Intracomm(data) { }
  // inter-language operability
  inline Graphcomm(const MPI_Comm& data);
#if _MPIPP_PROFILING_
  Graphcomm(const Graphcomm& data) : Intracomm(data), pmpi_comm(data)  { }
  Graphcomm(const PMPI::Graphcomm& d) : Intracomm((const PMPI::Intracomm&)d), pmpi_comm(d) { }
  
  // assignment
  Graphcomm& operator=(const Graphcomm& data) {
    Intracomm::operator=(data);
    pmpi_comm = data.pmpi_comm; return *this; }
  Graphcomm& operator=(const Comm_Null& data) {
    Intracomm::operator=(data);
    pmpi_comm = (PMPI::Graphcomm)data; return *this; }
  // inter-language operability
  Graphcomm& operator=(const MPI_Comm& data) {
    Intracomm::operator=(data);
    pmpi_comm = data; return *this; }

#else
  Graphcomm(const Graphcomm& data) : Intracomm(data.mpi_comm) { }
  // assignment
  Graphcomm& operator=(const Graphcomm& data) {
    mpi_comm = data.mpi_comm; return *this; }
  Graphcomm& operator=(const Comm_Null& data) {
    mpi_comm = data; return *this; }
  // inter-language operability
  Graphcomm& operator=(const MPI_Comm& data) {
    mpi_comm = data; return *this; } 
#endif

  //
  // Groups, Contexts, and Communicators
  //
  
  Graphcomm Dup() const;

  virtual
#if MPI2CPP_VIRTUAL_FUNC_RET  
  Graphcomm&
#else
  Comm&
#endif
  Clone() const;

  //
  //  Process Topologies
  //

  virtual void Get_dims(int nnodes[], int nedges[]) const;

  virtual void Get_topo(int maxindex, int maxedges, int index[], 
			int edges[]) const;

  virtual int Get_neighbors_count(int rank) const;

  virtual void Get_neighbors(int rank, int maxneighbors, 
			     int neighbors[]) const;

  virtual int Map(int nnodes, const int index[], 
		  const int edges[]) const;

#if _MPIPP_PROFILING_
private:
  PMPI::Graphcomm pmpi_comm;
#endif
};

