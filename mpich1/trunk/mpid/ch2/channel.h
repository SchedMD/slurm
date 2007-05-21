/*
   Sending and receiving packets

   Packets are sent and received on connections.  In order to simultaneously
   provide a good fit with conventional message-passing systems and with 
   other more direct systems (e.g., sockets), I've defined a set of
   connection macros that are here translated into Chameleon message-passing
   calls.  These are somewhat complicated by the need to provide access to
   non-blocking operations

   These are not yet fully integrated into the code.  

   This file is designed for use with vendor message-passing systems through
   the Chameleon definitions.  Other systems should REPLACE this file.
   See mpid/ch_tcp and mpid/ch_shmem for examples.  Note also that once
   NewDevice creates a device, the local mpid.h is not modified, so that
   changes to packets.h can be accomplised by #defines

   In addition, we provide a simple way to log the "channel" operations
   If MPID_TRACE_FILE is set, we write information on the operation (both
   start and end) to the given file.  In order to simplify the code, we
   use the macro MPID_TRACE_CODE(name,channel).  Other implementations
   of channel.h are encourage to provide the trace calls; note that as macros,
   they can be completely removed at compile time for more 
   performance-critical systems.

 */
/* Do we need stdio here, or has it already been included? */

#define MPIDPATCHLEVEL 2.0

#define MPID_RecvAnyControl( pkt, size, from ) \
    { MPID_TRACE_CODE("BRecvAny",-1);\
      PIbrecv(MPID_PT2PT_TAG, pkt, size, MSG_OTHER); *(from) = PIfrom();\
      MPID_TRACE_CODE("ERecvAny",*(from));}
#define MPID_RecvFromChannel( buf, size, channel ) \
    { MPID_TRACE_CODE("BRecvFrom",channel);\
      PIbrecv( MPID_PT2PT2_TAG(channel), buf, size, MSG_OTHER );\
      MPID_TRACE_CODE("ERecvFrom",channel);}
#define MPID_ControlMsgAvail( ) \
    PInprobe(MPID_PT2PT_TAG)
#define MPID_SendControl( pkt, size, channel ) \
    { MPID_TRACE_CODE("BSendControl",channel);\
      PIbsend( MPID_PT2PT_TAG, pkt, size, channel, MSG_OTHER );\
      MPID_TRACE_CODE("ESendControl",channel);}
#if defined(MPID_USE_SEND_BLOCK) && ! defined(MPID_SendControlBlock)
/* 
   SendControlBlock allows the send to wait until the message is 
   received (but does NOT require it).  This can simplify some buffer 
   handling.
 */
#define MPID_SendControlBlock( pkt, size, channel ) \
    { MPID_TRACE_CODE("BSendControl",channel);\
      PIbsend( MPID_PT2PT_TAG, pkt, size, channel, MSG_OTHER );\
      MPID_TRACE_CODE("ESendControl",channel);}
#endif

/* If we did not define SendControlBlock, make it the same as SendControl */
#if !defined(MPID_SendControlBlock)
#define MPID_SendControlBlock(pkt,size,channel) \
      MPID_SendControl(pkt,size,channel)
#endif

/* MPID_WaitForMsg is optional channel.  This is a *blocking* call
   that waits until either a control message or a data transfer
   message is available).  It does not process the data; the semantics
   are very similar to a Unix select or poll operation.  Define
   HAVE_MPID_WAIT_FOR_MSG if this is available.  This is used in
   chbrndv.c to wait until an expected rendezvous transfer message is
   available.  It may also return some other (unspecified) message
   activity has occurred.  Thus, even if this routine returns, a
   subsequent *blocking* recv may not return immediately (or at all).
   This is basically a good way to say "block until something interesting
   happens".

   Syntax is like
   void MPID_WaitForMsg( void )

   This is similar to an PIbprobe( ANY )
*/

/* Because a common operation is to send a control block, and decide whether
   to use SendControl or SendControlBlock based on whether the send is 
   non-blocking, we include a definition for it here: 
 */
#ifdef MPID_USE_SEND_BLOCK
#define MPID_SENDCONTROL(mpid_send_handle,pkt,len,dest) \
if (mpid_send_handle->is_non_blocking) {\
    MPID_SendControl( pkt, len, dest );}\
else {\
    MPID_SendControlBlock( pkt, len, dest );}
#else
#define MPID_SENDCONTROL(mpid_send_handle,pkt,len,dest) \
MPID_SendControl( pkt, len, dest )
#endif

/* 
   Note that this must be non-blocking.  On systems with tiny buffers,
   we can't do this.  Instead, we use a nonblocking send, combined
   with tests for completion of the send and incoming messages.

   This will still require that the destination process the eager message,
   but that is one of the fundemental assumptions.
 */
#ifdef MPID_TINY_BUFFERS
#define MPID_SendChannel( buf, size, channel ) \
    { ASYNCSendId_t sid; \
      MPID_ISendChannel( buf, size, channel, sid );\
      while (!MPID_TSendChannel(sid)) {\
	  MPID_DeviceCheck( MPID_NOTBLOCKING );\
          }\
      MPID_TRACE_CODE("ESend",channel);}
