/*
 *  $Id: intra_fns.c,v 1.21 2002/11/06 00:15:59 thakur Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
#include "mpimem.h"
/* pt2pt for MPIR_Type_get_limits */
#include "mpipt2pt.h"
#include "coll.h"
#include "mpiops.h"

#define MPIR_ERR_OP_NOT_DEFINED MPIR_ERRCLASS_TO_CODE(MPI_ERR_OP,MPIR_ERR_NOT_DEFINED)

/* These should *always* use the PMPI versions of the functions to ensure that
   only any user code to catch an MPI function only uses the PMPI functions.

   Here is a partial solution.  In the weak symbol case, we simply change all 
   of the routines to their PMPI versions. 
 */
#ifdef HAVE_WEAK_SYMBOLS
/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif

/*
 * Provide the collective ops structure for intra communicators.
 * Reworked from the existing code by James Cownie (Meiko) 31 May 1995
 *
 * We put all of the functions in this one file, since this allows 
 * them to be static, avoiding name space pollution, and
 * we're going to need them all anyway. 
 *
 * These functions assume that the communicator is valid; routines that
 * call these should confirm that
 */

/* Forward declarations */
static int intra_Barrier (struct MPIR_COMMUNICATOR *comm );
static int intra_Bcast (void* buffer, int count, 
			struct MPIR_DATATYPE * datatype, int root, 
			struct MPIR_COMMUNICATOR *comm );
static int intra_Gather (void*, int, 
			 struct MPIR_DATATYPE *, void*, 
			 int, struct MPIR_DATATYPE *, 
			 int, struct MPIR_COMMUNICATOR *); 
static int intra_Gatherv (void*, int, 
			  struct MPIR_DATATYPE *, 
			  void*, int *, 
			  int *, struct MPIR_DATATYPE *, 
			  int, struct MPIR_COMMUNICATOR *); 
static int intra_Scatter (void* sendbuf, int sendcount, 
			  struct MPIR_DATATYPE * sendtype, 
			  void* recvbuf, int recvcount, 
			  struct MPIR_DATATYPE * recvtype, 
			  int root, struct MPIR_COMMUNICATOR *comm);
static int intra_Scatterv (void*, int *, 
			   int *, struct MPIR_DATATYPE *, 
			   void*, int, 
			   struct MPIR_DATATYPE *, int, 
			   struct MPIR_COMMUNICATOR *);
static int intra_Allgather (void*, int, 
			    struct MPIR_DATATYPE *, 
			    void*, int, 
			    struct MPIR_DATATYPE *, 
			    struct MPIR_COMMUNICATOR *comm);
static int intra_Allgatherv (void*, int, struct MPIR_DATATYPE *, 
			     void*, int *, int *, 
			     struct MPIR_DATATYPE *, 
			     struct MPIR_COMMUNICATOR *);
static int intra_Alltoall (void*, int, struct MPIR_DATATYPE *, 
			   void*, int, struct MPIR_DATATYPE *, 
			   struct MPIR_COMMUNICATOR *);
static int intra_Alltoallv (void*, int *, int *, 
			    struct MPIR_DATATYPE *, void*, int *, 
			    int *, struct MPIR_DATATYPE *, 
			    struct MPIR_COMMUNICATOR *);
static int intra_Reduce (void*, void*, int, struct MPIR_DATATYPE *, 
			 MPI_Op, int, struct MPIR_COMMUNICATOR *);
static int intra_Allreduce (void*, void*, int, 
			    struct MPIR_DATATYPE *, MPI_Op, 
			    struct MPIR_COMMUNICATOR *);
static int intra_Reduce_scatter (void*, void*, int *, 
				 struct MPIR_DATATYPE *, MPI_Op, 
				 struct MPIR_COMMUNICATOR *);
#ifdef MPIR_USE_BASIC_COLL
static int intra_Scan (void* sendbuf, void* recvbuf, int count, 
		       struct MPIR_DATATYPE * datatype, 
		       MPI_Op op, struct MPIR_COMMUNICATOR *comm );
#endif

/* I don't really want to to this this way, but for now... */
static struct _MPIR_COLLOPS intra_collops =  {
#ifdef MPID_Barrier
    MPID_FN_Barrier,
#else
    intra_Barrier,
#endif
#ifdef MPID_Bcast
    MPID_FN_Bcast,
#else
    intra_Bcast,
#endif
#ifdef MPID_Gather
    MPID_FN_Gather,
#else
    intra_Gather,
#endif
#ifdef MPID_Gatherv
    MPID_FN_Gatherv,
#else
    intra_Gatherv,
#endif
#ifdef MPID_Scatter
    MPID_FN_Scatter,
#else
    intra_Scatter,
#endif
#ifdef MPID_Scatterv
    MPID_FN_Scatterv,
#else
    intra_Scatterv,
#endif
#ifdef MPID_Allgather
    MPID_FN_Allgather,
#else
    intra_Allgather,
#endif
#ifdef MPID_Allgatherv
    MPID_FN_Allgatherv,
#else
    intra_Allgatherv,
#endif
#ifdef MPID_Alltoall
    MPID_FN_Alltoall,
#else
    intra_Alltoall,
#endif
#ifdef MPID_Alltoallv
    MPID_FN_Alltoallv,
#else
    intra_Alltoallv,
#endif
    0, /* Fix me! a dummy for alltoallw */
#ifdef MPID_Reduce
    MPID_FN_Reduce,
#else
    intra_Reduce,
#endif
#ifdef MPID_Allreduce
    MPID_FN_Allreduce,
#else
    intra_Allreduce,
#endif
#ifdef MPID_Reduce_scatter
    MPID_FN_Reduce_scatter,
#else
    intra_Reduce_scatter,
#endif
#ifdef FOO
#ifdef MPID_Reduce_scatterv
    MPID_FN_Reduce_scatterv,
#else
    intra_Reduce_scatterv,
#endif
#endif
#ifdef MPIR_USE_BASIC_COLL
    intra_Scan,
#else
    /* This is in intra_scan.c and is now the default */
#ifdef MPID_Scan
    MPID_FN_Scan,
#else
    MPIR_intra_Scan,
#endif
#endif
    1                              /* Giving it a refcount of 1 ensures it
                                    * won't ever get freed.
                                    *                                     */
};

MPIR_COLLOPS MPIR_intra_collops = &intra_collops;

