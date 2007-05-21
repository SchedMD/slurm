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
// Groups, Contexts, and Communicators
//

inline int
_REAL_MPI_::Group::Get_size() const
{
  int size;
  (void)MPI_Group_size(mpi_group, &size);
  return size;
}

inline int
_REAL_MPI_::Group::Get_rank() const 
{
  int rank;
  (void)MPI_Group_rank(mpi_group, &rank);
  return rank;
}

inline void
_REAL_MPI_::Group::Translate_ranks (const _REAL_MPI_::Group& group1, int n,
				    const int ranks1[], 
				    const _REAL_MPI_::Group& group2, int ranks2[])
{
  (void)MPI_Group_translate_ranks(group1, n, (int*)ranks1, group2, (int*)ranks2);
}

inline int
_REAL_MPI_::Group::Compare(const _REAL_MPI_::Group& group1, const _REAL_MPI_::Group& group2)
{
  int result;
  (void)MPI_Group_compare(group1, group2, &result);
  return result;
}

inline _REAL_MPI_::Group
_REAL_MPI_::Group::Union(const _REAL_MPI_::Group &group1, const _REAL_MPI_::Group &group2)
{
  MPI_Group newgroup;
  (void)MPI_Group_union(group1, group2, &newgroup);
  return newgroup;
}

inline _REAL_MPI_::Group
_REAL_MPI_::Group::Intersect(const _REAL_MPI_::Group &group1, const _REAL_MPI_::Group &group2)
{
  MPI_Group newgroup;
  (void)MPI_Group_intersection( group1,  group2, &newgroup);
  return newgroup;
}

inline _REAL_MPI_::Group
_REAL_MPI_::Group::Difference(const _REAL_MPI_::Group &group1, const _REAL_MPI_::Group &group2)
{
  MPI_Group newgroup;  
  (void)MPI_Group_difference(group1, group2, &newgroup);
  return newgroup;
}

inline _REAL_MPI_::Group
_REAL_MPI_::Group::Incl(int n, const int ranks[]) const
{
  MPI_Group newgroup;
  (void)MPI_Group_incl(mpi_group, n, (int*)ranks, &newgroup);
  return newgroup;
}

inline _REAL_MPI_::Group
_REAL_MPI_::Group::Excl(int n, const int ranks[]) const
{
  MPI_Group newgroup;
  (void)MPI_Group_excl(mpi_group, n, (int*)ranks, &newgroup);
  return newgroup;
}

inline _REAL_MPI_::Group
_REAL_MPI_::Group::Range_incl(int n, const int ranges[][3]) const
{
  MPI_Group newgroup;
  (void)MPI_Group_range_incl(mpi_group, n, (int(*)[3])ranges, &newgroup);
  return newgroup;
}

inline _REAL_MPI_::Group
_REAL_MPI_::Group::Range_excl(int n, const int ranges[][3]) const
{
  MPI_Group newgroup;
  (void)MPI_Group_range_excl(mpi_group, n, (int(*)[3])ranges, &newgroup);
  return newgroup;
}

inline void
_REAL_MPI_::Group::Free()
{
  (void)MPI_Group_free(&mpi_group);
}
