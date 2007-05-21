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

inline _REAL_MPI_::Intercomm
_REAL_MPI_::Intercomm::Dup() const
{
  MPI_Comm newcomm;
  (void)MPI_Comm_dup(mpi_comm, &newcomm);
  return newcomm;
}

#if MPI2CPP_VIRTUAL_FUNC_RET
inline _REAL_MPI_::Intercomm&
_REAL_MPI_::Intercomm::Clone() const
{
  MPI_Comm newcomm;
  (void)MPI_Comm_dup(mpi_comm, &newcomm);
  _REAL_MPI_::Intercomm* dup = new _REAL_MPI_::Intercomm(newcomm);
  return *dup;
}
#else
inline _REAL_MPI_::Comm&
_REAL_MPI_::Intercomm::Clone() const
{
  MPI_Comm newcomm;
  (void)MPI_Comm_dup(mpi_comm, &newcomm);
  _REAL_MPI_::Intercomm* dup = new _REAL_MPI_::Intercomm(newcomm);
  return *dup;
}
#endif

inline int
_REAL_MPI_::Intercomm::Get_remote_size() const 
{
  int size;
  (void)MPI_Comm_remote_size(mpi_comm, &size);
  return size;
}

inline _REAL_MPI_::Group
_REAL_MPI_::Intercomm::Get_remote_group() const 
{
  MPI_Group group;
  (void)MPI_Comm_remote_group(mpi_comm, &group);
  return group;
}

inline _REAL_MPI_::Intracomm
_REAL_MPI_::Intercomm::Merge(MPI2CPP_BOOL_T high) 
{
  MPI_Comm newcomm;
  (void)MPI_Intercomm_merge(mpi_comm, (int)high, &newcomm);
  return newcomm;
}