/* Now the functions */
static int intra_Barrier ( struct MPIR_COMMUNICATOR *comm )
{
  int        rank, size, N2_prev, surfeit;
  int        d, dst, src;
  MPI_Status status;

  /* Intialize communicator size */
  (void) MPIR_Comm_size ( comm, &size );

#if defined(MPID_Barrier)  &&  !defined(TOPOLOGY_INTRA_FNS_H)
  if (comm->ADIBarrier) {
      MPID_Barrier( comm->ADIctx, comm );
      return MPI_SUCCESS;
      }
#endif
  /* If there's only one member, this is trivial */
  if ( size > 1 ) {

    /* Initialize collective communicator */
    comm = comm->comm_coll;
    (void) MPIR_Comm_rank ( comm, &rank );
    (void) MPIR_Comm_N2_prev ( comm, &N2_prev );
    surfeit = size - N2_prev;

    /* Lock for collective operation */
    MPID_THREAD_LOCK(comm->ADIctx,comm);

    /* Perform a combine-like operation */
    if ( rank < N2_prev ) {
      if( rank < surfeit ) {

        /* get the fanin letter from the upper "half" process: */
        dst = N2_prev + rank;

        MPI_Recv((void *)0,0,MPI_INT,dst,MPIR_BARRIER_TAG, comm->self, &status);
      }

      /* combine on embedded N2_prev power-of-two processes */
      for (d = 1; d < N2_prev; d <<= 1) {
        dst = (rank ^ d);

        MPI_Sendrecv( (void *)0,0,MPI_INT,dst, MPIR_BARRIER_TAG,
                     (void *)0,0,MPI_INT,dst, MPIR_BARRIER_TAG, 
                     comm->self, &status);
      }

      /* fanout data to nodes above N2_prev... */
      if ( rank < surfeit ) {
        dst = N2_prev + rank;
        MPI_Send( (void *)0, 0, MPI_INT, dst, MPIR_BARRIER_TAG, comm->self);
      }
    } 
    else {
      /* fanin data to power of 2 subset */
      src = rank - N2_prev;
      MPI_Sendrecv( (void *)0, 0, MPI_INT, src, MPIR_BARRIER_TAG,
                   (void *)0, 0, MPI_INT, src, MPIR_BARRIER_TAG, 
                   comm->self, &status);
    }

    /* Unlock for collective operation */
    MPID_THREAD_UNLOCK(comm->ADIctx,comm);

  } 
  return(MPI_SUCCESS); 
}

static int intra_Bcast ( 
	void *buffer, 
	int count, 
	struct MPIR_DATATYPE *datatype, 
	int root, 
	struct MPIR_COMMUNICATOR *comm )
{
  MPI_Status status;
  int        rank, size, src, dst;
  int        relative_rank, mask;
  int        mpi_errno = MPI_SUCCESS;
  static char myname[] = "MPI_BCAST";

  /* See the overview in Collection Operations for why this is ok */
  if (count == 0) return MPI_SUCCESS;

  /* Is root within the comm and more than 1 processes involved? */
  MPIR_Comm_size ( comm, &size );
#ifndef MPIR_NO_ERROR_CHECKING
  if (root >= size) 
      mpi_errno = MPIR_Err_setmsg( MPI_ERR_ROOT, MPIR_ERR_ROOT_TOOBIG, 
				   myname, (char *)0, (char *)0, root, size );
  else if (root < 0) 
      /* This catches the use of MPI_ROOT in an intracomm broadcast */
      mpi_errno = MPIR_Err_setmsg( MPI_ERR_ROOT, MPIR_ERR_DEFAULT, myname,
				   (char *)0, (char *)0, root );
  if (mpi_errno)
      return MPIR_ERROR(comm, mpi_errno, myname );
#endif
  
  /* If there is only one process */
  if (size == 1)
	return (mpi_errno);

  /* Get my rank and switch communicators to the hidden collective */
  MPIR_Comm_rank ( comm, &rank );
  comm = comm->comm_coll;
  
  /* Algorithm:
     This uses a fairly basic recursive subdivision algorithm.
     The root sends to the process size/2 away; the receiver becomes
     a root for a subtree and applies the same process. 

     So that the new root can easily identify the size of its
     subtree, the (subtree) roots are all powers of two (relative to the root)
     If m = the first power of 2 such that 2^m >= the size of the
     communicator, then the subtree at root at 2^(m-k) has size 2^k
     (with special handling for subtrees that aren't a power of two in size).
     
     Optimizations:
     
     The original code attempted to switch to a linear broadcast when
     the subtree size became too small.  As a further variation, the subtree
     broadcast sent data to the center of the block, rather than to one end.
     However, the original code did not properly compute the communications,
     resulting in extraneous (though harmless) communication.    

     For very small messages, using a linear algorithm (process 0 sends to
     process 1, who sends to 2, etc.) can be better, since no one process
     takes more than 1 send/recv time, and successive bcasts using the same
     root can overlap.  

     Another important technique for long messages is pipelining---sending
     the messages in blocks so that the message can be pipelined through
     the network without waiting for the subtree roots to receive the entire
     message before forwarding it to other processors.  This is hard to
     do if the datatype/count are not the same on each processor (note that
     this is allowed - only the signatures must match).  Of course, this can
     be accomplished at the byte transfer level, but it is awkward 
     from the MPI point-to-point routines.

     Nonblocking operations can be used to achieve some "horizontal"
     pipelining (on some systems) by allowing multiple send/receives
     to begin on the same processor.
  */

  relative_rank = (rank >= root) ? rank - root : rank - root + size;

  /* Lock for collective operation */
  MPID_THREAD_LOCK(comm->ADIctx,comm);

  /* Do subdivision.  There are two phases:
     1. Wait for arrival of data.  Because of the power of two nature
        of the subtree roots, the source of this message is alwyas the
        process whose relative rank has the least significant bit CLEARED.
        That is, process 4 (100) receives from process 0, process 7 (111) 
        from process 6 (110), etc.   
     2. Forward to my subtree

     Note that the process that is the tree root is handled automatically
     by this code, since it has no bits set.
     
   */
  mask = 0x1;
  while (mask < size) {
    if (relative_rank & mask) {
      src = rank - mask; 
      if (src < 0) src += size;
      mpi_errno = MPI_Recv(buffer,count,datatype->self,src,
			   MPIR_BCAST_TAG,comm->self,&status);
      if (mpi_errno) return mpi_errno;
      break;
    }
    mask <<= 1;
  }

