
/* 
   This file defines the packet/message format that WILL be used in
   the next generation ADI.  
 */

#ifndef MPID_PKT_DEF
#define MPID_PKT_DEF
#include <stdio.h>
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
 */
#define MPID_PT2PT_TAG 0
#define MPID_PT2PT2_TAG(src) (1+(src))

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

   For debugging purposes, it would be useful to detect various
   recoverable "multi-party" errors.  These include

       Rsend not matched be receive
       Type signature mismatch between sender and receiver
       Error in collective operation
 */
#define MPID_PKT_LAST_MSG MPID_PKT_DO_GET
typedef enum { MPID_PKT_SHORT=0, MPID_PKT_LONG=1, 
	       MPID_PKT_REQUEST_SEND=2,
	       MPID_PKT_DO_GET=3, 
               MPID_PKT_OK_TO_SEND=4, 
	       MPID_PKT_ANTI_SEND=5,
	       MPID_PKT_ANTI_SEND_OK=6,
               MPID_PKT_DONE_GET = 7, MPID_PKT_CONT_GET = 8,
	       MPID_PKT_FLOW = 9,
	       MPID_PKT_PROTO_ACK = 10,
	       MPID_PKT_ACK_PROTO = 11
               }
    MPID_Pkt_t;

/* Comments on packets - see the readme */
   
#ifdef MPID_HAS_HETERO
#define MPID_DO_HETERO(a) a
/* ??? Make this the high bit on the message mode? 
   Add a single bit???? */
#define MPID_PKT_MSGREP_DECL int msgrep:32;
#else
#define MPID_DO_HETERO(a) 
#define MPID_PKT_MSGREP_DECL 
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

/* 
   Flow control.  When flow control is enabled, EVERY packet includes
   a flow word (int:32).  This word will (usually) contain two fields
   that indicate how much channel/buffer memory has be used since the 
   last message
 */

#ifdef MPID_FLOW_CONTROL
#define MPID_PKT_FLOW_DECL int flow_info:32;
#else
#define MPID_PKT_FLOW_DECL
#endif

/* Note that context_id and lrank may be unused; they are present in 
   case they are needed to fill out the word
   To simplify the handling of heterogeneous types, we DON'T pack the
   mode/context/lrank when the system is heterogeneous.  If we do, then
   we need to carefully unwind the byte/bit orderings.
 */
#ifdef MPID_HAS_HETERO
#define MPID_PKT_MODE  \
    MPID_PKT_PRIVATE   \
    int mode:32;                 /* Contains MPID_Pkt_t */             \
    int context_id:32;           /* Context_id */                      \
    int lrank:32;                /* Local rank in sending context */   \
    int to:32;                   /* destination rank */                \
    int src:32;                  /* source of the packet */            \
    int seqnum:32;               /* bytes sent / bytes received */     \
    MPID_PKT_LEN_DECL            /* size of packets in bytes */        \
    MPID_PKT_LINK_DECL           /* link to 'next' packet    */        \
    MPID_PKT_SRC_DECL            /* Source of packet in COMM_WORLD system */ \
    MPID_PKT_FLOW_DECL           /* Flow control info */
#else
#define MPID_PKT_MODE  \
    MPID_PKT_PRIVATE   \
    unsigned mode:5;             /* Contains MPID_Pkt_t */             \
    unsigned context_id:16;      /* Context_id */                      \
    unsigned lrank:11;           /* Local rank in sending context */   \
    int to:32;                   /* destination rank */                \
    int src:32;                  /* source of the packet */            \
    int seqnum:32;               /* bytes sent / bytes received */     \
    MPID_PKT_LEN_DECL            /* size of packets in bytes */        \
    MPID_PKT_LINK_DECL           /* link to 'next' packet    */        \
    MPID_PKT_SRC_DECL            /* Source of packet in COMM_WORLD system */ \
    MPID_PKT_FLOW_DECL           /* Flow control info */
