/*
 *  $Id: shmemdebug.c,v 1.13 2001/04/03 19:14:52 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */


#include "mpid.h"
#include "mpiddev.h"
#include "chpackflow.h"
#include <string.h>

#ifdef USE_HOLD_LAST_DEBUG
char ch_debug_buf[CH_MAX_DEBUG_LINE];
#endif

/* Grumble.  It is no longer valid to initialize FILEs to things like stderr 
   at compile time.  
 */
FILE *MPID_DEBUG_FILE = 0;
FILE *MPID_TRACE_FILE = 0;
int MPID_UseDebugFile = 0;

int MPID_DebugFlag = 0;

void MPID_Get_print_pkt ( FILE *, MPID_PKT_T *);
int MPID_Cancel_print_pkt ( FILE *, MPID_PKT_T *);
int  MPID_Rndv_print_pkt (FILE *, MPID_PKT_T *);
void MPID_Print_Send_Handle ( MPIR_SHANDLE * );

/* Should each mode have its own print routines? */

int MPID_Print_packet( fp, pkt )
FILE        *fp;
MPID_PKT_T  *pkt;
{
    FPRINTF( fp, "[%d] PKT =\n", MPID_MyWorldRank );
    switch (pkt->head.mode) {
    case MPID_PKT_SHORT:
	FPRINTF( fp, "\
\tlen        = %d\n\
\ttag        = %d\n\
\tcontext_id = %d\n\
\tlrank      = %d\n\
\tseqnum     = %d\n\
\tmode       = ", 
	pkt->head.len, pkt->head.tag, pkt->head.context_id, pkt->head.lrank,
        pkt->head.seqnum );
	break;
    case MPID_PKT_REQUEST_SEND_GET:
    case MPID_PKT_SEND_ADDRESS:
    case MPID_PKT_OK_TO_SEND_GET:
    case MPID_PKT_CONT_GET:
	MPID_Get_print_pkt( fp, pkt );
	break;
    case MPID_PKT_ANTI_SEND:
    case MPID_PKT_ANTI_SEND_OK:
	MPID_Cancel_print_pkt( fp, pkt );
	break;
    case MPID_PKT_PROTO_ACK:
    case MPID_PKT_ACK_PROTO:
#ifdef MPID_PACK_CONTROL
	fprintf( fp, "\
\tlrank  = %d\n\
\tto     = %d\n\
\tmode   = ",
	pkt->head.lrank, pkt->head.to);
#endif      
	break;
    default:
	FPRINTF( fp, "\n" );
    }
    MPID_Print_mode( fp, pkt );
    FPUTS( "\n", fp );
    return MPI_SUCCESS;
}

int MPID_Cancel_print_pkt( fp, pkt )
FILE       *fp;
MPID_PKT_T *pkt;
{
    /* A "send_id" is a 64bit item on heterogeneous systems.  On 
       systems without 64bit longs, we need special code to print these.
       To help keep the output "nearly" atomic, we first convert the
       send_id to a string, and then print that
       */
    char sendid[40];
    MPID_Aint send_id;

    send_id = pkt->antisend_pkt.send_id;
    sprintf( sendid, "%lx", (long)send_id );

    if (pkt->head.mode != MPID_PKT_ANTI_SEND_OK)
	fprintf( fp, "\
\tlrank      = %d\n\
\tdest       = %d\n\
\tsend_id    = %s\n\
\tmode       = ", 
	pkt->head.lrank, pkt->head.to, sendid);
    else
	fprintf( fp, "\
\tlrank      = %d\n\
\tdest       = %d\n\
\tcancel     = %d\n\
\tsend_id    = %s\n\
\tmode       = ", 
	pkt->head.lrank, pkt->head.to, pkt->antisend_pkt.cancel, sendid);

    return MPI_SUCCESS;
}

