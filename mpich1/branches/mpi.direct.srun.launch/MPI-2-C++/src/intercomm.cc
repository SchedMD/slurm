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

MPI::Intercomm MPI::Intercomm::Dup() const
{
  return pmpi_comm.Dup();
}

#if MPI2CPP_VIRTUAL_FUNC_RET
MPI::Intercomm& MPI::Intercomm::Clone() const
{
  PMPI::Comm* pmpi_inter = &pmpi_comm.Clone();
  MPI::Intercomm* mpi_inter = new MPI::Intercomm(*(PMPI::Intercomm*)pmpi_inter);
  return *mpi_inter;
}
#else
MPI::Comm& MPI::Intercomm::Clone() const
{
  // JGS Memory leak, but no other way...
  PMPI::Comm* pmpi_inter = &pmpi_comm.Clone();
  MPI::Intercomm* mpi_inter = new MPI::Intercomm(*(PMPI::Intercomm*)pmpi_inter);
  return *mpi_inter;
}
#endif

int MPI::Intercomm::Get_remote_size() const
{
  return pmpi_comm.Get_remote_size();
}

MPI::Group MPI::Intercomm::Get_remote_group() const
{
  return pmpi_comm.Get_remote_group();
}

MPI::Intracomm MPI::Intercomm::Merge(MPI2CPP_BOOL_T high)
{
  return pmpi_comm.Merge(high);
}
