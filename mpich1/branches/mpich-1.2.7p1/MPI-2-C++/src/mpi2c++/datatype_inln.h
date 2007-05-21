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

inline _REAL_MPI_::Datatype
_REAL_MPI_::Datatype::Create_contiguous(int count) const
{
  MPI_Datatype newtype;
  (void)MPI_Type_contiguous(count, mpi_datatype, &newtype);
  return newtype;
}

inline _REAL_MPI_::Datatype
_REAL_MPI_::Datatype::Create_vector(int count, int blocklength,
			     int stride) const
{
  MPI_Datatype newtype;
  (void)MPI_Type_vector(count, blocklength, stride, mpi_datatype, &newtype);
  return newtype;
}

inline _REAL_MPI_::Datatype
_REAL_MPI_::Datatype::Create_indexed(int count,
				     const int array_of_blocklengths[], 
				     const int array_of_displacements[]) const
{
  MPI_Datatype newtype;
  (void)MPI_Type_indexed(count, (int *) array_of_blocklengths, 
			 (int *) array_of_displacements, mpi_datatype, &newtype);
  return newtype;
}

// JGS MPI_TYPE_STRUCT soon to be depracated in favor of
// MPI_TYPE_CREATE_STRUCT
inline _REAL_MPI_::Datatype
_REAL_MPI_::Datatype::Create_struct(int count, const int array_of_blocklengths[],
				    const _REAL_MPI_::Aint array_of_displacements[],
				    const _REAL_MPI_::Datatype array_of_types[])
{
  MPI_Datatype newtype;
  int i;
  MPI_Datatype* type_array = new MPI_Datatype[count];
  for (i=0; i < count; i++)
    type_array[i] = array_of_types[i];

  (void)MPI_Type_struct(count, (int*)array_of_blocklengths,
			(MPI_Aint*)array_of_displacements, type_array, &newtype);
  delete[] type_array;
  return newtype;
}

//JGS MPI_Type_hindexed to be replaced by MPI_Type_create_hindexed
inline _REAL_MPI_::Datatype
_REAL_MPI_::Datatype::Create_hindexed(int count, const int array_of_blocklengths[],
				      const _REAL_MPI_::Aint array_of_displacements[]) const
{
  MPI_Datatype newtype;
  (void)MPI_Type_hindexed(count, (int*)array_of_blocklengths,
			  (MPI_Aint*)array_of_displacements,
			  mpi_datatype, &newtype) ;
  return newtype;
}

//JGS MPI_Type_hvector to be replaced by MPI_Type_create_hvector
inline _REAL_MPI_::Datatype
_REAL_MPI_::Datatype::Create_hvector(int count, int blocklength,
				     _REAL_MPI_::Aint stride) const
{
  MPI_Datatype newtype;
  (void)MPI_Type_hvector(count, blocklength, (MPI_Aint)stride,
			 mpi_datatype, &newtype);

  return newtype;
}

inline int
_REAL_MPI_::Datatype::Get_size() const 
{
  int size;
  (void)MPI_Type_size(mpi_datatype, &size);
  return size;
}

inline void
_REAL_MPI_::Datatype::Get_extent(_REAL_MPI_::Aint& lb, _REAL_MPI_::Aint& extent) const
{
  // JGS MPI_TYPE_EXTENT and MPI_LB soon to be deprecated
  // in favor of MPI_TYPE_GET_EXTENT
  (void)MPI_Type_lb(mpi_datatype, &lb);
  (void)MPI_Type_extent(mpi_datatype, &extent); 
}

inline void
_REAL_MPI_::Datatype::Commit() 
{
  (void)MPI_Type_commit(&mpi_datatype);
}

inline void
_REAL_MPI_::Datatype::Free()
{
  (void)MPI_Type_free(&mpi_datatype);
}

inline void
_REAL_MPI_::Datatype::Pack(const void* inbuf, int incount,
			   void *outbuf, int outsize,
			   int& position, const _REAL_MPI_::Comm &comm) const
{
  (void)MPI_Pack((void *) inbuf, incount,  mpi_datatype, outbuf,
		 outsize, &position, comm);
}

inline void
_REAL_MPI_::Datatype::Unpack(const void* inbuf, int insize,
			     void *outbuf, int outcount, int& position,
			     const _REAL_MPI_::Comm& comm) const 
{
  (void)MPI_Unpack((void *) inbuf, insize, &position,
		   outbuf, outcount, mpi_datatype, comm);
}

inline int
_REAL_MPI_::Datatype::Pack_size(int incount, const _REAL_MPI_::Comm& comm) const 
{
  int size;
  (void)MPI_Pack_size(incount, mpi_datatype, comm, &size);
  return size;
}
