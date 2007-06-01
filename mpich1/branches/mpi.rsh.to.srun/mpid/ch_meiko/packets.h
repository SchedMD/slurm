






/* 
   This file defines the packet/message format that WILL be used in
   the next generation ADI.  
 */

#ifndef MPID_PKT_DEF
#define MPID_PKT_DEF
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
  The implementation reserves some message tags.

  (An optimization is to allow the use of all but a few very large tags
  for messages in the initial communicator, thus eliminating a separate
  header.  Messages in a different communicator would be sent on a reserved
  set of tags.  An alternate is to use the Chameleon tags for communicator
  types, making the message-passing system handle the queueing of messages
  by communicator.  Note that if tags are used to separate communicators,
  message-passing systems that have stream semantics, like EUI-H, will
  fail to operate correctly.  Another approach is to make the tags a 
  combination of tag, context, and source.)

  PT2PT_TAG is the tag for short messages and the headers of long messages
  PT2PT2_TAG(source) is the tag for longer messages (by source).  This permits
  the header messages to be freely received into preallocated buffers, and
  for long messages to be received directly into user-buffers.

  The mode field is overloaded for the synchronous case because we support
  NONBLOCKING, SYNCHRONOUS sends; thus, there can be a variety of outstanding
  synchronous sends at any time, and we have to match them up.

  We do this by making the mode field look like this:

  <syncreqnum><modetype>

  The mode field is required because, while there are different sends for each
  mode, there is only one kind of receive, and hence we need the mode field
  to decide what to do.  In fact, our only need is to handle MPI_SYNCHRONOUS
  sends.
 */
#define MPID_PT2PT_TAG 0
#define MPID_PT2PT2_TAG(src) (1+(src))

/*
   The mode field contains an ID if the mode is SYNCHRONOUS 

   This is a very simple, open, single packet structure.  Additional
   performance, particularly for short messages, can be obtained by:
       mode is two bits; if sync or sync_ack, next nn bits are id
       lrank is 8 or 14 bits (depending on size of system; the reason
       for 14 is made clear below).
       len is provided only if a "large message" bit is set; otherwise,
       the length is computed from what is actually delivered.
       context_id is 16 bits
   With these changes, the short, non-synchronous send packet 
   is only 31 (tag) + 1 (short) + 2 (mode) + 16 context_id + 14 (lrank) =
   64 bits.  The packet defined here is 160 bits.

   Similar games can be played for other design points.  Note that the 
   lrank could be determined by looking up the absolute rank in the 
   matching context_id; this approach may be cost effective if many small
   messages are sent on a slow system.
 */

#define MPID_MODE_MASK  0x1f
#define MPID_MODE_BITS    5

#define MPID_MODE_XDR   0x4
#define MPIR_MODE_IS_SYNC(mpid) ((mpid)->mode & (int)MPIR_MODE_SYNCHRONOUS)
#define MPIR_MODE_SYNC_ID(mpid) ((mpid)->mode >> MPID_MODE_BITS)

#define MPID_MIN(a,b) ((a) < (b) ? (a) : (b))



/***************************************************************************
   What follows is the next-generation packet format.  We may try to 
   incrementally port to it...
 ***************************************************************************/


/* Here are all of the packet types.  We use the first bit to indicate
   short or long; by using small integers, we can use a single 
   select statement to jump to the correct code (here's hoping the 
   compiler generates good code for that!).
   In the cases where the packed is a control packet (neither long nor short),
   the first bit does NOT mean long/short.

   NOTE: YOU MUST CHECK MPID_PKT_IS_MSG WHEN CHANGING THESE!!!
 */
