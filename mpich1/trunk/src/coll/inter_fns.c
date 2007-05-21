/*
 *  $Id: inter_fns.c,v 1.5 2000/07/12 17:18:41 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
#include "coll.h"

#define MPI_ERR_COMM_INTER MPIR_ERRCLASS_TO_CODE(MPI_ERR_COMM,MPIR_ERR_COMM_INTER)
/*
 * Provide the collective ops structure for inter communicators.
 * Doing it this way (as a different set of functions) removes a test
 * from each collective call, and gets us back at least the cost of the
 * additional indirections and function call we have to provide the 
 * abstraction !
 *
 * Written by James Cownie (Meiko) 31 May 1995
 */

/* Forward declarations */
static int inter_Barrier (struct MPIR_COMMUNICATOR *);
static int inter_Bcast (void*, int, struct MPIR_DATATYPE *, int, struct MPIR_COMMUNICATOR *);
static int inter_Gather (void*, int, struct MPIR_DATATYPE *, void*, 
				   int, struct MPIR_DATATYPE *, int, struct MPIR_COMMUNICATOR *);
static int inter_Gatherv (void*, int, struct MPIR_DATATYPE *, void*, int *, 
				    int *, struct MPIR_DATATYPE *, int, struct MPIR_COMMUNICATOR *); 
static int inter_Scatter (void*, int, struct MPIR_DATATYPE *, void*, int, 
				    struct MPIR_DATATYPE *, int, struct MPIR_COMMUNICATOR *);
static int inter_Scatterv (void*, int *, int *, struct MPIR_DATATYPE *, 
				     void*, int, struct MPIR_DATATYPE *, int, struct MPIR_COMMUNICATOR *);
static int inter_Allgather (void*, int, struct MPIR_DATATYPE *, void*, int, 
				      struct MPIR_DATATYPE *, struct MPIR_COMMUNICATOR *);
static int inter_Allgatherv (void*, int, struct MPIR_DATATYPE *, void*, int *,
				       int *, struct MPIR_DATATYPE *, struct MPIR_COMMUNICATOR *);
static int inter_Alltoall (void*, int, struct MPIR_DATATYPE *, 
				     void*, int, struct MPIR_DATATYPE *, struct MPIR_COMMUNICATOR *);
static int inter_Alltoallv (void*, int *, int *, 
				      struct MPIR_DATATYPE *, void*, int *, 
				      int *, struct MPIR_DATATYPE *, struct MPIR_COMMUNICATOR *);
static int inter_Alltoallw (void*, int *, int *, 
				      struct MPIR_DATATYPE *, void*, int *, 
				      int *, struct MPIR_DATATYPE *, struct MPIR_COMMUNICATOR *); 
static int inter_Reduce (void*, void*, int, 
				   struct MPIR_DATATYPE *, MPI_Op, int, struct MPIR_COMMUNICATOR *);
static int inter_Allreduce (void*, void*, int, 
				      struct MPIR_DATATYPE *, MPI_Op, struct MPIR_COMMUNICATOR *);
static int inter_Reduce_scatter (void*, void*, int *, 
					   struct MPIR_DATATYPE *, MPI_Op, struct MPIR_COMMUNICATOR *);
static int inter_Scan (void*, void*, int, struct MPIR_DATATYPE *, 
				 MPI_Op, struct MPIR_COMMUNICATOR * );

static struct _MPIR_COLLOPS inter_collops = {
    inter_Barrier,
    inter_Bcast,
    inter_Gather, 
    inter_Gatherv, 
    inter_Scatter,
    inter_Scatterv,
    inter_Allgather,
    inter_Allgatherv,
    inter_Alltoall,
    inter_Alltoallv,
    inter_Alltoallw,
    inter_Reduce,
    inter_Allreduce,
    inter_Reduce_scatter,
    inter_Scan,
    1                              /* Giving it a refcount of 1 ensures it
				    * won't ever get freed.
				    */
};

MPIR_COLLOPS MPIR_inter_collops = &inter_collops;

/* Now the functions, each one simply raises an error */
static int inter_Barrier( struct MPIR_COMMUNICATOR * comm )
{
    return MPIR_ERROR(comm, MPI_ERR_COMM_INTER,
		      "MPI_BARRIER");
}

static int inter_Bcast ( 
	void *buffer, 
	int count, 
	struct MPIR_DATATYPE *datatype, 
	int root, 
	struct MPIR_COMMUNICATOR *comm )
{
    return MPIR_ERROR(comm, MPI_ERR_COMM_INTER,
		      "MPI_BCAST");
}

static int inter_Gather ( 
	void *sendbuf, 
	int sendcnt, 
	struct MPIR_DATATYPE *sendtype, 
	void *recvbuf, 
	int recvcount, 
	struct MPIR_DATATYPE *recvtype, 
	int root, 
	struct MPIR_COMMUNICATOR *comm )
{
    return MPIR_ERROR(comm, MPI_ERR_COMM_INTER,
		      "MPI_GATHER");
}

