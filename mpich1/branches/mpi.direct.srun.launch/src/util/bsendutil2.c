/*
 *  $Id: bsendutil2.c,v 1.14 2002/11/27 19:58:10 gropp Exp $
 *
 *  (C) 1993, 1996 by Argonne National Laboratory and 
 *      Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 *
 * The handling of nonblocking bsend operations needs some work.  Currently,
 * There is a single request for a nonblocking bsend operation, and this can
 * cause problems when we try to complete a nonblocking bsend operation, becase
 * both we and the user may have a copy of the same request.  
 *
 * The solution to this is a little complicated.  Note that the MPI standard
 * requires that you can free an active request (just like the other MPI
 * objects, freeing an object just decrements its reference count; anything
 * that makes an object "active" increments its reference count).  
 * So, one solution is to implement this reference count, and then make
 * use of it here (so that MPI_TEST will execute the Free and set the 
 * pointer to NULL, but the actual free won't happen until the ref count is
 * set to zero).
 *
 * But to really do this, we need have some way to complete a nonblocking
 * operation even though the user will never again call it with a WAIT
 * or TEST call.  
 *
 * As a short term fix, we ONLY call MPI_TEST in this code for blocking
 * BSENDs; this is safe, because the ONLY copy of the request is here.
 * Thus, the test on whether to check a request includes a check on the
 * blocking nature.  Note also that the routine called to free a request
 * calls a special routine (MPIR_BufferFreeReq), so we can keep the
 * information here properly updated.
 *
 * Another approach, which I discussed with Hubertus, would be to alloc a
 * new request, have the buffer point at that, and copy all of the relavent
 * details into the given buffer.
 *
 * The "best" thing to do depends on how you interpret the various flavors
 * of buffered send:
 *    Method 1.  Bsend, Ibsend, and Bsend_init/Start all copy the data
 *    into a buffer; when the data is copied, the routines return.  In this
 *    case, both Ibsend and Bsend_init/Start should indicate that the 
 *    send has completed, since the data INPUT to these routines has 
 *    been copied and my now be re-used.  (There is, thank goodness, no
 *    Ibs(ync)send).  Note that in this case, the user's request and the
 *    internal request are VERY different.
 *    
 *    Method 2.  Ibsend and Bsend_init would not complete coping data into
 *    the buffer until a later time.  This may be intended for systems with
 *    special move engines that operate asynchronously; some mechanism
 *    would be required to determine completion.  
 *
 * My choice is to copy the request and mark the "users" request as completed
 * when the data has been moved.
 */

#include "mpiimpl.h"

/* #define DEBUG_BSEND */

#ifdef DEBUG_BSEND     /* #DEBUG_BSEND_START# */ 
static int DebugBsend = 0;
#define DEBUG_PRINT(str) PRINTF( "%s\n", str );
#else
#define DEBUG_PRINT(str) 
#endif                 /* #DEBUG_BSEND_END# */

#include "reqalloc.h"
#ifndef MEMCPY
#define MEMCPY(a,b,n) memcpy(a,b,n)
#endif

/* 
   This file contains the code for managing the "Buffered" sends (with 
   a user-provided buffer).  This uses the simple buffer scheme described 
   in the MPI standard.

   Because the data in this list is sensitive, and because we could easily
   overwrite the data if we are not careful, I've added "Cookies" around the
   data.
 */
#define BSEND_HEAD_COOKIE 0xfea7600d
#define BSEND_TAIL_COOKIE 0xcadd5ac9
typedef struct _bsenddata {
    long              HeadCookie;
    struct _bsenddata *next, *prev;
    MPI_Request       req;             /* This is the actual request that
					  is used to send the message; 
					  note that this is a POINTER to the
					  appropriate structure.  It is
					  ALSO not the user's request,
					  in the case that a nonblocking
					  buffered send is used. */
    /* area to use */
    int               len;
    void              *buf;
    long              TailCookie;
    } BSendData;

/* If "req" is null, the block is not in use */
static BSendData *Bsend = 0;
static int BsendSize = 0;

static BSendData *MPIR_MergeBlock ( BSendData *);
static int MPIR_BsendAlloc (int, BSendData **);
static void MPIR_BsendCopyData (BSendData *,
					 struct MPIR_COMMUNICATOR *,
					 void *, 
					 int,
					 struct MPIR_DATATYPE *,
					 void **,
					 int *);
