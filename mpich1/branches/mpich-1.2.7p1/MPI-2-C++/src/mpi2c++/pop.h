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


class Op {
public:
  // construction
  Op() : mpi_op(MPI_OP_NULL) { }

  Op(const Op& op) : op_user_function(op.op_user_function), mpi_op(op.mpi_op)  { }

  Op(const MPI_Op &i) : mpi_op(i) { }

  // destruction
  virtual ~Op() { }

  // assignment
  Op& operator=(const Op& op);
  Op& operator= (const MPI_Op &i);
  // comparison
  MPI2CPP_BOOL_T operator== (const Op &a);
  MPI2CPP_BOOL_T operator!= (const Op &a);
  // conversion functions for inter-language operability
  operator MPI_Op () const;
  //  operator MPI_Op* () const;

  // Collective Communication  (defined in op_inln.h)
  virtual void Init(User_function *func, MPI2CPP_BOOL_T commute);
  virtual void Free(void);

  User_function *op_user_function;

protected:
  MPI_Op mpi_op;

};