  /* This process is responsible for all processes that have bits set from
     the LSB upto (but not including) mask.  Because of the "not including",
     we start by shifting mask back down one.

     We can easily change to a different algorithm at any power of two
     by changing the test (mask > 1) to (mask > block_size) 

     One such version would use non-blocking operations for the last 2-4
     steps (this also bounds the number of MPI_Requests that would
     be needed).
   */
  mask >>= 1;
  while (mask > 0) {
    if (relative_rank + mask < size) {
      dst = rank + mask;
      if (dst >= size) dst -= size;
      mpi_errno = MPI_Send (buffer,count,datatype->self,dst,
			    MPIR_BCAST_TAG,comm->self);
      if (mpi_errno) return mpi_errno;
    }
    mask >>= 1;
  }

  /* Unlock for collective operation */
  MPID_THREAD_UNLOCK(comm->ADIctx,comm);

  return (mpi_errno);
}

static int intra_Gather ( 
	void *sendbuf, 
	int sendcnt, 
	struct MPIR_DATATYPE *sendtype, 
	void *recvbuf, 
	int recvcount, 
	struct MPIR_DATATYPE *recvtype, 
	int root, 
	struct MPIR_COMMUNICATOR *comm )
{
  int        size, rank;
  int        mpi_errno = MPI_SUCCESS;
  MPI_Aint   extent;            /* Datatype extent */
  static char myname[] = "MPI_GATHER";

  /* Is root within the communicator? */
  MPIR_Comm_size ( comm, &size );
#ifndef MPIR_NO_ERROR_CHECKING
    if ( root >= size )
	mpi_errno = MPIR_Err_setmsg(MPI_ERR_ROOT,MPIR_ERR_ROOT_TOOBIG,
				    myname,(char *)0, (char *)0, root,size);
    if (root < 0) 
	mpi_errno = MPIR_Err_setmsg(MPI_ERR_ROOT,MPIR_ERR_DEFAULT,myname,
				    (char *)0,(char *)0,root);
    if (mpi_errno)
	return MPIR_ERROR(comm, mpi_errno, myname );
#endif

  /* Get my rank and switch communicators to the hidden collective */
  MPIR_Comm_rank ( comm, &rank );
  comm = comm->comm_coll;

  /* Lock for collective operation */
  MPID_THREAD_LOCK(comm->ADIctx,comm);

  /* If rank == root, then I recv lots, otherwise I send */
  /* This should use the same mechanism used in reduce; the intermediate nodes
     will need to allocate space. 
   */
  if ( rank == root ) {
    int         i;
    MPI_Request req;
    MPI_Status  status;

    /* This should really be COPYSELF.... , with the for look skipping
       root. */
    mpi_errno = MPI_Isend(sendbuf, sendcnt, sendtype->self, root, 
			  MPIR_GATHER_TAG, comm->self, &req);
    if (mpi_errno) return mpi_errno;
    MPI_Type_extent(recvtype->self, &extent);
    for ( i=0; i<size; i++ ) {
	mpi_errno = MPI_Recv( (void *)(((char*)recvbuf)+i*extent*recvcount), 
			     recvcount, recvtype->self, i, 
			     MPIR_GATHER_TAG, comm->self, &status);
	if (mpi_errno) return mpi_errno;
    }
    mpi_errno = MPI_Wait(&req, &status);
  }
  else 
      mpi_errno = MPI_Send(sendbuf, sendcnt, sendtype->self, root, 
			   MPIR_GATHER_TAG, comm->self);

  /* Unlock for collective operation */
  MPID_THREAD_UNLOCK(comm->ADIctx,comm);

  return (mpi_errno);
}

static int intra_Gatherv ( 
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
  int        size, rank;
  int        mpi_errno = MPI_SUCCESS;
  static char myname[] = "MPI_GATHERV";

  /* Is root within the communicator? */
  MPIR_Comm_size ( comm, &size );
#ifndef MPIR_NO_ERROR_CHECKING
    if ( root >= size )
	mpi_errno = MPIR_Err_setmsg(MPI_ERR_ROOT, MPIR_ERR_ROOT_TOOBIG,
				    myname,(char *)0,(char *)0,root,size);
    if (root < 0) 
	mpi_errno = MPIR_Err_setmsg(MPI_ERR_ROOT,MPIR_ERR_DEFAULT,myname,
				    (char *)0,(char*)0,root);
    if (mpi_errno)
	return MPIR_ERROR(comm, mpi_errno, myname );
#endif

  /* Get my rank and switch communicators to the hidden collective */
  MPIR_Comm_rank ( comm, &rank );
  comm = comm->comm_coll;

  /* Lock for collective operation */
  MPID_THREAD_LOCK(comm->ADIctx,comm);

  /* If rank == root, then I recv lots, otherwise I send */
  if ( rank == root ) {
      MPI_Aint       extent;
      int            i;
	MPI_Request req;
	MPI_Status       status;

    mpi_errno = MPI_Isend(sendbuf, sendcnt, sendtype->self, root, 
			  MPIR_GATHERV_TAG, comm->self, &req);
      if (mpi_errno) return mpi_errno;
    MPI_Type_extent(recvtype->self, &extent);
    for ( i=0; i<size; i++ ) {
	mpi_errno = MPI_Recv( (void *)((char *)recvbuf+displs[i]*extent), 
			     recvcnts[i], recvtype->self, i,
			     MPIR_GATHERV_TAG, comm->self, &status );
	if (mpi_errno) return mpi_errno;
    }
      mpi_errno = MPI_Wait(&req, &status);
  }
  else 
      mpi_errno = MPI_Send( sendbuf, sendcnt, sendtype->self, root, 
			   MPIR_GATHERV_TAG, comm->self );

  /* Unlock for collective operation */
  MPID_THREAD_UNLOCK(comm->ADIctx,comm);

  return (mpi_errno);
}

static int intra_Scatter ( 
	void *sendbuf, 
	int sendcnt, 
	struct MPIR_DATATYPE *sendtype, 
	void *recvbuf, 
	int recvcnt, 
	struct MPIR_DATATYPE *recvtype, 
	int root, 
	struct MPIR_COMMUNICATOR *comm )
{
  MPI_Status status;
  MPI_Aint   extent;
  int        rank, size, i;
  int        mpi_errno = MPI_SUCCESS;
  static char myname[] = "MPI_SCATTER";

  /* Get size and rank */
  MPIR_Comm_size ( comm, &size );
  MPIR_Comm_rank ( comm, &rank );

