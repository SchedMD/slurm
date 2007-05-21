#ifdef MPID_DEBUG_ALL  
/***************************************************************************/
/* This variable controls debugging output                                 */
/***************************************************************************/
extern int MPID_DebugFlag;

                       /* #DEBUG_START# */
#ifdef MEMCPY
#undef MEMCPY
#endif
#define MEMCPY(a,b,c)\
{if (MPID_DebugFlag) {\
    fprintf( MPID_DEBUG_FILE, \
	    "[%d]R About to copy to %d from %d (n=%d) (%s:%d)...\n", \
	    MPID_MyWorldRank, a, b, c, __FILE__, __LINE__ );\
    fflush( MPID_DEBUG_FILE ); }\
memcpy( a, b, c );}

#define DEBUG_PRINT_SEND_PKT(msg,pkt)\
    {if (MPID_DebugFlag) {\
	fprintf( MPID_DEBUG_FILE,\
"[%d]%s of tag = %d, dest = %d, ctx = %d, len = %d, mode = ", \
	       MPID_MyWorldRank, msg, MPID_PKT_SEND_GET(pkt,tag), dest, \
	       MPID_PKT_SEND_GET(pkt,context_id), \
	       MPID_PKT_SEND_GET(pkt,len) );\
	MPID_Print_mode( MPID_DEBUG_FILE, MPID_PKT_SEND_ADDR(pkt) );\
	fprintf( MPID_DEBUG_FILE, "(%s:%d)\n", __FILE__, __LINE__ );\
	fflush( MPID_DEBUG_FILE );\
	}}

#define DEBUG_PRINT_BASIC_SEND_PKT(msg,pkt)\
    {if (MPID_DebugFlag) {\
	fprintf( MPID_DEBUG_FILE,\
		"[%d]%s ", MPID_MyWorldRank, msg );\
	MPID_Print_packet( MPID_DEBUG_FILE, \
			  (MPID_PKT_T *)MPID_PKT_SEND_ADDR(pkt) );\
	fprintf( MPID_DEBUG_FILE, "(%s:%d)\n", __FILE__, __LINE__ );\
	fflush( MPID_DEBUG_FILE );\
	}}

#define DEBUG_PRINT_FULL_SEND_PKT(msg,pkt)\
    {if (MPID_DebugFlag) {\
	fprintf( MPID_DEBUG_FILE,\
"[%d]%s of tag = %d, dest = %d, ctx = %d, len = %d, mode = ", \
	       MPID_MyWorldRank, msg, MPID_PKT_SEND_GET(pkt,tag), dest, \
	       MPID_PKT_SEND_GET(pkt,context_id), \
	       MPID_PKT_SEND_GET(pkt,len) );\
	MPID_Print_mode( MPID_DEBUG_FILE, MPID_PKT_SEND_ADDR(pkt) );\
	fprintf( MPID_DEBUG_FILE, "(%s:%d)\n", __FILE__, __LINE__ );\
	MPID_Print_packet( MPID_DEBUG_FILE,\
			  (MPID_PKT_T *)MPID_PKT_SEND_ADDR(pkt) );\
	fflush( MPID_DEBUG_FILE );\
	}}

#define DEBUG_PRINT_MSG(msg)\
{if (MPID_DebugFlag) {\
    fprintf( MPID_DEBUG_FILE, "[%d]%s (%s:%d)\n", \
	    MPID_MyWorldRank, msg, __FILE__, __LINE__ );\
    fflush( MPID_DEBUG_FILE );}}
	    
#define DEBUG_PRINT_RECV_PKT(msg,pkt)\
    {if (MPID_DebugFlag) {\
	fprintf( MPID_DEBUG_FILE,\
"[%d]%s for tag = %d, source = %d, ctx = %d, len = %d, mode = ", \
	       MPID_MyWorldRank, msg, MPID_PKT_RECV_GET(pkt,head.tag), from, \
	       MPID_PKT_RECV_GET(pkt,head.context_id), \
	       MPID_PKT_RECV_GET(pkt,head.len) );\
	MPID_Print_mode( MPID_DEBUG_FILE, MPID_PKT_RECV_ADDR(pkt) );\
	fprintf( MPID_DEBUG_FILE, "(%s:%d)\n", __FILE__, __LINE__ );\
	fflush( MPID_DEBUG_FILE );\
	}}