void MPID_Get_print_pkt( fp, pkt )
FILE       *fp;
MPID_PKT_T *pkt;
{

    /* A "send_id" and "recv_id" are 64bit items on heterogeneous systems.  On 
       systems without 64bit longs, we need special code to print these.
       To help keep the output "nearly" atomic, we first convert the
       send_id to a string, and then print that
       */
    char sendid[40];
    char recvid[40];
    MPID_Aint send_id;
    MPID_Aint recv_id;

    if (pkt->head.mode != MPID_PKT_SEND_ADDRESS) {
	/* begin if mode != address */
	send_id = pkt->get_pkt.send_id;
	sprintf( sendid, "%lx", (long)send_id );
	if (pkt->head.mode != MPID_PKT_REQUEST_SEND_GET) {
	    recv_id = pkt->get_pkt.recv_id;
	    sprintf( recvid, "%lx", (long)recv_id );
	}
    }  /* end if mode != address */

#ifndef MPID_HAS_HETERO
/* Casts from MPI_Aint to long are used to match the format descriptor (lx);
   this will be ok as long as MPI_Aint is not long long.  In that case,
   we'd need to edit the format.  For that, we could use ANSI C preprocessor
   string concatenation */
	if (pkt->head.mode == MPID_PKT_SEND_ADDRESS)
	    fprintf( fp, "\
\tlen        = %d\n\
\ttag        = %d\n\
\tcontext_id = %d\n\
\tlrank      = %d\n\
\taddress    = %lx\n\
\tmode       = ", 
	pkt->head.len, pkt->head.tag, pkt->head.context_id, pkt->head.lrank,
	     (long)(MPI_Aint)pkt->get_pkt.address );
	
	else if (pkt->head.mode == MPID_PKT_REQUEST_SEND_GET)
	    fprintf( fp, "\
\tlen        = %d\n\
\ttag        = %d\n\
\tcontext_id = %d\n\
\tlrank      = %d\n\
\tsend_id    = %lx\n\
\tmode       = ", 
	pkt->head.len, pkt->head.tag, pkt->head.context_id, pkt->head.lrank,
	(long) pkt->get_pkt.send_id );
	else fprintf( fp, "\
\tcur_offset = %d\n\
\tlen_avail  = %d\n\
\tsend_id    = %lx\n\
\trecv_id    = %lx\n\
\taddress    = %lx\n\
\tmode       = ", 
	pkt->get_pkt.cur_offset, pkt->get_pkt.len_avail, 
		      (long)pkt->get_pkt.send_id,
	(long)pkt->get_pkt.recv_id, (long)(MPI_Aint)pkt->get_pkt.address );
#endif
}

int MPID_Print_mode( fp, pkt )
FILE        *fp;
MPID_PKT_T  *pkt;
{
    char *modename=0;
    switch (pkt->short_pkt.mode) {
    case MPID_PKT_SHORT:
	FPUTS( "short", fp );
	break;
    case MPID_PKT_SEND_ADDRESS:
	FPUTS( "send address", fp );
	break;
    case MPID_PKT_REQUEST_SEND_GET:
	FPUTS( "do get", fp );
	break; 
    case MPID_PKT_OK_TO_SEND_GET:
	FPUTS( "ok to send get", fp );
	break;
    case MPID_PKT_CONT_GET:
	FPUTS( "continue get", fp );
	break;
    case MPID_PKT_FLOW:
	fputs( "flow control", fp );
	break;
    case MPID_PKT_PROTO_ACK:
	fputs( "protocol ACK", fp );
        break;
    case MPID_PKT_ACK_PROTO:
	fputs( "Ack protocol", fp );
	break;
    case MPID_PKT_ANTI_SEND:
	fputs( "anti send", fp );
	break;
    case MPID_PKT_ANTI_SEND_OK:
	fputs( "anti send ok", fp );
	break;
    default:
	fprintf( fp, "Mode %d is unknown!\n", pkt->short_pkt.mode );
	break;
    }
    /* if (MPID_MODE_HAS_XDR(pkt)) FPUTS( "xdr", fp ); */

    if (modename) {
	FPUTS( modename, fp );
    }
    return MPI_SUCCESS;
}
    
