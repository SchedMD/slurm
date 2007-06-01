
/* 
   This file defines the packet/message format for the shared-memory
   system.
 */

#ifndef MPID_PKT_DEF
#define MPID_PKT_DEF
#include <stdio.h>
#include <unistd.h>
/* 
   This packet size should be selected such that
   (s + r*(n+h)) + c*n \approx (s+r*n) + s+r*h
   where s = latency, r = time to send a byte, n = total message length, 
   h = header size, and c = time to copy a byte.  This condition reduces to
   c n \approx s
   For a typical system with
   s = 30us
   c = .03us/byte
   this gives
   n = s / c = 30 us / (.03us/byte) = 1000 bytes

   When the message does not fit into a single packet, ALL of the message
   should be placed in the "extension" packet (see below).  This removes 
   an extra copy from the code.
 */

/*
   This is a very simple, open, single packet structure.  

   Similar games can be played for other design points.  Note that the 
   lrank could be determined by looking up the absolute rank in the 
   matching context_id; this approach may be cost effective if many small
   messages are sent on a slow system.
 */

#define MPID_MIN(a,b) ((a) < (b) ? (a) : (b))

/* 
   Here are all of the packet types.  

   There is no special support for ready-send messages.  It isn't hard
   to add, but at the level of hardware that a portable implementation
   can reach, there isn't much to do.

   There are three ways to send messages:
   SHORT (data in envelope)
   SEND_ADDRESS (data in receive buffer or in sending process)
   REQUEST_SEND (data not available until sender receives OK_TO_SEND_GET.
                 Receiver may return OK_TO_SEND_GET)

 */
#define MPID_PKT_LAST_MSG MPID_PKT_REQUEST_SEND_GET
typedef enum { MPID_PKT_SHORT=0, MPID_PKT_SEND_ADDRESS = 1, 
	       MPID_PKT_REQUEST_SEND_GET=2, 
               MPID_PKT_OK_TO_SEND_GET = 3, MPID_PKT_CONT_GET = 4
               }
    MPID_Pkt_t;

/* Comments on packets - see the readme */
   
#define MPID_DO_HETERO(a) 
#define MPID_PKT_MSGREP_DECL 

#ifndef MPID_PKT_PRIVATE
#define MPID_PKT_PRIVATE
#endif

/* Currently two modi in order to send the control packet are implemented :
   ------------------------------------------------------------------------

   MPID_TWO_WRITES : The control packet is sent using two remote writes.
                     In the first remote write, "head.ready" is set to 0
                     and the entire packet is written
                     In the second, "head.ready = 1" is transported.
   Otherwise  :      In the other mode the entire packet is written in
                     a single remote write. To ensure that the
                     entire packet is written, the last word/bytes contains
                     a control value. To determine the last word, the
                     size of the packet is contained in "head.size".

*/

#ifdef MPID_TWO_WRITES
#define MPID_END_OF_PKT
#else
#define MPID_END_OF_PKT \
    volatile char     end_of_pkt;      /* Last memory location of packet */
#endif /* MPID_TWO_WRITES */

/* Note that context_id and lrank may be unused; they are present in 
   case they are needed to fill out the word */
#define MPID_PKT_MODE  \
    MPID_PKT_PRIVATE   \
    unsigned mode:5;             /* Contains MPID_Pkt_t */             \
    unsigned context_id:16;      /* Context_id */                      \
    unsigned lrank:11;           /* Local rank in sending context */   \
    VOLATILE int      size;      /* Size of the packet to be read */   \
    VOLATILE int      ready;     /* Indicates packet is ready to be read */
#define MPID_PKT_BASIC \
    MPID_PKT_MODE      \
    int      tag:32;             /* tag is full sizeof(int) */         \
    int      len:32;             /* Length of DATA */                  


/* If you change the length of the tag field, change the defn of MPID_TAG_UB
   in mpid.h */

#ifndef MPID_PKT_MAX_DATA_SIZE
#define MPID_PKT_MAX_DATA_SIZE 1024
#endif /* MPID_PKT_MAX_DATA_SIZE */

#define MPID_PKT_IS_MSG(mode) ((mode) <= MPID_PKT_LAST_MSG)

