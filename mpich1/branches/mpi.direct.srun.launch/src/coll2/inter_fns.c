/*
 *  $Id: inter_fns.c,v 1.6 2001/10/19 22:01:19 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

/* ---------------------------------
 * Modifications by Rajkumar Venkat
 */

#include "mpimem.h" 		/* Temporary buffer allocation */
#include "../coll/coll.h"
#include "mpipt2pt.h"		/* MPIR_Type_get_limits */
#include "mpiops.h"

/* End of Modifications
 * ---------------------------------
 */

#define MPI_ERR_COMM_INTER MPIR_ERRCLASS_TO_CODE(MPI_ERR_COMM,MPIR_ERR_COMM_INTER)

/* ---------------------------------
 * Additions by Rajkumar Venkat
 */

/* Define MPI-2 attribute functions as their MPI-1 counterparts */
# define MPI_Comm_get_attr 		MPI_Attr_get
# define MPI_Comm_set_attr 		MPI_Attr_put
# define MPI_Comm_create_keyval		MPI_Keyval_create

/* This is the current assumption made
 * regarding the rank of the root/leader of
 * each of the communicators in the InterComm
 */

# define DESIGNATED_LOCAL_ROOT			0
# define DESIGNATED_REMOTE_ROOT			0

/* If defined, invokes default method, if removed invokes alternative */
# define NO_ALTERNATIVE				1

/* These definitions need to be moved into the appropriate header file 
 * (src/coll/coll.h?)
 */
# define MPI_INTER_BARRIER_DATA_TAG		25
# define MPI_INTER_BCAST_PT_TAG 		26
# define MPI_INTER_GATHER_PT_TAG 		27
# define MPI_INTER_GATHERV_DATA_TAG 		28
# define MPI_INTER_SCATTER_PT_TAG 		29
# define MPI_INTER_SCATTERV_DATA_TAG 		30
# define MPI_INTER_ALLGATHER_PT_TAG 		31
# define MPI_INTER_ALLTOALLW_DATA_TAG 		32
# define MPI_INTER_REDUCE_PT_TAG		33
# define MPI_INTER_ALLREDUCE_PT_TAG		34
# define MPI_INTER_REDUCESCATTER_DATA_TAG	35

/* Used for error message construction. Definition to be moved later */
# define MPI_ERR_REMOTE				29
# define MPIR_ERR_REMOTE_ZERO			30

/* End of Additions
 * ---------------------------------
 */


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

/* ---------------------------------
 * Addition by Rajkumar Venkat
 */
static int inter_Alltoallw (void *, int *, int *, struct MPIR_DATATYPE *, 
					void *, int *, int *, struct MPIR_DATATYPE *,
						struct MPIR_COMMUNICATOR *);
/* End of Addition
 * ---------------------------------
 */

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

/* ---------------------------------
 * Modifications by Rajkumar Venkat
 */

    inter_Alltoallw,	/* Newly added to the collops structure */

/* End of Modifications
 * ---------------------------------
 */

    inter_Reduce,
    inter_Allreduce,
    inter_Reduce_scatter,
    inter_Scan,

    1                              /* Giving it a refcount of 1 ensures it
				    * won't ever get freed.
				    */
};

MPIR_COLLOPS MPIR_inter_collops = &inter_collops;

/* ---------------------------------
 * Modifications by Rajkumar Venkat
 */

/* Global variable for cached attributes */
int key_comm_local = MPI_KEYVAL_INVALID;

/* Internal utility function */

/* Attribute caching/retrieval...
 *   The only attribute involved is the communicator object
 *   corresponding to the local group of which the process
 *   is a member.
 * A separate function that all inter_* methods can invoke
 */
void inter_Attr_prepare(struct MPIR_COMMUNICATOR *comm_coll,
			MPI_Comm *comm_local)	/* 'out' parameter */
{
	int flag = 0;	/* false initially */

	struct MPIR_COMMUNICATOR *comm_local_ptr;
	
	/* 'comm_coll' is assumed to represent a valid communicator
	 * So, no checking.
	 */

	if(key_comm_local != MPI_KEYVAL_INVALID)
	{
		/* Get the cached attribute */
		MPI_Comm_get_attr(comm_coll->self, key_comm_local, comm_local_ptr, &flag);
	}
	else
	{
		/* The keyval needs to be created */
	
		/* Create a keyval for holding the local Comm */
		MPI_Comm_create_keyval(MPI_COMM_NULL_COPY_FN,
					MPI_COMM_NULL_DELETE_FN,
					&key_comm_local,
				 	(void *) NULL);		/* extra_state is not used */
	}

	if(!flag)
	{
		/* At this point, the keyval exists, but there is no
		 * object (i.e. local communicator) associated with it.
		 * So, create a communicator for Local Group
		 */
		MPI_Comm_create(comm_coll->self, comm_coll->local_group->self, comm_local);

		/* Note that we could also cache the MPI_Comm value comm_local directly */
		comm_local_ptr = MPIR_GET_COMM_PTR(*comm_local);

		/* Cache the newly created local Comm as attribute of the InterComm */
		MPI_Comm_set_attr(comm_coll->self, key_comm_local, (void *)comm_local_ptr);
	}
	
	*comm_local = comm_local_ptr->self;

	/* The attribute is ready to be used at this stage */
}

/* End of Modifications
 * ---------------------------------
 */


/* Now the functions */

static int inter_Barrier( struct MPIR_COMMUNICATOR * comm )
{

/* ---------------------------------
 * Modifications by Rajkumar Venkat
 */

	/* The following is just a temporary implementation
	 * TBD: When all processes in the remote group have
	 * entered the barrier, I can leave the barrier.
	 * How do I implement this?
	 */

	/* Communicator Attributes */
	MPI_Comm comm_local, comm_intra;
	int my_rank, dest_rank, remote_root = DESIGNATED_REMOTE_ROOT;
	int comm_size, remote_size, mpi_errno = MPI_SUCCESS;
	static char myname[] = "MPI_BARRIER";

	/* Temporary variables */
	MPI_Status status;
	MPI_Group intra_group;

	/* Switch to hidden collective */
	struct MPIR_COMMUNICATOR *comm_coll = comm->comm_coll;	

	remote_size = comm_coll->np;

	/* Conditional checking */
#ifndef MPIR_NO_ERROR_CHECKING
	if(remote_size <= 0)
	{
		mpi_errno = MPIR_Err_setmsg(MPI_ERR_REMOTE, MPIR_ERR_REMOTE_ZERO,
                                   myname, (char *)0, (char *)0, (char *)0, remote_size);
		if (mpi_errno)
			return MPIR_ERROR(comm, mpi_errno, myname);
	}
#endif

	/* Get/create the cached local communicator */
	inter_Attr_prepare(comm_coll, &comm_local);
	MPI_Comm_size(comm_local, &comm_size);
	MPI_Comm_rank(comm_local, &my_rank);

	/* Get the underlying "safe" intraComm
	 * from the "safe" interComm. Also get its group.
	 */
	comm_intra = comm_coll->comm_coll->self;
	MPI_Comm_group(comm_intra, &intra_group);

	/* Lock for collective operation */
	MPID_THREAD_LOCK(comm_coll->ADIctx, comm_coll);

	/* The other way to do it is to call a collective
	 * operation like inter_Allreduce, that holds all
	 * the processes...
	 * This method does not make use of the
	 * "safe" intra-communicator
	 */

	/* The following MPI_Bcast should invoke
	 * the corresponding "intra" version
	 */

	if(my_rank == DESIGNATED_LOCAL_ROOT)
	{
		/* Send to AND get from the other side */
		MPI_Group_translate_ranks(comm_coll->group->self,
					   1, &remote_root,
					    intra_group, &dest_rank);

		mpi_errno = MPI_Sendrecv(&comm_size, 1, MPI_INT,
						dest_rank,
						MPI_INTER_BARRIER_DATA_TAG,
						&remote_size, 1, MPI_INT,
						dest_rank,
						MPI_INTER_BARRIER_DATA_TAG,
						comm_intra, &status);
	}
	
	if(comm_size > 1)
		mpi_errno = MPI_Bcast(&remote_size, 1, MPI_INT,
					 DESIGNATED_LOCAL_ROOT, comm_local);
	
	/* Unlock to end collective operation */
	MPID_THREAD_UNLOCK(comm_coll->ADIctx, comm_coll);

	if(mpi_errno)
		return MPIR_ERROR(comm, MPI_ERR_COMM_INTER,
		      			"MPI_BARRIER");
	else
		return mpi_errno;

/* End of Modifications
 * ---------------------------------
 */

}

static int inter_Bcast ( 
	void *buffer, 
	int count, 
	struct MPIR_DATATYPE *datatype, 
	int root, 
	struct MPIR_COMMUNICATOR *comm )
{

/* ---------------------------------
 * Modifications by Rajkumar Venkat
 */

	/* Communicator Attributes */
	MPI_Comm comm_local, comm_intra;
	int my_rank, dest_rank, remote_root = DESIGNATED_REMOTE_ROOT;
	int comm_size, remote_size, mpi_errno = MPI_SUCCESS;
	static char myname[] = "MPI_BCAST";

	/* Temporary variables */
	MPI_Status status;
	MPI_Group intra_group;

	/* Switch to hidden collective */
	struct MPIR_COMMUNICATOR *comm_coll = comm->comm_coll;	
	
	/* Conditional checking */
#ifndef MPIR_NO_ERROR_CHECKING
	if(remote_size <= 0)
	{
		mpi_errno = MPIR_Err_setmsg(MPI_ERR_REMOTE, MPIR_ERR_REMOTE_ZERO,
                                   myname, (char *)0, (char *)0, root, remote_size);
		if (mpi_errno)
			return MPIR_ERROR(comm, mpi_errno, myname);
	}
#endif