void MPID_Print_pkt_data( msg, address, len )
char *msg;
char *address;
int  len;
{
    int i; char *aa = (char *)address;

    if (!MPID_DEBUG_FILE) MPID_DEBUG_FILE = stderr;
    if (msg)
	FPRINTF( MPID_DEBUG_FILE, "[%d]%s\n", MPID_MyWorldRank, msg );
    if (len < 78 && address) {
	for (i=0; i<len; i++) {
	    FPRINTF( MPID_DEBUG_FILE, "%x", aa[i] );
	}
	FPRINTF( MPID_DEBUG_FILE, "\n" );
    }
    fflush( MPID_DEBUG_FILE );
}

void MPID_Print_Send_Handle( shandle )
MPIR_SHANDLE *shandle;
{
    FPRINTF( stdout, "[%d]* dmpi_send_contents:\n\
* totallen    = %d\n\
* recv_handle = %x\n", MPID_MyWorldRank, 
		 shandle->bytes_as_contig, 
		 shandle->recv_handle );
}

void MPID_SetDebugFile( name )
char *name;
{
    char filename[1024];
    
    if (strcmp( name, "-" ) == 0) {
	MPID_DEBUG_FILE = stdout;
	return;
    }
    if (strchr( name, '%' )) {
	sprintf( filename, name, MPID_MyWorldRank );
	MPID_DEBUG_FILE = fopen( filename, "w" );
    }
    else
	MPID_DEBUG_FILE = fopen( name, "w" );

    if (!MPID_DEBUG_FILE) MPID_DEBUG_FILE = stdout;
}

void MPID_Set_tracefile( name )
char *name;
{
    char filename[1024];

    if (strcmp( name, "-" ) == 0) {
	MPID_TRACE_FILE = stdout;
	return;
    }
    if (strchr( name, '%' )) {
	sprintf( filename, name, MPID_MyWorldRank );
	MPID_TRACE_FILE = fopen( filename, "w" );
    }
    else
	MPID_TRACE_FILE = fopen( name, "w" );

    /* Is this the correct thing to do? */
    if (!MPID_TRACE_FILE)
	MPID_TRACE_FILE = stdout;
}

void MPID_SetSpaceDebugFlag( flag )
int flag;
{
/*      DebugSpace = flag; */
#ifdef CHAMELEON_COMM   /* #CHAMELEON_START# */
/* This file may be used to generate non-Chameleon versions */
    if (flag) {
	/* Check the validity of the malloc arena on every use of 
	   trmalloc/free */
	trDebugLevel( 1 );
    }
#endif                  /* #CHAMELEON_END# */
}
void MPID_SetDebugFlag( f )
int f;
{
    MPID_DebugFlag = f;
    MPID_UseDebugFile = f;
}

/*
   Data about messages
 */
static int DebugMsgFlag = 0;
void MPID_SetMsgDebugFlag( f )
int f;
{
    DebugMsgFlag = f;
}
int MPID_GetMsgDebugFlag()
{
    return DebugMsgFlag;
}
void MPID_PrintMsgDebug()
{
}

/*
 * Print information about a request
 */
void MPID_Print_rhandle( fp, rhandle )
FILE *fp;
MPIR_RHANDLE *rhandle;
{
    FPRINTF( fp, "rhandle at %lx\n\
\tcookie     \t= %lx\n\
\tis_complete\t= %d\n\
\tbuf        \t= %lx\n", 
	     (long)(MPI_Aint)rhandle, 
#ifdef MPIR_HAS_COOKIES
	     rhandle->cookie, 
#else
	     0,
#endif
	     rhandle->is_complete, 
	     (long)(MPI_Aint)rhandle->buf );
}

void MPID_Print_shandle( fp, shandle )
FILE *fp;
MPIR_SHANDLE *shandle;
{
    FPRINTF( fp, "shandle at %lx\n\
\tcookie     \t= %lx\n\
\tis_complete\t= %d\n\
\tstart      \t= %lx\n\
\tbytes_as_contig\t= %d\n\
", 
	     (long)(MPI_Aint)shandle, 
#ifdef MPIR_HAS_COOKIES
	     shandle->cookie, 
#else
	     0,
#endif
	     shandle->is_complete, 
	     (long)(MPI_Aint)shandle->start,
	     shandle->bytes_as_contig
 );
}
