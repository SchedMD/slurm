#include "mpiimpl.h"
#include "mpimem.h"
/* pt2pt for MPIR_Type_get_limits */
#include "mpipt2pt.h"
#include "coll.h"
#include "mpiops.h"


/* This is the default implementation of scan. The algorithm is:
   
   Algorithm: MPI_Scan

   We use a lgp recursive doubling algorithm. The basic algorithm is
   given below. (You can replace "+" with any other scan operator.)
   The result is stored in recvbuf.

   recvbuf = sendbuf;
   partial_scan = sendbuf;
   mask = 0x1;
   while (mask < size) {
      dst = rank^mask;
      if (dst < size) {
         send partial_scan to dst;
         recv from dst into tmp_buf;
         if (rank > dst) {
            partial_scan = tmp_buf + partial_scan;
            recvbuf = tmp_buf + recvbuf;
         }
         else {
            if (op is commutative)
               partial_scan = tmp_buf + partial_scan;
            else {
               tmp_buf = partial_scan + tmp_buf;
               partial_scan = tmp_buf;
            }
         }
      }
      mask <<= 1;
   }  

   End Algorithm: MPI_Scan
*/



int MPIR_intra_Scan ( void *sendbuf, void *recvbuf, int count, 
		      struct MPIR_DATATYPE *datatype, MPI_Op op, 
		      struct MPIR_COMMUNICATOR *comm )
{
  MPI_Status status;
  int        rank, size;
  int        mpi_errno = MPI_SUCCESS;
  MPI_User_function   *uop;
  struct MPIR_OP *op_ptr;
  int mask, dst; 
  MPI_Aint extent, lb;
  void *partial_scan, *tmp_buf;
  static char myname[] = "MPI_SCAN";

  if (count == 0) return MPI_SUCCESS;

  MPIR_Comm_size(comm, &size);
  MPIR_Comm_rank(comm, &rank);

  /* Switch communicators to the hidden collective */
  comm = comm->comm_coll;
 
  /* Lock for collective operation */
  MPID_THREAD_LOCK(comm->ADIctx,comm);

  op_ptr = MPIR_GET_OP_PTR(op);
  MPIR_TEST_MPI_OP(op,op_ptr,comm,myname);
  uop  = op_ptr->op;

  /* need to allocate temporary buffer to store partial scan*/
  MPI_Type_extent(datatype->self, &extent);
  MPIR_ALLOC(partial_scan,(void *)MALLOC(count*extent), comm,
             MPI_ERR_EXHAUSTED, myname);
  /* adjust for potential negative lower bound in datatype */
  MPI_Type_lb( datatype->self, &lb );
  partial_scan = (void *)((char*)partial_scan - lb);

  /* need to allocate temporary buffer to store incoming data*/
  MPIR_ALLOC(tmp_buf,(void *)MALLOC(count*extent), comm,
             MPI_ERR_EXHAUSTED, myname);
  /* adjust for potential negative lower bound in datatype */
  tmp_buf = (void *)((char*)tmp_buf - lb);

  /* Since this is an inclusive scan, copy local contribution into
     recvbuf. */
  mpi_errno = MPI_Sendrecv ( sendbuf, count, datatype->self,
                             rank, MPIR_SCAN_TAG, 
                             recvbuf, count, datatype->self,
                             rank, MPIR_SCAN_TAG,
                             comm->self, &status );
  if (mpi_errno) return mpi_errno;

  mpi_errno = MPI_Sendrecv ( sendbuf, count, datatype->self,
                             rank, MPIR_SCAN_TAG, 
                             partial_scan, count, datatype->self,
                             rank, MPIR_SCAN_TAG,
                             comm->self, &status );
  if (mpi_errno) return mpi_errno;

  mask = 0x1;
  while (mask < size) {
      dst = rank ^ mask;
      if (dst < size) {
          /* Send partial_scan to dst. Recv into tmp_buf */
          mpi_errno = MPI_Sendrecv(partial_scan, count, datatype->self,
                                   dst, MPIR_SCAN_TAG, tmp_buf,
                                   count, datatype->self, dst,
                                   MPIR_SCAN_TAG, comm->self,
                                   &status); 
          if (mpi_errno) return mpi_errno;
          
          if (rank > dst) {
              (*uop)(tmp_buf, partial_scan, &count, &datatype->self);
              (*uop)(tmp_buf, recvbuf, &count, &datatype->self);
          }
          else {
              if (op_ptr->commute)
                  (*uop)(tmp_buf, partial_scan, &count, &datatype->self);
              else {
                  (*uop)(partial_scan, tmp_buf, &count, &datatype->self);
                  mpi_errno = MPI_Sendrecv(tmp_buf, count, datatype->self,
                                           rank, MPIR_SCAN_TAG, partial_scan,
                                           count, datatype->self, rank,
                                           MPIR_SCAN_TAG, comm->self,
                                           &status); 
                  if (mpi_errno) return mpi_errno;
              }
          }
      }
      mask <<= 1;
  }
  