#ifdef DEBUG_BSEND     /* #DEBUG_BSEND_START# */
static int MPIR_BsendBufferPrint( );
#endif                 /* #DEBUG_BSEND_END# */


/*
 * The algorithm and the routines.
 * The basic operation is started by MPI_Ibsend.  A MPI_Bsend just does
 * MPI_Ibsend and MPI_Wait.  These call
 *
 *    MPIR_BsendInitBuffer( )  - to initialize bsend buffer
 *    MPIR_BsendRelease( ) - to release bsend buffer (first completing
 *                           all communication).
 *    MPIR_IbsendDatatype( ) - to buffer a message and begin sending it
 *
 * Internal routines for buffer management are
 *    MPIR_TestBufferPtr - Tests that bsend arena pointer is ok
 *    MPIR_BsendBufferPrint - prints out the state of the buffer
 *    MPIR_BsendAlloc( ) - to allocate space for the bsend buffer for a 
 *             Ibsend/Bsend_init, as well as the request that will
 *             be used internally.  this routine also frees up buffers
 *             once the send has completed.
 *    MPIR_BsendCopyData( ) - Copies data from user area into previously
 *                            allocated bsend area.
 */

/*
   MPIR_SetBuffer - Set the buffer area for the buffered sends, and 
   initialize the internal data structures
 */
int MPIR_BsendInitBuffer( void *bufp, int size )
{
    BSendData *p;

    DEBUG_PRINT("Starting MPIR_BsendInitBuffer");
    if (size < sizeof(BSendData)) 
	return MPIR_Err_setmsg( MPI_ERR_OTHER, MPIR_ERR_BUFFER_TOO_SMALL, 
				(char *)0, 
				(char *)0, (char *)0, sizeof(BSendData) );
    if (Bsend)
	return MPIR_ERRCLASS_TO_CODE(MPI_ERR_BUFFER,MPIR_ERR_BUFFER_EXISTS);
    p	  = (BSendData *)bufp;
#ifdef DEBUG_BSEND     /* #DEBUG_BSEND_START# */
    if (DebugBsend) 
	FPRINTF( stderr, "Initializing buffer to %d bytes at %lx\n", size, 
		 (long) p );
#endif                 /* #DEBUG_BSEND_END# */
    p->next	      = 0;
    p->prev	      = 0;
    p->req	      = 0;
    p->len	      = size - sizeof(BSendData);
    p->HeadCookie = BSEND_HEAD_COOKIE;
    p->TailCookie = BSEND_TAIL_COOKIE;
    BsendSize     = size;
    Bsend	      = p;

    DEBUG_PRINT("Exiting MPIR_BsendInitBuffer" );
    return MPI_SUCCESS;
}

/*
    Tests that a buffer area has not been corrupted by checking sentinals
    at the head and tail of a buffer area.
 */
#define MPIR_TestBufferPtr( b ) \
    (((b)->HeadCookie != BSEND_HEAD_COOKIE || \
	    (b)->TailCookie != BSEND_TAIL_COOKIE))

/* 
   Free a buffer (MPI_BUFFER_DETACH).  Note that this will wait to
   complete any pending operations.

   This routine is called by MPI_Finalize to make sure than any pending
   operations are completed.

   When called, it returns the current buffer and size in its arguments
   (both are output).
 */
int MPIR_BsendRelease( 
	void **buf, 
	int *size )
{
    BSendData *p = Bsend;
    MPI_Status status;
    int        mpi_errno;

    DEBUG_PRINT("Entering MPIR_BsendRelease");
/* If we are using the buffer, we must first wait on all pending messages */
    while (p) {
	if (MPIR_TestBufferPtr(p)) {
	    /* Error in pointer */
	    mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, MPIR_ERR_BSEND_CORRUPT,
					 (char *)0, (char *)0, (char *)0, 
					 "FreeBuffer" );
	    return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, (char *)0 );
	}
	if (p->req) {
#ifdef DEBUG_BSEND     /* #DEBUG_BSEND_START# */
	    if (DebugBsend) 
		FPRINTF( stderr, 
		     "Waiting for release of buffer at %lx with request %lx\n",
			     (long) p, (long)p->req );
#endif                 /* #DEBUG_BSEND_END# */
		MPI_Wait( &p->req, &status );
	    }
	    p = p->next;
	}
    /* Note that this works even when the buffer does not exist */
    *buf	  = (void *)Bsend;
    *size	  = BsendSize;
    Bsend	  = 0;
    BsendSize = 0;
    DEBUG_PRINT("Exiting MPIR_BsendRelease");
    return MPI_SUCCESS;
}