/* 
   One unanswered question is whether it is better to send the length of
   a short message in the short packet types, or to compute it from the
   message-length provided by the underlying message-passing system.
   Currently, I'm planning to send it.  Note that for short messages, I 
   only need another 2 bytes to hold the length (1 byte if I restrict
   short messages to 255 bytes).  The tradeoff here is additional computation
   at sender and receiver versus reduced data-load on the connection between
   sender and receiver.
 */

/* This is the minimal packet */
typedef struct {
    MPID_PKT_MODE
    MPID_END_OF_PKT
    } MPID_PKT_MODE_T;

/* This is the minimal message packet */
typedef struct {
    MPID_PKT_BASIC
    MPID_END_OF_PKT
    } MPID_PKT_HEAD_T;

/* Short messages are sent eagerly (unless Ssend) */
typedef struct { 
    MPID_PKT_BASIC
    char     buffer[MPID_PKT_MAX_DATA_SIZE];
    MPID_END_OF_PKT
    } MPID_PKT_SHORT_T;

/* Eager message can use this simple packet */
typedef struct {
    MPID_PKT_BASIC
    MPID_END_OF_PKT
    } MPID_PKT_SEND_ADDRESS_T;

/* Note that recv_buf and recv_complete are only used for remote writes.
   The same packet type is used for all get operations so that it can
   be returned to the partner for updating. */
typedef struct {
    MPID_PKT_BASIC
    MPID_Aint    send_id;       /* Id sent by SENDER, identifies MPI_Request */
    void         *address;      /* Location of data ON SENDER */
    /* The following support remote writes */
    void         *recv_buf;     /* Location of receive buffer in receiving process */
    void         *recv_complete;/* Location of rhandle->is_complete in receiving process */
    int          len_avail;     /* Actual length available */
    MPID_END_OF_PKT
    } MPID_PKT_GET_T;
/* Get done is the same type with a different mode */

/* We may want to make all of the packets an exact size (e.g., memory/cache
   page.  This is done by defining a pad */
#ifndef MPID_PKT_PAD
#define MPID_PKT_PAD 128
#endif

typedef union _MPID_PKT_T {
    MPID_PKT_HEAD_T          head;
    MPID_PKT_SHORT_T         short_pkt;
    MPID_PKT_SEND_ADDRESS_T  sendadd_pkt;
    MPID_PKT_GET_T           get_pkt;
    char                     pad[MPID_PKT_PAD];
    } MPID_PKT_T;

extern FILE *MPID_TRACE_FILE;

#ifdef MPID_DEBUG_ALL
#define MPID_TRACE_CODE(name,channel) {if (MPID_TRACE_FILE){\
fprintf( MPID_TRACE_FILE,"[%d] %20s on %4d at %s:%d\n", MPID_MyWorldRank, \
         name, channel, __FILE__, __LINE__ ); fflush( MPID_TRACE_FILE );}}
#define MPID_TRACE_CODE_PKT(name,channel,mode) {if (MPID_TRACE_FILE){\
fprintf( MPID_TRACE_FILE,"[%d] %20s on %4d (type %d) at %s:%d\n", \
	 MPID_MyWorldRank, name, channel, mode, __FILE__, __LINE__ ); \
	 fflush( MPID_TRACE_FILE );}}
#else
#define MPID_TRACE_CODE(name,channel)
#define MPID_TRACE_CODE_PKT(name,channel,mode)
#endif

/*
   Sending and receiving packets

   Packets are sent and received on connections.  In order to simultaneously
   provide a good fit with conventional message-passing systems and with 
   other more direct systems (e.g., sockets), I've defined a set of
   connection macros that are here translated into Chameleon message-passing
   calls or into other, data-channel transfers.  These are somewhat 
   complicated by the need to provide access to non-blocking operations

   This file is designed for use with the portable shared memory code from
   p2p.

   In addition, we provide a simple way to log the "channel" operations
   If MPID_TRACE_FILE is set, we write information on the operation (both
   start and end) to the given file.  In order to simplify the code, we
   use the macro MPID_TRACE_CODE(name,channel).  Other implementations
   of channel.h are encouraged to provide the trace calls; note that as macros,
   they can be completely removed at compile time for more 
   performance-critical systems.

 */
/* Do we need stdio here, or has it already been included? */


#if !defined(VOLATILE)
#if (HAS_VOLATILE || defined(__STDC__))
#define VOLATILE volatile
#else
#define VOLATILE
#endif
#endif

