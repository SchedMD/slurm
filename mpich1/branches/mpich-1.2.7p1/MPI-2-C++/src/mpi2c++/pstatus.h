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
  friend class PMPI::Comm; //so I can access pmpi_status data member in comm.cc
  friend class PMPI::Request; //and also from request.cc

public:
  // construction
  inline Status() { }
  // copy
  Status(const Status& data) : mpi_status(data.mpi_status) { }

  inline Status(const MPI_Status &i) : mpi_status(i) { }

  inline virtual ~Status() {}

  Status& operator=(const Status& data) {
    mpi_status = data.mpi_status; return *this;
  } 

  // comparison, don't need for status

  // inter-language operability
  inline Status& operator= (const MPI_Status &i) {
    mpi_status = i; return *this; }
  inline operator MPI_Status () const { return mpi_status; }
  inline operator MPI_Status* () const { return (MPI_Status*)&mpi_status; }

  //
  // Point-to-Point Communication
  //

  inline virtual int Get_count(const Datatype& datatype) const;

  inline virtual MPI2CPP_BOOL_T Is_cancelled() const;

  virtual int Get_elements(const Datatype& datatype) const;

  //
  // Status Access
  //
  inline virtual int Get_source() const;

  inline virtual void Set_source(int source);
  
  inline virtual int Get_tag() const;

  inline virtual void Set_tag(int tag);
  
  inline virtual int Get_error() const;

  inline virtual void Set_error(int error);
  
protected:
  MPI_Status mpi_status;

};