/* 
   This is an internal routine for merging bsend buffer blocks.
   Merge b with any previous or next empty blocks.  Return the block to use
   next 
*/
static BSendData *MPIR_MergeBlock( BSendData *b )
{
    BSendData *tp, *nextb;
    int mpi_errno;

    DEBUG_PRINT("Entering MPIR_MergeBlock" );
    nextb = b;
    tp    = b->prev;
    if (tp && MPIR_TestBufferPtr(tp)) {
	/* Error in pointer */
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, MPIR_ERR_BSEND_CORRUPT,
				     (char *)0, (char *)0, (char *)0, 
				     "MergeBlock" );
	(void)MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, (char *)0 );
	return 0;
    }

    if (tp && tp->req == MPI_REQUEST_NULL) {
	/* Merge with previous block */
#ifdef DEBUG_BSEND     /* #DEBUG_BSEND_START# */
	if (DebugBsend) 
	    FPRINTF( stderr, "Merging block at %lx with next block\n", 
		     (long)tp );
#endif                 /* #DEBUG_BSEND_END# */
	tp->next = b->next;
	if (b->next) b->next->prev = tp;
	tp->len += b->len + sizeof(BSendData);
	b	     = tp;
	nextb    = b;
    }
    tp = b->next;
    if (tp && MPIR_TestBufferPtr(tp)) {
	/* Error in pointer */
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, MPIR_ERR_BSEND_CORRUPT,
					 (char *)0, (char *)0, (char *)0, 
					 "MergeBlock" );
	(void)MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, (char *)0 );
	return 0;
    }
    if (tp && tp->req == MPI_REQUEST_NULL) {
	/* Merge with next block */
#ifdef DEBUG_BSEND     /* #DEBUG_BSEND_START# */
	if (DebugBsend) 
	    FPRINTF( stderr, 
		     "Merging block at %lx with previous block at %lx\n", 
		     (long)tp, (long)b );
#endif                 /* #DEBUG_BSEND_END# */
	b->next = tp->next;
	if (tp->next) tp->next->prev = b;  
	b->len += tp->len + sizeof(BSendData);
    }
    DEBUG_PRINT("Exiting MPIR_MergeBlock");
    return nextb;
}

/* 
   The input to this routine is a size (in bytes) and an already created
   MPI_Request; the output is a pointer to the allocated buffer space.
   It also holds all of the information needed to pack the data, in 
   the event that this is a persistent, non-blocking, buffered send (!).

   Note that this must be called ONLY after all other fields in the 
   incoming request are set.  This routine will modify the request
   by marking it as completed.
 */
