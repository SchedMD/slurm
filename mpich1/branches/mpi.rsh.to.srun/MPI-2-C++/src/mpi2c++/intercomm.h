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


class Intercomm : public Comm {
#if _MPIPP_PROFILING_
  //  friend class PMPI::Intercomm;
#endif
public:

  // construction
  Intercomm() : Comm(MPI_COMM_NULL) { }
  // copy
  Intercomm(const Comm_Null& data) : Comm(data) { }
  // inter-language operability
  Intercomm(const MPI_Comm& data) : Comm(data) { }

#if _MPIPP_PROFILING_
  // copy
  Intercomm(const Intercomm& data) : Comm(data), pmpi_comm(data.pmpi_comm) { }
  Intercomm(const PMPI::Intercomm& d) : Comm((const PMPI::Comm&)d), pmpi_comm(d) { }

  // assignment
  Intercomm& operator=(const Intercomm& data) {
    Comm::operator=(data);
    pmpi_comm = data.pmpi_comm; return *this; }
  Intercomm& operator=(const Comm_Null& data) {
    Comm::operator=(data);
    Intercomm& ic = (Intercomm&)data;
    pmpi_comm = ic.pmpi_comm; return *this; }
  // inter-language operability
  Intercomm& operator=(const MPI_Comm& data) {
    Comm::operator=(data);
    pmpi_comm = PMPI::Intercomm(data); return *this; }
#else
  // copy
  Intercomm(const Intercomm& data) : Comm(data.mpi_comm) { }
  // assignment
  Intercomm& operator=(const Intercomm& data) {
    mpi_comm = data.mpi_comm; return *this; }
  Intercomm& operator=(const Comm_Null& data) {
    mpi_comm = data; return *this; }
  // inter-language operability
  Intercomm& operator=(const MPI_Comm& data) {
    mpi_comm = data; return *this; } 

#endif
  

  //
  // Groups, Contexts, and Communicators
  //

  Intercomm Dup() const;

  virtual
#if MPI2CPP_VIRTUAL_FUNC_RET
  Intercomm&
#else
  Comm&
#endif
  Clone() const;

  virtual int Get_remote_size() const;

  virtual Group Get_remote_group() const;

  virtual Intracomm Merge(MPI2CPP_BOOL_T high);

  //#if _MPIPP_PROFILING_
  //  virtual const PMPI::Comm& get_pmpi_comm() const { return pmpi_comm; }
  //#endif

#if _MPIPP_PROFILING_
private:
  PMPI::Intercomm pmpi_comm;
#endif
};
