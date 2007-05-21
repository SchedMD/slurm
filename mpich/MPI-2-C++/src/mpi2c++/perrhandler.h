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

class Errhandler {
public:
  // construction
  inline Errhandler()
    : mpi_errhandler(MPI_ERRHANDLER_NULL) {}
  // inter-language operability
  inline Errhandler(const MPI_Errhandler &i)
    : mpi_errhandler(i) {}
  // copy
  inline Errhandler(const Errhandler& e);

  inline virtual ~Errhandler() {}

  inline Errhandler& operator=(const Errhandler& e);

  // comparison
  inline MPI2CPP_BOOL_T operator==(const Errhandler &a);

  inline MPI2CPP_BOOL_T operator!=(const Errhandler &a) {
    return (MPI2CPP_BOOL_T)!(*this == a); }

  // inter-language operability
  inline Errhandler& operator= (const MPI_Errhandler &i) {
    mpi_errhandler = i; return *this; }
 
  inline operator MPI_Errhandler() const { return mpi_errhandler; }
 
  inline operator MPI_Errhandler*() { return &mpi_errhandler; }
  
  //
  // Errhandler access functions
  //
  
  inline virtual void Free(void);

  Comm::Errhandler_fn* handler_fn;

protected:
  MPI_Errhandler mpi_errhandler;

public:
  //this is for ERRORS_THROW_EXCEPTIONS
  //this is called from MPI::Real_init
  // g++ doesn't understand friends so this must be public :(
  inline void init() const {
    (void)MPI_Errhandler_create((MPI_Handler_function*)&throw_excptn_fctn,
				(MPI_Errhandler *)&mpi_errhandler); 
  }
};
