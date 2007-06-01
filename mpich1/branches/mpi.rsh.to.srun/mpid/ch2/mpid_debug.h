#ifndef _MPID_DEBUG_H
#define _MPID_DEBUG_H
#include <stdio.h>

int  MPID_Print_mode   ( FILE *, MPID_PKT_T * );
int  MPID_Print_packet ( FILE *, MPID_PKT_T * );
void MPID_Print_pkt_data ( char *, char *, int );
void MPID_SetMsgDebugFlag (int);
int  MPID_GetMsgDebugFlag (void);
void MPID_PrintMsgDebug   (void);
void MPID_Print_rhandle   ( FILE *, MPIR_RHANDLE * );
void MPID_Print_shandle   ( FILE *, MPIR_SHANDLE * );
void MPID_Print_Short_data ( MPID_PKT_SHORT_T * );
void MPID_Print_last_args( char * );
void MPID_Ch_dprint_last( void );
void MPID_Ch_send_last_p4error( char * );

#define CH_MAX_DEBUG_LINE 128
#define CH_LAST_DEBUG 128

#ifdef MPID_DEBUG_ALL  

/* If messages are held for printing if an error is detected, add them
   to the message ring.  Otherwise do nothing */
extern char ch_debug_buf[CH_MAX_DEBUG_LINE];
#ifdef USE_HOLD_LAST_DEBUG
#define MPID_SAVE_MSG MPID_Print_last_args(ch_debug_buf)
#else
#define MPID_SAVE_MSG 
#endif

#define MPID_DEBUG_PRINTF(args) if (MPID_DebugFlag) { SPRINTF args ;\
    MPID_DEBUG_MSG;MPID_SAVE_MSG;}

#define MPID_DEBUG_MSG if (MPID_UseDebugFile) { \
    fputs( ch_debug_buf, MPID_DEBUG_FILE); fflush( MPID_DEBUG_FILE );}

/***************************************************************************/
/* This variable controls debugging output                                 */
/***************************************************************************/

extern int MPID_DebugFlag;
extern int MPID_UseDebugFile;
extern FILE *MPID_DEBUG_FILE;

/* Use these instead of printf to simplify finding stray error messages */
#ifndef FPRINTF
#define FPRINTF fprintf
#define PRINTF printf
#define SPRINTF sprintf
#endif

#ifdef MEMCPY
#undef MEMCPY
#endif
#define MEMCPY(a,b,c)\
{  if (MPID_DebugFlag) {\
     SPRINTF( ch_debug_buf, \
	      "[%d]R About to copy to %lx from %lx (n=%d) (%s:%d)...\n", \
           MPID_MyWorldRank, (long)a, (long)b, c, __FILE__, __LINE__ );\
     MPID_DEBUG_MSG;\
     MPID_SAVE_MSG;}\
memcpy( a, b, c );}

/* Print standard send/recv args */
#define DEBUG_PRINT_ARGS(msg)\
{  if (MPID_DebugFlag) {\
     SPRINTF( ch_debug_buf, \
              "[%d]%s for tag = %d, source = %d, ctx = %d, (%s:%d)\n", \
           MPID_MyWorldRank, msg, tag, src_lrank, context_id, \
            __FILE__, __LINE__ );\
     MPID_DEBUG_MSG;\
     MPID_SAVE_MSG;}}

#define DEBUG_PRINT_SEND_PKT(msg,pkt)\
{  if (MPID_DebugFlag) {\
     SPRINTF( ch_debug_buf, \
              "[%d]%s of tag = %d, dest = %d, ctx = %d, len = %d, mode = ", \
	   MPID_MyWorldRank, msg, (pkt)->tag, dest, \
           (pkt)->context_id, (pkt)->len );\
     if (MPID_UseDebugFile) {\
       fputs( ch_debug_buf, MPID_DEBUG_FILE );\
       MPID_Print_mode( MPID_DEBUG_FILE, (MPID_PKT_T *)(pkt) );\
       FPRINTF( MPID_DEBUG_FILE, "(%s:%d)\n", __FILE__, __LINE__ );\
       if ((pkt)->mode == MPID_PKT_SHORT) {\
           MPID_Print_Short_data( (MPID_PKT_SHORT_T*)pkt );\
       }\
       fflush( MPID_DEBUG_FILE );}\
     MPID_SAVE_MSG;}}

