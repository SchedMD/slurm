/*
   This file contains routines that are private and unique to the Cenju3
   implementation
 */

#define MALLOC malloc
#define FREE   free

#include "mpid.h"
#include "shpackets.h"
#include "mpid_debug.h"

void  *MPID_CENJU3_malloc ();
void   MPID_CENJU3_free ();
double MPID_Cenju3_Time ();
char  *MPID_CENJU3_Get_Stack();

/*
   MPID_myid       : Process id of current process
   MPID_numids     : Numper of processes
   MPID_mypool     : Pool to receive packets in current process
   MPID_mypackets  = & (MPID_mypool[MPID_myid])
   MPID_destready  : Flags of receiving processes indicating
                     whether packets or eager buffers are free or used.
   MPID_eager_pool : Pool for eager messages
                     (maximal length = MPID_BUF_EAGER_MAX_DATA_SIZE)
   MPID_ready_pkt_to_clr: Address of the ready word to clear
                          after the packet was read.
   MPID_next_pkt_to_read : Index of the next packet to be read
                           for each processor.
*/

int MPID_myid;
int MPID_numids;
char                  *MPID_ready_pkt_to_clr;

static int            *MPID_next_pkt_to_read;
static MPID_POOL_T    *MPID_mypool, *MPID_mypackets;

MPID_DEST_READY       *MPID_destready;
char                  **MPID_eager_pool;

void MPID_CENJU3_Init( argc, argv )
int *argc;
char ***argv;
{
    register int i;
    register void *ptr;

    CJprocinfo (&MPID_numids);
    MPID_myid = CJfork (MPID_numids);

    MPID_MyWorldRank = MPID_myid;
    MPID_MyWorldSize = MPID_numids;

    /* Sycnronize the the clock to get global timings */

    CJbarrier (NULL);
    MPID_Cenju3_Time ();

    /* Allocate for each process :
       (i)  Own pool for eager_messages and packets.
       (ii) Vector of flags indicating the status of the pools
             in destination processes.
    */

    MPID_eager_pool = (char **)
                      (MALLOC (MPID_numids * sizeof (char *)));
    MPID_mypool     = (MPID_POOL_T *)
                      (MALLOC (MPID_numids * sizeof (MPID_POOL_T)));
    MPID_destready  = (MPID_DEST_READY *)
                      (MALLOC (MPID_numids * sizeof (MPID_DEST_READY)));

    MPID_next_pkt_to_read = (int *)
                      (MALLOC (MPID_numids * sizeof (int)));


    if ( (! MPID_eager_pool) || (! MPID_mypool) ||
         (! MPID_destready)  || (! MPID_next_pkt_to_read) ){
         fflush (stdout);
         MPID_Abort( (MPI_Comm)0, 1, "MPI internal",
                     "Cannot allocate memory in MPID_CENJU3_Init" );
         return;
    }

    /* Initialize pool of packets and flags of destination process */

    memset ((void *) MPID_mypool, 0,
            (size_t) (MPID_numids * sizeof (MPID_POOL_T)) );

    memset ((void *) MPID_destready, 0,
            (size_t) (MPID_numids * sizeof (MPID_DEST_READY)) );

    for (i=0; i<MPID_numids; i++) {
      MPID_next_pkt_to_read[i] = 0;
      MPID_destready[i].buf = NULL;
      MPID_destready[i].buf_ready = 1;
      *(MPID_eager_pool+i) = NULL;
    }

    MPID_mypackets = MPID_mypool + MPID_myid;

    /* Dummy malloc to increase sbrk and to avoid checking of adresses */

    for (i=6000000; i > 0; i = i-1000000) {
       if (ptr = (void *) MALLOC (i)) break;
    }

    if (! ptr) {
       for (i = 500000; i > 0; i=i/2) {
          if (ptr = (void *) MALLOC (i)) break;
       }
    }

    if (ptr) free ((void *)ptr);

#ifdef MPID_DEBUG_ALL
    if (MPID_DebugFlag) {
       fprintf (MPID_DEBUG_FILE,
                "[%d] MPID_mypool = %d\n", MPID_MyWorldRank, MPID_mypool);

       fprintf (MPID_DEBUG_FILE,
                "[%d] MPID_destready = %d\n", MPID_MyWorldRank, MPID_destready);

       fflush (MPID_DEBUG_FILE);
    }
#endif /* MPID_DEBUG_ALL */
 }