#else
#define MPID_SendChannel( buf, size, channel ) \
    { MPID_TRACE_CODE("BSend",channel);\
      PIbsend( MPID_PT2PT2_TAG(PImytid), buf, size, channel, MSG_OTHER );\
      MPID_TRACE_CODE("ESend",channel);}
#endif
/* 
   Non-blocking versions (NOT required, but if PI_NO_NRECV and PI_NO_NSEND
   are NOT defined, they must be provided)
 */
#define MPID_IRecvFromChannel( buf, size, channel, id ) \
    {MPID_TRACE_CODE("BIRecvFrom",channel);\
     PInrecv( MPID_PT2PT2_TAG(channel), buf, size, MSG_OTHER, id );\
     MPID_TRACE_CODE("EIRecvFrom",channel);}
#define MPID_WRecvFromChannel( buf, size, channel, id ) \
    {MPID_TRACE_CODE("BWRecvFrom",channel);\
     PIwrecv( MPID_PT2PT2_TAG(channel), buf, size, MSG_OTHER, id );\
     MPID_TRACE_CODE("EWRecvFrom",channel);}
#define MPID_RecvStatus( id ) \
    PInstatus( (id) )
#define MPID_CancelRecvChannel( id ) \
    PIcrecv( (id) )

/* Note that these use the tag based on the SOURCE, not the channel
   See MPID_SendChannel */
#define MPID_ISendChannel( buf, size, channel, id ) \
    {MPID_TRACE_CODE("BISend",channel);\
     PInsend( MPID_PT2PT2_TAG(PImytid), buf, size, channel, MSG_OTHER, id );\
     MPID_TRACE_CODE("EISend",channel);}
#define MPID_WSendChannel( id ) \
    {MPID_TRACE_CODE("BWSend",-1);\
    PIwsend( 0, 0, 0, 0, 0, id );\
    MPID_TRACE_CODE("EWSend",-1);}
/* Test the channel operation */
#define MPID_TSendChannel( id ) \
    PInstatus( (id) )
#define MPID_CancelSendChannel( id ) \
    PIcsend( (id) )

/* If nonblocking sends are defined, the MPID_SendData command uses them;
   otherwise, the blocking version is used.
   These rely on dmpi_send_handle and mpid_send_handle 
 */
#ifndef PI_NO_NSEND
#define MPID_SendData( buf, size, channel, mpid_send_handle ) \
if (mpid_send_handle->is_non_blocking) {\
    MPID_ISendChannel( address, len, dest, mpid_send_handle->sid );\
    dmpi_send_handle->completer=MPID_CMPL_WSEND;\
    }\
else \
    {\
    mpid_send_handle->sid = 0;\
    MPID_SendChannel( address, len, dest );\
    DMPI_mark_send_completed( dmpi_send_handle );\
    }
#else
#define MPID_SendData( buf, size, channel, mpid_send_handle ) \
    mpid_send_handle->sid = 0;\
    MPID_SendChannel( address, len, dest );\
    DMPI_mark_send_completed( dmpi_send_handle );
#endif

/*
   We also need an abstraction for out-of-band operations.  These could
   use transient channels or some other operation.  This is essentially for
   performing remote memory operations without local intervention; the need
   to determine completion of the operation requires some sort of handle.
   Here are the ones that we've chosen. Rather than call them transient 
   channels, we define "transfers", which are split operations.  Both 
   receivers and senders may create a transfer.

   Note that the message-passing version of this uses the 'ready-receiver'
   version of the operations.

   There is a problem with the receive transfer definition.  The simplest
   form would be MPID_TestRecvTransfer( dmpi_recv_handle->dev_rhandle.rid )
   However, it might be that that test only indicates whether the transfer
   if READY for completion, not that it HAS been completed.  It may require
   an additional step to actually complete the transfer, using more 
   information than just the rid.  For this reason, there is an additional
   MPID_CompleteRecvTransfer( ... ) called only when a test succeeds.
   Some implementations may leave this empty

   Note that since MPID_RecvTransfer is blocking (and may obstruct other 
   messages), the chbrndv.c code that uses it calls it only after 
   MPID_TestRecvTransfer succeeds.  This may be expensive in some
   applications.
   
 */
#define MPID_CreateSendTransfer( buf, size, partner, id ) {*(id) = 0;}
#define MPID_CreateRecvTransfer( buf, size, partner, id ) \
       {*(id) = CurTag++;TagsInUse++;}

/*
 * Receive transfers may be blocking or nonblocking.  Since a single system
 * may use both, there are separate definitions for the two cases.
 */
#define MPID_StartNBRecvTransfer( buf, size, partner, id, request, rid ) \
    {MPID_TRACE_CODE("BIRRRecv",id);\
     PInrecvrr( MPID_PT2PT2_TAG(id), buf, size, MSG_OTHER, rid );\
     MPID_TRACE_CODE("EIRRRecv",id);}