#define DEBUG_PRINT_BASIC_SEND_PKT(msg,pkt)\
{  if (MPID_DebugFlag) {\
     SPRINTF( ch_debug_buf,\
	      "[%d]%s ", MPID_MyWorldRank, msg );\
     if (MPID_UseDebugFile) {\
       fputs( ch_debug_buf, MPID_DEBUG_FILE );\
       MPID_Print_packet( MPID_DEBUG_FILE, (MPID_PKT_T *)(pkt) );\
       FPRINTF( MPID_DEBUG_FILE, "(%s:%d)\n", __FILE__, __LINE__ );\
       fflush( MPID_DEBUG_FILE );}\
     MPID_SAVE_MSG;}}

#define DEBUG_PRINT_FULL_SEND_PKT(msg,pkt)\
{  if (MPID_DebugFlag) {\
     if (MPID_UseDebugFile) {\
       FPRINTF( MPID_DEBUG_FILE,\
              "[%d]%s of tag = %d, dest = %d, ctx = %d, len = %d, mode = ", \
	     MPID_MyWorldRank, msg, (pkt)->tag, dest, (pkt)->context_id, \
	     (pkt)->len );\
       MPID_Print_mode( MPID_DEBUG_FILE, (MPID_PKT_T *)(pkt) );\
       FPRINTF( MPID_DEBUG_FILE, "(%s:%d)\n", __FILE__, __LINE__ );\
       MPID_Print_packet( MPID_DEBUG_FILE, (MPID_PKT_T *)(pkt) );\
       fflush( MPID_DEBUG_FILE );}}}

#define DEBUG_PRINT_MSG(msg)\
{  if (MPID_DebugFlag) {\
     SPRINTF( ch_debug_buf, "[%d]%s (%s:%d)\n", \
           MPID_MyWorldRank, msg, __FILE__, __LINE__ );\
     MPID_DEBUG_MSG;\
     MPID_SAVE_MSG;}}

#define DEBUG_PRINT_MSG2(msg,val)\
{if (MPID_DebugFlag) {\
    char localbuf[1024]; sprintf( localbuf, msg, val );\
    DEBUG_PRINT_MSG(localbuf);}}
	    
#define DEBUG_PRINT_RECV_PKT(msg,pkt)\
{  if (MPID_DebugFlag) {\
     SPRINTF( ch_debug_buf,\
            "[%d]%s for tag = %d, source = %d, ctx = %d, len = %d, mode = ", \
	    MPID_MyWorldRank, msg, (pkt)->head.tag, from_grank, \
	    (pkt)->head.context_id, (pkt)->head.len );\
     if (MPID_UseDebugFile) {\
       fputs( ch_debug_buf, MPID_DEBUG_FILE );\
       MPID_Print_mode( MPID_DEBUG_FILE, (MPID_PKT_T *)(pkt) );\
       FPRINTF( MPID_DEBUG_FILE, "(%s:%d)\n", __FILE__, __LINE__ );\
       fflush( MPID_DEBUG_FILE );}\
     MPID_SAVE_MSG;}}