/*
   MPID_NUM_PKTS = Maximal number of packets
                   a process can store per sending process
   MPID_BUF_EAGER_MAX_DATA_SIZE = Maximal number of bytes in an eager send
*/

#define MPID_NUM_PKTS 4
#define MPID_BUF_EAGER_MAX_DATA_SIZE 16384

typedef struct {
    char         *buf;
    int           next_pkt_to_write;
    VOLATILE int  buf_ready;
    VOLATILE char pkt_ready [MPID_NUM_PKTS];
} MPID_DEST_READY;

typedef struct {
MPID_PKT_T packets [MPID_NUM_PKTS];
} MPID_POOL_T;

/*
 * We need macros to set/clear/read the ready fields
 */

#define MPID_PKT_READY_SET(x)    *(x) = 1
#define MPID_PKT_READY_CLR(x)    *(x) = 0
#define MPID_PKT_READY_IS_SET(x) (*(x) == 1) 

#define MPID_BUF_READY_IS_SET(x) (*(x) == 1) 
#define MPID_BUF_READY_CLR(x)     *(x) = 0
#define MPID_BUF_READY_SET(x)     *(x) = 1

/*
   Definition of the REMOTE_WRITE command.

   On the Cenju-3, the write command CJrmwrite controls the addresses
   to be written with the local data space.

   Thus, the data segment break value and the stack pointer have to be
   examined and to be incremented, if necessary.
   
*/
#ifdef CENJU3_DOES_NOT_CHECK

#define MPID_REMOTE_WRITE(dest_rank, into, src, len) \
    if (CJrmwrite ((char *) (src), dest_rank, (char *) (into), len) != (len)) {\
       fflush (stdout);                                                        \
       fprintf (stderr, "[%d] Internal error in CJrmwrite to destination %d\n src = %d, into = %d, len in bytes = %d\n",                                        \
       MPID_MyWorldRank,dest_rank, (char *) (src), (char *) (into), len);      \
       MPID_Abort( (MPI_Comm)0, 1, "MPI internal",                             \
                   "Error in REMOTE_WRITE: Data wasn't written" );             \
       return MPI_ERR_INTERN;}
     
         
#else

#define MPID_REMOTE_WRITE(dest_rank, into, src, len) \
{register char *old_stack = MPID_CENJU3_Get_Stack(); register int written;     \
 register void *MPID_brk_pointer = sbrk ((int) 0);                             \
 if ((char *) (into) + len - (char *) MPID_brk_pointer < 0 ||                  \
     old_stack - (char *) (into) + 1 < 0)                                      \
     written = CJrmwrite ((char *) (src), dest_rank, (char *) (into), len);    \
 else if ((char *) (into) + len - (char *) MPID_brk_pointer <                  \
          old_stack - (char *) (into) + 1) {                                   \
    register int i;                                                            \
    i = brk ((void *) ((char *)(into) + len));                                 \
                                                                               \
/*  printf ("old brk_pointer = %d, new brk_pointer = %d, into = %d, len = %d\n", (char *) MPID_brk_pointer, (char *) sbrk ((int) 0), (char *) into, len); */    \
                                                                               \
    written = CJrmwrite ((char *) (src), dest_rank, (char *) (into), len);     \
                                                                               \
    if (! i) brk (MPID_brk_pointer);                                           \
 }                                                                             \
 else {                                                                        \
/*  printf ("get_stack %d; into = %d, len = %d\n", old_stack, (char *) into, len); */\
    __builtin_alloca(old_stack - (char*) into);                                \
    written = CJrmwrite ((char *) (src), dest_rank, (char *) (into), len);     \
 }                                                                             \
 if (written != (len)) {                                                       \
    fflush (stdout);                                                           \
    fprintf (stderr, "[%d] Internal error in CJrmwrite to destination %d\n src = %d, into = %d, len in bytes = %d\n",                                           \
    MPID_MyWorldRank, dest_rank, (char *) (src), (char *) (into), len);        \
    fprintf (stderr, "old brk_pointer = %d, old_stack = %d\n",                 \
             (char *) MPID_brk_pointer, old_stack);                            \
    MPID_Abort( (MPI_Comm)0, 1, "MPI internal",                                \
                "Error in REMOTE_WRITE: Data wasn't written" );                \
    return MPI_ERR_INTERN;                                                     \
 }                                                                             \
}
#endif /* CENJU3_DOES_NOT_CHECK */

#endif