	/* Get/create the cached local communicator */
	inter_Attr_prepare(comm_coll, &comm_local);
	MPI_Comm_size(comm_local, &comm_size);
	MPI_Comm_rank(comm_local, &my_rank);

	/* Get the underlying "safe" intraComm
	 * from the "safe" interComm. Also get its group.
	 */
	comm_intra = comm_coll->comm_coll->self;
	MPI_Comm_group(comm_intra, &intra_group);

	/* Lock for collective operation */
	MPID_THREAD_LOCK(comm_coll->ADIctx, comm_coll);

	/* -- Algorithm for inter_Bcast --
	 * Check if the rank of the root...
	 *  - If it is positive, I am in the receiving group
	 *    . If I am the designated root, I receive the data
	 *      from the "root" on the other side (actually by
	 *      translating the rank of "root" to its rank in the
	 *      underlying intraComm) and participate in the broadcast
	 *      in my local group
         *    . Otherwise, simply participate in the broadcast
	 *  - If it is MPI_ROOT, then I am the root in the
	 *    sending group for this broadcast
	 *    . Send the data to the designated remote root (actually by
	 *      translating the rank of remote root to its rank in the
	 *      underlying intraComm)
	 *  - If it is MPI_PROC_NULL, then I am one of the
	 *    "uninvolved" processes of the sending group
	 *    (NOTE: I could also happen to be the designated root!)
	 *    . So, do nothing!
	 */
	if(root == MPI_ROOT)
	{
		/* I AM the source of this broadcast...
		 * So, send the data to the designated remote root
		 */
		MPI_Group_translate_ranks(comm_coll->group->self, 1, &remote_root,
						intra_group, &dest_rank);

		mpi_errno = MPI_Send(buffer, count, datatype->self, dest_rank,
					MPI_INTER_BCAST_PT_TAG, comm_intra);

	}
	else if(root != MPI_PROC_NULL)
	{
		/* Remote Group i.e. Receiving Group */
		if(my_rank == DESIGNATED_LOCAL_ROOT)
		{
			/* Receive the data from the actual root */
			MPI_Group_translate_ranks(comm_coll->group->self, 1, &root,
							intra_group, &dest_rank);

			mpi_errno = MPI_Recv(buffer, count, datatype->self, dest_rank,
					MPI_INTER_BCAST_PT_TAG, comm_intra, &status);

		}

		/* Now, do a broadcast within my local group
		 * Before that, check if this process is the only process
		 * in the group. If so, we can skip this Bcast
		 */

		/* Note that the following Bcast should invoke
		 * intra_Bcast() 
		 */
		if(comm_size > 1)
			mpi_errno = MPI_Bcast(buffer, count, datatype->self,
						 DESIGNATED_LOCAL_ROOT, comm_local);
	}

	/* Unlock to end collective operation */
	MPID_THREAD_UNLOCK(comm_coll->ADIctx, comm_coll);

	if (mpi_errno)
		return MPIR_ERROR(comm, mpi_errno, myname);
	else
		return mpi_errno;

	/* Note that Bcast can be done by calling MPI_Alltoallw */

/* End of Modifications
 * ---------------------------------
 */
	
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

/* ---------------------------------
 * Modifications by Rajkumar Venkat
 */

	/* Communicator Attributes */
	MPI_Comm comm_local, comm_intra;
	MPI_Group intra_group;
	int my_rank, dest_rank, remote_root = DESIGNATED_REMOTE_ROOT;
	int comm_size, remote_size, mpi_errno = MPI_SUCCESS;

	static char myname[] = "MPI_GATHER";
	
# ifndef NO_ALTERNATIVE

	/* Point to Point */
	int *recvcnts = (int *)0;
	int *displs = (int *)0;
	int i;

# else

	/* Temporary variables */
	MPI_Aint ub, lb, m_extent;	 /* Buffer Allocation */
	MPI_Status status;
	int buf_size;
	void *buffer;

# endif

	/* Switch to hidden collective */
	struct MPIR_COMMUNICATOR *comm_coll = comm->comm_coll;

	remote_size = comm_coll->np;

	/* Conditional checking */
#ifndef MPIR_NO_ERROR_CHECKING
	if(remote_size <= 0)
	{
		mpi_errno = MPIR_Err_setmsg(MPI_ERR_REMOTE, MPIR_ERR_REMOTE_ZERO,
                                   myname, (char *)0, (char *)0, root, remote_size);
		if (mpi_errno)
			return MPIR_ERROR(comm, mpi_errno, myname);
	}
#endif

	/* Get the cached local communicator */
	inter_Attr_prepare(comm_coll, &comm_local);
	MPI_Comm_size(comm_local, &comm_size);
	MPI_Comm_rank(comm_local, &my_rank);

	/* Get the underlying "safe" intraComm
	 * from the "safe" interComm. Also get its group.
	 */
	comm_intra = comm_coll->comm_coll->self;
	MPI_Comm_group(comm_intra, &intra_group);

# ifndef NO_ALTERNATIVE

	/* Point to point */

	/* Allocate the arrays - only at the root
	 * and only if we have something to receive
	 */