#define DEBUG_PRINT_FULL_RECV_PKT(msg,pkt)\
    {if (MPID_DebugFlag) {\
	FPRINTF( MPID_DEBUG_FILE,\
"[%d]%s for tag = %d, source = %d, ctx = %d, len = %d, mode = ", \
	       MPID_MyWorldRank, msg, (pkt)->head.tag, from, \
	       (pkt)->head.context_id, \
	       (pkt)->head.len );\
        if (MPID_UseDebugFile) \
	  MPID_Print_mode( MPID_DEBUG_FILE, (MPID_PKT_T *)(pkt) );\
	  FPRINTF( MPID_DEBUG_FILE, "(%s:%d)\n", __FILE__, __LINE__ );\
	  MPID_Print_packet( MPID_DEBUG_FILE,\
		             (MPID_PKT_T *)(pkt) );\
	   fflush( MPID_DEBUG_FILE );}\
	}}

#define DEBUG_PRINT_PKT(msg,pkt) \
{  if (MPID_DebugFlag) {\
     SPRINTF( ch_debug_buf,\
           "[%d]%s (%s:%d)\n", MPID_MyWorldRank, msg, __FILE__, __LINE__ );\
     if (MPID_UseDebugFile) {\
       fputs( ch_debug_buf, MPID_DEBUG_FILE );\
       MPID_Print_packet( MPID_DEBUG_FILE, (pkt) );\
       fflush( MPID_DEBUG_FILE );}\
     MPID_SAVE_MSG;}}	

#define DEBUG_PRINT_PKT_DATA(msg,pkt)\
    if (MPID_DebugFlag) {\
      if (MPID_UseDebugFile) {\
	MPID_Print_pkt_data( msg, (pkt)->buffer, len );}\
	}

#define DEBUG_PRINT_LONG_MSG(msg,pkt)     \
if (MPID_DebugFlag) {\
    if (MPID_UseDebugFile) {\
      FPRINTF( MPID_DEBUG_FILE, \
	     "[%d]S Getting data from mpid->start, first int is %d (%s:%d)\n",\
	     MPID_MyWorldRank, *(int *)mpid_send_handle->start, \
	     __FILE__, __LINE__ );\
      FPRINTF( MPID_DEBUG_FILE, "[%d]%s (%s:%d)...\n", \
	      MPID_MyWorldRank, msg, __FILE__, __LINE__ );\
      MPID_Print_packet( MPID_DEBUG_FILE, \
		         (MPID_PKT_T*)(pkt) );\
      fflush( MPID_DEBUG_FILE );}\
    }

#define DEBUG_TEST_FCN(fcn,msg) {\
    if (!fcn) {\
      SPRINTF( ch_debug_buf, "Bad function pointer (%s) in %s at %d\n",\
	       msg, __FILE__, __LINE__);\
     MPID_DEBUG_MSG\
     MPID_SAVE_MSG;\
     MPID_Abort( (struct MPIR_COMMUNICATOR *)0, 1, "MPI internal", "Bad function pointer" );\
      }}

/* This is pretty expensive.  It should be an option ... */
#ifdef DEBUG_INIT_MEM
#define DEBUG_INIT_STRUCT(s,size) memset(s,0xfa,size)		
#else
#define DEBUG_INIT_STRUCT(s,size)
#endif

#else
#define DEBUG_PRINT_PKT(msg,pkt)
#define DEBUG_PRINT_MSG(msg)
#define DEBUG_PRINT_MSG2(msg,val)
#define DEBUG_PRINT_ARGS(msg) 
#define DEBUG_PRINT_SEND_PKT(msg,pkt)
#define DEBUG_PRINT_BASIC_SEND_PKT(msg,pkt)
#define DEBUG_PRINT_FULL_SEND_PKT(msg,pkt)
#define DEBUG_PRINT_RECV_PKT(msg,pkt)
#define DEBUG_PRINT_FULL_RECV_PKT(msg,pkt)
#define DEBUG_PRINT_PKT_DATA(msg,pkt)
#define DEBUG_PRINT_LONG_MSG(msg,pkt)     
#define DEBUG_TEST_FCN(fcn,msg)
#define DEBUG_INIT_STRUCT(s,size)
#endif

#endif