#define DEBUG_PRINT_FULL_RECV_PKT(msg,pkt)\
    {if (MPID_DebugFlag) {\
	fprintf( MPID_DEBUG_FILE,\
"[%d]%s for tag = %d, source = %d, ctx = %d, len = %d, mode = ", \
	       MPID_MyWorldRank, msg, MPID_PKT_RECV_GET(pkt,head.tag), from, \
	       MPID_PKT_RECV_GET(pkt,head.context_id), \
	       MPID_PKT_RECV_GET(pkt,head.len) );\
	MPID_Print_mode( MPID_DEBUG_FILE, MPID_PKT_RECV_ADDR(pkt) );\
	fprintf( MPID_DEBUG_FILE, "(%s:%d)\n", __FILE__, __LINE__ );\
	MPID_Print_packet( MPID_DEBUG_FILE,\
			  (MPID_PKT_T *)MPID_PKT_SEND_ADDR(pkt) );\
	fflush( MPID_DEBUG_FILE );\
	}}

#define DEBUG_PRINT_SYNCACK(msg,pkt) \
if (MPID_DebugFlag) {\
    fprintf( MPID_DEBUG_FILE,\
	   "[%d]%s Returning sync to %d with mode ", MPID_MyWorldRank,\
	   (msg?msg:"SYNC"), from );\
    MPID_Print_mode( MPID_DEBUG_FILE, pkt );\
    fprintf( MPID_DEBUG_FILE, "(%s:%d)\n", __FILE__, __LINE__ );\
    fflush( MPID_DEBUG_FILE );\
    }

#define DEBUG_PRINT_PKT(msg,pkt)    \
if (MPID_DebugFlag) {\
    fprintf( MPID_DEBUG_FILE,\
   "[%d]%s (%s:%d)\n", MPID_MyWorldRank, msg, __FILE__, __LINE__ );\
    MPID_Print_packet( MPID_DEBUG_FILE, MPID_PKT_RECV_ADDR(pkt) );\
    }

#define DEBUG_PRINT_PKT_DATA(msg,pkt)\
    if (MPID_DebugFlag) {\
	MPID_MEIKO_Print_pkt_data( msg, MPID_PKT_SEND_GET(pkt,buffer), len );\
	}

#define DEBUG_PRINT_LONG_MSG(msg,pkt)     \
if (MPID_DebugFlag) {\
    fprintf( MPID_DEBUG_FILE, \
	   "[%d]S Getting data from mpid->start, first int is %d (%s:%d)\n",\
	   MPID_MyWorldRank, *(int *)mpid_send_handle->start, \
	   __FILE__, __LINE__ );\
    fprintf( MPID_DEBUG_FILE, "[%d]%s (%s:%d)...\n", \
	    MPID_MyWorldRank, msg, __FILE__, __LINE__ );\
    MPID_Print_packet( MPID_DEBUG_FILE, \
		      (MPID_PKT_T*)MPID_PKT_SEND_ADDR(pkt) );\
    fflush( MPID_DEBUG_FILE );\
    }

#else
#define DEBUG_PRINT_PKT(msg,pkt)
#define DEBUG_PRINT_MSG(msg)
#define DEBUG_PRINT_SEND_PKT(msg,pkt)
#define DEBUG_PRINT_BASIC_SEND_PKT(msg,pkt)
#define DEBUG_PRINT_FULL_SEND_PKT(msg,pkt)
#define DEBUG_PRINT_RECV_PKT(msg,pkt)
#define DEBUG_PRINT_FULL_RECV_PKT(msg,pkt)
#define DEBUG_PRINT_SYNCACK(msg,pkt)
#define DEBUG_PRINT_PKT_DATA(msg,pkt)
#define DEBUG_PRINT_LONG_MSG(msg,pkt)     

#endif


