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
// Point-to-Point Communication
//

inline int
_REAL_MPI_::Status::Get_count(const _REAL_MPI_::Datatype& datatype) const
{
  int count;
  //(MPI_Status*) is to cast away the const
  (void)MPI_Get_count((MPI_Status*)&mpi_status, datatype, &count);
  return count;
}

inline MPI2CPP_BOOL_T
_REAL_MPI_::Status::Is_cancelled() const
{
  int t;
  (void)MPI_Test_cancelled((MPI_Status*)&mpi_status, &t);
  return (MPI2CPP_BOOL_T) t;
}

inline int
_REAL_MPI_::Status::Get_elements(const _REAL_MPI_::Datatype& datatype) const
{
  int count;
  (void)MPI_Get_elements((MPI_Status*)&mpi_status, datatype, &count);
  return count;
}

//
// Status Access
//
inline int
_REAL_MPI_::Status::Get_source() const
{
  int source;
  source = mpi_status.MPI_SOURCE;
  return source;
}

inline void
_REAL_MPI_::Status::Set_source(int source)
{
  mpi_status.MPI_SOURCE = source;
}

inline int
_REAL_MPI_::Status::Get_tag() const
{
  int tag;
  tag = mpi_status.MPI_TAG;
  return tag;
}

inline void
_REAL_MPI_::Status::Set_tag(int tag)
{
  mpi_status.MPI_TAG = tag;
}

inline int
_REAL_MPI_::Status::Get_error() const
{
  int error;
  error = mpi_status.MPI_ERROR;
  return error;
}

inline void
_REAL_MPI_::Status::Set_error(int error)
{
  mpi_status.MPI_ERROR = error;
}
