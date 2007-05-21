/*
  This file contains the structures, macros, and routine prototypes 
  that are used to control the usage of resources, primarily memory.

  In the initial version, we just manage the memory use by eagerly delivered
  messages, since this is flow control that can not be provided by the 
  channel interface (or the various shared memory versions).  

  Variables for managing flow in the "buffer" are present but unused for 
  now.

  A special debugging option allows monitoring of the flow control management.
  
 */

#ifndef _MPID_FLOW
#define _MPID_FLOW

/*
  Each connection (processor pair) has this information associated with
  it.  We need to know about our partner (to see if we can send more)
  and ourselves (so we can send our partner updates)
 */
typedef struct {
    /* Information about our partner */
    int buf_use, buf_thresh, 
	mem_use, mem_thresh;
    /* Information about us */
    int buf_read, mem_read, mem_recvd, 
	need_update;
} MPID_Flow;

extern MPID_Flow *MPID_flow_info;
extern int MPID_DebugFlow;

#ifndef MPID_FLOW_BASE_THRESH
/* 1MB by default */
#define MPID_FLOW_BASE_THRESH 1048576 
/* #define MPID_FLOW_BASE_THRESH 20000 */
#endif

/*
 * These are the macros to update the flow information.
 *
 * Memory (used at partner)
 * When sending to a partner, the sender first checks that there is 
 * memory available.  If not, the sender should defer sending (for example,
 * choose rendezvous instead or enter a CheckDevice loop (for why, see
 * below).  If there is space, then send the message.  
 *
 * On the receive side, when receiving an eagerly sent message,
 * update the memory used immediately (regardless of whether there is
 * a posted receive for it).  
 *
 * When the message is received, the receiver decrements its memory used
 * by the size of the message.  If the number crosses the "threshold" value,
 * it generates a control message to the partner with the size of messages.
 * 
 * The threshold should be set roughly long/rendevous size LESS than the
 * maximum about of memory you want to use.
 */
#ifdef MPID_FLOW_CONTROL
/* Checking that space is available at partner */
#define MPID_FLOW_MEM_OK(size,partner) \
  (MPID_flow_info[partner].mem_use < MPID_flow_info[partner].mem_thresh)
/* Update what we have sent */
#ifdef MPID_DEBUG_ALL
#define MPID_FLOW_MEM_SEND(size,partner) \
      MPID_flow_info[partner].mem_use += size; \
      if (MPID_DebugFlag || MPID_DebugFlow) {\
        FPRINTF( MPID_DEBUG_FILE,\
                 "[%d] (%d).mem_use = %d\n", MPID_MyWorldRank, partner, \
	         MPID_flow_info[partner].mem_use ); } \

/* On the receive side, increment what we're received */
#define MPID_FLOW_MEM_READ(size,partner) \
   MPID_flow_info[partner].mem_read += size; \
   if (MPID_DebugFlag || MPID_DebugFlow) {\
        FPRINTF( MPID_DEBUG_FILE,\
                 "[%d] +(%d).mem_read = %d\n", MPID_MyWorldRank, partner, \
	         MPID_flow_info[partner].mem_read ); }\
   if (MPID_flow_info[partner].mem_read > MPID_flow_info[partner].mem_thresh){\
    if (MPID_flow_info[partner].mem_recvd > 0) {\
        MPID_SendFlowPacket( partner ); } \
    else {\
        MPID_flow_info[partner].need_update = 1;\
        if (MPID_DebugFlag || MPID_DebugFlow) {\
	  FPRINTF( MPID_DEBUG_FILE,\
"[%d] R Flow control mem_thresh reached", MPID_MyWorldRank );}\
	 }\
   }

/* When the message is received, decrement it.  Careful of the threshold */
#define MPID_FLOW_MEM_RECV(size,partner) \
   MPID_flow_info[partner].mem_recvd += size; \
   if (MPID_DebugFlag || MPID_DebugFlow) {\
        FPRINTF( MPID_DEBUG_FILE,\
                 "[%d] +(%d).mem_recvd = %d\n", MPID_MyWorldRank, partner, \
	         MPID_flow_info[partner].mem_recvd ); }\
   if (MPID_flow_info[partner].need_update && \
      MPID_flow_info[partner].mem_recvd > 0) {\
          MPID_SendFlowPacket( partner );\
   if (MPID_flow_info[partner].mem_read < MPID_flow_info[partner].mem_thresh)\
          MPID_flow_info[partner].need_update = 0;}

/* Tell our partner how much we've read since the last message */
#define MPID_FLOW_MEM_ADD(pkt,partner) \
     (pkt)->flow_info = MPID_flow_info[partner].mem_recvd;\
     MPID_flow_info[partner].mem_read -= (pkt)->flow_info; \
     MPID_flow_info[partner].mem_recvd = 0;
#define MPID_FLOW_MEM_GET(pkt,partner)  \
     MPID_flow_info[partner].mem_use -= (pkt)->flow_info; \
     if (MPID_DebugFlag || MPID_DebugFlow) {\
       FPRINTF( MPID_DEBUG_FILE,\
     "[%d] -(%d).mem_use = %d\n", MPID_MyWorldRank, partner, \
		MPID_flow_info[partner].mem_use );}

#else  /* ndef MPID_DEBUG_ALL */
#define MPID_FLOW_MEM_SEND(size,partner) \
      MPID_flow_info[partner].mem_use += size;

/* On the receive side, increment what we're received */
#define MPID_FLOW_MEM_READ(size,partner) \
   MPID_flow_info[partner].mem_read += size; \
   if (MPID_flow_info[partner].mem_read > MPID_flow_info[partner].mem_thresh){\
    if (MPID_flow_info[partner].mem_recvd > 0) {\
        MPID_SendFlowPacket( partner ); } \
    else {\
        MPID_flow_info[partner].need_update = 1;\
	 }\
   }

/* When the message is received, decrement it.  Careful of the threshold */
#define MPID_FLOW_MEM_RECV(size,partner) \
   MPID_flow_info[partner].mem_recvd += size; \
   if (MPID_flow_info[partner].need_update && \
      MPID_flow_info[partner].mem_recvd > 0) {\
          MPID_SendFlowPacket( partner );\
   if (MPID_flow_info[partner].mem_read < MPID_flow_info[partner].mem_thresh)\
          MPID_flow_info[partner].need_update = 0;}

/* Tell our partner how much we've read since the last message */
#define MPID_FLOW_MEM_ADD(pkt,partner) \
     (pkt)->flow_info = MPID_flow_info[partner].mem_recvd;\
     MPID_flow_info[partner].mem_read -= (pkt)->flow_info; \
     MPID_flow_info[partner].mem_recvd = 0;
define MPID_FLOW_MEM_GET(pkt,partner)  \
     MPID_flow_info[partner].mem_use -= (pkt)->flow_info;

#endif /* MPID_DEBUG_ALL */

#else
#define MPID_FLOW_MEM_OK(size,partner) (1)
#define MPID_FLOW_MEM_SEND(size,partner)
#define MPID_FLOW_MEM_READ(size,partner)
#define MPID_FLOW_MEM_RECV(size,partner)
#define MPID_FLOW_MEM_ADD(pkt,partner)
#define MPID_FLOW_MEM_GET(pkt,partner)
#endif

extern void MPID_SendFlowPacket (int);
extern void MPID_RecvFlowPacket (MPID_PKT_T *, int);
extern void MPID_FlowSetup (int,int);
extern void MPID_FlowDelete (void);
extern void MPID_FlowDump (FILE *);
extern void MPID_FlowDebug (int);
#endif
