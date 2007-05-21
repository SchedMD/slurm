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

MPI::Datatype
MPI::Datatype::Create_contiguous(int count) const
{
  return pmpi_datatype.Create_contiguous(count);
}

MPI::Datatype
MPI::Datatype::Create_vector(int count, int blocklength,
			     int stride) const
{
  return pmpi_datatype.Create_vector(count, blocklength, stride);
}

MPI::Datatype
MPI::Datatype::Create_indexed(int count,
			      const int array_of_blocklengths[], 
			      const int array_of_displacements[]) const
{
  return pmpi_datatype.Create_indexed(count, array_of_blocklengths,
				      array_of_displacements);
}

MPI::Datatype
MPI::Datatype::Create_struct(int count, const int array_of_blocklengths[],
			     const Aint array_of_displacements[],
			     const Datatype array_of_types[])
{
  PMPI::Datatype* pmpi_types = new PMPI::Datatype[count];
  int i;
  for (i = 0; i < count; i++)
    pmpi_types[i] = array_of_types[i].pmpi();

  PMPI::Datatype data = PMPI::Datatype::Create_struct(count, 
						      array_of_blocklengths,
						      array_of_displacements, 
						      pmpi_types);
  delete[] pmpi_types;
  return data;
}

MPI::Datatype
MPI::Datatype::Create_hindexed(int count, const int array_of_blocklengths[],
			       const Aint array_of_displacements[]) const
{
  return pmpi_datatype.Create_hindexed(count, array_of_blocklengths,
				       array_of_displacements);
}


MPI::Datatype
MPI::Datatype::Create_hvector(int count, int blocklength, MPI::Aint stride) const
{
  return pmpi_datatype.Create_hvector(count, blocklength, stride);
}

int
MPI::Datatype::Get_size() const
{
  return pmpi_datatype.Get_size();
}

void
MPI::Datatype::Get_extent(_REAL_MPI_::Aint& lb, _REAL_MPI_::Aint& extent) const
{
  pmpi_datatype.Get_extent(lb, extent);
}

void
MPI::Datatype::Commit()
{
  pmpi_datatype.Commit();
}

void
MPI::Datatype::Free()
{
  pmpi_datatype.Free();
}



void
MPI::Datatype::Pack(const void* inbuf, int incount,
		    void *outbuf, int outsize,
		    int& position, const MPI::Comm &comm) const
{
  pmpi_datatype.Pack(inbuf, incount, outbuf, outsize, position,
		     comm);
}

void
MPI::Datatype::Unpack(const void* inbuf, int insize,
		      void *outbuf, int outcount, int& position,
		      const MPI::Comm& comm) const 
{
  pmpi_datatype.Unpack(inbuf, insize, outbuf, outcount, position,
		       comm);
}

int
MPI::Datatype::Pack_size(int incount, const MPI::Comm& comm) const 
{
  return pmpi_datatype.Pack_size(incount, comm);
}
