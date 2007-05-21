/*
   This file contains routines that are private and unique to the ch_shmem
   implementation
 */

#include "mpid.h"
#include "mpiddev.h"
#include "mpimem.h"

/* Pointer used to store the address for eager sends */
/* void               *MPID_Eager_address; */

/* MPID_shmem is not volatile but its contents are */
MPID_SHMEM_globmem *MPID_shmem = 0;
/* LOCAL copy of some of MPID_shmem */
MPID_SHMEM_lglobmem MPID_lshmem;
int                 MPID_myid = -1;
int                 MPID_numids = 0;
/* Forward declarations */
void				MPID_SHMEM_lbarrier  (void);

/*
   Get an integer from the environment; otherwise, return defval.
 
int MPID_GetIntParameter( name, defval )
char *name;
int  defval;
{
    extern char *getenv();
    char *p = getenv( name );

    if (p) 
	return atoi(p);
    return defval;
} */

void MPID_SHMEM_init( argc, argv )
int  *argc;
char **argv;
{
    int numprocs, i;
    int cnt, j, pkts_per_proc;
    int memsize;

/* Make one process the default */

    numprocs = MPID_GetIntParameter( "MPICH_NP" , 1 );
    for (i=1; i<*argc; i++) {
	if (strcmp( argv[i], "-np" ) == 0) {
	    /* Need to remove both args and check for missing value for -np */
	    if (i + 1 == *argc) {
		fprintf( stderr, 
			 "Missing argument to -np for number of processes\n" );
		exit( 1 );
	    }
	    numprocs = atoi( argv[i+1] );
	    argv[i] = 0;
	    argv[i+1] = 0;
	    MPID_ArgSqueeze( argc, argv );
	    break;
	}
    }

    if (numprocs <= 0 || numprocs > MPID_MAX_PROCS) {
	fprintf( stderr, "Invalid number of processes (%d) invalid\n", numprocs );
	exit( 1 );
    }

/* The environment variable MPI_GLOBMEMSIZE may be used to select memsize */
    memsize = MPID_GetIntParameter( "MPI_GLOBMEMSIZE", MPID_MAX_SHMEM );

    if (memsize < sizeof(MPID_SHMEM_globmem) + numprocs * 128)
	memsize = sizeof(MPID_SHMEM_globmem) + numprocs * 128;

    p2p_init( numprocs, memsize );

    MPID_shmem = p2p_shmalloc(sizeof(MPID_SHMEM_globmem));

    if (!MPID_shmem) {
	fprintf( stderr, "Could not allocate shared memory (%d bytes)!\n",
		 sizeof( MPID_SHMEM_globmem ) );
	exit(1);
    }

/* Initialize the shared memory */

    MPID_shmem->barrier.phase = 1;
    MPID_shmem->barrier.cnt1  = numprocs;
    MPID_shmem->barrier.cnt2  = 0;
    MPID_shmem->barrier.size  = numprocs;

    p2p_lock_init( &MPID_shmem->globlock );
    cnt	      = 0;    /* allocated packets */

    MPID_shmem->globid = 0;

/* The following is rough if numprocs doesn't divide the MAX_PKTS */
/*
 * Determine the packet flush count at runtime.
 * (delay the harsh reality of resource management :-) )
 */

    for (i=0; i<numprocs; i++) {
      for (j=0; j<numprocs; j++) {
	/* setup the local copy of the addresses of objects in MPID_shmem */
        MPID_shmem->pool[i][j].head.ready = 0;
      }
    }

    MPID_numids = numprocs;
    MPID_MyWorldSize = numprocs;
/* Above this point, there was a single process.  After the p2p_create_procs
   call, there are more */
    p2p_setpgrp();

    p2p_create_procs( numprocs - 1, *argc, argv );

#if 0
     /* This is now done inside p2p_create_procs, so that the processes
      * can be ordered in the pid array
      */
    p2p_lock( &MPID_shmem->globlock );
    MPID_myid = MPID_shmem->globid++;
    p2p_unlock( &MPID_shmem->globlock );
#endif

    MPID_lshmem.mypool = MPID_shmem->pool[MPID_myid];
    for (i=0; i<MPID_numids; i++) 
      MPID_lshmem.pool[i]   = MPID_shmem->pool[i];

    MPID_MyWorldRank = MPID_myid;

}

void MPID_SHMEM_lbarrier()
{
    VOLATILE int *cnt, *cntother;

/* Figure out which counter to decrement */
    if (MPID_shmem->barrier.phase == 1) {
	cnt	     = &MPID_shmem->barrier.cnt1;
	cntother = &MPID_shmem->barrier.cnt2;
    }
    else {
	cnt	     = &MPID_shmem->barrier.cnt2;
	cntother = &MPID_shmem->barrier.cnt1;
    }

/* Decrement it atomically */
    p2p_lock( &MPID_shmem->globlock );
    *cnt = *cnt - 1;
    p2p_unlock( &MPID_shmem->globlock );
    
/* Wait for everyone to to decrement it */
    while ( *cnt ) p2p_yield();

/* If process 0, change phase. Reset the OTHER counter*/
    if (MPID_myid == 0) {
	/* Note that this requires that these operations occur
	   in EXACTLY THIS ORDER */
	MPID_shmem->barrier.phase = ! MPID_shmem->barrier.phase;
	p2p_write_sync();
	*cntother = MPID_shmem->barrier.size;
    }
    else 
	while (! *cntother) p2p_yield();
}