typedef enum { MPID_PKT_SHORT=0, MPID_PKT_LONG=1, MPID_PKT_SHORT_SYNC=2,
	       MPID_PKT_LONG_SYNC=3, MPID_PKT_SHORT_READY=4, 
               MPID_PKT_LONG_READY=5, 
               MPID_PKT_REQUEST_SEND=6,
	       MPID_PKT_REQUEST_SEND_READY=7,
	       MPID_PKT_DO_GET=8, MPID_PKT_DO_GET_SYNC=9,
               MPID_PKT_OK_TO_SEND=10, MPID_PKT_SYNC_ACK=11, 
	       MPID_PKT_READY_ERROR=12, 
               MPID_PKT_DONE_GET = 13, MPID_PKT_CONT_GET = 14,
	       MPID_PKT_COMPLETE_SEND=15, MPID_PKT_COMPLETE_RECV=16 }
    MPID_Pkt_t;

/* Comments on packets - see the readme */
   
#ifdef MPID_HAS_HETERO
#define MPID_PKT_XDR_DECL int has_xdr:32;
#define MPID_PKT_SEND_SET_HETERO(pkt,msgrep) \
if (msgrep == MPIR_MSGREP_XDR) \
    MPID_PKT_SEND_SET(pkt,has_xdr,MPID_MODE_XDR);\
else \
    MPID_PKT_SEND_SET(pkt,has_xdr,0);
#else
#define MPID_PKT_XDR_DECL 
#define MPID_PKT_SEND_SET_HETERO(pkt,msgrep) 
#endif

/*
   When used with message - passing interfaces, the packets do NOT include
   their own length since this information is carried in the message-passing
   systems envelope.  However, for direct network and stream interfaces
   it can be valuable to have an explicit length field as the second 32
   bit entry.
 */
#ifdef MPID_PKT_INCLUDE_LEN
#define MPID_PKT_LEN_DECL     int pkt_len:32;
#define MPID_PKT_LEN_SET(p,l) (p)->pkt_len = l
#define MPID_PKT_LEN_GET(p,l) l = (p)->pkt_len
#else
#define MPID_PKT_LEN_DECL 
#define MPID_PKT_LEN_SET(p,l) 
#define MPID_PKT_LEN_GET(p,l) ????
#endif

#ifdef MPID_PKT_INCLUDE_LINK
#define MPID_PKT_LINK_DECL union _MPID_PKT_T *next;
#else
#define MPID_PKT_LINK_DECL 
#endif

#ifdef MPID_PKT_INCLUDE_SRC
#define MPID_PKT_SRC_DECL int src:32;
#else
#define MPID_PKT_SRC_DECL
#endif

#ifndef MPID_PKT_PRIVATE
#define MPID_PKT_PRIVATE
#endif

#ifdef MPID_PKT_COMPRESSED
/* Note that context_id and lrank may be unused; they are present in 
   case they are needed to fill out the word */
#define MPID_PKT_MODE  \
    MPID_PKT_PRIVATE   \
    unsigned mode:5;             /* Contains MPID_Pkt_t */             \
    unsigned context_id:16;      /* Context_id */                      \
    unsigned lrank:11;           /* Local rank in sending context */   \
    MPID_PKT_LEN_DECL            /* size of packets in bytes */        \
    MPID_PKT_LINK_DECL           /* link to 'next' packet    */        \
    MPID_PKT_SRC_DECL            /* Source of packet in COMM_WORLD system */ 
#define MPID_PKT_BASIC \
    MPID_PKT_MODE      \
    int      tag:32;             /* tag is full sizeof(int) */         \
    int      len:32;             /* Length of DATA */                  \
    MPID_PKT_XDR_DECL
#else
/* We'd like to use MPID_Pkt_t, but as an enum, we can't specify the length 
   (!!) */
#define MPID_PKT_MODE  \
    MPID_PKT_PRIVATE   \
    int         mode:32;   \
    MPID_PKT_LEN_DECL  \
    MPID_PKT_LINK_DECL \
    MPID_PKT_SRC_DECL
#define MPID_PKT_BASIC \
    MPID_PKT_MODE      \
    int         context_id:32; \
    int         lrank:32;      \
    int         tag:32;        \
    int         len:32;        \
    MPID_PKT_XDR_DECL
#endif

