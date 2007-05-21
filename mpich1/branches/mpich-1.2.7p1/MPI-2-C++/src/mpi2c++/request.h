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


class Request {
#if _MPIPP_PROFILING_
  //    friend class PMPI::Request;
#endif
public:
#if _MPIPP_PROFILING_

  // construction
  Request() { }
  Request(const MPI_Request &i) : pmpi_request(i) { }

  // copy / assignment
  Request(const Request& r) : pmpi_request(r.pmpi_request) { }

  Request(const PMPI::Request& r) : pmpi_request(r) { }

  virtual ~Request() {}

  Request& operator=(const Request& r) {
    pmpi_request = r.pmpi_request; return *this; }

  // comparison
  MPI2CPP_BOOL_T operator== (const Request &a) 
  { return (MPI2CPP_BOOL_T)(pmpi_request == a.pmpi_request); }
  MPI2CPP_BOOL_T operator!= (const Request &a) 
  { return (MPI2CPP_BOOL_T)!(*this == a); }

  // inter-language operability
  Request& operator= (const MPI_Request &i) {
    pmpi_request = i; return *this;  }

  operator MPI_Request () const { return pmpi_request; }
  //  operator MPI_Request* () const { return pmpi_request; }
  operator const PMPI::Request&() const { return pmpi_request; }

#else

  // construction / destruction
  Request() { mpi_request = MPI_REQUEST_NULL; }
  virtual ~Request() {}
  Request(const MPI_Request &i) : mpi_request(i) { }

  // copy / assignment
  Request(const Request& r) : mpi_request(r.mpi_request) { }

  Request& operator=(const Request& r) {
    mpi_request = r.mpi_request; return *this; }

  // comparison
  MPI2CPP_BOOL_T operator== (const Request &a) 
  { return (MPI2CPP_BOOL_T)(mpi_request == a.mpi_request); }
  MPI2CPP_BOOL_T operator!= (const Request &a) 
  { return (MPI2CPP_BOOL_T)!(*this == a); }

  // inter-language operability
  Request& operator= (const MPI_Request &i) {
    mpi_request = i; return *this; }
  operator MPI_Request () const { return mpi_request; }
  //  operator MPI_Request* () const { return (MPI_Request*)&mpi_request; }

#endif

  //
  // Point-to-Point Communication
  //

  virtual void Wait(Status &status);

  virtual void Wait();

  virtual MPI2CPP_BOOL_T Test(Status &status);

  virtual MPI2CPP_BOOL_T Test();

  virtual void Free(void);

  static int Waitany(int count, Request array[], Status& status);

  static int Waitany(int count, Request array[]);

  static MPI2CPP_BOOL_T Testany(int count, Request array[], int& index, Status& status);

  static MPI2CPP_BOOL_T Testany(int count, Request array[], int& index);

  static void Waitall(int count, Request req_array[], Status stat_array[]);
 
  static void Waitall(int count, Request req_array[]);

  static MPI2CPP_BOOL_T Testall(int count, Request req_array[], Status stat_array[]);
 
  static MPI2CPP_BOOL_T Testall(int count, Request req_array[]);

  static int Waitsome(int incount, Request req_array[],
			     int array_of_indices[], Status stat_array[]) ;

  static int Waitsome(int incount, Request req_array[],
			     int array_of_indices[]);

  static int Testsome(int incount, Request req_array[],
			     int array_of_indices[], Status stat_array[]);

  static int Testsome(int incount, Request req_array[],
			     int array_of_indices[]);

  virtual void Cancel(void) const;


protected:
#if ! _MPIPP_PROFILING_
  MPI_Request mpi_request;
#endif

private:
#if !MPI2CPP_HAVE_STATUS_IGNORE
  static Status ignored_status;
#endif

#if _MPIPP_PROFILING_
  PMPI::Request pmpi_request;
#endif 

};


class Prequest : public Request {
#if _MPIPP_PROFILING_
  //  friend class PMPI::Prequest;
#endif
public:

  Prequest() { }

#if _MPIPP_PROFILING_
  Prequest(const Request& p) : Request(p), pmpi_request(p) { }

  Prequest(const PMPI::Prequest& r) : Request((const PMPI::Request&)r), pmpi_request(r)  { }

  Prequest(const MPI_Request &i) : Request(i), pmpi_request(i) { }
  
  virtual ~Prequest() { }

  Prequest& operator=(const Request& r) {
    Request::operator=(r);
    pmpi_request = (PMPI::Prequest)r; return *this; }

  Prequest& operator=(const Prequest& r) {
    Request::operator=(r);
    pmpi_request = r.pmpi_request; return *this; }
#else
  Prequest(const Request& p) : Request(p) { }

  Prequest(const MPI_Request &i) : Request(i) { }

  virtual ~Prequest() { }

  Prequest& operator=(const Request& r) {
    mpi_request = r; return *this; }

  Prequest& operator=(const Prequest& r) {
    mpi_request = r.mpi_request; return *this; }
#endif

  virtual void Start();

  static void Startall(int count, Prequest array_of_requests[]);

#if _MPIPP_PROFILING_
private:
  PMPI::Prequest pmpi_request;
#endif 
};
