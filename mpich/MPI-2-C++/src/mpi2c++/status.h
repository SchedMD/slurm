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


class Status {
#if _MPIPP_PROFILING_
  //  friend class PMPI::Status;
#endif
  friend class MPI::Comm; //so I can access pmpi_status data member in comm.cc
  friend class MPI::Request; //and also from request.cc

public:
#if _MPIPP_PROFILING_

  // construction / destruction
  Status() { }
  virtual ~Status() {}

  // copy / assignment
  Status(const Status& data) : pmpi_status(data.pmpi_status) { }

  Status(const MPI_Status &i) : pmpi_status(i) { }

  Status& operator=(const Status& data) {
    pmpi_status = data.pmpi_status; return *this; }

  // comparison, don't need for status

  // inter-language operability
  Status& operator= (const MPI_Status &i) {
    pmpi_status = i; return *this; }
  operator MPI_Status () const { return pmpi_status; }
  //  operator MPI_Status* () const { return pmpi_status; }
  operator const PMPI::Status&() const { return pmpi_status; }

#else

  Status() { }
  // copy
  Status(const Status& data) : mpi_status(data.mpi_status) { }

  Status(const MPI_Status &i) : mpi_status(i) { }

  virtual ~Status() {}

  Status& operator=(const Status& data) {
    mpi_status = data.mpi_status; return *this; }

  // comparison, don't need for status

  // inter-language operability
  Status& operator= (const MPI_Status &i) {
    mpi_status = i; return *this; }
  operator MPI_Status () const { return mpi_status; }
  //  operator MPI_Status* () const { return (MPI_Status*)&mpi_status; }

#endif

  //
  // Point-to-Point Communication
  //

  virtual int Get_count(const Datatype& datatype) const;

  virtual MPI2CPP_BOOL_T Is_cancelled() const;

  virtual int Get_elements(const Datatype& datatype) const;

  //
  // Status Access
  //
  virtual int Get_source() const;

  virtual void Set_source(int source);

  virtual int Get_tag() const;
  
  virtual void Set_tag(int tag);
  
  virtual int Get_error() const;

  virtual void Set_error(int error);

protected:
#if _MPIPP_PROFILING_
  PMPI::Status pmpi_status;
#else
  MPI_Status mpi_status;
#endif

};