#ifndef MPID_PKT_MAX_DATA_SIZE
#if !defined(MPID_HAS_HETERO)
/* Probably an MPP with 100 us latency */
#define MPID_PKT_MAX_DATA_SIZE 1024
/* 
   Warning: It must be possible for a blocking send to deliver this much
   data and return unless nonblocking sends are used for control packets
   (currently not available).  This is important on IBM MPL and TMC CMMD.
 */
#else
/* Probably a workstation with 1000 us latency */
#define MPID_PKT_MAX_DATA_SIZE 16384
#endif
#endif

#ifdef MPID_PKT_VAR_SIZE
extern int MPID_PKT_DATA_SIZE;
#else
#define MPID_PKT_DATA_SIZE MPID_PKT_MAX_DATA_SIZE
#endif

#define MPID_PKT_IS_MSG(mode) ((mode) <= 9)

#ifdef MPID_HAS_HETERO
#define DECL_SYNC_ID    int       sync_id:32
#else
#define DECL_SYNC_ID    MPID_Aint sync_id
#endif

/* 
   One unanswered question is whether it is better to send the length of
   a short message in the short packet types, or to compute it from the
   message-length provided by the underlying message-passing system.
   Currently, I'm planning to send it.  Note that for short messages, I 
   only need another 2 bytes to hold the length (1 byte if I restrict
   short messages to 255 bytes).  The tradeoff here is addition computation
   at sender and receiver versus reduced data-load on the connection between
   sender and receiver.
 */

/* This is the minimal packet */
typedef struct {
    MPID_PKT_MODE
    } MPID_PKT_MODE_T;

/* This is the minimal message packet */
typedef struct {
    MPID_PKT_BASIC
    } MPID_PKT_HEAD_T;
typedef struct { 
    MPID_PKT_BASIC
    char     buffer[MPID_PKT_MAX_DATA_SIZE];
    } MPID_PKT_SHORT_T;
typedef struct {
    MPID_PKT_BASIC
    } MPID_PKT_LONG_T;
typedef struct { 
    MPID_PKT_BASIC
    char     buffer[MPID_PKT_MAX_DATA_SIZE];
    } MPID_PKT_SHORT_READY_T;
typedef struct {
    MPID_PKT_BASIC
    } MPID_PKT_LONG_READY_T;

typedef struct {
    MPID_PKT_BASIC
    DECL_SYNC_ID;
    char      buffer[MPID_PKT_MAX_DATA_SIZE];
    } MPID_PKT_SHORT_SYNC_T;
typedef struct {
    MPID_PKT_BASIC
    DECL_SYNC_ID;
    } MPID_PKT_LONG_SYNC_T;

typedef struct {
    MPID_PKT_MODE
    DECL_SYNC_ID;
    } MPID_PKT_SYNC_ACK_T;

typedef struct {
    MPID_PKT_MODE
    MPID_Aint send_id;
    } MPID_PKT_COMPLETE_SEND_T;
typedef struct {
    MPID_PKT_MODE
    MPID_Aint recv_id;
    } MPID_PKT_COMPLETE_RECV_T;
typedef struct {
    MPID_PKT_BASIC
    MPID_Aint   send_id;         /* Id to return when ok to send */
    MPID_RNDV_T send_handle;     /* additional data for receiver */
    } MPID_PKT_REQUEST_SEND_T;
typedef struct {
    MPID_PKT_BASIC
    MPID_Aint   send_id;
    MPID_RNDV_T send_handle;
    } MPID_PKT_REQUEST_SEND_READY_T;
typedef struct {
    MPID_PKT_MODE
    MPID_Aint   send_id;        /* Id sent by REQUEST_SEND */
    MPID_RNDV_T recv_handle;    /* additional data for sender */
    } MPID_PKT_OK_TO_SEND_T;

typedef struct {
    MPID_PKT_BASIC
    } MPID_PKT_READY_ERROR_T;

/* Note that recv_id, len_avail, and cur_offset are needed only for
   partial transfers; sync_id is redundant (but eliminating it requires
   some additional code in chget) */