static int inter_Gatherv ( 
	void *sendbuf, 
	int sendcnt,  
	struct MPIR_DATATYPE *sendtype, 
	void *recvbuf, 
	int *recvcnts, 
	int *displs, 
	struct MPIR_DATATYPE *recvtype, 
	int root, 
	struct MPIR_COMMUNICATOR *comm )
{
    return MPIR_ERROR(comm, MPI_ERR_COMM_INTER,
		      "MPI_GATHERV");
}

static int inter_Scatter ( 
	void *sendbuf, 
	int sendcnt, 
	struct MPIR_DATATYPE *sendtype, 
	void *recvbuf, 
	int recvcnt, 
	struct MPIR_DATATYPE *recvtype, 
	int root, 
	struct MPIR_COMMUNICATOR *comm )
{
    return MPIR_ERROR(comm, MPI_ERR_COMM_INTER,
		      "MPI_SCATTER");
}

static int inter_Scatterv ( 
	void *sendbuf, 
	int *sendcnts, 
	int *displs, 
	struct MPIR_DATATYPE *sendtype, 
	void *recvbuf, 
	int recvcnt,  
	struct MPIR_DATATYPE *recvtype, 
	int root, 
	struct MPIR_COMMUNICATOR *comm )
{
    return MPIR_ERROR(comm, MPI_ERR_COMM_INTER,
		      "MPI_SCATTERV");
}

static int inter_Allgather ( 
	void *sendbuf, 
	int sendcount, 
	struct MPIR_DATATYPE *sendtype,
	void *recvbuf, 
	int recvcount, 
	struct MPIR_DATATYPE *recvtype, 
	struct MPIR_COMMUNICATOR *comm )
{
    return MPIR_ERROR(comm, MPI_ERR_COMM_INTER,
		      "MPI_ALLGATHER");
}

static int inter_Allgatherv ( 
	void *sendbuf, 
	int sendcount,  
	struct MPIR_DATATYPE *sendtype, 
	void *recvbuf, 
	int *recvcounts, 
	int *displs,   
	struct MPIR_DATATYPE *recvtype, 
	struct MPIR_COMMUNICATOR *comm )
{
    return MPIR_ERROR(comm, MPI_ERR_COMM_INTER,
		      "MPI_ALLGATHERV");
}

static int inter_Alltoall( 
	void *sendbuf, 
	int sendcount, 
	struct MPIR_DATATYPE *sendtype, 
	void *recvbuf, 
	int recvcnt, 
	struct MPIR_DATATYPE *recvtype, 
	struct MPIR_COMMUNICATOR *comm )
{
    return MPIR_ERROR(comm, MPI_ERR_COMM_INTER,
		      "MPI_GATHERV");
}

static int inter_Alltoallv ( 
	void *sendbuf, 
	int *sendcnts, 
	int *sdispls, 
	struct MPIR_DATATYPE *sendtype, 
	void *recvbuf, 
	int *recvcnts, 
	int *rdispls, 
	struct MPIR_DATATYPE *recvtype, 
	struct MPIR_COMMUNICATOR *comm )
{
    return MPIR_ERROR(comm, MPI_ERR_COMM_INTER,
		      "MPI_ALLTOALLV");
}

static int inter_Alltoallw ( 
	void *sendbuf, 
	int *sendcnts, 
	int *sdispls, 
	struct MPIR_DATATYPE *sendtype, 
	void *recvbuf, 
	int *recvcnts, 
	int *rdispls, 
	struct MPIR_DATATYPE *recvtype, 
	struct MPIR_COMMUNICATOR *comm )
{
    return MPIR_ERROR(comm, MPI_ERR_COMM_INTER,
		      "MPI_ALLTOALLW");
}

static int inter_Reduce ( 
	void *sendbuf, 
	void *recvbuf, 
	int count, 
	struct MPIR_DATATYPE *datatype, 
	MPI_Op op, 
	int root, 
	struct MPIR_COMMUNICATOR *comm )
{
    return MPIR_ERROR(comm, MPI_ERR_COMM_INTER,
		      "MPI_REDUCE");
}

static int inter_Allreduce ( 
	void *sendbuf, 
	void *recvbuf, 
	int count, 
	struct MPIR_DATATYPE *datatype, 
	MPI_Op op, 
	struct MPIR_COMMUNICATOR *comm )
{
    return MPIR_ERROR(comm, MPI_ERR_COMM_INTER,
		      "MPI_ALLREDUCE");
}

static int inter_Reduce_scatter ( 
	void *sendbuf, 
	void *recvbuf, 
	int *recvcnts, 
	struct MPIR_DATATYPE *datatype, 
	MPI_Op op, 
	struct MPIR_COMMUNICATOR *comm )
{
    return MPIR_ERROR(comm, MPI_ERR_COMM_INTER,
		      "MPI_REDUCE_SCATTER");
}

static int inter_Scan ( 
	void *sendbuf, 
	void *recvbuf, 
	int count, 
	struct MPIR_DATATYPE *datatype, 
	MPI_Op op, 
	struct MPIR_COMMUNICATOR *comm )
{
    return MPIR_ERROR(comm, MPI_ERR_COMM_INTER,
		      "MPI_SCAN");
}

