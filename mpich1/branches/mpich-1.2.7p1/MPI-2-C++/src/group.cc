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

int MPI::Group::Get_size() const
{
  return pmpi_group.Get_size();
}

int MPI::Group::Get_rank() const 
{
  return pmpi_group.Get_rank();
}

void MPI::Group::Translate_ranks(const MPI::Group& group1, int n, const int ranks1[], 
				 const MPI::Group& group2, int ranks2[])
{
  PMPI::Group::Translate_ranks(group1, n, ranks1, group2, ranks2);
}

int MPI::Group::Compare(const MPI::Group& group1, const MPI::Group& group2)
{
  return PMPI::Group::Compare(group1, group2);
}

MPI::Group MPI::Group::Union(const MPI::Group &group1, const MPI::Group &group2)
{
  return PMPI::Group::Union(group1, group2);
}

MPI::Group MPI::Group::Intersect(const MPI::Group &group1, const MPI::Group &group2)
{
  return PMPI::Group::Intersect(group1, group2);
}

MPI::Group MPI::Group::Difference(const MPI::Group &group1, const MPI::Group &group2)
{
  return PMPI::Group::Difference(group1, group2);
}

MPI::Group MPI::Group::Incl(int n, const int ranks[]) const
{
  return pmpi_group.Incl(n, ranks);
}

MPI::Group MPI::Group::Excl(int n, const int ranks[]) const
{
  return pmpi_group.Excl(n, ranks);
}

MPI::Group MPI::Group::Range_incl(int n, const int ranges[][3]) const
{
  return pmpi_group.Range_incl(n, ranges);
}

MPI::Group MPI::Group::Range_excl(int n, const int ranges[][3]) const
{
  return pmpi_group.Range_excl(n, ranges);
}

void MPI::Group::Free()
{
  pmpi_group.Free();
}
