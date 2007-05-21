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

inline PMPI::Op&
PMPI::Op::operator=(const PMPI::Op& op)
{
  mpi_op = op.mpi_op;
  op_user_function = op.op_user_function;
  return *this;
} 

// comparison
inline MPI2CPP_BOOL_T
PMPI::Op::operator==(const PMPI::Op &a)
{
  return (MPI2CPP_BOOL_T)(mpi_op == a.mpi_op);
}

inline MPI2CPP_BOOL_T //this will never be used?
PMPI::Op::operator!= (const PMPI::Op &a) { return (MPI2CPP_BOOL_T)!(*this == a); }

// inter-language operability
inline PMPI::Op&
PMPI::Op::operator= (const MPI_Op &i) { mpi_op = i; return *this; }

inline
PMPI::Op::operator MPI_Op () const { return mpi_op; }

//inline
//PMPI::Op::operator MPI_Op* () const { return (MPI_Op*)&mpi_op; }


