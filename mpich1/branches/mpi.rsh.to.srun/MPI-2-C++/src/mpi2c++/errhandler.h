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

#if _MPIPP_PROFILING_

  // construction / destruction
  inline Errhandler() { }

  inline virtual ~Errhandler() { }

  inline Errhandler(const MPI_Errhandler &i)
    : pmpi_errhandler(i) { }

 // copy / assignment
  inline Errhandler(const Errhandler& e)
    : pmpi_errhandler(e.pmpi_errhandler) { }

  inline Errhandler(const PMPI::Errhandler& e)
    : pmpi_errhandler(e) { }

  inline Errhandler& operator=(const Errhandler& e) {
    pmpi_errhandler = e.pmpi_errhandler; return *this; }

  // comparison
  inline MPI2CPP_BOOL_T operator==(const Errhandler &a) {
    return (MPI2CPP_BOOL_T)(pmpi_errhandler == a); }
  
  inline MPI2CPP_BOOL_T operator!=(const Errhandler &a) {
    return (MPI2CPP_BOOL_T)!(*this == a); }

  // inter-language operability
  inline Errhandler& operator= (const MPI_Errhandler &i) {
    pmpi_errhandler = i; return *this; }
 
  inline operator MPI_Errhandler() const { return pmpi_errhandler; }
 
  //  inline operator MPI_Errhandler*() { return pmpi_errhandler; }
  
  inline operator const PMPI::Errhandler&() const { return pmpi_errhandler; }

#else

  // construction / destruction
  inline Errhandler()
    : mpi_errhandler(MPI_ERRHANDLER_NULL) {}

  inline virtual ~Errhandler() { }

  inline Errhandler(const MPI_Errhandler &i)
    : mpi_errhandler(i) {}

 // copy / assignment
  inline Errhandler(const Errhandler& e)
    : handler_fn(e.handler_fn), mpi_errhandler(e.mpi_errhandler) { }

  inline Errhandler& operator=(const Errhandler& e)
  {
    mpi_errhandler = e.mpi_errhandler;
    handler_fn = e.handler_fn;
    return *this;
  }

  // comparison
  inline MPI2CPP_BOOL_T operator==(const Errhandler &a) {
    return (MPI2CPP_BOOL_T)(mpi_errhandler == a.mpi_errhandler); }
  
  inline MPI2CPP_BOOL_T operator!=(const Errhandler &a) {
    return (MPI2CPP_BOOL_T)!(*this == a); }

  // inter-language operability
  inline Errhandler& operator= (const MPI_Errhandler &i) {
    mpi_errhandler = i; return *this; }
 
  inline operator MPI_Errhandler() const { return mpi_errhandler; }
 
  //  inline operator MPI_Errhandler*() { return &mpi_errhandler; }
  
#endif

  //
  // Errhandler access functions
  //
  
  virtual void Free();

#if !_MPIPP_PROFILING_
  Comm::Errhandler_fn* handler_fn;
#endif

protected:
#if _MPIPP_PROFILING_
  PMPI::Errhandler pmpi_errhandler;
#else
  MPI_Errhandler mpi_errhandler;
#endif


public:
  // took out the friend decls
  //private:

  //this is for ERRORS_THROW_EXCEPTIONS
  //this is called from MPI::Real_init
  inline void init() const {
#if ! _MPIPP_PROFILING_
    // $%%@#%# AIX/POE 2.3.0.0 makes us put in this cast here
    (void)MPI_Errhandler_create((MPI_Handler_function*) &throw_excptn_fctn,
				(MPI_Errhandler *) &mpi_errhandler); 
#else
    pmpi_errhandler.init();
#endif
  }

  //this is for ERRORS_THROW_EXCEPTIONS
  //this is called from MPI::Finalize
  inline void free() const {
#if ! _MPIPP_PROFILING_
    (void)MPI_Errhandler_free((MPI_Errhandler *) &mpi_errhandler); 
#else
    pmpi_errhandler.init();
#endif
  }
};