void MPID_SHMEM_finalize()
{
    VOLATILE int *globid;

    fflush(stdout);
    fflush(stderr);

/* There is a potential race condition here if we want to catch
   exiting children.  We should probably have each child indicate a successful
   termination rather than this simple count.  To reduce this race condition,
   we'd like to perform an MPI barrier before clearing the signal handler.

   However, in the current code, MPID_xxx_End is called after most of the
   MPI system is deactivated.  Thus, we use a simple count-down barrier.
   Eventually, we the fast barrier routines.
 */
    MPID_SHMEM_lbarrier();
    p2p_clear_signal();

/* Wait for everyone to finish 
   We can NOT simply use MPID_shmem->globid here because there is always the 
   possibility that some process is already exiting before another process
   has completed starting (and we've actually seen this behavior).
   Instead, we perform an additional barrier (but not an MPI barrier, because
   it is too late).
*/
    MPID_SHMEM_lbarrier();
    p2p_cleanup();
}

/* 
  Read an incoming control message.
 */
/* #define BACKOFF_LMT 1048576 */
#define BACKOFF_LMT 1024
int MPID_SHMEM_ReadControl( pkt, size, from, is_blocking )
MPID_PKT_T **pkt;
int        size, *from;
MPID_BLOCKING_TYPE is_blocking;
{
    register int          backoff, cnt, i, n;
    register volatile int *ready;
    register MPID_PKT_T   *tpkt;

    backoff = 1;
    n       = MPID_numids;
    tpkt    = (MPID_PKT_T *) MPID_lshmem.mypool;
    while (1) {
      for (i=0; i<n; i++) {
	/*	fprintf( stderr, "[%d] testing [%d] = %d\n", MPID_MyWorldRank,
		 i, tpkt[i].head.ready ); */
	ready = &tpkt[i].head.ready;
	if (MPID_PKT_READY_IS_SET(ready)){
	  *from = i;
	  *pkt  = tpkt + i;
	  MPID_TRACE_CODE_PKT("Readpkt",i,(*pkt)->head.mode);
	  /*	  fprintf( stderr, "[%d] read packet from %d\n", MPID_MyWorldRank, i ); */
	  return MPI_SUCCESS;
	}
      }
      /* If nonblocking and nothing found, return 1 */
      if (!is_blocking) return 1;
      cnt	    = backoff;
      while (cnt--) ;
      backoff = 2 * backoff;
      if (backoff > BACKOFF_LMT) backoff = BACKOFF_LMT;
      p2p_yield();
    }

    return MPI_SUCCESS;
}

int MPID_SHMEM_SendControl( pkt, size, dest )
MPID_PKT_T *pkt;
int        size, dest;
{
  VOLATILE int *destready = &MPID_lshmem.pool[dest][MPID_myid].head.ready;
  int backoff, cnt;

  /* Place the actual length into the packet */
  /* pkt->head.size = size; */ /* ??? */ 
  MPID_TRACE_CODE_PKT("Sendpkt",dest,pkt->head.mode);

  /*  fprintf( stderr, "[%d] dest ready flag is %d\n", 
	   MPID_MyWorldRank, *destready );*/
  backoff = 1;
  if (MPID_PKT_READY_IS_SET(destready)) {
    while (MPID_PKT_READY_IS_SET(destready)) {
      cnt = backoff;
      while (cnt--);
      backoff = 2 * backoff;
      if (backoff > BACKOFF_LMT) backoff = BACKOFF_LMT;
      MPID_DeviceCheck( MPID_NOTBLOCKING );
      if (backoff > 8)
	p2p_yield();
    }
  }
  /* Force ready == 0 until we actually do the set; this does NOT need
     to be memory synchronous */
  pkt->head.ready = 0;
  MPID_PKT_COPYIN( (void *)&MPID_lshmem.pool[dest][MPID_myid], pkt, size );
  MPID_PKT_READY_SET(destready);

  return MPI_SUCCESS;
}

/* 
   Return the address the destination (dest) should use for getting the 
   data at in_addr.  len is INOUT; it starts as the length of the data
   but is returned as the length available, incase all of the data can 
   not be transfered 
 */
void * MPID_SetupGetAddress( in_addr, len, dest )
void *in_addr;
int  *len, dest;
{
    void *new;
    int  tlen = *len;

    MPID_TRACE_CODE("Allocating shared space",len);
/* To test, just comment out the first line and set new to null */
    new = p2p_shmalloc( tlen );
/* new = 0; */
    if (!new) {
	tlen = tlen / 2; 
	while(tlen > 0 && !(new = p2p_shmalloc(tlen))) 
	    tlen = tlen / 2;
	if (tlen == 0) {
	    p2p_error( "Could not get any shared memory for long message!",0 );
	    exit(1);
	}
	/* fprintf( stderr, "Message too long; sending partial data\n" ); */
	*len = tlen;
    }
#ifdef FOO
/* If this mapped the address space, we wouldn't need to copy anywhere */
/*
if (MPID_DEBUG_FILE) {
    fprintf( MPID_DEBUG_FILE, 
	    "[%d]R About to copy to %d from %d (n=%d) (%s:%d)...\n", 
	    MPID_MyWorldRank, new, in_addr, tlen, 
	    __FILE__, __LINE__ );
    fflush( MPID_DEBUG_FILE );
    }
 */

    MEMCPY( new, in_addr, tlen );
#endif

    MPID_TRACE_CODE("Allocated space at",(long)new );
    return new;
}

void MPID_FreeGetAddress( addr )
void *addr;
{
    MPID_TRACE_CODE("Freeing space at",(long)addr );
    p2p_shfree( addr );
}

/*
 * Debugging support
 */
void MPID_SHMEM_Print_internals( fp )
FILE *fp;
{}
