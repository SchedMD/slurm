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

//
// Point-to-Point Communication
//

int
MPI::Status::Get_count(const MPI::Datatype& datatype) const
{
  return pmpi_status.Get_count(datatype);
}

MPI2CPP_BOOL_T
MPI::Status::Is_cancelled() const
{
  return pmpi_status.Is_cancelled();
}

int
MPI::Status::Get_elements(const MPI::Datatype& datatype) const
{
  return pmpi_status.Get_elements(datatype);
}

//
// Status Access
//
int
MPI::Status::Get_source() const
{
  return pmpi_status.Get_source();
}

void
MPI::Status::Set_source(int source)
{
  pmpi_status.Set_source(source);
}
  
int
MPI::Status::Get_tag() const
{
  return pmpi_status.Get_tag();
}
  
void
MPI::Status::Set_tag(int tag)
{
  pmpi_status.Set_tag(tag);
}
  
int
MPI::Status::Get_error() const
{
  return pmpi_status.Get_error();
}

void
MPI::Status::Set_error(int error)
{
  pmpi_status.Set_error(error);
}