#define MPID_EndNBRecvTransfer( request, id, rid ) \
    {MPID_TRACE_CODE("BIWRRecv",id);\
     PIwrecvrr( 0, 0, 0, 0, rid );\
     MPID_TRACE_CODE("EIWRRecv",id);\
     if (--TagsInUse == 0) CurTag = 1024; else if (id == CurTag-1) CurTag--;}
#define MPID_TestNBRecvTransfer( request ) \
    PInstatus( (request)->rid )
#define MPID_CompleteNBRecvTransfer( buf, size, partner, id, rid )

/* Put the tag value into rid so that we can probe it ... */
/* Remember the args so that we can use them later ... need request */
/* If rhandle.buf set and different from buf, we probably have a problem ... */
#define MPID_StartRecvTransfer( buf, size, partner, id, request, rid ) \
    {MPID_TRACE_CODE("BIRRRecv",id);\
     rid = MPID_PT2PT2_TAG(id);\
     (request)->rhandle.buf = buf; (request)->rhandle.len = size;\
     (request)->rhandle.dev_rhandle.from_grank = partner;\
     MPID_TRACE_CODE("EIRRRecv",id);}
#define MPID_EndRecvTransfer( request, id, rid ) \
    {MPID_TRACE_CODE("BIWRRecv",id);\
     PIwrecvrr( MPID_PT2PT2_TAG(id), (request)->rhandle.buf, (request)->rhandle.len, MSG_OTHER, rid );\
     MPID_TRACE_CODE("EIWRRecv",id);\
     if (--TagsInUse == 0) CurTag = 1024; else if (id == CurTag-1) CurTag--;}
#define MPID_TestRecvTransfer( request ) \
    PInprobe( (request)->recv_handle )
#define MPID_CompleteRecvTransfer( buf, size, partner, id, rid ) \
        MPID_EndRecvTransfer( buf, size, partner, id, rid )

/* This is the blocking version ONLY version */
#define MPID_RecvTransfer( buf, size, partner, id ) {\
    MPID_TRACE_CODE("BRecvTransfer",id);\
    PIbrecv( id, buf, size, MSG_OTHER );\
    if (--TagsInUse == 0) CurTag = 1024; else if (id == CurTag-1) CurTag--;\
    MPID_TRACE_CODE("ERecvTransfer",id);}
#define MPID_SendTransfer( buf, size, partner, id ) {\
    MPID_TRACE_CODE("BSendTransfer",id);\
    PIbsend( id, buf, size, partner, MSG_OTHER );\
    MPID_TRACE_CODE("ESendTransfer",id);}
    
#define MPID_StartSendTransfer( buf, size, partner, id, sid ) \
    {MPID_TRACE_CODE("BIRRSend",id);\
     PIbsendrr( MPID_PT2PT2_TAG(id), buf, size, partner, MSG_OTHER );\
     sid = 1;\
     MPID_TRACE_CODE("EIRRSend",id);}
#define MPID_EndSendTransfer( buf, size, partner, id, sid ) \
    {MPID_TRACE_CODE("BWRRSend",id);\
    MPID_TRACE_CODE("EWRRSend",id);}
#define MPID_TestSendTransfer( sid ) \
    1

#define MPID_StartNBSendTransfer( buf, size, partner, id, sid ) \
    {MPID_TRACE_CODE("BIRRSend",id);\
     PInsendrr( MPID_PT2PT2_TAG(id), buf, size, partner, MSG_OTHER, sid );\
     MPID_TRACE_CODE("EIRRSend",id);}
#define MPID_EndNBSendTransfer( request, id, sid ) \
    {MPID_TRACE_CODE("BWRRSend",id);\
    PIwsendrr( MPID_PT2PT2_TAG(id), buf, size, partner, MSG_OTHER, sid );\
    MPID_TRACE_CODE("EWRRSend",id);}
#define MPID_TestNBSendTransfer( sid ) \
    PInstatus( sid )

/* 
   These macros control the conversion of packet information to a standard
   representation.  On homogeneous systems, these do nothing.
 */
#ifdef MPID_HAS_HETERO
#define MPID_PKT_PACK(pkt,size,dest) MPID_CH_Pkt_pack((MPID_PKT_T*)(pkt),size,dest)
#define MPID_PKT_UNPACK(pkt,size,src) MPID_CH_Pkt_unpack((MPID_PKT_T*)(pkt),size,src)
#else
#define MPID_PKT_PACK(pkt,size,dest) 
#define MPID_PKT_UNPACK(pkt,size,src) 
#endif

/* 
   On message-passing systems with very small message buffers, or on 
   systems where it is advantageous to frequently check the incoming
   message queue, we use the MPID_DRAIN_INCOMING definition
 */
#define MPID_DRAIN_INCOMING \
    while (MPID_DeviceCheck( MPID_NOTBLOCKING ) != -1) ;
#ifdef MPID_TINY_BUFFERS 
#define MPID_DRAIN_INCOMING_FOR_TINY(is_non_blocking) \
{if (is_non_blocking) {MPID_DRAIN_INCOMING;}}
#else
#define MPID_DRAIN_INCOMING_FOR_TINY(is_non_blocking)
#endif

