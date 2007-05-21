/* 	$Id: coll.h,v 1.4 2000/02/26 14:57:39 gropp Exp $	 */

#ifndef MPIR_MIN

/*

coll.h - Defines used for point to point communications within
         collective operations.

*/
#include "mpicoll.h"

/* Various operations */
#ifndef MPIR_MIN
#define MPIR_MIN(a,b) (((a)>(b))?(b):(a))
#endif
#ifndef MPIR_MAX
#define MPIR_MAX(a,b) (((b)>(a))?(b):(a))
#endif

/* One common case is copy-to-self.  A PORTABLE way to do this is to use
   MPI_Sendrecv; however, the routine MPIR_Pack2 (in src/dmpi) provides the
   same operation for general datatypes.  To make it easy to switch between
   these two approaches, we define MPIR_COPYSELF
   Note that we provide a tag/comm/rank argument for the sendrecv case.
   
   This only works for when the send and receive types are the same.
   We will require something different (a combination of MPIR_Pack2 and
   MPIR_Unpack2, perhaps?) for the data-movement operations (e.g., MPI_Gather).
   
   In addition, the pack/unpack routines can be used only for contiguous
   copies, in which case we can just use memcpy.  But in that case, we
   need to handle non-contiguous data.  We should probably introduce a
   memcpy( dest, src, count, datatype ) routine for this...
 */ 
#if 1 || defined(MPIR_USE_SENDRECV)
#define MPIR_COPYSELF( src, count, datatype, dest, tag, rank, comm ) \
{MPI_Status _status;\
mpi_errno = MPI_Sendrecv ( (void *)(src), count, datatype, rank, tag, \
	       (void *)(dest), count, datatype, rank, tag, comm, &_status );}
#else
#define MPIR_COPYSELF( src, count, datatype, dest, tag, rank, comm ) \
{int _outlen, _totlen; \
mpi_errno = MPIR_Pack2( src, count, maxcount, datatype, \
			(int (*) (unsigned char *, unsigned char *, \
					    MPI_Datatype, int, void *))0, \
			(void*)0, dest, &_outlen, &_totlen );}

#endif

/* 
   Block sizes for various collective operations
   
   For most systems, a size of 1 is optimal.  The claim has been made that
   for the SP1, 3 is better.  Experiments disagree.
   
   NOTE THAT THIS MUST BE DISABLED FOR HETEROGENEOUS SYSTEMS
 */
#define MPIR_BCAST_BLOCK_SIZE 1

/* 
 * Unfortunately, the MPI_Op's are declared as returning void rather than
 * an error code.  Thus, it isn't obvious how to return an error.  Our
 * solution is to use an external int, MPIR_Op_errno.  This IS NOT 
 * THREAD SAFE, so it will need to be setup as thread-private in a 
 * multithreaded implementation.
 */
extern int MPIR_Op_errno;

/* Tags for point to point operations which implement collective operations */
#define MPIR_BARRIER_TAG               1
#define MPIR_BCAST_TAG                 2
#define MPIR_GATHER_TAG                3
#define MPIR_GATHERV_TAG               4
#define MPIR_SCATTER_TAG               5
#define MPIR_SCATTERV_TAG              6
#define MPIR_ALLGATHER_TAG             7
#define MPIR_ALLGATHERV_TAG            8
#define MPIR_ALLTOALL_TAG              9
#define MPIR_ALLTOALLV_TAG            10
#define MPIR_REDUCE_TAG               11
#define MPIR_USER_REDUCE_TAG          12
#define MPIR_USER_REDUCEA_TAG         13
#define MPIR_ALLREDUCE_TAG            14
#define MPIR_USER_ALLREDUCE_TAG       15
#define MPIR_USER_ALLREDUCEA_TAG      16
#define MPIR_REDUCE_SCATTER_TAG       17
#define MPIR_USER_REDUCE_SCATTER_TAG  18
#define MPIR_USER_REDUCE_SCATTERA_TAG 19
#define MPIR_SCAN_TAG                 20
#define MPIR_USER_SCAN_TAG            21
#define MPIR_USER_SCANA_TAG           22

#endif