  /* Check for invalid arguments */
#ifndef MPIR_NO_ERROR_CHECKING
    if ( root >= size )
	mpi_errno = MPIR_Err_setmsg(MPI_ERR_ROOT, MPIR_ERR_ROOT_TOOBIG,
				    myname,(char *)0,(char *)0,root,size);
    if (root < 0) 
	mpi_errno = MPIR_Err_setmsg(MPI_ERR_ROOT,MPIR_ERR_DEFAULT,myname,
				    (char *)0,(char *)0,root);
    if (mpi_errno)
	return MPIR_ERROR(comm, mpi_errno, myname );
#endif
 
  /* Switch communicators to the hidden collective */
  comm = comm->comm_coll;

  /* Lock for collective operation */
  MPID_THREAD_LOCK(comm->ADIctx,comm);

  /* If I'm the root, send messages to the rest of 'em */
  if ( rank == root ) {

    /* Get the size of the send type */
    MPI_Type_extent ( sendtype->self, &extent );

    for ( i=0; i<root; i++ ) {
	mpi_errno = MPI_Send( (void *)((char *)sendbuf+i*sendcnt*extent), 
			     sendcnt, sendtype->self, i, MPIR_SCATTER_TAG, comm->self);
	if (mpi_errno) return mpi_errno;
	}

    mpi_errno = MPI_Sendrecv ( (void *)((char *)sendbuf+rank*sendcnt*extent),
			      sendcnt,sendtype->self, rank, MPIR_SCATTER_TAG,
			       recvbuf, recvcnt, recvtype->self, rank, 
			       MPIR_SCATTER_TAG, 
			      comm->self, &status );
    if (mpi_errno) return mpi_errno;

    for ( i=root+1; i<size; i++ ) {
	mpi_errno = MPI_Send( (void *)((char *)sendbuf+i*sendcnt*extent), 
			      sendcnt, sendtype->self, i, 
			      MPIR_SCATTER_TAG, comm->self);
	if (mpi_errno) return mpi_errno;
	}
  }
  else 
      mpi_errno = MPI_Recv(recvbuf,recvcnt,recvtype->self,root,
			   MPIR_SCATTER_TAG,comm->self,&status);
  
  /* Unlock for collective operation */
  MPID_THREAD_UNLOCK(comm->ADIctx,comm);
  
  return (mpi_errno);
}

static int intra_Scatterv ( 
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
  MPI_Status status;
  int        rank, size;
  int        mpi_errno = MPI_SUCCESS;
  static char myname[] = "MPI_SCATTERV";

  /* Get size and rank */
  MPIR_Comm_size ( comm, &size );
  MPIR_Comm_rank ( comm, &rank );

  /* Check for invalid arguments */
#ifndef MPIR_NO_ERROR_CHECKING
    if ( root >= size )
	mpi_errno = MPIR_Err_setmsg(MPI_ERR_ROOT, MPIR_ERR_ROOT_TOOBIG,
				    myname,(char *)0,(char *)0,root,size);
    if (root < 0) 
	mpi_errno = MPIR_Err_setmsg(MPI_ERR_ROOT,MPIR_ERR_DEFAULT,myname,
				    (char *)0,(char *)0,root);
    if (mpi_errno)
	return MPIR_ERROR(comm, mpi_errno, myname );
#endif

  /* Switch communicators to the hidden collective */
  comm = comm->comm_coll;

  /* Lock for collective operation */
  MPID_THREAD_LOCK(comm->ADIctx,comm);

  /* If I'm the root, then scatter */
  if ( rank == root ) {
    MPI_Aint extent;
    int      i;

    MPI_Type_extent(sendtype->self, &extent);
    /* We could use Isend here, but since the receivers need to execute
       a simple Recv, it may not make much difference in performance, 
       and using the blocking version is simpler */
    for ( i=0; i<root; i++ ) {
      mpi_errno = MPI_Send( (void *)((char *)sendbuf+displs[i]*extent), 
			   sendcnts[i], sendtype->self, i, MPIR_SCATTERV_TAG, comm->self);
      if (mpi_errno) return mpi_errno;
      }
    mpi_errno = MPI_Sendrecv((void *)((char *)sendbuf+displs[rank]*extent), 
		 sendcnts[rank], 
                 sendtype->self, rank, MPIR_SCATTERV_TAG, 
			     recvbuf, recvcnt, recvtype->self, 
                 rank, MPIR_SCATTERV_TAG, comm->self, &status);
    if (mpi_errno) return mpi_errno;

    for ( i=root+1; i<size; i++ ) {
      mpi_errno = MPI_Send( (void *)((char *)sendbuf+displs[i]*extent), 
			   sendcnts[i], sendtype->self, i, 
			    MPIR_SCATTERV_TAG, comm->self);
      if (mpi_errno) return mpi_errno;
      }
  }
  else
      mpi_errno = MPI_Recv(recvbuf,recvcnt,recvtype->self,root,
			   MPIR_SCATTERV_TAG,comm->self,&status);

  /* Unlock for collective operation */
  MPID_THREAD_UNLOCK(comm->ADIctx,comm);

  return (mpi_errno);
}

/* 
   General comments on Allxxx operations
   
   It is hard (though not impossible) to avoid having each at least one process
   doing a send to every other process.  In that case, the order of the
   operations becomes important.
   For example, in the alltoall case, you do NOT want all processes to send 
   to process 1, then all to send to process 2, etc.  In addition, you
   don't want the messages to compete for bandwidth in the network (remember,
   most networks don't provide INDEPENDENT paths between every pair of nodes).
   In that case, the topology of the underlying network becomes important.
   This can further control the choice of ordering for the sends/receives.
   Unfortunately, there is no interface to find this information (one was
   considered by the MPI-1 Forum but not adopted).  Vendor-specific 
   implementations of these routines can take advantage of such information.
 */

static int intra_Allgather ( 
	void *sendbuf, 
	int sendcount, 
	struct MPIR_DATATYPE *sendtype,
	void *recvbuf, 
	int recvcount, 
	struct MPIR_DATATYPE *recvtype, 
	struct MPIR_COMMUNICATOR *comm )
{
  int        size, rank;
  int        mpi_errno = MPI_SUCCESS;
  MPI_Status status;
  MPI_Aint   recv_extent;
  int        j, jnext, i, right, left;

  /* Get the size of the communicator */
  MPIR_Comm_size ( comm, &size );
  MPIR_Comm_rank ( comm, &rank );