typedef struct {
    MPID_PKT_BASIC
    MPID_Aint    send_id;       /* Id sent by SENDER, identifies MPI_Request */
    MPID_Aint    recv_id;       /* Used by receiver for partial gets */
    void         *address;      /* Location of data ON SENDER */
    /* The following support partial sends */
    int          len_avail;     /* Actual length available */
    int          cur_offset;    /* Offset (for sender to use) */
    /* The following supports synchronous sends */
    DECL_SYNC_ID;               /* Sync id; we should use send_id instead.
				   This is just to get started */
    } MPID_PKT_GET_T;
/* Get done is the same type with a different mode */

/* We may want to make all of the packets an exact size (e.g., memory/cache
   page.  This is done by defining a pad */
#ifndef MPID_PKT_PAD
#define MPID_PKT_PAD 8
#endif

typedef union _MPID_PKT_T {
    MPID_PKT_HEAD_T          head;
    MPID_PKT_SHORT_T         short_pkt;
    MPID_PKT_SHORT_SYNC_T    short_sync_pkt;
    MPID_PKT_SHORT_READY_T   short_ready_pkt;
    MPID_PKT_REQUEST_SEND_T  request_pkt;
    MPID_PKT_REQUEST_SEND_T  request_ready_pkt;
    MPID_PKT_OK_TO_SEND_T    sendok_pkt;
    MPID_PKT_LONG_T          long_pkt;
    MPID_PKT_LONG_SYNC_T     long_sync_pkt;
    MPID_PKT_LONG_READY_T    long_ready_pkt;
    MPID_PKT_SYNC_ACK_T      sync_ack_pkt;
    MPID_PKT_COMPLETE_SEND_T send_pkt;
    MPID_PKT_COMPLETE_RECV_T recv_pkt;
    MPID_PKT_READY_ERROR_T   error_pkt;
    MPID_PKT_GET_T           get_pkt;
    char                     pad[MPID_PKT_PAD];
    } MPID_PKT_T;

#define MPID_PKT_HAS_XDR(pkt) (pkt)->head.has_xdr

/* Managing packets

   In a perfect world, there would always be a place for an incoming packet
   to be received.  In systems that work at the hardware level, this is
   often managed by having a separate pool for each possible source, and
   having each pair of processors keep track of how much space is being used.
   In the implementations of the ADI on top of existing message-passing 
   systems, we usually allow the underlying message-passing system to 
   manage flow control.  

   The message-passing equivalent of having an available buffer is to pre-post
   a non-blocking receive into which an incoming message can be placed.  The
   pros of this are that unnecessary data movement (from internal to ADI's 
   buffers) can be avoided, and that systems with interrupt-driven 
   receives (e.g., Intel, TMC, IBM) can repsond on an interrupt basis
   to incoming packets.

   The con-side to this is that doing an Irecv/Wait pair can be more
   expensive than a (blocking) Recv, and that interrupts can be expensive.
   Since we intend to do both a native shared-memory and active-message 
   version, and since there probably isn't a correct answer to question of
   which approach is best, we provide for both based on whether the macro
   MPID_PKT_PRE_POST is defined.

   An additional option is provided to allow the message packets to be
   preallocated.   This may be appropriate for p4, for example, where
   preallocating the message packets may eliminate a memory copy.

   Note also that the send packets need to be managed on some systems where
   blocking sends should not be used to dispatch the control information 
   for non-blocking operations.

   The operations are
   MPID_PKT_ALLOC() - Allocate the packets.  Either a single packet is
   used (non-pre-posted) or multiple packets (possibly two, to use double  
   buffering, for starters).

   MPID_PKT_INIT() - Initialize the packets .  Called during init portion.

   MPID_PKT_FREE() - Fress the allocated packets.  Note: if the packets
   are allocated on the calling-routines stack, this does nothing.
   
   MPID_PKT_CHECK() - Basically a check to see if a packet is available
 
   MPID_PKT_WAIT() - Waits for a packet to be available

   MPID_PKT_POST() - Post a non-blocking receive for the next packet.
   May be a nop in the case that packets are held by the underlying
   message-passing system

   MPID_PKT_POST_AND_WAIT() - Post and wait for a packet to be 
   available.  A blocking receive in both cases

 */

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

