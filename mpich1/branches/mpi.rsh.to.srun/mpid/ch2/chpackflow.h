#define MPID_PACK_CONTROL
/* #undef MPID_PACK_CONTROL */

#define MPI_Pk_ackmark 25 /* number of packets received by the DESTINATION
			     process before a protocol ACK is sent back
			     to the source */

#define MPI_Pk_hiwater 40 /* maximum number of unreceived packets the SOURCE
			     can send before requiring a protocol ACK to
			     be sent back */

typedef struct {
    int  *pack_sent;     /* keeps track of packets sent - indices of array
			    are the ranks of processors */
    int  *pack_rcvd;     /* keeps track of packets received - indices of
			    array are the ranks of processors */
} MPID_Packets;

extern MPID_Packets MPID_pack_info;

#ifdef MPID_GET_LAST_PKT
extern int total_pack_unacked;  /* keeps track of total number of unacked 
			           packets */
extern int expect_ack;          /* true if sender is expecting an ack */
#endif

/* If packet control is turned on */
#ifdef MPID_PACK_CONTROL
/* Checking that I have not reached the hiwater mark */
#define MPID_PACKET_CHECK_OK(partner) \
      (MPID_pack_info.pack_sent[partner] < MPI_Pk_hiwater)
/* Checking that I have not received ackmark packets */ 
#define MPID_PACKET_RCVD_GET(partner) \
      (MPID_pack_info.pack_rcvd[partner] + 1 == MPI_Pk_ackmark)

/* If packet control and debugging are turned on */
#ifdef MPID_DEBUG_ALL

/* If packet control, debugging and last packet are turned on */
#ifdef MPID_GET_LAST_PKT
/* Add one to packets sent */
#define MPID_PACKET_ADD_SENT(me, partner) \
      MPID_pack_info.pack_sent[partner] += 1;\
      if (MPID_pack_info.pack_sent[partner] == MPI_Pk_ackmark) \
          expect_ack++; \
      if (MPID_DebugFlag) {\
        SPRINTF( ch_debug_buf,\
                 "[%d] sent %d packet(s) to %d - expect_ack = %d\n", me, \
		 MPID_pack_info.pack_sent[partner], partner, expect_ack);\
        MPID_DEBUG_MSG;\
        MPID_SAVE_MSG;}

/* Subtract 'ackmark' from packets sent */
#define MPID_PACKET_SUB_SENT(me, partner) \
      MPID_pack_info.pack_sent[partner] -= MPI_Pk_ackmark; \
      expect_ack--; \
      if (MPID_DebugFlag) {\
        SPRINTF( ch_debug_buf,\
                 "[%d].pack_sent[%d] is %d - expect_ack = %d\n", me, partner, \
		 MPID_pack_info.pack_sent[partner], expect_ack);\
        MPID_DEBUG_MSG;\
        MPID_SAVE_MSG;}
        
/* If packet control and debugging are turned on and last packet is 
   turned off */
#else
/* Add one to packets sent */
#define MPID_PACKET_ADD_SENT(me, partner) \
      MPID_pack_info.pack_sent[partner] += 1;\
      if (MPID_DebugFlag) {\
        SPRINTF( ch_debug_buf,\
                 "[%d] sent %d packet(s) to %d\n", me, \
		 MPID_pack_info.pack_sent[partner], partner);\
        MPID_DEBUG_MSG;\
        MPID_SAVE_MSG;}

/* Subtract 'ackmark' from packets sent */
#define MPID_PACKET_SUB_SENT(me, partner) \
      MPID_pack_info.pack_sent[partner] -= MPI_Pk_ackmark; \
      if (MPID_DebugFlag) {\
        SPRINTF( ch_debug_buf,\
                 "[%d].pack_sent[%d] is %d\n", me, partner, \
		 MPID_pack_info.pack_sent[partner]);\
        MPID_DEBUG_MSG;\
        MPID_SAVE_MSG;}

#endif

/* If packet control and debugging are turned on and last packet is 
   on or off */
/* Add one to packets received */
#define MPID_PACKET_ADD_RCVD(me, partner) \
      MPID_pack_info.pack_rcvd[partner] += 1; \
      if (MPID_DebugFlag) {\
        SPRINTF( ch_debug_buf,\
                 "[%d] received %d packet(s) from %d\n", me, \
		 MPID_pack_info.pack_rcvd[partner], partner);\
        MPID_DEBUG_MSG;\
        MPID_SAVE_MSG;}

/* Subtract 'ackmark' from packets received */
#define MPID_PACKET_SUB_RCVD(me, partner) \
      MPID_pack_info.pack_rcvd[partner] -= MPI_Pk_ackmark; \
      MPID_DEBUG_PRINTF(( ch_debug_buf,\
                 "[%d].pack_rcvd[%d] is %d\n", me, \
		 partner, MPID_pack_info.pack_rcvd[partner]))

/* If packet control is turned on and debugging is turned off */
#else

/* If packet control and get last packet are turned on and debugging 
   is turned off */
#ifdef MPID_GET_LAST_PKT
/* Add one to packets sent */
#define MPID_PACKET_ADD_SENT(me, partner) \
      MPID_pack_info.pack_sent[partner] += 1; \
      expect_ack++; 
/* Subtract 'ackmark' from packets sent */
#define MPID_PACKET_SUB_SENT(me, partner) \
      MPID_pack_info.pack_sent[partner] -= MPI_Pk_ackmark; \
      expect_ack--;

/* If packet control is turned on and debugging and get last packet are 
   turned off */
#else
/* Add one to packets sent */
#define MPID_PACKET_ADD_SENT(me, partner) \
      MPID_pack_info.pack_sent[partner] += 1; 
/* Subtract 'ackmark' from packets sent */
#define MPID_PACKET_SUB_SENT(me, partner) \
      MPID_pack_info.pack_sent[partner] -= MPI_Pk_ackmark; 
#endif

/* If packet control is turned on, debugging is turned off, and last packet
   is turned on or off */
/* Add one to packets received */
#define MPID_PACKET_ADD_RCVD(me, partner) \
      MPID_pack_info.pack_rcvd[partner] += 1;
/* Subtract 'ackmark' from packets received */
#define MPID_PACKET_SUB_RCVD(me, partner) \
      MPID_pack_info.pack_rcvd[partner] -= MPI_Pk_ackmark; 
#endif

/* If packet control is turned off */
#else
#define MPID_PACKET_CHECK_OK(partner) (1)
#define MPID_PACKET_RCVD_GET(partner) (1)
#define MPID_PACKET_ADD_SENT(me, partner)
#define MPID_PACKET_SUB_SENT(me, partner)
#define MPID_PACKET_ADD_RCVD(me, partner)
#define MPID_PACKET_SUB_RCVD(me, partner)
#endif

extern void MPID_PacketFlowSetup( void );
extern void MPID_SendProtoAck( int, int );
extern void MPID_RecvProtoAck( MPID_PKT_T *, int );
extern void MPID_PackDelete( void );
extern void MPID_FinishRecvPackets( MPID_Device * );
#ifdef MPID_USE_SHMEM
/* Send the flow control using an existing packet */
extern void MPID_SendProtoAckWithPacket( int, int, MPID_PKT_T * );
#endif