  /* Do a gather for each process in the communicator
     This is the "circular" algorithm for allgather - each process sends to
     its right and receives from its left.  This is faster than simply
     doing size Gathers.
   */

  MPI_Type_extent ( recvtype->self, &recv_extent );

  /* Switch communicators to the hidden collective */
  comm = comm->comm_coll;
 
  /* First, load the "local" version in the recvbuf. */
  mpi_errno = 
      MPI_Sendrecv( sendbuf, sendcount, sendtype->self, rank, 
		    MPIR_ALLGATHER_TAG,
                    (void *)((char *)recvbuf + rank*recvcount*recv_extent),
		    recvcount, recvtype->self, rank, MPIR_ALLGATHER_TAG, 
		    comm->self,
		    &status );
  if (mpi_errno) return mpi_errno;

  /* 
     Now, send left to right.  This fills in the receive area in 
     reverse order.
   */
  left  = (size + rank - 1) % size;
  right = (rank + 1) % size;
  
  j     = rank;
  jnext = left;
  for (i=1; i<size; i++) {
      mpi_errno = 
	  MPI_Sendrecv( (void *)((char *)recvbuf+j*recvcount*recv_extent),
		    recvcount, recvtype->self, right, MPIR_ALLGATHER_TAG,
                    (void *)((char *)recvbuf + jnext*recvcount*recv_extent),
		    recvcount, recvtype->self, left, 
			MPIR_ALLGATHER_TAG, comm->self,
		    &status );
      if (mpi_errno) break;
      j	    = jnext;
      jnext = (size + jnext - 1) % size;
      }
  return (mpi_errno);
}


static int intra_Allgatherv ( 
	void *sendbuf, 
	int sendcount,  
	struct MPIR_DATATYPE *sendtype, 
	void *recvbuf, 
	int *recvcounts, 
	int *displs,   
	struct MPIR_DATATYPE *recvtype, 
	struct MPIR_COMMUNICATOR *comm )
{
  int        size, rank;
  int        mpi_errno = MPI_SUCCESS;
  MPI_Status status;
  MPI_Aint   recv_extent;
  int        j, jnext, i, right, left;

  /* Get the size of the communicator */
  MPIR_Comm_size ( comm, &size );
  MPIR_Comm_rank ( comm, &rank );

  /* Switch communicators to the hidden collective */
  comm = comm->comm_coll;

  /* Do a gather for each process in the communicator
     This is the "circular" algorithm for allgatherv - each process sends to
     its right and receives from its left.  This is faster than simply
     doing size Gathervs.
   */

  MPI_Type_extent ( recvtype->self, &recv_extent );

  /* First, load the "local" version in the recvbuf. */
  mpi_errno = 
      MPI_Sendrecv( sendbuf, sendcount, sendtype->self, rank, 
		    MPIR_ALLGATHERV_TAG,
                    (void *)((char *)recvbuf + displs[rank]*recv_extent),
		    recvcounts[rank], recvtype->self, rank, 
		    MPIR_ALLGATHERV_TAG, 
		    comm->self, &status );
  if (mpi_errno) {
      return mpi_errno;
  }

  left  = (size + rank - 1) % size;
  right = (rank + 1) % size;
  
  j     = rank;
  jnext = left;
  for (i=1; i<size; i++) {
      mpi_errno = 
	  MPI_Sendrecv( (void *)((char *)recvbuf+displs[j]*recv_extent),
		    recvcounts[j], recvtype->self, right, MPIR_ALLGATHERV_TAG,
                    (void *)((char *)recvbuf + displs[jnext]*recv_extent),
		    recvcounts[jnext], recvtype->self, left, 
		       MPIR_ALLGATHERV_TAG, comm->self,
		    &status );
      if (mpi_errno) break;
      j	    = jnext;
      jnext = (size + jnext - 1) % size;
      }
  return (mpi_errno);
}

static int intra_Alltoall( 
	void *sendbuf, 
	int sendcount, 
	struct MPIR_DATATYPE *sendtype, 
	void *recvbuf, 
	int recvcnt, 
	struct MPIR_DATATYPE *recvtype, 
	struct MPIR_COMMUNICATOR *comm )
{
  int          size, i, j, rank, dest;
  MPI_Aint     send_extent, recv_extent;
  int          mpi_errno = MPI_SUCCESS;
  MPI_Status  *starray;
  MPI_Request *reqarray;
  static char myname[] = "MPI_ALLTOALL";

  /* Get size and switch to collective communicator */
  MPIR_Comm_size ( comm, &size );
  MPIR_Comm_rank ( comm, &rank );
  comm = comm->comm_coll;
  
  /* Get extent of send and recv types */
  MPI_Type_extent ( sendtype->self, &send_extent );
  MPI_Type_extent ( recvtype->self, &recv_extent );

  /* Lock for collective operation */
  MPID_THREAD_LOCK(comm->ADIctx, comm);

/* 
 */
  /* 1st, get some storage from the heap to hold handles, etc. */
  MPIR_ALLOC(starray,(MPI_Status *)MALLOC(2*size*sizeof(MPI_Status)),
	     comm, MPI_ERR_EXHAUSTED, myname );

  MPIR_ALLOC(reqarray, (MPI_Request *)MALLOC(2*size*sizeof(MPI_Request)),
	     comm, MPI_ERR_EXHAUSTED, myname );

  /* do the communication -- post *all* sends and receives: */
  for ( i=0; i<size; i++ ) { 
      /* Performance fix sent in by Duncan Grove <duncan@cs.adelaide.edu.au>.
         Instead of posting irecvs and isends from rank=0 to size-1, scatter
         the destinations so that messages don't all go to rank 0 first. 
         Thanks Duncan! */
      dest = (rank+i) % size;

      if ( (mpi_errno=MPI_Irecv(
	  (void *)((char *)recvbuf + dest*recvcnt*recv_extent),
                           recvcnt,
                           recvtype->self,
                           dest,
                           MPIR_ALLTOALL_TAG,
                           comm->self,
                           &reqarray[i]))
          )
          return mpi_errno;
  }

  for ( i=0; i<size; i++ ) { 
      dest = (rank+i) % size;
      if ((mpi_errno=MPI_Isend(
	  (void *)((char *)sendbuf + dest*sendcount*send_extent),
                           sendcount,
                           sendtype->self,
                           dest,
                           MPIR_ALLTOALL_TAG,
                           comm->self,
                           &reqarray[i+size]))
          )
          return mpi_errno;
  }
  