#include "channel.h"

/*
   The packets may be allocated off a routines stack or dynamically allocated
   by a packet - management routine.  In order to represent both forms
   efficiently, we'll eventually want to use macros for accessing the
   fields in the packet, with different definitions for local and dynamic 
   packets. 

   Special notes on MPID_PKT_..._FREE

   Some operations, after receiving a packet, may return that packet to the
   sender.  In this case, the receiver must not then free() the packet.  
   To manage this case, the macro MPID_PKT_..._CLR(pkt) is used;
   this should somehow mark the packet (for free()) as already taken
   care of.
 */

#if defined(MPID_PKT_PRE_POST)
/* Single buffer for now. Note that this alloc must EITHER be in the
   same routine as all of the calls OR in the same file .
   An alternative is to make these POINTERS to global values
   declared elsewhere 

   Note that because this file is fed into m4 to generate the native versions
   for non-Chameleon systems, it is REQUIRED that the Chameleon calls
   be on different lines from the #define's.  

   Eventually, these will use only the Channel operations
 */
#define MPID_PKT_GALLOC \
    static MPID_PKT_T     pkt; \
    static int  pktid;
#define MPID_PKT_RECV_DECL(type,pkt)
#define MPID_PKT_RECV_GET(pkt,field) (pkt).field
#define MPID_PKT_RECV_SET(pkt,field,val) (pkt).field = val
#define MPID_PKT_RECV_ADDR(pkt) &(pkt)
#define MPID_PKT_RECV_FREE(pkt)
#define MPID_PKT_RECV_CLR(pkt)

#define MPID_PKT_INIT() MPID_PKT_POST()
#define MPID_PKT_CHECK()  \
    MPID_RecvStatus( pktid )
#define MPID_PKT_WAIT() \
    {msgwait(&pktid); from = infonode();}
#define MPID_PKT_POST() \
    &pktid=_irecv(MPID_PT2PT_TAG,&pkt,sizeof(MPID_PKT_T))
#define MPID_PKT_POST_AND_WAIT() \
    MPID_RecvAnyControl( &pkt, sizeof(MPID_PKT_T), &from )
 
/* #define MPID_PKT_FREE() */
/* #define MPID_PKT (pkt) */

#elif defined(MPID_PKT_PREALLOC)
/* Preallocate the buffer, but use blocking operations to access it.
   This is appropriate for p4 
   This is not ready yet since many pieces of code expect pkt to be
   statically and locally declared...
 */
#define MPID_PKT_GALLOC \
    static MPID_PKT_T     *pkt = 0; 
#define MPID_PKT_RECV_DECL(type,pkt)
#define MPID_PKT_RECV_GET(pkt,field) (pkt)->field
#define MPID_PKT_RECV_SET(pkt,field,val) (pkt)->field = val
#define MPID_PKT_RECV_ADDR(pkt) (pkt)
#define MPID_PKT_RECV_FREE(pkt) \
    {if (pkt) free(pkt);}
#define MPID_PKT_RECV_CLR(pkt) pkt = 0;

#define MPID_PKT_INIT() \
    pkt = (MPID_PKT_T *)malloc(sizeof(MPID_PKT_T));
#define MPID_PKT_CHECK()  \
    MPID_ControlMsgAvail()
#define MPID_PKT_WAIT() MPID_PKT_POST_AND_WAIT()
#define MPID_PKT_POST() 
#define MPID_PKT_POST_AND_WAIT() \
    {_crecv(MPID_PT2PT_TAG,pkt,sizeof(MPID_PKT_T)); \
	 from = infonode() ; }
/* #define MPID_PKT_FREE() \
    free(pkt) */
/* #define MPID_PKT (*pkt) */