#endif
#define MPID_PKT_BASIC \
    MPID_PKT_MODE      \
    int      tag:32;             /* tag is full sizeof(int) */         \
    int      len:32;             /* Length of DATA */                  \
    MPID_PKT_MSGREP_DECL

/* If you change the length of the tag field, change the defn of MPID_TAG_UB
   in mpid.h */

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
    } MPID_PKT_MODE_T;

/* This is the minimal message packet */
typedef struct {
    MPID_PKT_BASIC
    } MPID_PKT_HEAD_T;

/* Short messages are sent eagerly (unless Ssend) */
typedef struct { 
    MPID_PKT_BASIC
    MPID_Aint   send_id;         /* Id needed in case of a cancel */    
    char     buffer[MPID_PKT_MAX_DATA_SIZE];
    } MPID_PKT_SHORT_T;

/* Eager long messages */
typedef struct {
    MPID_PKT_BASIC
    MPID_Aint   send_id;         /* Id needed in case of a cancel */    
    } MPID_PKT_LONG_T;

/* Long messages (and Ssend) are send in rendezvous mode, or with "get" */
typedef struct {
    MPID_PKT_BASIC
    MPID_Aint   send_id;         /* Id to return when ok to send */
    MPID_RNDV_T send_handle;     /* additional data for receiver */
    } MPID_PKT_REQUEST_SEND_T;
typedef struct {
    MPID_PKT_MODE
    MPID_Aint   send_id;        /* Id sent by REQUEST_SEND */
    MPID_Aint   recv_id;         /* rhandle's address */
    MPID_RNDV_T recv_handle;    /* additional data for sender */
    } MPID_PKT_OK_TO_SEND_T;
/* The "rendezvous" packets can be canceled by sending a message with
   mode MPID_PKT_ANTI_SEND and acknowledged with MPID_PKT_ANTI_SEND_OK
 */

/* Note that recv_id, len_avail, and cur_offset are needed only for
   partial transfers; sync_id is redundant (but eliminating it requires
   some additional code in chget).  The same packet type is
   used for all get operations so that it can be returned to the
   partner for updating. */
typedef struct {
    MPID_PKT_BASIC
    MPID_Aint    send_id;       /* Id sent by SENDER, identifies MPI_Request */
    void         *address;      /* Location of data ON SENDER */
    /* The following support partial sends */
    MPID_Aint    recv_id;       /* Used by receiver for partial gets */
    int          len_avail;     /* Actual length available */
    int          cur_offset;    /* Offset (for sender to use) */
    } MPID_PKT_GET_T;
/* Get done is the same type with a different mode */

typedef struct {
    MPID_PKT_BASIC
} MPID_PKT_FLOW_T;

typedef struct {
    MPID_PKT_BASIC
    int          cancel;        /* set to 1 if msg was cancelled - 
				   0 otherwise */
    MPID_Aint    send_id;       /* Id sent by SENDER, identifies MPI_Request */
    MPID_Aint    recv_id;       /* rhandle's address */
} MPID_PKT_ANTI_SEND_T;

/* We may want to make all of the packets an exact size (e.g., memory/cache
   page.  This is done by defining a pad */
#ifndef MPID_PKT_PAD
#define MPID_PKT_PAD 8
#endif

typedef union _MPID_PKT_T {
    MPID_PKT_HEAD_T          head;
    MPID_PKT_SHORT_T         short_pkt;
    MPID_PKT_LONG_T          long_pkt;
    MPID_PKT_REQUEST_SEND_T  request_pkt;
    MPID_PKT_OK_TO_SEND_T    sendok_pkt;
    MPID_PKT_ANTI_SEND_T     antisend_pkt;
    MPID_PKT_GET_T           get_pkt;
    MPID_PKT_FLOW_T          flow_pkt;
    char                     pad[MPID_PKT_PAD];
    } MPID_PKT_T;

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
#define MPID_TRACE_CODE_X(name,longvalue) {if (MPID_TRACE_FILE){\
fprintf( MPID_TRACE_FILE,"[%d] %20s on %4d at %s:%lx\n", MPID_MyWorldRank, \
         name, longvalue, __FILE__, __LINE__ ); fflush( MPID_TRACE_FILE );}}