  /* ... then wait for *all* of them to finish: */
  mpi_errno = MPI_Waitall(2*size,reqarray,starray);
  if (mpi_errno == MPI_ERR_IN_STATUS) {
      for (j=0; j<2*size; j++) {
	  if (starray[j].MPI_ERROR != MPI_SUCCESS) 
	      mpi_errno = starray[j].MPI_ERROR;
      }
  }
  
  /* clean up */
  FREE(starray);
  FREE(reqarray);

  /* Unlock for collective operation */
  MPID_THREAD_UNLOCK(comm->ADIctx,comm);

  return (mpi_errno);
}

static int intra_Alltoallv ( 
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
  int        size, i, j, rcnt, rank, dest;
  MPI_Aint   send_extent, recv_extent;
  int        mpi_errno = MPI_SUCCESS;
  MPI_Status  *starray;
  MPI_Request *reqarray;
  
  /* Get size and switch to collective communicator */
  MPIR_Comm_size ( comm, &size );
  MPIR_Comm_rank ( comm, &rank );
  comm = comm->comm_coll;

  /* Get extent of send and recv types */
  MPI_Type_extent(sendtype->self, &send_extent);
  MPI_Type_extent(recvtype->self, &recv_extent);

  /* Lock for collective operation */
  MPID_THREAD_LOCK(comm->ADIctx,comm);

  /* 1st, get some storage from the heap to hold handles, etc. */
  MPIR_ALLOC(starray,(MPI_Status *)MALLOC(2*size*sizeof(MPI_Status)),
	     comm, MPI_ERR_EXHAUSTED, "MPI_ALLTOALLV" );

  MPIR_ALLOC(reqarray,(MPI_Request *)MALLOC(2*size*sizeof(MPI_Request)),
	     comm, MPI_ERR_EXHAUSTED, "MPI_ALLTOALLV" );

  /* do the communication -- post *all* sends and receives: */
  rcnt = 0;
  for ( i=0; i<size; i++ ) { 
      /* Performance fix sent in by Duncan Grove <duncan@cs.adelaide.edu.au>.
         Instead of posting irecvs and isends from rank=0 to size-1, scatter
         the destinations so that messages don't all go to rank 0 first. 
         Thanks Duncan! */
      dest = (rank+i) % size;
      if (( mpi_errno=MPI_Irecv(
	                  (void *)((char *)recvbuf+rdispls[dest]*recv_extent), 
                           recvcnts[dest], 
                           recvtype->self,
                           dest,
                           MPIR_ALLTOALLV_TAG,
                           comm->self,
                           &reqarray[i]))
          )
          break;
      rcnt++;
  }

  if (!mpi_errno) {
      for ( i=0; i<size; i++ ) { 
          dest = (rank+i) % size;
          if (( mpi_errno=MPI_Isend(
                     (void *)((char *)sendbuf+sdispls[dest]*send_extent), 
                     sendcnts[dest], 
                     sendtype->self,
                     dest,
                     MPIR_ALLTOALLV_TAG,
                     comm->self,
                     &reqarray[i+size]))
           )
              break;
          rcnt++;
      }
  }
  
  /* ... then wait for *all* of them to finish: */
  if (mpi_errno) {
      /* We should really cancel all of the active requests */
      for (j=0; j<rcnt; j++) {
	  MPI_Cancel( &reqarray[j] );
      }
  }
  else {
      mpi_errno = MPI_Waitall(2*size,reqarray,starray);
      if (mpi_errno == MPI_ERR_IN_STATUS) {
	  for (j=0; j<2*size; j++) {
	      if (starray[j].MPI_ERROR != MPI_SUCCESS) 
		  mpi_errno = starray[j].MPI_ERROR;
	  }
      }
  }
  
  /* clean up */
  FREE(reqarray);
  FREE(starray);

  /* Unlock for collective operation */
  MPID_THREAD_UNLOCK(comm->ADIctx,comm);

  return (mpi_errno);
}

static int intra_Reduce ( 
	void *sendbuf, 
	void *recvbuf, 
	int count, 
	struct MPIR_DATATYPE *datatype, 
	MPI_Op op, 
	int root, 
	struct MPIR_COMMUNICATOR *comm )
{
  MPI_Status status;
  int        size, rank;
  int        mask, relrank, source, lroot;
  int        mpi_errno = MPI_SUCCESS;
  MPI_User_function *uop;
  MPI_Aint   lb, ub, m_extent;  /* Extent in memory */
  void       *buffer;
  struct MPIR_OP *op_ptr;
  static char myname[] = "MPI_REDUCE";

  /* Is root within the communicator? */
  MPIR_Comm_size ( comm, &size );
#ifndef MPIR_NO_ERROR_CHECKING
    if ( root >= size )
	mpi_errno = MPIR_Err_setmsg(MPI_ERR_ROOT, MPIR_ERR_ROOT_TOOBIG,
				    myname,(char *)0,(char *)0,root,size);
    if (root < 0) 
	mpi_errno = MPIR_Err_setmsg(MPI_ERR_ROOT,MPIR_ERR_DEFAULT,myname,
				    (char *)0,(char *)0,root);
    if (mpi_errno)
	return MPIR_ERROR(comm, mpi_errno, myname );
#endif

  /* See the overview in Collection Operations for why this is ok */
  if (count == 0) return MPI_SUCCESS;

  /* If the operation is predefined, we could check that the datatype's
     type signature is compatible with the operation.  
   */
#if defined(MPID_Reduce)  &&  !defined(TOPOLOGY_INTRA_FNS_H)
  /* Eventually, this could apply the MPID_Reduce routine in a loop for
     counts > 1 */
  if (comm->ADIReduce && count == 1) {
      /* Call a routine to sort through the datatypes and operations ...
	 This allows us to provide partial support (e.g., only SUM_DOUBLE)
       */
      if (MPIR_ADIReduce( comm->ADIctx, comm, sendbuf, recvbuf, count, 
                      datatype->self, op, root ) == MPI_SUCCESS)
	  return MPI_SUCCESS;
      }
#endif

  /* Get my rank and switch communicators to the hidden collective */
  MPIR_Comm_rank ( comm, &rank );
  comm = comm->comm_coll;
  op_ptr = MPIR_GET_OP_PTR(op);
  MPIR_TEST_MPI_OP(op,op_ptr,comm,myname);
  uop  = op_ptr->op;