	if((root == MPI_ROOT) && (recvcount))
	{
		MPIR_ALLOC(recvcnts, (int *)MALLOC(sizeof(int) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED,
				"MPI_GATHER");
		MPIR_ALLOC(displs, (int *)MALLOC(sizeof(int) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED,
				"MPI_GATHER");

		for(i = 0; i < remote_size; i++)
		{	
			recvcnts[i] = recvcount;
			displs[i] = i;
		}
	}

	/* I'll simply call inter_Gatherv */
	mpi_errno = inter_Gatherv(sendbuf, sendcnt, sendtype,
					recvbuf, recvcnts, displs,
					recvtype, root, comm);

	/* Free the arrays */
	if((root == MPI_ROOT) && (recvcount))
	{
		FREE(recvcnts);
		FREE(displs);
	}

# else

	if(root >= 0)
	{
		if((my_rank == DESIGNATED_LOCAL_ROOT) && (comm_size > 1))
		{
			/* I'm the remote root and the root for
			 * the intra_Gather... 
			 * Allocate a temporary extra buffer
			 */

			/* First, calculate the buffer size */
			MPIR_Type_get_limits(sendtype, &lb, &ub);
  			m_extent = ub - lb;
			
			buf_size = m_extent * comm_size;

			MPIR_ALLOC(buffer, (void *)MALLOC(buf_size),
					comm_coll, MPI_ERR_EXHAUSTED,
           				"MPI_GATHER");

			buffer = (void *)((char *)buffer - lb);
		}
		else
		{
			/* Buffer not used */
			buffer = sendbuf;
		}
	}

	/* Lock for collective operation */ 
	MPID_THREAD_LOCK(comm_coll->ADIctx, comm_coll);
                
	/* -- Algorithm for inter_Gather --
	 * - Perform MPI_Gather (intra_Gather) on the "remote"
	 *   group rooted at the designated "remote" root
	 *   (Note that we DO need to have an additional buffer
	 *   at the designated "remote" root to hold the result).
	 * - Send the result to the actual root of this operation
	 *   in the "local" group through the underlying intra-comm
	 */

	if(root == MPI_ROOT)
	{
		/* I'm the root for this Gather operation
		 * So, I just receive the "gathered" data
		 * from the "remote" root
		 */
		MPI_Group_translate_ranks(comm_coll->group->self,
						1, &remote_root,
						intra_group, &dest_rank); 

		/* Receive "gathered" data */
		mpi_errno = MPI_Recv(recvbuf, remote_size * recvcount,
					recvtype->self, dest_rank,
					MPI_INTER_GATHER_PT_TAG,
					comm_intra, &status);

	}
	else if(root >= 0)
	{
		/* I am a process in the remote group */

		/* Perform an intra_Gather...
		 * The following MPI_Gather should invoke
	 	 * the corresponding "intra" version  
	 	 */
		if(comm_size > 1)
		{
			mpi_errno = MPI_Gather(sendbuf, sendcnt, sendtype->self,
					buffer, sendcnt, sendtype->self,
				 	DESIGNATED_LOCAL_ROOT, comm_local);
		}

		if(my_rank == DESIGNATED_LOCAL_ROOT)
		{
			/* Send the "gathered" data to the other side */
			MPI_Group_translate_ranks(comm_coll->group->self,
						1, &root, intra_group,
						&dest_rank); 

			mpi_errno = MPI_Send(buffer, sendcnt * comm_size,
						sendtype->self, dest_rank,
						MPI_INTER_GATHER_PT_TAG,
						comm_intra);
		}
	}

	/* Unlock to end collective operation */
	MPID_THREAD_UNLOCK(comm_coll->ADIctx, comm_coll);

	if((root >= 0) && (my_rank == DESIGNATED_LOCAL_ROOT) && (comm_size > 1))
	{
	    /* Now that the work is over, free the buffer */
	    FREE((char *)buffer + lb);
	}

# endif

	if (mpi_errno)
		return MPIR_ERROR(comm, mpi_errno, myname);
	else
		return mpi_errno;

/* End of Modifications
 * ---------------------------------
 */

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

/* ---------------------------------
 * Modifications by Rajkumar Venkat
 */

	/* Initialize them to NULL - just for safety
	 * Alltoallw checks for the validity of some of these
	 * pointers to make out the direction of data flow.
	 */
	int *sendcounts = (int *)0;
	struct MPIR_DATATYPE *sendtypes = (struct MPIR_DATATYPE *)0;
	struct MPIR_DATATYPE *recvtypes = (struct MPIR_DATATYPE *)0;
	int *rdispls = (int *)0;
	int *sdispls = (int *)0;

	/* Communicator Attributes */
	MPI_Comm comm_local;
	int my_rank, dest_rank;
	int comm_size, remote_size, mpi_errno = MPI_SUCCESS;
	static char myname[] = "MPI_GATHERV";

	/* Temporary variables */
	MPI_Aint ub, lb, m_extent;	 /* Type extent info */
	int i;

	/* Switch to hidden collective */
	struct MPIR_COMMUNICATOR *comm_coll = comm->comm_coll;

	remote_size = comm_coll->np;

	/* Conditional checking? */
#ifndef MPIR_NO_ERROR_CHECKING
	if(remote_size <= 0)
	{
		mpi_errno = MPIR_Err_setmsg(MPI_ERR_REMOTE, MPIR_ERR_REMOTE_ZERO,
                                   myname, (char *)0, (char *)0, root, remote_size);
		if (mpi_errno)
			return MPIR_ERROR(comm, mpi_errno, myname);
	}
#endif

	/* Get the cached local communicator */
	inter_Attr_prepare(comm_coll, &comm_local);
	MPI_Comm_size(comm_local, &comm_size);
	MPI_Comm_rank(comm_local, &my_rank);

	/* Now that we have Alltoallw, simply make a call to it.
	 * Prepare the parameters accordingly.
	 */
	if((root == MPI_ROOT) && (recvcnts))
	{
		/* I am the root for this Gatherv.
		 * So, I do NOT contribute anything,
		 * but receive in the displs specified
		 */
		MPIR_ALLOC(rdispls, (int *) MALLOC(sizeof(int) * remote_size),
					comm_coll, MPI_ERR_EXHAUSTED, "MPI_GATHERV");
		MPIR_ALLOC(recvtypes, (struct MPIR_DATATYPE *) MALLOC(sizeof(struct MPIR_DATATYPE) * remote_size),
					comm_coll, MPI_ERR_EXHAUSTED, "MPI_GATHERV");

		/* Calculate the actual recvtype size
		 * Note that the size obtained is always
		 * > = the required size for the receive
		 */
		MPIR_Type_get_limits(recvtype, &lb, &ub);
  		m_extent = ub - lb;

		for(i = 0; i < remote_size; i++)
		{
			recvtypes[i] = *recvtype;

			/* Rewrite the displacement in terms of bytes
		 	 * as that is what Alltoallw would understand
		 	 */
			rdispls[i] = displs[i] * m_extent;
		}
	}
	else
	{
		if((root >= 0) && (sendcnt))		/* Or if(root != MPI_PROC_NULL) */
		{
			/* I am a process in the remote group
			 * wanting to send data to 'root'.
		 	 * So, I do NOT receive anything,
		 	 * but send from the location specified
		 	 */

			MPIR_ALLOC(sendcounts, (int *) MALLOC(sizeof(int) * remote_size),
					comm_coll, MPI_ERR_EXHAUSTED, "MPI_GATHERV");
			MPIR_ALLOC(sdispls, (int *) MALLOC(sizeof(int) * remote_size),
					comm_coll, MPI_ERR_EXHAUSTED, "MPI_GATHERV");
			MPIR_ALLOC(sendtypes, (struct MPIR_DATATYPE *) MALLOC(sizeof(struct MPIR_DATATYPE) * remote_size),
					comm_coll, MPI_ERR_EXHAUSTED, "MPI_GATHERV");

			for(i = 0; i < remote_size; i++)
			{
				sendcounts[i] = (i == root) ? sendcnt : 0;
				sdispls[i] = 0; 
				sendtypes[i] = *sendtype;
			}
		}
		else
		{
			/* Make sure that AlltoAllw does not do
			 * anything for the "uninvolved" processes
			 */
			recvcnts = (int *)0;
		}
	}			

	mpi_errno = inter_Alltoallw(sendbuf, sendcounts,
					sdispls, sendtypes,
					recvbuf, recvcnts,
					rdispls, recvtypes, comm);

	/* Clean up */
	if((root == MPI_ROOT) && (recvcnts))
	{
		FREE(rdispls);
		FREE(recvtypes);
	}
	else if((root >= 0) && (sendcnt))
	{
		FREE(sendcounts);
		FREE(sdispls);
		FREE(sendtypes);
	}

	if (mpi_errno)
		return MPIR_ERROR(comm, mpi_errno, myname);
	else
		return mpi_errno;

/* End of Modifications
 * ---------------------------------
 */

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

/* ---------------------------------
 * Modifications by Rajkumar Venkat
 */

	/* The alorithm should be similar to that of inter_Gather
	 * except for a change of direction...
	 */

	/* Communicator Attributes */
	MPI_Comm comm_local, comm_intra;
	MPI_Group intra_group;
	int my_rank, dest_rank, remote_root = DESIGNATED_REMOTE_ROOT;
	int comm_size, remote_size, buf_size, mpi_errno = MPI_SUCCESS;
	static char myname[] = "MPI_SCATTER";

# ifndef NO_ALTERNATIVE

	/* Point to Point */
	int *sendcnts = (int *)0;
	int *displs = (int *)0;
	int i;

# else

	/* Temporary variables */
	MPI_Aint ub, lb, m_extent;	 /* Buffer Allocation */
	MPI_Status status;

	void *buffer;

# endif

	/* Switch to hidden collective */
	struct MPIR_COMMUNICATOR *comm_coll = comm->comm_coll;

	remote_size = comm_coll->np;

	/* Conditional checking */
#ifndef MPIR_NO_ERROR_CHECKING
	if(remote_size <= 0)
	{
		mpi_errno = MPIR_Err_setmsg(MPI_ERR_REMOTE, MPIR_ERR_REMOTE_ZERO,
                                   myname, (char *)0, (char *)0, root, remote_size);
		if (mpi_errno)
			return MPIR_ERROR(comm, mpi_errno, myname);
	}
#endif

	/* Get the cached local communicator */
	inter_Attr_prepare(comm_coll, &comm_local);
	MPI_Comm_size(comm_local, &comm_size);
	MPI_Comm_rank(comm_local, &my_rank);

	/* Get the underlying "safe" intraComm
	 * from the "safe" interComm. Also get its group.
	 */
	comm_intra = comm_coll->comm_coll->self;
	MPI_Comm_group(comm_intra, &intra_group);

# ifndef NO_ALTERNATIVE

	/* Point to point */

	/* Allocate the arrays - only at the root
	 * and only if we have something to send
	 */
	if((root == MPI_ROOT) && (sendcnt))
	{
		MPIR_ALLOC(sendcnts, (int *)MALLOC(sizeof(int) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED,
				"MPI_SCATTER");
		MPIR_ALLOC(displs, (int *)MALLOC(sizeof(int) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED,
				"MPI_SCATTER");
	
		for(i = 0; i < remote_size; i++)
		{
			sendcnts[i] = sendcnt;
			displs[i] = i;
		}
	}

	/* I'll simply call inter_Scatterv */
	mpi_errno = inter_Scatterv(sendbuf, sendcnts, displs, sendtype,
					recvbuf, recvcnt, recvtype,
					root, comm);

	/* Free the arrays */
	if((root == MPI_ROOT) && (sendcnt))
	{
		FREE(sendcnts);
		FREE(displs);
	}

# else

	if(root >= 0)
	{
		if((my_rank == DESIGNATED_LOCAL_ROOT) && (comm_size > 1))
		{
			/* I'm the remote root and the root for
			 * the intra_Scatter... 
			 * Allocate a temporary extra buffer
			 */

			/* First, calculate the buffer size */
			MPIR_Type_get_limits(recvtype, &lb, &ub);
  			m_extent = ub - lb;
			
			buf_size = m_extent * comm_size;

			MPIR_ALLOC(buffer, (void *)MALLOC(buf_size),
					comm_coll, MPI_ERR_EXHAUSTED,
           				"MPI_SCATTER");

			buffer = (void *)((char *)buffer - lb);
		}
		else
		{
			/* Buffer not used */
			buffer = recvbuf;
		}
	}

	/* Lock for collective operation */ 
	MPID_THREAD_LOCK(comm_coll->ADIctx, comm_coll);
                
	/* -- Algorithm for inter_Scatter --
	 * - Send the data from the actual root of this operation
	 *   in the "local" group through the underlying intra-comm
	 *   to the "designated root" of the "remote" group. 
	 * - Perform MPI_Scatter (intra_Scatter) on the "remote"
	 *   group rooted at the designated "remote" root
	 *   (Note that we DO need to have an additional buffer
	 *   at the designated "remote" root to initially hold the
	 *   data to be scattered).
	 */

	if(root == MPI_ROOT)
	{
		/* I'm the root for this Scatter operation
		 * So, I just send receive the data to be
		 * "scattered" to the "remote" root
		 */
		MPI_Group_translate_ranks(comm_coll->group->self,
						1, &remote_root,
						intra_group, &dest_rank); 

		/* Send the data to be "scattered" */
		mpi_errno = MPI_Send(sendbuf, remote_size * sendcnt,
					sendtype->self, dest_rank,
					MPI_INTER_SCATTER_PT_TAG,
					comm_intra);
	}
	else if(root != MPI_PROC_NULL)	/* or (root >= 0) */
	{
		/* I am a process in the remote group */

		if(my_rank == DESIGNATED_LOCAL_ROOT)
		{
			/* Receive the data to be scattered from the other side */
			MPI_Group_translate_ranks(comm_coll->group->self,
						1, &root, intra_group,
						&dest_rank); 

			/* We need to a intra_Scatter next. So receive the
			 * data from the root into the temporary buffer
			 */
			mpi_errno = MPI_Recv(buffer, comm_size * recvcnt,
						recvtype->self, dest_rank, 
						MPI_INTER_SCATTER_PT_TAG,
						comm_intra, &status);
		}

		/* Now the buffer is ready
		 * Perform an intra_Scatter...
		 * The following MPI_Scatter should invoke
	 	 * the corresponding "intra" version  
	 	 */
		if(comm_size > 1)
		{
			mpi_errno = MPI_Scatter(buffer, recvcnt, recvtype->self,
						recvbuf, recvcnt, recvtype->self,
				 		DESIGNATED_LOCAL_ROOT, comm_local);
		}
	}

	/* Unlock to end collective operation */
	MPID_THREAD_UNLOCK(comm_coll->ADIctx, comm_coll);

	/* Now that the work is over, free the buffer */
	if((root >= 0) && (my_rank == DESIGNATED_LOCAL_ROOT) && (comm_size > 1))
	{
		FREE((char *)buffer + lb);
	}

# endif

	if (mpi_errno)
		return MPIR_ERROR(comm, mpi_errno, myname);
	else
		return mpi_errno;
    
/* End of Modifications
 * ---------------------------------
 */

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

/* ---------------------------------
 * Modifications by Rajkumar Venkat
 */

    	/* Initialize them to NULL - just for safety
	 * Alltoallw checks for the validity of some of these
	 * pointers to make out the direction of data flow.
	 */
	int *recvcounts = (int *)0;
	struct MPIR_DATATYPE *sendtypes = (struct MPIR_DATATYPE *)0;
	struct MPIR_DATATYPE *recvtypes = (struct MPIR_DATATYPE *)0;
	int *sdispls = (int *)0;
	int *rdispls = (int *)0;

	/* Communicator Attributes */
	MPI_Comm comm_local;
	int my_rank, dest_rank;
	int comm_size, remote_size, mpi_errno = MPI_SUCCESS;
	static char myname[] = "MPI_SCATTERV";

	/* Temporary variables */
	MPI_Aint ub, lb, m_extent;	 /* Type extent info */
	int i;

	/* Switch to hidden collective */
	struct MPIR_COMMUNICATOR *comm_coll = comm->comm_coll;

	remote_size = comm_coll->np;

	/* Conditional checking? */
#ifndef MPIR_NO_ERROR_CHECKING
	if(remote_size <= 0)
	{
		mpi_errno = MPIR_Err_setmsg(MPI_ERR_REMOTE, MPIR_ERR_REMOTE_ZERO,
                                   myname, (char *)0, (char *)0, root, remote_size);
		if (mpi_errno)
			return MPIR_ERROR(comm, mpi_errno, myname);
	}
#endif

	/* Get the cached local communicator */
	inter_Attr_prepare(comm_coll, &comm_local);
	MPI_Comm_size(comm_local, &comm_size);
	MPI_Comm_rank(comm_local, &my_rank);

	/* Now that we have Alltoallw, simply make a call to it.
	 * Prepare the parameters accordingly.
	 */
	if((root == MPI_ROOT) && (sendcnts))
	{
		/* I am the root for this Scatterv.
		 * So, I do NOT receive anything,
		 * but send from the displs specified
		 */
		MPIR_ALLOC(sdispls, (int *) MALLOC(sizeof(int) * remote_size),
					comm_coll, MPI_ERR_EXHAUSTED, "MPI_SCATTERV");
		MPIR_ALLOC(sendtypes, (struct MPIR_DATATYPE *) MALLOC(sizeof(struct MPIR_DATATYPE) * remote_size),
					comm_coll, MPI_ERR_EXHAUSTED, "MPI_SCATTERV");

		/* Calculate the actual sendtype size
		 * Note that the size obtained is always
		 * > = the required size for the send
		 */
		MPIR_Type_get_limits(sendtype, &lb, &ub);
  		m_extent = ub - lb;

		for(i = 0; i < remote_size; i++)
		{
			sendtypes[i] = *sendtype;

			/* Rewrite the displacement in terms of bytes
		 	 * as that is what Alltoallw would understand
		 	 */
			sdispls[i] = displs[i] * m_extent;
		}
	}
	else
	{
		if((root >= 0) && (recvcnt))		/* Or if(root != MPI_PROC_NULL) */
		{
			/* I am a process in the remote group
			 * wanting to receive data from 'root'.
		 	 * So, I do NOT send anything,
		 	 * but receive into the location specified
		 	 */

			MPIR_ALLOC(recvcounts, (int *) MALLOC(sizeof(int) * remote_size),
					comm_coll, MPI_ERR_EXHAUSTED, "MPI_SCATTERV");
			MPIR_ALLOC(rdispls, (int *) MALLOC(sizeof(int) * remote_size),
					comm_coll, MPI_ERR_EXHAUSTED, "MPI_SCATTERV");
			MPIR_ALLOC(recvtypes, (struct MPIR_DATATYPE *) MALLOC(sizeof(struct MPIR_DATATYPE) * remote_size),
					comm_coll, MPI_ERR_EXHAUSTED, "MPI_SCATTERV");

			for(i = 0; i < remote_size; i++)
			{
				recvcounts[i] = (i == root) ? recvcnt : 0;
				rdispls[i] = 0; 
				recvtypes[i] = *recvtype;
			}
		}
		else
		{
			/* Make sure that AlltoAllw does not do
			 * anything for the "uninvolved" processes
			 */
			sendcnts = (int *)0;
		}
	}			

	mpi_errno = inter_Alltoallw(sendbuf, sendcnts,
					sdispls, sendtypes,
					recvbuf, recvcounts,
					rdispls, recvtypes, comm);

	/* Clean up */
	if((root == MPI_ROOT) && (sendcnts))
	{
		FREE(sdispls);
		FREE(sendtypes);
	}
	else if((root >= 0) && (recvcnt))
	{
		FREE(recvcounts);
		FREE(rdispls);
		FREE(recvtypes);
	}

	if (mpi_errno)
		return MPIR_ERROR(comm, mpi_errno, myname);
	else
		return mpi_errno;
                                             
/* End of Modifications
 * ---------------------------------
 */

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

/* ---------------------------------
 * Modifications by Rajkumar Venkat
 */

	/* Gather on both sides, exchange data, then broadcast locally */
	
	/* Communicator Attributes */
	MPI_Comm comm_local, comm_intra;
	MPI_Group intra_group;
	int my_rank, dest_rank, remote_root = DESIGNATED_REMOTE_ROOT;
	int comm_size, remote_size, mpi_errno = MPI_SUCCESS;
	static char myname[] = "MPI_ALLGATHER";

# ifndef NO_ALTERNATIVE

	/* Point to Point */
	int *recvcnts = (int *)0;
	int *displs = (int *)0;
	int i;

# else

	/* Temporary variables */
	MPI_Aint ub, lb, m_extent;	 /* Buffer Allocation */
	MPI_Status status;
	int buf_size;
	void *buffer;

# endif

	/* Switch to hidden collective */
	struct MPIR_COMMUNICATOR *comm_coll = comm->comm_coll;

	remote_size = comm_coll->np;

	/* Conditional checking */
#ifndef MPIR_NO_ERROR_CHECKING
	if(remote_size <= 0)
	{
		mpi_errno = MPIR_Err_setmsg(MPI_ERR_REMOTE, MPIR_ERR_REMOTE_ZERO,
                                   myname, (char *)0, (char *)0, (char *)0, remote_size);
		if (mpi_errno)
			return MPIR_ERROR(comm, mpi_errno, myname);
	}
#endif

	/* Get the cached local communicator */
	inter_Attr_prepare(comm_coll, &comm_local);
	MPI_Comm_size(comm_local, &comm_size);
	MPI_Comm_rank(comm_local, &my_rank);

	/* Get the underlying "safe" intraComm
	 * from the "safe" interComm. Also get its group.
	 */
	comm_intra = comm_coll->comm_coll->self;
	MPI_Comm_group(comm_intra, &intra_group);

# ifndef NO_ALTERNATIVE

	/* Point to point */

	/* Allocate the arrays - only if we have something to receive */
	if((my_rank == DESIGNATED_LOCAL_ROOT) && (recvcount))
	{
		MPIR_ALLOC(recvcnts, (int *)MALLOC(sizeof(int) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED,
				"MPI_ALLGATHER");
		MPIR_ALLOC(displs, (int *)MALLOC(sizeof(int) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED,
				"MPI_ALLGATHER");

		for(i = 0; i < remote_size; i++)
		{	
			recvcnts[i] = recvcount;
			displs[i] = i;
		}
	}

	/* I'll simply call inter_Gatherv */
	mpi_errno = inter_Allgatherv(sendbuf, sendcnt, sendtype,
					recvbuf, recvcnts, displs,
					recvtype, comm);

	/* Free the arrays */
	if((my_rank == DESIGNATED_LOCAL_ROOT) && (recvcount))
	{
		FREE(recvcnts);
		FREE(displs);
	}

# else

	if((my_rank == DESIGNATED_LOCAL_ROOT) && (comm_size > 1) && (sendcount))
	{
		/* I'm the remote root and the root for
		 * the intra_Gather... 	
		 * Allocate a temporary extra buffer
		 */

		/* First, calculate the buffer size */
		MPIR_Type_get_limits(sendtype, &lb, &ub);
  		m_extent = ub - lb;
			
		buf_size = m_extent * comm_size;

		MPIR_ALLOC(buffer, (void *)MALLOC(buf_size),
				comm_coll, MPI_ERR_EXHAUSTED,
          			"MPI_ALLGATHER");

		buffer = (void *)((char *)buffer - lb);
	}
	else
	{
		/* Buffer not used */
		buffer = sendbuf;
	}

	/* Lock for collective operation */ 
	MPID_THREAD_LOCK(comm_coll->ADIctx, comm_coll);
	
	/* -- Algorithm for inter_Allgather --
	 * - Perform MPI_Gather (intra_Gather)
	 *   rooted at the designated "remote" root
	 *   (Note that we DO need to have an additional buffer
	 *   at the designated "remote" root to hold the result).
	 * - Send the result to the designated root
	 *   in the "other" group through the underlying intra-comm
	 * - Broadcast the result received from the other side to
	 *   all processes in the local group
	 */

	if(comm_size > 1)
	{
		mpi_errno = MPI_Gather(sendbuf, sendcount, sendtype->self,
					buffer, sendcount, sendtype->self,
				 	DESIGNATED_LOCAL_ROOT, comm_local);
	}	

	if(my_rank == DESIGNATED_LOCAL_ROOT)
	{
		MPI_Group_translate_ranks(comm_coll->group->self,
						1, &remote_root, intra_group,
						&dest_rank); 

		/* Exchange "gathered" data */
		mpi_errno = MPI_Sendrecv(buffer, comm_size * sendcount,
					sendtype->self, dest_rank,
					MPI_INTER_ALLGATHER_PT_TAG,
					recvbuf, remote_size * recvcount,
					recvtype->self, dest_rank,
					MPI_INTER_ALLGATHER_PT_TAG,
					comm_intra, &status);
	}

	mpi_errno = MPI_Bcast(recvbuf, remote_size * recvcount, recvtype->self,
				DESIGNATED_LOCAL_ROOT, comm_local);

	/* Unlock to end collective operation */
	MPID_THREAD_UNLOCK(comm_coll->ADIctx, comm_coll);

	/* Clean up */
	if((my_rank == DESIGNATED_LOCAL_ROOT) && (comm_size > 1) && (sendcount))
		FREE((char *)buffer + lb);

#endif

	if (mpi_errno)
		return MPIR_ERROR(comm, mpi_errno, myname);
	else
		return mpi_errno;


/* End of Modifications
 * ---------------------------------
 */

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
/* ---------------------------------
 * Modifications by Rajkumar Venkat
 */

	/* We are trying to use the generalized Alltoallw
	 * function for this...
	 */

	/* First declare the "extra" parameters
	 * that will be passed to Alltoallw
	 */
	int *sendcounts = (int *)0;
	int *sdispls = (int *)0;
	int *rdispls = (int *)0;
	struct MPIR_DATATYPE *sendtypes = (struct MPIR_DATATYPE *)0;
	struct MPIR_DATATYPE *recvtypes = (struct MPIR_DATATYPE *)0;

	int i;

	/* Usual stuff */

	/* Communicator Attributes */
	MPI_Comm comm_local;
	int comm_size, remote_size, mpi_errno = MPI_SUCCESS;
	static char myname[] = "MPI_ALLGATHERV";

	MPI_Aint ub, lb, m_extent;

	/* Switch to hidden collective */
	struct MPIR_COMMUNICATOR *comm_coll = comm->comm_coll;

	remote_size = comm_coll->np;

	/* Conditional checking */
#ifndef MPIR_NO_ERROR_CHECKING
	if(remote_size <= 0)
	{
		mpi_errno = MPIR_Err_setmsg(MPI_ERR_REMOTE, MPIR_ERR_REMOTE_ZERO,
                                   myname, (char *)0, (char *)0, (char *)0, remote_size);
		if (mpi_errno)
			return MPIR_ERROR(comm, mpi_errno, myname);
	}
#endif

	/* Get the cached local communicator */
	inter_Attr_prepare(comm_coll, &comm_local);
	MPI_Comm_size(comm_local, &comm_size);

	/* Prepare the data for Alltoallw */
	if(sendcount)
	{
		MPIR_ALLOC(sendcounts, (int *) MALLOC(sizeof(int) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED, "MPI_ALLGATHERV");
		MPIR_ALLOC(sdispls, (int *) MALLOC(sizeof(int) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED, "MPI_ALLGATHERV");
		MPIR_ALLOC(sendtypes, (struct MPIR_DATATYPE *) MALLOC(sizeof(struct MPIR_DATATYPE) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED, "MPI_ALLGATHERV");

		for(i = 0; i < remote_size; i++)
		{
			sendcounts[i] = sendcount;
			sdispls[i] = 0;
			sendtypes[i] = *sendtype;
		}
	}

	if(recvcounts)
	{
		MPIR_ALLOC(rdispls, (int *) MALLOC(sizeof(int) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED, "MPI_ALLGATHERV");

		MPIR_ALLOC(recvtypes, (struct MPIR_DATATYPE *) MALLOC(sizeof(struct MPIR_DATATYPE) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED, "MPI_ALLGATHERV");


		MPIR_Type_get_limits(recvtype, &lb, &ub);
  		m_extent = ub - lb;

		for(i = 0; i < remote_size; i++)
		{
			recvtypes[i] = *recvtype;
			rdispls[i] = displs[i] * m_extent;
		}
	}

	mpi_errno = inter_Alltoallw(sendbuf, sendcounts, sdispls, sendtypes,
					recvbuf, recvcounts, rdispls, recvtypes,
						comm);

	/* Clean up */
	if(sendcount)
	{
		FREE(sendcounts);
		FREE(sdispls);
		FREE(sendtypes);
	}
	if(recvcounts)
	{
		FREE(rdispls);
		FREE(recvtypes);
	}

	if (mpi_errno)
		return MPIR_ERROR(comm, mpi_errno, myname);
	else
		return mpi_errno;
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

	/* We are trying to use the generalized Alltoallw
	 * function for this...
	 */

	/* First declare the "extra" parameters
	 * that will be passed to Alltoallw
	 */
	int *sendcounts = (int *)0;
	int *sdispls = (int *)0;
	struct MPIR_DATATYPE *sendtypes = (struct MPIR_DATATYPE *)0;
	int *recvcounts = (int *)0;
	int *rdispls = (int *)0;
	struct MPIR_DATATYPE *recvtypes = (struct MPIR_DATATYPE *)0;

	int i;

	/* Usual stuff */

	/* Communicator Attributes */
	MPI_Comm comm_local;

	/* Temporary variables */
	MPI_Aint ub, lb, m_extent;

	int comm_size, remote_size, mpi_errno = MPI_SUCCESS;
	static char myname[] = "MPI_ALLTOALL";

	/* Switch to hidden collective */
	struct MPIR_COMMUNICATOR *comm_coll = comm->comm_coll;

	/* Since we are here, the local communicator is obviously
	 * NOT NULL. However, the remote group could be NULL i.e.
	 * with no processes at all! So we check the remote size
	 * if it is zero, bail out! Set the appropriate error msg
	 */
	remote_size = comm_coll->np;

	/* Conditional checking */
#ifndef MPIR_NO_ERROR_CHECKING
	if(remote_size <= 0)
	{
		mpi_errno = MPIR_Err_setmsg(MPI_ERR_REMOTE, MPIR_ERR_REMOTE_ZERO,
                                   myname, (char *)0, (char *)0, (char *)0, remote_size);
		if (mpi_errno)
			return MPIR_ERROR(comm, mpi_errno, myname);
	}
#endif

	/* Get the cached local communicator */
	inter_Attr_prepare(comm_coll, &comm_local);
	MPI_Comm_size(comm_local, &comm_size);

	/* Prepare the data for Alltoallw */
	if(sendcount)
	{
		MPIR_ALLOC(sendcounts, (int *) MALLOC(sizeof(int) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED, "MPI_ALLTOALL");
		MPIR_ALLOC(sdispls, (int *) MALLOC(sizeof(int) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED, "MPI_ALLTOALL");
		MPIR_ALLOC(sendtypes, (struct MPIR_DATATYPE *) MALLOC(sizeof(struct MPIR_DATATYPE) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED, "MPI_ALLTOALL");

		for(i = 0; i < remote_size; i++)
		{
			sendcounts[i] = sendcount;
			sdispls[i] = i;
			sendtypes[i] = *sendtype;
		}
	}
	if(recvcnt)
	{
		MPIR_ALLOC(recvcounts, (int *) MALLOC(sizeof(int) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED, "MPI_ALLTOALL");
		MPIR_ALLOC(rdispls, (int *) MALLOC(sizeof(int) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED, "MPI_ALLTOALL");
		MPIR_ALLOC(recvtypes, (struct MPIR_DATATYPE *) MALLOC(sizeof(struct MPIR_DATATYPE) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED, "MPI_ALLTOALL");


		for(i = 0; i < remote_size; i++)
		{
			recvcounts[i] = recvcnt;
			rdispls[i] = i;
			recvtypes[i] = *recvtype;
		}
	}

	mpi_errno = inter_Alltoallv(sendbuf, sendcounts, sdispls, sendtypes,
						recvbuf, recvcounts, rdispls, recvtypes, comm);

	/* Free allocated memory */
	if(sendcount)
	{
		FREE(sendcounts);
		FREE(sdispls);
		FREE(sendtypes);
	}
	if(recvcnt)
	{
		FREE(recvcounts);
		FREE(rdispls);
		FREE(recvtypes);
	}

	if (mpi_errno)
		return MPIR_ERROR(comm, mpi_errno, myname);
	else
		return mpi_errno;
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
/* ---------------------------------
 * Modifications by Rajkumar Venkat
 */

	/* We are trying to use the generalized Alltoallw
	 * function for this...
	 */

	/* First declare the "extra" parameters
	   that will be passed to Alltoallw
	 */
	int *senddispls = (int *)0;
	int *recvdispls = (int *)0;
	struct MPIR_DATATYPE *sendtypes = (struct MPIR_DATATYPE *)0;
	struct MPIR_DATATYPE *recvtypes = (struct MPIR_DATATYPE *)0;

	int i;

	/* Usual stuff */

	/* Communicator Attributes */
	MPI_Comm comm_local;
	int comm_size, remote_size, mpi_errno = MPI_SUCCESS;
	static char myname[] = "MPI_ALLTOALLV";

	/* Temporary variables */
	MPI_Aint ub, lb, m_extent;	 /* Buffer Allocation */

	/* Switch to hidden collective */
	struct MPIR_COMMUNICATOR *comm_coll = comm->comm_coll;

	remote_size = comm_coll->np;

	/* Conditional checking */
#ifndef MPIR_NO_ERROR_CHECKING
	if(remote_size <= 0)
	{
		mpi_errno = MPIR_Err_setmsg(MPI_ERR_REMOTE, MPIR_ERR_REMOTE_ZERO,
                                   myname, (char *)0, (char *)0, (char *)0, remote_size);
		if (mpi_errno)
			return MPIR_ERROR(comm, mpi_errno, myname);
	}
#endif

	/* Get the cached local communicator */
	inter_Attr_prepare(comm_coll, &comm_local);
	MPI_Comm_size(comm_local, &comm_size);

	/* Prepare the data for Alltoallw */
	if(sendcnts)
	{
		MPIR_ALLOC(senddispls, (int *) MALLOC(sizeof(int) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED, "MPI_ALLTOALLV");
		MPIR_ALLOC(sendtypes, (struct MPIR_DATATYPE *) MALLOC(sizeof(struct MPIR_DATATYPE) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED, "MPI_ALLTOALLV");

		/* To find the displacement in each case for the send
		 * buffer, compute the true extent of the `send' datatype
		 */
		MPIR_Type_get_limits(sendtype, &lb, &ub);
  		m_extent = ub - lb;

		for(i = 0; i < remote_size; i++)
		{
			sendtypes[i] = *sendtype;

			/* Rewrite the displacements in terms of bytes as that is
			 * what Alltoallw would understand...
			 */
			senddispls[i] = sdispls[i] * m_extent;
		}
	}
	if(recvcnts)
	{
		MPIR_ALLOC(recvdispls, (int *) MALLOC(sizeof(int) * remote_size),
				comm_coll, MPI_ERR_EXHAUSTED, "MPI_ALLTOALLV");
		MPIR_ALLOC(recvtypes, (struct MPIR_DATATYPE *) MALLOC(sizeof(struct MPIR_DATATYPE) * remote_size),
			comm_coll, MPI_ERR_EXHAUSTED, "MPI_ALLTOALLV");
		
		/* To find the displacement in each case for the recv
		 * buffer, compute the true extent of the `recv' datatype
		 */
		MPIR_Type_get_limits(recvtype, &lb, &ub);
  		m_extent = ub - lb;

		for(i = 0; i < remote_size; i++)
		{
			recvdispls[i] = rdispls[i] * m_extent;
			recvtypes[i] = *recvtype;
		}
	}

	mpi_errno = inter_Alltoallw(sendbuf, sendcnts, senddispls, sendtypes,
					recvbuf, recvcnts, recvdispls, recvtypes,
						comm);

	/* Free the allocated arrays */
	if(sendcnts) 
	{
		FREE(senddispls);
		FREE(sendtypes);
	}
	if(recvcnts) 
	{
		FREE(recvdispls);
		FREE(recvtypes);
	}

	if (mpi_errno)
		return MPIR_ERROR(comm, mpi_errno, myname);
	else
		return mpi_errno;
}


static int inter_Alltoallw (
	void *sendbuf, 
	int *sendcounts, 
	int *sdispls,
	struct MPIR_DATATYPE *sendtypes, 
	void *recvbuf, 
	int *recvcounts, 
	int *rdispls, 
	struct MPIR_DATATYPE *recvtypes,
	struct MPIR_COMMUNICATOR *comm)
{
	/* This is the generalized version of the
	 * Alltoall functions and has been introduced
	 * IN MPI-2.
	 */

	/* Note that we are sending/receiving data to/from the remote
	 * group directly without any additional buffering
	 */

	/* Communicator Attributes */
	MPI_Comm comm_local, comm_intra;
	int my_rank, comm_size, remote_size, mpi_errno = MPI_SUCCESS;
	static char myname[] = "MPI_ALLTOALLW";

	/* Temporary variables */
	MPI_Status *status;		/* For both send and receive */
	MPI_Request *request;		/* For both send and receive */
	MPI_Group intra_group;

	int i, offset = 0, sendcnt = 0, recvcnt = 0;
	int *rank;			/* For both input and dest. ranks */

	/* Switch to hidden collective */
	struct MPIR_COMMUNICATOR *comm_coll = comm->comm_coll;

	remote_size = comm_coll->np;

	/* Conditional checking */
#ifndef MPIR_NO_ERROR_CHECKING
	if(remote_size <= 0)
	{
		mpi_errno = MPIR_Err_setmsg(MPI_ERR_REMOTE, MPIR_ERR_REMOTE_ZERO,
                                   myname, (char *)0, (char *)0, (char *)0, remote_size);
		if (mpi_errno)
			return MPIR_ERROR(comm, mpi_errno, myname);
	}
#endif

	/* Get the cached local communicator */
	inter_Attr_prepare(comm_coll, &comm_local);
	MPI_Comm_size(comm_local, &comm_size);
	MPI_Comm_rank(comm_local, &my_rank);

	/* Get the underlying "safe" intraComm
	 * from the "safe" interComm. Also get its group.
	 * NOTE: Be sure to test the validity of the
	 * underlying intraComm
	 */
	comm_intra = comm_coll->comm_coll->self;
	MPI_Comm_group(comm_intra, &intra_group);

	/* Trying to "save" extra calls to MPIR_ALLOC by clubbing
	 * requests, statuses and ranks, as shown below.
	 */

	/* The implementation relies on the sendcounts and recvcounts
	 * arrays to determine if there is actually any movement of data.
	 * It is perfectly valid for the caller to use this method for
	 * unidirectional flow of data or no flow of data (like a Barrier)
	 */

	if(sendcounts && recvcounts)
	{
		/* Normal flow */
		offset = remote_size;
	}
	else
	{
		/* Unidirectional or no flow */
		if(!(sendcounts || recvcounts))
		{
			/* No flow - just end this call */
			return mpi_errno;
		}
	}

	/* At this point, we are having either a uni-directional
	 * or bi-directional flow
	 */

	/* Allocate space for all the requests... */
	MPIR_ALLOC(request, (void *)MALLOC(sizeof(MPI_Request) * (remote_size + offset)),
				comm_coll, MPI_ERR_EXHAUSTED,
				"MPI_ALLTOALLW");

	/* Allocate space for all the status replies */
	MPIR_ALLOC(status, (void *)MALLOC(sizeof(MPI_Status) * (remote_size + offset)),
				comm_coll, MPI_ERR_EXHAUSTED,
				"MPI_ALLTOALLW");

	/* Allocate space for the rank array */
	MPIR_ALLOC(rank, (void *)MALLOC(sizeof(int) * remote_size * 2),
				comm_coll, MPI_ERR_EXHAUSTED,
				"MPI_ALLTOALLW");

	for(i = 0; i < remote_size; i++)
		rank[remote_size + i] = i;

	/* Get the rank of the processes in the remote group
	 * w.r.to the underlying intra-comm.
	 */
	MPI_Group_translate_ranks(comm_coll->group->self, remote_size,
					&rank[remote_size], intra_group,
					rank);

	/* Lock for collective operation */ 
	MPID_THREAD_LOCK(comm_coll->ADIctx, comm_coll);

	/* -- Algorithm for inter_Alltoallw --
	 * - I (i.e. every process) run in a loop directly sending and receiving
	 *   data to and from every process in the remote group from/into the 
	 *   appropriate location in the buffer using the underlying intra-comm.
	 * . Note that topology information is not of any use to us, since
	 *   inter-communicators are not bound by any topological
	 *   considerations.
	 * . Also note that we are using non-blocking send/receive calls in
	 *   the loop and so have to synchronize using a Wait...
	 */
        
	for(i = 0; i < remote_size; i++)
	{
		/* In the general case, we have something to send to
		 * AND something to receive from every process in the
		 * remote group. What needs to be checked is that if
		 * something is zero, will the Isend and/or Irecv act
		 * accordingly by doing nothing.
		 */

		/* First do the send (non-blocking)
		 * Note that we are checking if sendcounts is a valid
		 * array or not. If sendcounts is NULL, it could mean
		 * unidirectional flow of data (i.e. receive only)
		 */
		if(sendcounts)
		{
			mpi_errno = MPI_Isend((void *)(((char *)sendbuf)
						+ sdispls[i]),
						sendcounts[i], sendtypes[i].self,
						rank[i],
						MPI_INTER_ALLTOALLW_DATA_TAG,
						comm_intra, &request[i]);

			if(mpi_errno)
				break;

			sendcnt++;
		}

		/* Next do the receive (non-blocking)
		 * Note that we are checking if recvcounts is a valid
		 * array or not. If recvcounts is NULL, it could mean
		 * unidirectional flow of data (i.e. send only)
		 */
		if(recvcounts)
		{
			mpi_errno = MPI_Irecv((void *)(((char *)recvbuf)
						 + rdispls[i]), 
						recvcounts[i], recvtypes[i].self,
						rank[i],
						MPI_INTER_ALLTOALLW_DATA_TAG,
						comm_intra, &request[offset + i]);

			if(mpi_errno)
				break;

			recvcnt++;
		}
	}
	
	/* Check if any of the send/receive calls failed
	 * If so, we need to cancel all the successfully
	 * completed ones
	 */
	if(mpi_errno)
	{
		for(i = 0; i < sendcnt; i++)
			MPI_Cancel(&request[i]);

		for(i = 0; i < recvcnt; i++)
			MPI_Cancel(&request[offset + i]);
	}
	else
	{
		/* Wait for all the send's and receive's to complete */
		mpi_errno = MPI_Waitall((remote_size + offset), request, status);
	}

	/* Unlock to end collective operation */
	MPID_THREAD_UNLOCK(comm_coll->ADIctx, comm_coll);

	/* Free the request and status arrays */
	FREE(request);
	FREE(status);
	FREE(rank);
                                           
	if (mpi_errno)
		return MPIR_ERROR(comm, mpi_errno, myname);
	else
		return mpi_errno;

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

/* ---------------------------------
 * Modifications by Rajkumar Venkat
 */

	/* Communicator Attributes */
	MPI_Comm comm_local, comm_intra;

	/* Temporary variables */
	MPI_Aint ub, lb, m_extent;	/* Buffer Allocation */
	MPI_Status status;
	MPI_Group intra_group;

	int my_rank, dest_rank, remote_root = DESIGNATED_REMOTE_ROOT;
	int comm_size, remote_size, mpi_errno = MPI_SUCCESS;
	void *buffer;
	static char myname[] = "MPI_REDUCE";

	/* Switch to hidden collective */
	struct MPIR_COMMUNICATOR *comm_coll = comm->comm_coll;

	remote_size = comm_coll->np;

	/* Conditional checking */
#ifndef MPIR_NO_ERROR_CHECKING
	if(remote_size <= 0)
	{
		mpi_errno = MPIR_Err_setmsg(MPI_ERR_REMOTE, MPIR_ERR_REMOTE_ZERO,
                                   myname, (char *)0, (char *)0, root, remote_size);
		if (mpi_errno)
			return MPIR_ERROR(comm, mpi_errno, myname);
	}
#endif

	/* TBD: Additional Reduce-specific checks include checking if
	 * the 'Op' is valid for the 'datatype', if predefined.
	 */

	/* Get the cached local communicator */
	inter_Attr_prepare(comm_coll, &comm_local);
	MPI_Comm_size(comm_local, &comm_size);
	MPI_Comm_rank(comm_local, &my_rank);

	/* Get the underlying "safe" intraComm
	 * from the "safe" interComm. Also get its group.
	 */
	comm_intra = comm_coll->comm_coll->self;
	MPI_Comm_group(comm_intra, &intra_group);

	if((root >= 0) && (my_rank == DESIGNATED_LOCAL_ROOT) && (comm_size > 1))
	{
		/* I'm the remote root and the root for
		 * the intra-reduce... 
		 * Allocate a temporary extra buffer
		 */
 		MPIR_Type_get_limits(datatype, &lb, &ub);
  		m_extent = ub - lb;

		/* The reason for using a different variable
		 * for the buffer instead of using the recvbuf
		 * is: As the recvbuf is insignificant except
		 * at the root, the recvbuf passed could be
		 * "anything" - even a valid buffer (allocated
		 * in the calling program for e.g.) and expect
		 * it to remain intact at the end of this call
		 * as it is supposed to be ignored by the
		 * MPI_Reduce call. Actually, intra_Reduce reuses
		 * the variable currently. Is it Ok?
		 */
 
		MPIR_ALLOC(buffer,
				(void *)MALLOC(m_extent * count),
				comm_coll, MPI_ERR_EXHAUSTED,
           			"MPI_REDUCE");
		buffer = (void *)((char *)buffer - lb);
	}
	else
		buffer = sendbuf;

	/* Lock for collective operation */ 
	MPID_THREAD_LOCK(comm_coll->ADIctx, comm_coll);
                
	/* -- Algorithm for inter_Reduce --
	 * - Perform MPI_Reduce (intra_reduce) on the "remote"
	 *   group rooted at the designated "remote" root
	 *   (Note that we DO need to have an additional buffer
	 *   at the designated "remote" root to hold the result).
	 * - Send the result to the actual root of this operation
	 *   in the "local" group through the underlying intra-comm
	 */

	if(root == MPI_ROOT)
	{
		/* I'm the root for this Reduce operation
		 * So, I just receive the "reduced" data
		 * from the "remote" root
		 */
		MPI_Group_translate_ranks(comm_coll->group->self,
						1, &remote_root,
						intra_group, &dest_rank); 

		mpi_errno = MPI_Recv(recvbuf, count,
					datatype->self, dest_rank,
					MPI_INTER_REDUCE_PT_TAG,
					comm_intra, &status);
	}
	else if(root != MPI_PROC_NULL)
	{
		/* I am a process in the remote group */

		/* Perform an intra_reduce...
		 * The following MPI_Reduce should invoke
	 	 * the corresponding "intra" version  
	 	 */
		if(comm_size > 1)
		{
			mpi_errno = MPI_Reduce(sendbuf, buffer, count, datatype->self,
				 op, DESIGNATED_LOCAL_ROOT, comm_local);
		}

		if(my_rank == DESIGNATED_LOCAL_ROOT)
		{
			/* Send the "reduced" data to the other side */
			MPI_Group_translate_ranks(comm_coll->group->self,
						1, &root, intra_group,
						&dest_rank); 

			mpi_errno = MPI_Send(buffer, count,
						datatype->self, dest_rank,
						MPI_INTER_REDUCE_PT_TAG,
						comm_intra);
		}
	}

	/* Unlock to end collective operation */
	MPID_THREAD_UNLOCK(comm_coll->ADIctx, comm_coll);

	if((root >= 0) && (my_rank == DESIGNATED_LOCAL_ROOT) && (comm_size > 1))
		FREE((char *)buffer + lb);	

	if (mpi_errno)
		return MPIR_ERROR(comm, mpi_errno, myname);
	else
		return mpi_errno;

/* End of Modifications
 * ---------------------------------
 */

}

static int inter_Allreduce ( 
	void *sendbuf, 
	void *recvbuf, 
	int count, 
	struct MPIR_DATATYPE *datatype, 
	MPI_Op op, 
	struct MPIR_COMMUNICATOR *comm )
{

/* ---------------------------------
 * Modifications by Rajkumar Venkat  
 */

	/* Communicator Attributes */
	MPI_Comm comm_local, comm_intra;
	int my_rank, dest_rank, remote_root = DESIGNATED_REMOTE_ROOT;
	int comm_size, remote_size, mpi_errno = MPI_SUCCESS;
	static char myname[] = "MPI_ALLREDUCE";
            
	/* Temporary variables */
	MPI_Aint ub, lb, m_extent;	/* Buffer Allocation */
	MPI_Status status;
	MPI_Group intra_group;

	void *buffer;

	/* Switch to hidden collective */
	struct MPIR_COMMUNICATOR *comm_coll = comm->comm_coll;
        
	/* Check for zero-length message
	 * To verify if it is allowed or not
	 */
	if(count == 0)
		return MPI_SUCCESS;
        
	remote_size = comm_coll->np;

	/* Conditional checking */
#ifndef MPIR_NO_ERROR_CHECKING
	if(remote_size <= 0)
	{
		mpi_errno = MPIR_Err_setmsg(MPI_ERR_REMOTE, MPIR_ERR_REMOTE_ZERO,
                                   myname, (char *)0, (char *)0, (char *)0, remote_size);
		if (mpi_errno)
			return MPIR_ERROR(comm, mpi_errno, myname);
	}
#endif

	/* TBD: Additional Reduce-specific checks include checking if
	 * the 'Op' is valid for the 'datatype', if predefined.
	 */

	/* Get the cached local communicator */
	inter_Attr_prepare(comm_coll, &comm_local);
	MPI_Comm_size(comm_local, &comm_size);
	MPI_Comm_rank(comm_local, &my_rank);

	/* Get the underlying "safe" intraComm
	 * from the "safe" interComm. Also get its group.
	 */
	comm_intra = comm_coll->comm_coll->self;
	MPI_Comm_group(comm_intra, &intra_group);
                  
	if(my_rank == DESIGNATED_LOCAL_ROOT && comm_size > 1)
	{
 		MPIR_Type_get_limits(datatype, &lb, &ub);
  		m_extent = ub - lb;

		MPIR_ALLOC(buffer,
				(void *)MALLOC(m_extent * count),
				comm_coll, MPI_ERR_EXHAUSTED,
           			"MPI_ALLREDUCE");

		buffer = (void *)((char *)buffer - lb);
	}
	else
		buffer = sendbuf;

	/* Lock for collective operation */
	MPID_THREAD_LOCK(comm_coll->ADIctx,comm_coll);

	/* -- Algorithm for inter_Allreduce --
	 * - Perform MPI_Reduce (intra_reduce) on the respective
	 *   groups with the same sendbuf and rooted at the
	 *   designated "local" root for each group. Note that
	 *   all the processes OTHER THAN the root can pass in the
	 *   same recvbuf, but with no effect, as the recvbuf will
	 *   be significant only at the designated root.
	 *   NOTE: For the above operation, we need to allocate a
	 *         temporary buffer at each of the two "designated"
	 *         roots. Earlier, the plan was to reuse the recvbuf
	 *         but then it is impossible to exchange the results
	 *         without using sendbuf as a temporary buffer. So,
	 *         we need to allocate another buffer so as to keep
	 *         the sendbuf unaffected.
	 * - At this point, the designated roots have the result of
	 *   the reduction of their local groups. Just send this result
	 *   over to the designated root on the other side using
	 *   MPI_Sendrecv(). Apparently, there seems to be NO issue
	 *   with mismatch of the send and recv buffers as the count
	 *   and datatype are common. Further, the problem of checking
	 *   if the data is contiguous or not rests with the underlying
	 *   implementation of the intra_Allreduce, NOT here (?).
	 * - Now the results are in the correct groups - but just at the
	 *   designated root. So locally do an intra_Bcast()
	 */

	/* The following MPI_Reduce and MPI_Bcast should invoke
	 * the corresponding "intra" versions
	 */
	if(comm_size > 1)
	{
		mpi_errno = MPI_Reduce(sendbuf, buffer,
					count, datatype->self, op,
					DESIGNATED_LOCAL_ROOT,
					comm_local);
	}
	if(my_rank == DESIGNATED_LOCAL_ROOT)
	{
		/* Send to AND get from the other side */
		MPI_Group_translate_ranks(comm_coll->group->self,
					   1, &remote_root,
					    intra_group, &dest_rank);

		mpi_errno = MPI_Sendrecv(buffer, count, datatype->self,
						dest_rank,
						MPI_INTER_ALLREDUCE_PT_TAG,
						recvbuf, count, datatype->self,
				    		dest_rank,
						MPI_INTER_ALLREDUCE_PT_TAG,
				     		comm_intra, &status);
	}		
	if(comm_size > 1)
	{
		mpi_errno = MPI_Bcast(recvbuf, count, datatype->self,
					 DESIGNATED_LOCAL_ROOT, comm_local);
	}
                 
	/* Unlock to end collective operation */
	MPID_THREAD_UNLOCK(comm_coll->ADIctx,comm_coll);

	if(my_rank == DESIGNATED_LOCAL_ROOT && comm_size > 1)
		FREE((char *)buffer + lb);
	
	if (mpi_errno)
		return MPIR_ERROR(comm, mpi_errno, myname);
	else
		return mpi_errno;

/* End of Modifications
 * ---------------------------------
 */

}

static int inter_Reduce_scatter ( 
	void *sendbuf, 
	void *recvbuf, 
	int *recvcnts, 
	struct MPIR_DATATYPE *datatype, 
	MPI_Op op, 
	struct MPIR_COMMUNICATOR *comm )
{

/* ---------------------------------
 * Modifications by Rajkumar Venkat  
 */

	/* Communicator Attributes */
	MPI_Comm comm_local, comm_intra;
	int my_rank, dest_rank, remote_root = DESIGNATED_REMOTE_ROOT;
	int comm_size, remote_size, mpi_errno = MPI_SUCCESS;
	static char myname[] = "MPI_REDUCESCATTER";

	/* Temporary variables */
	MPI_Aint ub, lb, m_extent;	/* Buffer Allocation */
	MPI_Status status;
	MPI_Group intra_group;

	int count = 0, local_count = 0, i;
	int disp = 0;			/* Computing displacements */

	void *send_buffer;
	void *recv_buffer;
	int *displs;
        
	/* Switch to hidden collective */
	struct MPIR_COMMUNICATOR *comm_coll = comm->comm_coll;

	remote_size = comm_coll->np;

	/* Conditional checking */
#ifndef MPIR_NO_ERROR_CHECKING
	if(remote_size <= 0)
	{
		mpi_errno = MPIR_Err_setmsg(MPI_ERR_REMOTE, MPIR_ERR_REMOTE_ZERO,
                                   myname, (char *)0, (char *)0, (char *)0, remote_size);
		if (mpi_errno)
			return MPIR_ERROR(comm, mpi_errno, myname);
	}
#endif

	/* TBD: Additional Reduce-specific checks include checking if
	 * the 'Op' is valid for the 'datatype', if predefined.
	 */

	/* Get the cached local communicator */
	inter_Attr_prepare(comm_coll, &comm_local);
	MPI_Comm_size(comm_local, &comm_size);
	MPI_Comm_rank(comm_local, &my_rank);

	/* Get the underlying "safe" intraComm
	 * from the "safe" interComm. Also get its group.
	 */
	comm_intra = comm_coll->comm_coll->self;
	MPI_Comm_group(comm_intra, &intra_group);

	if(my_rank == DESIGNATED_LOCAL_ROOT)
	{
		for(i = 0; i < comm_size; i++)
			local_count += recvcnts[i];

		/* Trying to minimise buffer creations and deletions
		 * within the synchronous part
		 */

		MPIR_Type_get_limits(datatype, &lb, &ub);
		m_extent = ub - lb;

		MPIR_ALLOC(recv_buffer,
				(void *)MALLOC(m_extent * local_count),
				comm_coll, MPI_ERR_EXHAUSTED,
				"MPI_REDUCESCATTER");

		recv_buffer = (void *)((char *)recv_buffer - lb);
	}

	MPIR_ALLOC(displs,
			(int *)MALLOC(sizeof(int) * comm_size),
			comm_coll, MPI_ERR_EXHAUSTED,
			"MPI_REDUCESCATTER");

	displs[0] = 0;
	for(i = 1; i < comm_size; i++)
		displs[i] = displs[i - 1] + recvcnts[i - 1];

	/* Lock for collective operation */
	MPID_THREAD_LOCK(comm_coll->ADIctx,comm_coll);
    
	/* -- Algorithm for inter_Reduce-scatter --
	 * - If I am a "designated" root,
	 *   * I exchange (sendrecv) the "count" values between the the
	 *     "designated" roots. The value of "count" is obtained by
	 *     summing up all the local "recvcnts" (Note that the
	 *     "recvcnts" array is assumed to be identical at all
	 *     processes within the local group. Even if it is not so,
	 *     doesn't matter in this case - atleast the sum of all values
	 *     in the array should be one and the same).
	 *   * Allocate two buffers - 1) to hold the locally reduced data
	 *     2) to hold the reduced data sent from the other side
	 * - Call an intra_Bcast within the local group rooted at the
	 *   "designated" root, so that the "count" parameter is now
	 *   available at all processes
	 * - Call an intra_Reduce within the local group rooted at the
	 *   "designated" root. Use the first buffer to collect the result
	 * - If I am a "designated" root,
	 *     I exchange (sendrecv) the reduced data with the "remote" root
	 *     on the other side. I use the second buffer to receive the
	 *     reduced data.
	 * - Perform a (local) intra_Scatterv rooted at the "designated" root
	 *   to scatter the reduced data obtained from the "other side", using
	 *   the "recvcnts" array as the "sendcnts" parameter in Scatterv.
	 *   Note the displacements have to be computed appropriately
	 */

	/* The following MPI_Reduce, MPI_Bcast and MPI_Scatter 
	 * should invoke the corresponding "intra" versions
	 */

	if(my_rank == DESIGNATED_LOCAL_ROOT)
	{
		/* Send to AND get from the other side */
		MPI_Group_translate_ranks(comm_coll->group->self,
					   1, &remote_root,
					    intra_group, &dest_rank);

		mpi_errno = MPI_Sendrecv(&local_count, 1, MPI_INT,
						dest_rank,
						MPI_INTER_REDUCESCATTER_DATA_TAG,
						&count, 1, MPI_INT,
						dest_rank,
						MPI_INTER_REDUCESCATTER_DATA_TAG,
						comm_intra, &status);
	}
	
	if(comm_size > 1)
		mpi_errno = MPI_Bcast(&count, 1, MPI_INT,
					 DESIGNATED_LOCAL_ROOT, comm_local);
	
	if(comm_size > 1)
	{
		/* Buffer to hold locally reduced data */
		if(my_rank == DESIGNATED_LOCAL_ROOT)
		{
			/* We do not need a separate local buffer if there
			 * is just one process in the local group
			 */
			MPIR_ALLOC(send_buffer,
				(void *)MALLOC(m_extent * count),
				comm_coll, MPI_ERR_EXHAUSTED,
				"MPI_REDUCESCATTER");
	
			send_buffer = (void *)((char *)send_buffer - lb);
		}

		mpi_errno = MPI_Reduce(sendbuf, send_buffer,
						count, datatype->self, op,
						DESIGNATED_LOCAL_ROOT,
						comm_local);
	}
	else
		send_buffer = sendbuf;

	if(my_rank == DESIGNATED_LOCAL_ROOT)
	{
		/* Send to AND get from the other side */
		mpi_errno = MPI_Sendrecv(send_buffer, count, datatype->self,
						dest_rank,
						MPI_INTER_REDUCESCATTER_DATA_TAG,
						recv_buffer, local_count, datatype->self,
				    		dest_rank,
						MPI_INTER_REDUCESCATTER_DATA_TAG,
				     		comm_intra, &status);
	}

	/* Finally, scatter the data.
	 * Note that we are calling it even if there
	 * is only one process, since we need to move
	 * the data from recv_buffer to recvbuf...
	 */
	mpi_errno = MPI_Scatterv(recv_buffer, recvcnts, displs,
					datatype->self, recvbuf,
					recvcnts[my_rank],
					datatype->self,
					DESIGNATED_LOCAL_ROOT,
					comm_local);

	/* Unlock to end collective operation */
	MPID_THREAD_UNLOCK(comm_coll->ADIctx,comm_coll);

	/* Clean up */
	FREE(displs);
	if(my_rank == DESIGNATED_LOCAL_ROOT)
	{
		FREE((char *)recv_buffer + lb);
		if(comm_size > 1)
			FREE((char *)send_buffer + lb);
	}

	if (mpi_errno)
		return MPIR_ERROR(comm, mpi_errno, myname);

/* End of Modifications
 * ---------------------------------
 */
}


/* This operation is NOT valid on inter-communicators */
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