static int MPIR_BsendAlloc( 
	int size, 
	BSendData ** bp )
{
    BSendData  *b, *new;
    int        flag;
    MPI_Status status;
    int        mpi_errno;

    DEBUG_PRINT("Entering MPIR_BsendAlloc");
/* Round size to a multiple of 8 */
    if (size & 0x7) size += (8 - (size & 0x7));
    do {
	b = Bsend;
	while (b) {
	    if (MPIR_TestBufferPtr(b)) {
		/* Error in pointer */
		mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, 
					     MPIR_ERR_BSEND_CORRUPT,
					     (char *)0, (char *)0, (char *)0, 
					     "BsendAlloc" );
		return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, (char *)0 );
	    }
	    /* Note that since the request in the bsend data is private, we can
	       always execute this test */
	    if (b->req != MPI_REQUEST_NULL)
	    {
		/* Test for completion; merge if necessary.  If the request
		   is not active, we don't do the test. */
#ifdef DEBUG_BSEND     /* #DEBUG_BSEND_START# */
		if (DebugBsend)
		    FPRINTF(stderr, "Testing for completion of block at %lx\n",
			    (long)b );
#endif                 /* #DEBUG_BSEND_END# */
		MPI_Test( &b->req, &flag, &status );
		/* If completed and not persistant, remove */
		if (flag && !b->req) {
#ifdef DEBUG_BSEND     /* #DEBUG_BSEND_START# */
		    if (DebugBsend)
			FPRINTF( stderr, "Found completed bsend\n" );
#endif                 /* #DEBUG_BSEND_END# */
		    /* Done; merge the blocks and test again */
		    b = MPIR_MergeBlock( b );
		    continue;
		}
	    }
	    if (b->req == MPI_REQUEST_NULL) {
		/* Try to merge with surrounding blocks */
		b = MPIR_MergeBlock( b );
	    }
	    if (b->req == MPI_REQUEST_NULL && b->len >= size) {
		MPIR_SHANDLE *shandle;
		/* Split the block if there is enough room */
		if (b->len > size + (int)sizeof(BSendData) + 8) {
#ifdef DEBUG_BSEND     /* #DEBUG_BSEND_START# */
		    if (DebugBsend)
			FPRINTF( stderr, 
				 "Found large block of size %d "
				 "(need %d) at %lx\n",
				 b->len, size, (long)b );
#endif                 /* #DEBUG_BSEND_END# */
		    new	  = (BSendData *)(((char *)b) + 
					  sizeof(BSendData) + size);
		    new->next = b->next;
		    if (b->next) b->next->prev = new;
		    new->prev		= b;
		    b->next		= new;
		    new->len		= b->len - size - sizeof(BSendData);
		    new->req		= MPI_REQUEST_NULL;
		    new->HeadCookie	= BSEND_HEAD_COOKIE;
		    new->TailCookie	= BSEND_TAIL_COOKIE;
		    b->len		= size;
		}
#ifdef DEBUG_BSEND     /* #DEBUG_BSEND_START# */
		if (DebugBsend)
		    FPRINTF( stderr, 
			     "Creating bsend block at %lx of size %d\n", 
			     (long)b, size );
#endif                 /* #DEBUG_BSEND_END# */
		/* Create a local request to use */
		/* BUG - This should be allocated in place */
		MPID_SendAlloc(shandle);
		if (!shandle) return MPI_ERR_EXHAUSTED;
		b->req = (MPI_Request)shandle;
		MPID_Request_init( shandle, MPIR_SEND );
		
		/* Save the buffer address */
		b->buf = (void *)(b+1);

		/* return the location of the new buffer */
		*bp = b;

		DEBUG_PRINT("Exiting MPIR_BsendAlloc");
		return MPI_SUCCESS;
	    }
	    b = b->next;
	}
    } while (MPID_DeviceCheck( MPID_NOTBLOCKING ) != -1);
    /* Formally, we don't need the DeviceCheck here; it is the user's 
       responsibility to provide enough buffering.  However, doing this
       gives us a better chance that user's program will run anyway, and
       since the program is erroneous if we get here, the behavior is
       up to the implementation.  We try to be nice to the user. */
#ifdef DEBUG_BSEND     /* #DEBUG_BSEND_START# */
    FPRINTF( stdout, "Could not find %d bytes in buffer\n", size );
    MPIR_BsendBufferPrint();
#endif                 /* #DEBUG_BSEND_END# */
    DEBUG_PRINT("Exiting MPIR_BsendAlloc");
    return MPIR_ERRCLASS_TO_CODE(MPI_ERR_BUFFER,MPIR_ERR_USER_BUFFER_EXHAUSTED);
}

/* 
   This routine actually transfers the data from the users buffer to the
   internal buffer.  A bsend area must already exist for it, and be
   marked by bine set in the rq->bsend field (see the MPIR_SHANDLE structure).
 */