/* Currently two modi in order to send the control packet are implemented :
   ------------------------------------------------------------------------

   MPID_TWO_WRITES : The control packet is sent using two remote writes.
                     In the first remote write, "head.ready" is set to 0
                     and the entire packet is written
                     In the second, "head.ready = 1" is transported.
   Otherwise  :      In the other mode the entire packet is written in
                     a single remote write. To ensure that the
                     entire packet is written, the last word contains
                     a control value. To determine the last word, the
                     size of the packet is contained in "head.size".

*/

/* 
  Send a control message.
 */

int MPID_CENJU3_SendControl( pkt, size, dest )
MPID_PKT_T *pkt;
int        size, dest;
{
#ifdef MPID_ONE_WRITE_int
  int  *ipkt = (int *) pkt;
#else
  char *ipkt = (char *) pkt;
#endif
  register i, new_size;
  register int next_write = MPID_destready[dest].next_pkt_to_write;
  VOLATILE char *destready =
               &(MPID_destready[dest].pkt_ready[next_write]);
  DEBUG_PRINT_MSG("Entering SendControl");

  /* Place the actual length into the packet */
  /* pkt->head.size = size; */ /* ??? */ 
  MPID_TRACE_CODE_PKT("Sendpkt",dest,pkt->head.mode);

  /* Force ready == 1 in the "one write mode":
     CJrmwrite of Cenju-3 stores data in sequential mode:
     Thus, the size of the packet is also stored and in the
     last location of the paket the size is store a second
     time indicating that the last memory location is written
     by the sending process.
  */

#ifdef MPID_TWO_WRITES
  pkt->head.ready = 0;
#else
#ifdef MPID_ONE_WRITE_int
  i = (size-1+sizeof(int)) / sizeof (int);
  new_size = (i + 1) * sizeof (int);
  ipkt [i] = new_size;
#else
  ipkt[size] = 1;
  new_size = size + 1;
#endif

  pkt->head.size  = new_size;
  pkt->head.ready = 1;
#endif /* MPID_TWO_WRITES */

  if (MPID_PKT_READY_IS_SET(destready)) {
    while (MPID_PKT_READY_IS_SET(destready)) {
      MPID_DeviceCheck( MPID_NOTBLOCKING );

      next_write = MPID_destready[dest].next_pkt_to_write;
      destready = &(MPID_destready[dest].pkt_ready[next_write]);
    }
  }

  MPID_destready[dest].next_pkt_to_write = 
  (MPID_destready[dest].next_pkt_to_write + 1) % MPID_NUM_PKTS;

 /* DEBUG_PRINT_PKT("packet to be sent", pkt); */

#ifdef MPID_DEBUG_ALL
  if (MPID_DebugFlag) {
      fprintf (MPID_DEBUG_FILE, "[ ]S dest = %d, Nr = %d\n", dest, next_write);
#ifdef HUHU
     {MPIR_SHANDLE *shandle=0;
      MPID_AINT_GET(shandle,pkt->get_pkt.send_id);
      printf ("shandle->cookie = %lx %d\n", shandle->cookie, &(shandle->cookie));}
#endif /* HUHU */
  }
#endif /* MPID_DEBUG_ALL */

  MPID_PKT_READY_SET(destready);

#ifdef MPID_TWO_WRITES
  MPID_REMOTE_WRITE(dest, &(MPID_mypackets->packets[next_write]), pkt, size );

  pkt->head.ready = 1;
  MPID_REMOTE_WRITE(dest, &(MPID_mypackets->packets[next_write].head.ready),
                          &(pkt->head.ready), sizeof (int) );
#else
  MPID_REMOTE_WRITE(dest, &(MPID_mypackets->packets[next_write]), pkt, new_size );
#endif /* MPID_TWO_WRITES */

  DEBUG_PRINT_MSG("Exiting SendControl");

  return MPI_SUCCESS;
}

/* 
  Read an incoming control message.
 */