#elif defined(MPID_PKT_DYNAMIC_RECV)
/* The recv routines RETURN the packet */
#define MPID_PKT_RECV_DECL(type,pkt) type *pkt=0
#define MPID_PKT_RECV_GET(pkt,field) (pkt)->field
#define MPID_PKT_RECV_SET(pkt,field,val) (pkt)->field = val
#define MPID_PKT_RECV_ADDR(pkt) (pkt)
#ifndef MPID_PKT_RECV_FREE
#define MPID_PKT_RECV_FREE(pkt) ???
#define MPID_PKT_RECV_CLR(pkt)  ???
#endif

#define MPID_PKT_GALLOC 
#define MPID_PKT_INIT()
#define MPID_PKT_CHECK()  \
    MPID_ControlMsgAvail()
#define MPID_PKT_WAIT() MPID_PKT_POST_AND_WAIT()
#define MPID_PKT_POST() 
/* This is still OK, but in this case, the ADDRESS of the pointer to the
   packet is passed (and the pointer is ASSIGNED) */
#define MPID_PKT_POST_AND_WAIT() \
    MPID_RecvAnyControl( &pkt, sizeof(MPID_PKT_T), &from )
/* #define MPID_PKT_FREE() */
/* #define MPID_PKT (pkt) */

#else
/* Just use blocking send/recieve operations */
#define MPID_PKT_RECV_DECL(type,pkt) type pkt
#define MPID_PKT_RECV_GET(pkt,field) (pkt).field
#define MPID_PKT_RECV_SET(pkt,field,val) (pkt).field = val
#define MPID_PKT_RECV_ADDR(pkt) &(pkt)
#define MPID_PKT_RECV_FREE(pkt)
#define MPID_PKT_RECV_CLR(pkt)

#define MPID_PKT_GALLOC 
#define MPID_PKT_INIT()
#define MPID_PKT_CHECK()  \
    MPID_ControlMsgAvail()
#define MPID_PKT_WAIT() MPID_PKT_POST_AND_WAIT()
#define MPID_PKT_POST() 
#define MPID_PKT_POST_AND_WAIT() \
    MPID_RecvAnyControl( &pkt, sizeof(MPID_PKT_T), &from )
/* #define MPID_PKT_FREE() */
/* #define MPID_PKT (pkt) */
#endif

/* These macros allow SEND packets to be allocated dynamically or statically */
/* ********* CHANGE **********
 * SEND_ALLOC now has a third argument that indicates whether it is ok
 * to block waiting for a packet to be available.  A value of 0 indicates
 * a BLOCKING wait, 1 indicates that the operation requesting the packet
 * is nonblocking.  Note that the allocation MUST complete before the
 * MPID_PKT_SEND_ALLOC returns.
 */
#ifdef MPID_PKT_DYNAMIC_SEND
#define MPID_PKT_SEND_DECL(type,pkt) type *pkt
#define MPID_PKT_SEND_SET(pkt,field,val) (pkt)->field = val
#define MPID_PKT_SEND_GET(pkt,field) (pkt)->field
#define MPID_PKT_SEND_ADDR(pkt) (pkt)
#ifndef MPID_PKT_SEND_ALLOC
#define MPID_PKT_SEND_ALLOC(type,pkt,nblk) ???
#endif
#ifndef MPID_PKT_SEND_ALLOC_TEST
#define MPID_PKT_SEND_ALLOC_TEST(pkt,action) if (!pkt) { action ; }
#endif
#define MPID_PKT_SEND_FREE(pkt)

#else
/* Packets allocated off of routines stack ... */
#define MPID_PKT_SEND_DECL(type,pkt) type pkt
#define MPID_PKT_SEND_SET(pkt,field,val) (pkt).field = val
#define MPID_PKT_SEND_GET(pkt,field) (pkt).field
#define MPID_PKT_SEND_ADDR(pkt) &(pkt)
#define MPID_PKT_SEND_ALLOC(type,pkt,nblk)
#define MPID_PKT_SEND_ALLOC_TEST(pkt,action)
#define MPID_PKT_SEND_FREE(pkt)
#endif

#endif
