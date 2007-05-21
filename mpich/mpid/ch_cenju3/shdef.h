#if !defined(VOLATILE)
#if (HAS_VOLATILE || defined(__STDC__))
#define VOLATILE volatile
#else
#define VOLATILE
#endif
#endif

/* 
   For many systems, it is important to align data structures on 
   cache lines, and to insure that separate structures are in
   different cache lines.  Currently, the largest cache line that we've seen
   is 128 bytes, so we pick that as the default.
 */

#ifndef MPID_CACHE_LINE_SIZE
#define MPID_CACHE_LINE_SIZE 128
#define MPID_CACHE_LINE_LOG_SIZE 7
#endif

/*
   MPID_myid       : Process id of current process
   MPID_numids     : Numper of processes
   MPID_destready  : Flags of receiving processes indicating 
                     whether packets or eager buffers are free or used.
   MPID_eager_pool : Pool for eager messages
                     (maximal length = MPID_BUF_EAGER_MAX_DATA_SIZE)
   MPID_ready_pkt_to_clr: Address of the ready word to clear
                          after the packet was read.
*/

extern char*                  MPID_CENJU3_Get_Stack();

extern int                    MPID_myid;
extern int                    MPID_numids;
extern MPID_DEST_READY       *MPID_destready;
extern char                  **MPID_eager_pool;
extern char                  *MPID_ready_pkt_to_clr;

#define MALLOC malloc
#define FREE   free

#ifndef MEMCPY
#define MEMCPY(d,s,n) memcpy (d,s,n)
#endif /* MEMCPY */