  /* Here's the algorithm.  Relative to the root, look at the bit pattern in 
     my rank.  Starting from the right (lsb), if the bit is 1, send to 
     the node with that bit zero and exit; if the bit is 0, receive from the
     node with that bit set and combine (as long as that node is within the
     group)

     Note that by receiving with source selection, we guarentee that we get
     the same bits with the same input.  If we allowed the parent to receive 
     the children in any order, then timing differences could cause different
     results (roundoff error, over/underflows in some cases, etc).

     Because of the way these are ordered, if root is 0, then this is correct
     for both commutative and non-commutitive operations.  If root is not
     0, then for non-commutitive, we use a root of zero and then send
     the result to the root.  To see this, note that the ordering is
     mask = 1: (ab)(cd)(ef)(gh)            (odds send to evens)
     mask = 2: ((ab)(cd))((ef)(gh))        (3,6 send to 0,4)
     mask = 4: (((ab)(cd))((ef)(gh)))      (4 sends to 0)

     Comments on buffering.  
     If the datatype is not contiguous, we still need to pass contiguous 
     data to the user routine.  
     In this case, we should make a copy of the data in some format, 
     and send/operate on that.

     In general, we can't use MPI_PACK, because the alignment of that
     is rather vague, and the data may not be re-usable.  What we actually
     need is a "squeeze" operation that removes the skips.
   */
  /* Make a temporary buffer */
  MPIR_Type_get_limits( datatype, &lb, &ub );
  m_extent = ub - lb;
  /* MPI_Type_extent ( datatype, &extent ); */
  MPIR_ALLOC(buffer,(void *)MALLOC(m_extent * count),comm, MPI_ERR_EXHAUSTED, 
	     "MPI_REDUCE" );
  buffer = (void *)((char*)buffer - lb);

  /* If I'm not the root, then my recvbuf may not be valid, therefore
     I have to allocate a temporary one */
  if (rank != root) {
      MPIR_ALLOC(recvbuf,(void *)MALLOC(m_extent * count),
		 comm, MPI_ERR_EXHAUSTED, "MPI_REDUCE" );
      recvbuf = (void *)((char*)recvbuf - lb);
  }

  /* This code isn't correct if the source is a more complex datatype */
  memcpy( recvbuf, sendbuf, m_extent*count );
  mask    = 0x1;
  if (op_ptr->commute) lroot   = root;
  else                 lroot   = 0;
  relrank = (rank - lroot + size) % size;

  /* Lock for collective operation */
  MPID_THREAD_LOCK(comm->ADIctx,comm);
  
  MPIR_Op_errno = MPI_SUCCESS;
  while (/*(mask & relrank) == 0 && */mask < size) {
	/* Receive */
	if ((mask & relrank) == 0) {
	    source = (relrank | mask);
	    if (source < size) {
		source = (source + lroot) % size;
		mpi_errno = MPI_Recv (buffer, count, datatype->self, source, 
				      MPIR_REDUCE_TAG, comm->self, &status);
		if (mpi_errno) return MPIR_ERROR( comm, mpi_errno, myname ); 
		/* The sender is above us, so the received buffer must be
		   the second argument (in the noncommutitive case). */
		/* error pop/push allows errors found by predefined routines
		   to be visible.  We need a better way to do this */
		/* MPIR_ERROR_POP(comm); */
		if (op_ptr->commute)
		    (*uop)(buffer, recvbuf, &count, &datatype->self);
		else {
		    (*uop)(recvbuf, buffer, &count, &datatype->self);
		    /* short term hack to keep recvbuf up-to-date */
		    memcpy( recvbuf, buffer, m_extent*count );
		    }
		/* MPIR_ERROR_PUSH(comm); */
		}
	    }
	else {
	    /* I've received all that I'm going to.  Send my result to 
	       my parent */
	    source = ((relrank & (~ mask)) + lroot) % size;
	    mpi_errno  = MPI_Send( recvbuf, count, datatype->self, 
				  source, 
				  MPIR_REDUCE_TAG, 
				  comm->self );
	    if (mpi_errno) return MPIR_ERROR( comm, mpi_errno, myname );
	    break;
	    }
	mask <<= 1;
	}
  FREE( (char *)buffer + lb );
  if (!op_ptr->commute && root != 0) {
      if (rank == 0) {
	  mpi_errno  = MPI_Send( recvbuf, count, datatype->self, root, 
				MPIR_REDUCE_TAG, comm->self );
	  }
      else if (rank == root) {
	  mpi_errno = MPI_Recv ( recvbuf, count, datatype->self, 0, /*size-1, */
				MPIR_REDUCE_TAG, comm->self, &status);
	  }
      }

  /* Free the temporarily allocated recvbuf */
  if (rank != root)
    FREE( (char *)recvbuf + lb );

  /* If the predefined operation detected an error, report it here */
  /* Note that only the root gets this result, so this can cause
     programs to hang, particularly if this is used to implement 
     MPI_Allreduce.  Use care with this.
   */
  if (mpi_errno == MPI_SUCCESS && MPIR_Op_errno) {
      /* PRINTF( "Error in performing MPI_Op in reduce\n" ); */
      mpi_errno = MPIR_Op_errno;
  }

  /* Unlock for collective operation */
  MPID_THREAD_UNLOCK(comm->ADIctx,comm);
  
  return (mpi_errno);
}

/* 
 * There are alternatives to this algorithm, particular one in which
 * the values are computed on all processors at the same time.  
 * However, this routine should be used on heterogeneous systems, since
 * the "same" value is required on all processors, and small changes
 * in floating-point arithmetic (including choice of round-off mode and
 * the infamous fused multiply-add) can lead to different results.
 */
static int intra_Allreduce ( 
	void *sendbuf, 
	void *recvbuf, 
	int count, 
	struct MPIR_DATATYPE *datatype, 
	MPI_Op op, 
	struct MPIR_COMMUNICATOR *comm )
{
  int mpi_errno, rc;

  if (count == 0) return MPI_SUCCESS;

  /* Reduce to 0, then bcast */
  mpi_errno = MPI_Reduce ( sendbuf, recvbuf, count, datatype->self, op, 0, 
			   comm->self );
  if (mpi_errno == MPIR_ERR_OP_NOT_DEFINED || mpi_errno == MPI_SUCCESS) {
      rc = MPI_Bcast  ( recvbuf, count, datatype->self, 0, comm->self );
      if (rc) mpi_errno = rc;
  }

  return (mpi_errno);
}