int MPID_CENJU3_ReadControl( pkt, size, from, is_blocking )
MPID_PKT_T **pkt;
int        size, *from;
MPID_BLOCKING_TYPE is_blocking;
{
#ifdef MPID_ONE_WRITE_int
    int                   *ipkt;
    register volatile int *int_size;
#else
    register volatile char *int_size;
#endif
    static   int          last_processor = -1;
    register int          i, j, n, *next;
    register volatile int *ready, *recv_size;
    register MPID_POOL_T  *tpkt;

    DEBUG_PRINT_MSG("Entering ReadControl");
    n       = MPID_numids;
    tpkt    = MPID_mypool;
    j       = last_processor;
    next    = MPID_next_pkt_to_read;

    while (1) {

      for (i=0; i<n; i++) {
        j = (j+1) % n;
	ready = &(tpkt[j].packets[next[j]].head.ready);

#ifdef MPID_DEBUG_ALL
          if (MPID_DebugFlag && j != MPID_MyWorldRank) {
	   fprintf(MPID_DEBUG_FILE, "[%d] testing [%d,%d] = %d\n", MPID_MyWorldRank,
		 j, next[j], tpkt[j].packets[next[j]].head.ready);
           fflush (stdout);
          }
#endif /* MPID_DEBUG_ALL */

	if (MPID_PKT_READY_IS_SET(ready)){

          last_processor = j;
	  *from = j;
	  *pkt  = &(tpkt[j].packets[next[j]]);
          MPID_ready_pkt_to_clr = (char *) &(MPID_destready[MPID_myid].pkt_ready[next[j]]);

          MPID_next_pkt_to_read[j] =
         (MPID_next_pkt_to_read[j]+1) % MPID_NUM_PKTS;

#ifndef MPID_TWO_WRITES
#ifdef MPID_ONE_WRITE_int
/*        ipkt = (int *) *pkt; */
          recv_size = &((*pkt)->head.size);

          int_size = (int *) *pkt + (*recv_size - 1)/sizeof(int);
          while (*recv_size != *int_size) {
#else
          int_size = (char *)*pkt + (*pkt)->head.size - 1;
          while (*int_size != 1) {
#endif
/*           printf ("recv_size = %d, int_size = %d, %d %d\n",
                      *recv_size, *int_size, recv_size, int_size); */
          }
#endif /* MPID_TWO_WRITES */

	  MPID_TRACE_CODE_PKT("Readpkt",j,(*pkt)->head.mode);
	  /*	  fprintf( stderr, "[%d] read packet from %d\n", MPID_MyWorldRank, j ); */

#ifdef MPID_DEBUG_ALL
          if (MPID_DebugFlag) {
             fprintf (MPID_DEBUG_FILE, "[ ]R sender = %d, Nr = %d\n", j, next[j]);
             DEBUG_PRINT_PKT("R received message", *pkt)

#ifdef HUHU
             {MPIR_SHANDLE *shandle=0;
              MPID_AINT_GET(shandle,(*pkt)->get_pkt.send_id);
              printf (MPID_DEBUG_FILE, "shandle.cookie = %lx %d\n",
                      shandle->cookie, &(shandle->cookie));}
#endif /* HUHU */
          }
#endif /* MPID_DEBUG_ALL */

	  return MPI_SUCCESS;
	}
      }
      /* If nonblocking and nothing found, return 1 */
      if (!is_blocking) return 1;

    }
}

/* Return elapsed time in seconds */

double MPID_Cenju3_Time ()
{
        static int first = 1;

        static int last_time1, last_time2;
        static double wcycle1 = 0.0, wcycle2 = 0.0;
        static double cycle1_time, cycle2_time, cycle2_time_2, r_cycle2_time;

        register int time, i;
        register double diff, d32, wtime1, wtime2;

        /* Initialization

           cycle1_time = Cycle Time of CJgettmr  in seconds.
           cycle2_time = Cycle Time of CJgettmr2 in seconds.

           CJgettmr()  returns the time in milliseconds.
           CJgettmr2() returns the time in microseconds.

        */

        if (first) {
           first = 0;

           d32 = (double) 1024;
           for (i = 10; i < 32; i++) d32 = d32 * 2;

           cycle1_time = d32 * (double) 0.001;

           cycle2_time = d32 * (double) 0.000001;
           cycle2_time_2 = cycle2_time / 2;

           r_cycle2_time = (double) 1.0 / cycle2_time;

           last_time2 = CJgettmr2 ();
           last_time1 = CJgettmr ();

           return ((double) 0.0);
        }

        /* get elapsed time in microseconds */

        time = CJgettmr2 ();

        diff = (time - last_time2) * (double) 0.000001;
        wtime2 = wcycle2 + diff;

        if (diff < 0.0) {
           wtime2  = wtime2  + cycle2_time;
           wcycle2 = wtime2;
           last_time2 = time;
        }

        /* get elapsed time in milliseconds */

        time = CJgettmr ();

        diff = (time - last_time1) * (double) 0.001;
        wtime1 = wcycle1 + diff;

        if (diff < 0.0) {
           wtime1  = wtime1  + cycle1_time;
           wcycle1 = wtime1;
           last_time1 = time;
        }

        if (wtime1-wtime2 > cycle2_time_2) {
           i = (wtime1-wtime2)*r_cycle2_time + (double) 0.5;
           wcycle2 = wcycle2 + i * cycle2_time;
           wtime2  = wtime2  + i * cycle2_time;
        }

        return (wtime2);
}
