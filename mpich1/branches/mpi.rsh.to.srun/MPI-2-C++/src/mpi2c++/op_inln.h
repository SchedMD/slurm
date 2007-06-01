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

#if _MPIPP_PROFILING_

inline
MPI::Op::Op() { }
  
inline
MPI::Op::Op(const MPI::Op& o) : pmpi_op(o.pmpi_op) { }
  
inline
MPI::Op::Op(const MPI_Op& o) : pmpi_op(o) { }

inline
MPI::Op::~Op() { }

inline
MPI::Op& MPI::Op::operator=(const MPI::Op& op) {
  pmpi_op = op.pmpi_op; return *this;
}

// comparison
inline MPI2CPP_BOOL_T
MPI::Op::operator== (const MPI::Op &a) {
  return (MPI2CPP_BOOL_T)(pmpi_op == a.pmpi_op);
}

inline MPI2CPP_BOOL_T
MPI::Op::operator!= (const MPI::Op &a) {
  return (MPI2CPP_BOOL_T)!(*this == a);
}

// inter-language operability
inline MPI::Op&
MPI::Op::operator= (const MPI_Op &i) { pmpi_op = i; return *this; }

inline
MPI::Op::operator MPI_Op () const { return pmpi_op; }

//inline
//MPI::Op::operator MPI_Op* () { return pmpi_op; }


#else  // ============= NO PROFILING ===================================

// construction
inline
MPI::Op::Op() : mpi_op(MPI_OP_NULL) { }

inline
MPI::Op::Op(const MPI_Op &i) : mpi_op(i) { }

inline
MPI::Op::Op(const MPI::Op& op)
  : op_user_function(op.op_user_function), mpi_op(op.mpi_op) { }

inline 
MPI::Op::~Op() 
{ 
#if _MPIPP_DEBUG_
  mpi_op = MPI_OP_NULL;
  op_user_function = 0;
#endif
}  

inline MPI::Op&
MPI::Op::operator=(const MPI::Op& op) {
  mpi_op = op.mpi_op;
  op_user_function = op.op_user_function;
  return *this;
}

// comparison
inline MPI2CPP_BOOL_T
MPI::Op::operator== (const MPI::Op &a) { return (MPI2CPP_BOOL_T)(mpi_op == a.mpi_op); }

inline MPI2CPP_BOOL_T
MPI::Op::operator!= (const MPI::Op &a) { return (MPI2CPP_BOOL_T)!(*this == a); }

// inter-language operability
inline MPI::Op&
MPI::Op::operator= (const MPI_Op &i) { mpi_op = i; return *this; }

inline
MPI::Op::operator MPI_Op () const { return mpi_op; }

//inline
//MPI::Op::operator MPI_Op* () { return &mpi_op; }

#endif

inline void
_REAL_MPI_::Op::Init(_REAL_MPI_::User_function *func, MPI2CPP_BOOL_T commute)
{
  (void)MPI_Op_create(op_intercept , (int) commute, &mpi_op);
  op_user_function = (User_function*)func;
}


inline void
_REAL_MPI_::Op::Free()
{
  (void)MPI_Op_free(&mpi_op);
}
