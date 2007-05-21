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

class Intracomm : public Comm {
public:

  // construction
  Intracomm() { }
  // copy
  Intracomm(const Comm_Null& data) : Comm(data) { }
  // inter-language operability

#if _MPIPP_PROFILING_
  //NOTE: it is extremely important that Comm(data) happens below
  //  because there is a not only pmpi_comm in this Intracomm but
  //  there is also a pmpi_comm in the inherited Comm part. Both
  //  of these pmpi_comm's need to be initialized with the same
  //  MPI_Comm object. Also the assignment operators must take this
  //  into account.
  Intracomm(const Intracomm& data) : Comm(data), pmpi_comm(data)  { }

  Intracomm(const MPI_Comm& data) : Comm(data), pmpi_comm(data)  { }
  
  Intracomm(const PMPI::Intracomm& data) 
    : Comm((const PMPI::Comm&)data), pmpi_comm(data) { }

  // assignment
  Intracomm& operator=(const Intracomm& data) {
    Comm::operator=(data);
    pmpi_comm = data.pmpi_comm; 
    return *this;
  }
  Intracomm& operator=(const Comm_Null& data) {
    Comm::operator=(data);
    pmpi_comm = (PMPI::Intracomm)data; return *this;
  }
  // inter-language operability
  Intracomm& operator=(const MPI_Comm& data) {
    Comm::operator=(data);
    pmpi_comm = data;
    return *this;
  }

#else
  Intracomm(const Intracomm& data) : Comm(data.mpi_comm) { }

  inline Intracomm(const MPI_Comm& data);

  // assignment
  Intracomm& operator=(const Intracomm& data) {
    mpi_comm = data.mpi_comm; return *this;
  }

  Intracomm& operator=(const Comm_Null& data) {
    mpi_comm = data; return *this;
  }

  // inter-language operability
  Intracomm& operator=(const MPI_Comm& data) {
    mpi_comm = data; return *this; } 

#endif

  //
  // Collective Communication
  //

  virtual void
  Barrier() const;

  virtual void
  Bcast(void *buffer, int count, 
	const Datatype& datatype, int root) const;
  
  virtual void
  Gather(const void *sendbuf, int sendcount, 
	 const Datatype & sendtype, 
	 void *recvbuf, int recvcount, 
	 const Datatype & recvtype, int root) const;
  
  virtual void
  Gatherv(const void *sendbuf, int sendcount, 
	  const Datatype & sendtype, void *recvbuf, 
	  const int recvcounts[], const int displs[], 
	  const Datatype & recvtype, int root) const;
  
  virtual void
  Scatter(const void *sendbuf, int sendcount, 
	  const Datatype & sendtype, 
	  void *recvbuf, int recvcount, 
	  const Datatype & recvtype, int root) const;
  
  virtual void
  Scatterv(const void *sendbuf, const int sendcounts[], 
	   const int displs[], const Datatype & sendtype,
	   void *recvbuf, int recvcount, 
	   const Datatype & recvtype, int root) const;
  
  virtual void
  Allgather(const void *sendbuf, int sendcount, 
	    const Datatype & sendtype, void *recvbuf, 
	    int recvcount, const Datatype & recvtype) const;
  
  virtual void
  Allgatherv(const void *sendbuf, int sendcount, 
	     const Datatype & sendtype, void *recvbuf, 
	     const int recvcounts[], const int displs[],
	     const Datatype & recvtype) const;
  
  virtual void
  Alltoall(const void *sendbuf, int sendcount, 
	   const Datatype & sendtype, void *recvbuf, 
	   int recvcount, const Datatype & recvtype) const;
  
  virtual void
  Alltoallv(const void *sendbuf, const int sendcounts[], 
	    const int sdispls[], const Datatype & sendtype, 
	    void *recvbuf, const int recvcounts[], 
	    const int rdispls[], const Datatype & recvtype) const;
  
  virtual void
  Reduce(const void *sendbuf, void *recvbuf, int count, 
	 const Datatype & datatype, const Op & op, 
	 int root) const;
  
  
  virtual void
  Allreduce(const void *sendbuf, void *recvbuf, int count,
	    const Datatype & datatype, const Op & op) const;
  
  virtual void
  Reduce_scatter(const void *sendbuf, void *recvbuf, 
		 int recvcounts[], 
		 const Datatype & datatype, 
		 const Op & op) const;
  
  virtual void
  Scan(const void *sendbuf, void *recvbuf, int count, 
       const Datatype & datatype, const Op & op) const;
  
  Intracomm
  Dup() const;
  

  virtual
#if MPI2CPP_VIRTUAL_FUNC_RET
  Intracomm&
#else
  Comm&
#endif
  Clone() const;

  virtual Intracomm
  Create(const Group& group) const;
  
  virtual Intracomm
  Split(int color, int key) const;

  virtual Intercomm
  Create_intercomm(int local_leader, const Comm& peer_comm,
		   int remote_leader, int tag) const;
  
  virtual Cartcomm
  Create_cart(int ndims, const int dims[],
	      const MPI2CPP_BOOL_T periods[], MPI2CPP_BOOL_T reorder) const;
  
  virtual Graphcomm
  Create_graph(int nnodes, const int index[],
	       const int edges[], MPI2CPP_BOOL_T reorder) const;

  //#if _MPIPP_PROFILING_
  //  virtual const PMPI::Comm& get_pmpi_comm() const { return pmpi_comm; }
  //#endif
protected:


#if _MPIPP_PROFILING_
  PMPI::Intracomm pmpi_comm;
#endif

public: // JGS see above about friend decls
#if ! _MPIPP_PROFILING_
  static Op* current_op;
#endif

};