static void MPIR_BsendCopyData( 
	BSendData * b, 
	struct MPIR_COMMUNICATOR *comm_ptr, 
	void *buf, 
	int count, 
	struct MPIR_DATATYPE *dtype_ptr, 
	void **bsend_buf, 
	int *bsend_len )
{
    int          outcount, position = 0;
    int          mpi_errno;

    DEBUG_PRINT("Entering MPIR_BsendCopyData");
    if (!b) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, MPIR_ERR_BSEND_DATA,
				     (char *)0, "Error in BSEND data",
				     (char *)0 );
	MPIR_ERROR( comm_ptr, mpi_errno, (char *)0 );
	return;
    }
    if (MPIR_TestBufferPtr(b)) {
	/* Error in pointer */
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, MPIR_ERR_BSEND_CORRUPT,
				     (char *)0, (char *)0, (char *)0, 
				     "BsendCopyData" );
	MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, (char *)0 );
	return;
    }
    outcount   = b->len;
    MPI_Pack( buf, count, dtype_ptr->self, b->buf, outcount, &position, 
	      comm_ptr->self );
    *bsend_buf = b->buf;
/* The number of bytes actually taken is returned in position */
    *bsend_len = position;

    /* Consistency tests */
    if (MPIR_TestBufferPtr(b)) {
	/* Error in pointer after we've packed into it */
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, MPIR_ERR_BSEND_PREPARE,
				     (char *)0, 
    "Error in BSEND data, corruption detected at end of PrepareBuffer",
				     (char *)0 );
	MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, (char *)0 );
    }
    if (b->next && MPIR_TestBufferPtr(b->next)) {
	/* Error in pointer after we've packed into it */
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, 
				     MPIR_ERR_BSEND_PREPAREDATA,
				     (char *)0, 
    "Error in BSEND data, corruption detected at data end of PrepareBuffer",
				     (char *)0 );
	MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, (char *)0 );
    }
    DEBUG_PRINT("Exiting MPIR_PrepareBuffer");
}

#ifdef DEBUG_BSEND     /* #DEBUG_BSEND_START# */
/* 
 * This is a debugging routine 
 */
static int MPIR_BsendBufferPrint( )
{
    BSendData *b;
    int       mpi_errno;

    FPRINTF( stdout, "Printing buffer arena\n" );
    b = Bsend;
    while (b) {
	if (MPIR_TestBufferPtr(b)) {
	    /* Error in pointer */
	    mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, MPIR_ERR_BSEND_CORRUPT,
					 (char *)0, (char *)0, (char *)0, 
					 "PrintBuffer" );
	    return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, (char *)0 );
	}
	FPRINTF( stdout, "%lx : len = %d, req = %lx\n", (long)b, b->len, 
		 (long)(b->req) );
	b = b->next;
    }
    FPRINTF( stdout, "End of printing buffer arena\n" );
    return 0;
}
#endif                 /* #DEBUG_BSEND_END# */

/* This routine is called by MPI_Start to start an persistent bsend.
   The incoming requests is the USERS request
 */
void MPIR_IbsendDatatype( 
	struct MPIR_COMMUNICATOR *comm_ptr, 
	void *buf, 
	int count, 
	struct MPIR_DATATYPE *dtype_ptr, 
	int src_lrank, 
	int tag, 
	int context_id, 
	int dest_grank, 
	MPI_Request request, 
	int *error_code )
{
    int         bsend_len;
    void        *bsend_buf;
    int		psize;
    int         mpi_errno = MPI_SUCCESS;
    BSendData * b;

    /* Trivial case first */
    if (dest_grank == MPI_PROC_NULL) {
	(request)->shandle.is_complete = 1;
	*error_code = MPI_SUCCESS;
	return;
    }

    /* Allocate space in buffer */
    MPI_Pack_size( count, dtype_ptr->self, comm_ptr->self, &psize );
    mpi_errno = MPIR_BsendAlloc( psize, &b );
    if (mpi_errno)
    {
	*error_code = MPIR_ERROR( comm_ptr, mpi_errno, (char *)0 );
	goto fn_exit;
    }

    /* Pack data as necessary into buffer */
    MPIR_BsendCopyData( b, comm_ptr, buf, count, dtype_ptr,
			&bsend_buf, &bsend_len );

    /* use ISendContig to send the message - note: request was already
       initialized in MPIR_BsendAlloc() */
    MPID_IsendDatatype( comm_ptr, bsend_buf, bsend_len, MPIR_PACKED_PTR, 
			src_lrank, tag, context_id, dest_grank, 
			b->req, &mpi_errno );
    if (mpi_errno) {
	*error_code = MPIR_ERROR( comm_ptr, mpi_errno, (char *)0 );
    }

  fn_exit:
    request->shandle.is_complete = 1;
}