static int intra_Reduce_scatter ( 
	void *sendbuf, 
	void *recvbuf, 
	int *recvcnts, 
	struct MPIR_DATATYPE *datatype, 
	MPI_Op op, 
	struct MPIR_COMMUNICATOR *comm )
{
  int   rank, size, i, count=0;
  MPI_Aint   lb, ub, m_extent;  /* Extent in memory */
  int  *displs;
  void *buffer;
  int   mpi_errno = MPI_SUCCESS, rc;
  static char myname[] = "MPI_REDUCE_SCATTER";

  /* Determine the "count" of items to reduce and set the displacements*/
  MPIR_Type_get_limits( datatype, &lb, &ub );
  m_extent = ub - lb;
  /* MPI_Type_extent (datatype, &extent); */
  MPIR_Comm_size   (comm, &size);
  MPIR_Comm_rank   (comm, &rank);

  /* Allocate the displacements and initialize them */
  MPIR_ALLOC(displs,(int *)MALLOC(size*sizeof(int)),comm, MPI_ERR_EXHAUSTED, 
			 myname);
  for (i=0;i<size;i++) {
    displs[i] = count;
    count += recvcnts[i];
    if (recvcnts[i] < 0) {
	FREE( displs );
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_COUNT, MPIR_ERR_COUNT_ARRAY_NEG,
				     myname, (char *)0, (char *)0,
				     i, recvcnts[i] );
	return mpi_errno;
    }
  }

  /* Allocate a temporary buffer */
  if (count == 0) {
      FREE( displs );
      return MPI_SUCCESS;
      }

  MPIR_ALLOC(buffer,(void *)MALLOC(m_extent*count), comm, MPI_ERR_EXHAUSTED, 
			 myname);
  buffer = (void *)((char*)buffer - lb);

  /* Reduce to 0, then scatter */
  mpi_errno = MPI_Reduce   ( sendbuf, buffer, count, datatype->self, op, 0, 
			     comm->self);
  if (mpi_errno == MPI_SUCCESS || mpi_errno == MPIR_ERR_OP_NOT_DEFINED) {
      rc = MPI_Scatterv ( buffer, recvcnts, displs, datatype->self,
			  recvbuf, recvcnts[rank], datatype->self, 0,
			  comm->self );
      if (rc) mpi_errno = rc;
  }
  /* Free the temporary buffers */
  FREE((char *)buffer+lb); FREE(displs);
  return (mpi_errno);
}

#ifdef MPIR_USE_BASIC_COLL
static int intra_Scan ( 
	void *sendbuf, 
	void *recvbuf, 
	int count, 
	struct MPIR_DATATYPE *datatype, 
	MPI_Op op, 
	struct MPIR_COMMUNICATOR *comm )
{
  MPI_Status status;
  int        rank, size;
  int        mpi_errno = MPI_SUCCESS;
  MPI_Aint   lb, ub, m_extent;  /* Extent in memory */
  MPI_User_function   *uop;
  struct MPIR_OP *op_ptr;
  MPIR_ERROR_DECL;
  mpi_comm_err_ret = 0;

  /* See the overview in Collection Operations for why this is ok */
  if (count == 0) return MPI_SUCCESS;

  /* Get my rank & size and switch communicators to the hidden collective */
  MPIR_Comm_size ( comm, &size );
  MPIR_Comm_rank ( comm, &rank );
  MPIR_Type_get_limits( datatype, &lb, &ub );
  m_extent = ub - lb;
  comm	   = comm->comm_coll;
  op_ptr = MPIR_GET_OP_PTR(op);
  MPIR_TEST_MPI_OP(op,op_ptr,comm,"MPI_SCAN");
  uop	   = op_ptr->op;

  /* Lock for collective operation */
  MPID_THREAD_LOCK(comm->ADIctx,comm);

  /* 
     This is an O(size) algorithm.  A modification of the algorithm in 
     reduce.c can be used to make this O(log(size)) 
   */
  /* commutative case requires no extra buffering */
  MPIR_Op_errno = MPI_SUCCESS;
  if (op_ptr->commute) {
      /* Do the scan operation */
      if (rank > 0) {
          mpi_errno = MPI_Recv(recvbuf,count,datatype->self,rank-1,
			       MPIR_SCAN_TAG,comm->self,&status);
	  if (mpi_errno) return mpi_errno;
	  /* See reduce for why pop/push */
	  MPIR_ERROR_POP(comm);
          (*uop)(sendbuf, recvbuf, &count, &datatype->self); 
	  MPIR_ERROR_PUSH(comm);
      }
      else {
	  MPIR_COPYSELF( sendbuf, count, datatype->self, recvbuf, 
			 MPIR_SCAN_TAG, rank, comm->self );
	  if (mpi_errno) return mpi_errno;
      }
  }
  /* non-commutative case requires extra buffering */
  else {
      /* Do the scan operation */
      if (rank > 0) {
          void *tmpbuf;
          MPIR_ALLOC(tmpbuf,(void *)MALLOC(m_extent * count),
		     comm, MPI_ERR_EXHAUSTED, "MPI_SCAN" );
	  tmpbuf = (void *)((char*)tmpbuf-lb);
	  MPIR_COPYSELF( sendbuf, count, datatype->self, recvbuf, 
			 MPIR_SCAN_TAG, rank, comm->self );
	  if (mpi_errno) return mpi_errno;
          mpi_errno = MPI_Recv(tmpbuf,count,datatype->self,rank-1,
			       MPIR_SCAN_TAG,comm->self,&status);
	  if (mpi_errno) return mpi_errno;
          (*uop)(tmpbuf, recvbuf, &count, &datatype->self); 
          FREE((char*)tmpbuf+lb);
      }
      else {
	  MPIR_COPYSELF( sendbuf, count, datatype->self, recvbuf, 
			 MPIR_SCAN_TAG, rank, comm->self );
	  if (mpi_errno) return mpi_errno;
	  }
  }

  /* send the letter to destination */
  if (rank < (size-1)) 
      mpi_errno = MPI_Send(recvbuf,count,datatype->self,rank+1,MPIR_SCAN_TAG,
			   comm->self);

  /* If the predefined operation detected an error, report it here */
  if (mpi_errno == MPI_SUCCESS && MPIR_Op_errno)
      mpi_errno = MPIR_Op_errno;

  /* Unlock for collective operation */
  MPID_THREAD_UNLOCK(comm->ADIctx,comm);

  return(mpi_errno);
}
#endif
