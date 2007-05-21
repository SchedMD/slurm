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

 
#include "mpi++.h"

void MPI::Intracomm::Barrier() const
{
  pmpi_comm.Barrier();
}

void MPI::Intracomm::Bcast(void *buffer, int count, 
			   const MPI::Datatype& datatype, int root) const
{
  pmpi_comm.Bcast(buffer, count, datatype, root);
}

void MPI::Intracomm::Gather(const void *sendbuf, int sendcount, 
			    const MPI::Datatype & sendtype, 
			    void *recvbuf, int recvcount, 
			    const MPI::Datatype & recvtype, int root) const
{
  pmpi_comm.Gather(sendbuf, sendcount, sendtype, recvbuf, recvcount,
		   recvtype, root);
}


void MPI::Intracomm::Gatherv(const void *sendbuf, int sendcount, 
			     const MPI::Datatype & sendtype, void *recvbuf, 
			     const int recvcounts[], const int displs[], 
			     const MPI::Datatype & recvtype, int root) const
{
  pmpi_comm.Gatherv(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs,
		    recvtype, root);
}

void MPI::Intracomm::Scatter(const void *sendbuf, int sendcount, 
			      const MPI::Datatype & sendtype, 
			      void *recvbuf, int recvcount, 
			      const MPI::Datatype & recvtype, int root) const
{
  pmpi_comm.Scatter(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, root);
}

void MPI::Intracomm::Scatterv(const void *sendbuf, const int sendcounts[], 
			      const int displs[], const MPI::Datatype & sendtype,
			      void *recvbuf, int recvcount, 
			      const MPI::Datatype & recvtype, int root) const
{
  pmpi_comm.Scatterv(sendbuf, sendcounts, displs, sendtype,
		     recvbuf, recvcount, recvtype, root);
}

void MPI::Intracomm::Allgather(const void *sendbuf, int sendcount, 
			       const MPI::Datatype & sendtype, void *recvbuf, 
			       int recvcount, const MPI::Datatype & recvtype) const 
{
  pmpi_comm.Allgather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype);
}

void MPI::Intracomm::Allgatherv(const void *sendbuf, int sendcount, 
				const MPI::Datatype & sendtype, void *recvbuf, 
				const int recvcounts[], const int displs[],
				const MPI::Datatype & recvtype) const
{
  pmpi_comm.Allgatherv(sendbuf, sendcount, sendtype,
		       recvbuf, recvcounts, displs, recvtype);
}

void MPI::Intracomm::Alltoall(const void *sendbuf, int sendcount, 
			      const MPI::Datatype & sendtype, void *recvbuf, 
			      int recvcount, const MPI::Datatype & recvtype) const
{
  pmpi_comm.Alltoall(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype);
}

void MPI::Intracomm::Alltoallv(const void *sendbuf, const int sendcounts[], 
			       const int sdispls[], const MPI::Datatype & sendtype, 
			       void *recvbuf, const int recvcounts[], 
			       const int rdispls[], const MPI::Datatype & recvtype) const 
{
  pmpi_comm.Alltoallv(sendbuf, sendcounts, sdispls, sendtype,
		      recvbuf, recvcounts, rdispls, recvtype);
}


void MPI::Intracomm::Reduce(const void *sendbuf, void *recvbuf, int count, 
			    const MPI::Datatype & datatype, const Op & op, 
			    int root) const
{
  pmpi_comm.Reduce(sendbuf, recvbuf, count, datatype, op, root);
}

void MPI::Intracomm::Allreduce(const void *sendbuf, void *recvbuf, int count,
			       const MPI::Datatype & datatype, const Op & op) const
{
  pmpi_comm.Allreduce(sendbuf, recvbuf, count, datatype, op);
}

void MPI::Intracomm::Reduce_scatter(const void *sendbuf, void *recvbuf, 
				    int recvcounts[], 
				    const MPI::Datatype & datatype, 
				    const Op & op) const
{
  pmpi_comm.Reduce_scatter(sendbuf, recvbuf, recvcounts, datatype, op);
}

void MPI::Intracomm::Scan(const void *sendbuf, void *recvbuf, int count, 
			  const MPI::Datatype & datatype, const Op & op) const
{
  pmpi_comm.Scan(sendbuf, recvbuf, count, datatype, op);
}

MPI::Intracomm MPI::Intracomm::Dup() const
{
  return pmpi_comm.Dup();
}

#if MPI2CPP_VIRTUAL_FUNC_RET
MPI::Intracomm& MPI::Intracomm::Clone() const
{
  return (MPI::Intracomm&)pmpi_comm.Clone();
}
#else
MPI::Comm& MPI::Intracomm::Clone() const
{
  PMPI::Intracomm& pmpiIntra = (PMPI::Intracomm&)pmpi_comm.Clone();
  MPI::Intracomm* intraclone = new MPI::Intracomm(pmpiIntra);
  delete &pmpiIntra;
  return *intraclone;
}
#endif


MPI::Intracomm
MPI::Intracomm::Create(const MPI::Group& group) const
{
  return pmpi_comm.Create(group);
}

MPI::Intracomm
MPI::Intracomm::Split(int color, int key) const
{
  return pmpi_comm.Split(color, key);
}


MPI::Intercomm MPI::Intracomm::Create_intercomm(int local_leader,
						const MPI::Comm& peer_comm,
						int remote_leader, int tag) const
{
  return pmpi_comm.Create_intercomm(local_leader, peer_comm, remote_leader, tag);
}

MPI::Cartcomm MPI::Intracomm::Create_cart(int ndims, const int dims[],
					  const MPI2CPP_BOOL_T periods[], MPI2CPP_BOOL_T reorder) const
{
  return pmpi_comm.Create_cart(ndims, dims, periods, reorder);
}

MPI::Graphcomm MPI::Intracomm::Create_graph(int nnodes, const int index[],
					    const int edges[], MPI2CPP_BOOL_T reorder) const
{
  return pmpi_comm.Create_graph(nnodes, index, edges, reorder);
}