  FREE((char *)partial_scan+lb); 
  FREE((char *)tmp_buf+lb); 
  
  /* Unlock for collective operation */
  MPID_THREAD_UNLOCK(comm->ADIctx,comm);

  return (mpi_errno);
}



#ifdef OLD
/* 
   This is an improved and more scalable version of MPI_Scan, contributed 
   originally by Jesper Larsson Traeff <traff@ccrl-nece.technopark.gmd.de>
 */
int MPIR_intra_Scan ( void *sendbuf, void *recvbuf, int count, 
		      struct MPIR_DATATYPE *datatype, MPI_Op op, 
		      struct MPIR_COMMUNICATOR *comm )
{
  MPI_Status status;
  int        rank, size;
  int        mpi_errno = MPI_SUCCESS;
  MPI_Aint   lb, ub, m_extent;  /* Extent in memory */
  MPI_User_function   *uop;
  struct MPIR_OP *op_ptr;

  int dd; /* displacement, no of hops to send (power of 2) */
  int rr; /* "round rank" */
  void *tmpbuf = 0;

  /* Nov. 98: Improved O(log(size)) algorithm */

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

  MPIR_Op_errno = MPI_SUCCESS;

  if (rank>0) {
    /* allocate temporary receive buffer
       (needed both in commutative and noncommutative case) */
    MPIR_ALLOC(tmpbuf,(void *)MALLOC(m_extent * count),
               comm, MPI_ERR_EXHAUSTED, "Out of space in MPI_SCAN" );
    tmpbuf = (void *)((char*)tmpbuf-lb);
  }
  MPIR_COPYSELF( sendbuf, count, datatype->self, recvbuf,
                 MPIR_SCAN_TAG, rank, comm->self );

  /* compute partial scans */
  rr = rank; dd = 1;
  while ((rr&1)==1) {
    /* odd "round rank"s receive */

    mpi_errno = MPI_Recv(tmpbuf,count,datatype->self,rank-dd,
                         MPIR_SCAN_TAG,comm->self,&status);
    if (mpi_errno) return mpi_errno;
    (*uop)(tmpbuf, recvbuf, &count, &datatype->self);

    dd <<= 1; /* dd*2 */
    rr >>= 1; /* rr/2 */

    /* Invariant: recvbuf contains the scan of
       (rank-dd)+1, (rank-dd)+2,..., rank */
  }
  /* rr even, rank==rr*dd+dd-1, recvbuf contains the scan of
     rr*dd, rr*dd+1,..., rank */

  /* send partial scan forwards */
  if (rank+dd<size) {
    mpi_errno = MPI_Send(recvbuf,count,datatype->self,rank+dd,MPIR_SCAN_TAG,
                         comm->self);
    if (mpi_errno) return mpi_errno;
  }

  if (rank-dd>=0) {
    mpi_errno = MPI_Recv(tmpbuf,count,datatype->self,rank-dd,
                         MPIR_SCAN_TAG,comm->self,&status);
    if (mpi_errno) return mpi_errno;
    (*uop)(tmpbuf, recvbuf, &count, &datatype->self);
    /* recvbuf contains the scan of 0,..., rank */
  }

  /* send result forwards */
  do {
    dd >>= 1; /* dd/2 */
  } while (rank+dd>=size);
  while (dd>0) {
    mpi_errno = MPI_Send(recvbuf,count,datatype->self,rank+dd,MPIR_SCAN_TAG,
                         comm->self);
    if (mpi_errno) return mpi_errno;
    dd >>= 1; /* dd/2 */
  }

  if (rank>0) {
    /* free temporary receive buffer */
    FREE((char*)tmpbuf+lb);
  }

  /* If the predefined operation detected an error, report it here */
  if (mpi_errno == MPI_SUCCESS && MPIR_Op_errno)
      mpi_errno = MPIR_Op_errno;

  /* Unlock for collective operation */
  MPID_THREAD_UNLOCK(comm->ADIctx,comm);

  return(mpi_errno);
}
#endif