#define MPID_TRACE_CODE_PKT(name,channel,mode) {if (MPID_TRACE_FILE){\
fprintf( MPID_TRACE_FILE,"[%d] %20s on %4d (type %d) at %s:%d\n", \
	 MPID_MyWorldRank, name, channel, mode, __FILE__, __LINE__ ); \
	 fflush( MPID_TRACE_FILE );}}
#else
#define MPID_TRACE_CODE(name,channel)
#define MPID_TRACE_CODE_X(name,channel)
#define MPID_TRACE_CODE_PKT(name,channel,mode)
#endif

/* chdef contains the definitions for a particular channel implementation */
#include "chdef.h"
/* channel contains the channel implementation */
#include "channel.h"

#if defined(MPID_PKT_PRE_POST)
/* Single buffer for now. Note that this alloc must EITHER be in the
   same routine as all of the calls OR in the same file .
   An alternative is to make these POINTERS to global values
   declared elsewhere 

   Eventually, these will use only the Channel operations
 */
#define MPID_PKT_GALLOC \
    static MPID_PKT_T     pkt; \
    static ASYNCRecvId_t  pktid;
#ifdef FOO
#define MPID_PKT_RECV_DECL(type,pkt)
#define MPID_PKT_RECV_GET(pkt,field) (pkt).field
#define MPID_PKT_RECV_SET(pkt,field,val) (pkt).field = val
#define MPID_PKT_RECV_ADDR(pkt) &(pkt)
#define MPID_PKT_RECV_FREE(pkt)
#define MPID_PKT_RECV_CLR(pkt)
#endif

#define MPID_PKT_INIT() MPID_PKT_POST()
#define MPID_PKT_CHECK()  \
    MPID_RecvStatus( pktid )
#define MPID_PKT_WAIT() \
    {PIwrecv(0,0,0,0,&pktid); from_grank = PIfrom();}
#define MPID_PKT_POST() \
    PInrecv(MPID_PT2PT_TAG, &pkt, sizeof(MPID_PKT_T), MSG_OTHER, &pktid)
#define MPID_PKT_POST_AND_WAIT() \
    MPID_RecvAnyControl( &pkt, sizeof(MPID_PKT_T), &from_grank )
 
/* #define MPID_PKT_FREE() */
/* #define MPID_PKT (pkt) */

#elif defined(MPID_PKT_PREALLOC)

#define MPID_PKT_CHECK()  \
    MPID_ControlMsgAvail()
#define MPID_PKT_WAIT() MPID_PKT_POST_AND_WAIT()
#define MPID_PKT_POST() 
#define MPID_PKT_POST_AND_WAIT() \
    {PIbrecvm(MPID_PT2PT_TAG, pkt, sizeof(MPID_PKT_T), MSG_OTHER ); \
	 from_grank = PIfrom ; }

#elif defined(MPID_PKT_DYNAMIC_RECV)
/* The recv routines RETURN the packet */

#define MPID_PKT_CHECK()  \
    MPID_ControlMsgAvail()
#define MPID_PKT_WAIT() MPID_PKT_POST_AND_WAIT()
#define MPID_PKT_POST() 
/* This is still OK, but in this case, the ADDRESS of the pointer to the
   packet is passed (and the pointer is ASSIGNED) */
#define MPID_PKT_POST_AND_WAIT() \
    MPID_RecvAnyControl( &pkt, sizeof(pkt), &from_grank )

#else
#define MPID_PKT_CHECK()  \
    MPID_ControlMsgAvail()
#define MPID_PKT_WAIT() MPID_PKT_POST_AND_WAIT()
#define MPID_PKT_POST() 
#define MPID_PKT_POST_AND_WAIT() \
    MPID_RecvAnyControl( &pkt, sizeof(pkt), &from_grank )
/* #define MPID_PKT_FREE() */
/* #define MPID_PKT (pkt) */
#endif

#endif
