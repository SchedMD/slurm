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
  Intracomm(const Intracomm& data) : Comm(data) { }
  
  // inter-language operability
  inline Intracomm(const MPI_Comm& data);

  //
  // Collective Communication
  //

  virtual void Barrier() const;

  virtual void Bcast(void *buffer, int count, 
		     const Datatype& datatype, int root) const;

  virtual void Gather(const void *sendbuf, int sendcount, 
		      const Datatype & sendtype, 
		      void *recvbuf, int recvcount, 
		      const Datatype & recvtype, int root) const;
  
  virtual void Gatherv(const void *sendbuf, int sendcount, 
		       const Datatype & sendtype, void *recvbuf, 
		       const int recvcounts[], const int displs[], 
		       const Datatype & recvtype, int root) const;
  
  virtual void Scatter(const void *sendbuf, int sendcount, 
		       const Datatype & sendtype, 
		       void *recvbuf, int recvcount, 
		       const Datatype & recvtype, int root) const;
  
  virtual void Scatterv(const void *sendbuf, const int sendcounts[], 
			const int displs[], const Datatype & sendtype,
			void *recvbuf, int recvcount, 
			const Datatype & recvtype, int root) const;
  
  virtual void Allgather(const void *sendbuf, int sendcount, 
			 const Datatype & sendtype, void *recvbuf, 
			 int recvcount, const Datatype & recvtype) const;

  virtual void Allgatherv(const void *sendbuf, int sendcount, 
			  const Datatype & sendtype, void *recvbuf, 
			  const int recvcounts[], const int displs[],
			  const Datatype & recvtype) const;

  virtual void Alltoall(const void *sendbuf, int sendcount, 
			const Datatype & sendtype, void *recvbuf, 
			int recvcount, const Datatype & recvtype) const;

  virtual void Alltoallv(const void *sendbuf, const int sendcounts[], 
			 const int sdispls[], const Datatype & sendtype, 
			 void *recvbuf, const int recvcounts[], 
			 const int rdispls[], const Datatype & recvtype) const;

  virtual void Reduce(const void *sendbuf, void *recvbuf, int count, 
		      const Datatype & datatype, const Op & op, 
		      int root) const;

  virtual void Allreduce(const void *sendbuf, void *recvbuf, int count,
			 const Datatype & datatype, const Op & op) const;

  virtual void Reduce_scatter(const void *sendbuf, void *recvbuf, 
			      int recvcounts[], 
			      const Datatype & datatype, 
			      const Op & op) const;

  virtual void Scan(const void *sendbuf, void *recvbuf, int count, 
		    const Datatype & datatype, const Op & op) const;

  Intracomm Dup() const;

#if MPI2CPP_VIRTUAL_FUNC_RET
  Intracomm&
#else
  Comm&
#endif
  Clone() const;

  virtual Intracomm Create(const Group& group) const;
  
  virtual Intracomm Split(int color, int key) const;

  virtual Intercomm Create_intercomm(int local_leader, const Comm& peer_comm,
				     int remote_leader, int tag) const;
  
  virtual Cartcomm Create_cart(int ndims, const int dims[],
			       const MPI2CPP_BOOL_T periods[], 
			       MPI2CPP_BOOL_T reorder) const;
  
  virtual Graphcomm Create_graph(int nnodes, const int index[],
				 const int edges[], 
				 MPI2CPP_BOOL_T reorder) const;


protected:

public: // JGS, friends issue
  static Op* current_op;

};
