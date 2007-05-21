/*
 *  $Id: shmemdebug.c,v 1.1.1.1 1997/09/17 20:41:31 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */


#include "mpid.h"
#include "mpiddev.h"
#include <string.h>

FILE *MPID_DEBUG_FILE = stderr;
FILE *MPID_TRACE_FILE = 0;
int MPID_DebugFlag = 0;

void MPID_Get_print_pkt ANSI_ARGS(( FILE *, MPID_PKT_T *));
int  MPID_Rndv_print_pkt ANSI_ARGS((FILE *, MPID_PKT_T *));
void MPID_Print_Send_Handle ANSI_ARGS(( MPIR_SHANDLE * ));

/* Should each mode have its own print routines? */

int MPID_Print_packet( fp, pkt )
FILE        *fp;
MPID_PKT_T  *pkt;
{
    fprintf( fp, "[%d] PKT =\n", MPID_MyWorldRank );
    switch (pkt->head.mode) {
    case MPID_PKT_SHORT:
	fprintf( fp, "\
\tlen        = %d\n\
\ttag        = %d\n\
\tcontext_id = %d\n\
\tlrank      = %d\n\
\tmode       = ", 
	pkt->head.len, pkt->head.tag, pkt->head.context_id, pkt->head.lrank );
	break;
    case MPID_PKT_REQUEST_SEND_GET:
    case MPID_PKT_SEND_ADDRESS:
    case MPID_PKT_OK_TO_SEND_GET:
    case MPID_PKT_CONT_GET:
	MPID_Get_print_pkt( fp, pkt );
	break;
    default:
	fprintf( fp, "\n" );
    }
    MPID_Print_mode( fp, pkt );
    fputs( "\n", fp );
    return MPI_SUCCESS;
}

void MPID_Get_print_pkt( fp, pkt )
FILE       *fp;
MPID_PKT_T *pkt;
{
#ifndef MPID_HAS_HETERO
    fprintf( fp, "\
\tlen        = %d\n\
\ttag        = %d\n\
\tcontext_id = %d\n\
\tlrank      = %d\n\
\tlen_avail  = %d\n\
\tsend_id    = %lx\n\
\taddress    = %lx\n\
\tmode       = ", 
	pkt->head.len, pkt->head.tag, pkt->head.context_id, pkt->head.lrank,
/*      pkt->get_pkt.cur_offset, */
        pkt->get_pkt.len_avail, 
	     (long)pkt->get_pkt.send_id,
/*           (long)pkt->get_pkt.recv_id, */
	     (long)pkt->get_pkt.address );
/* \tcur_offset = %d\n\
   \trecv_id    = %lx\n\ */
#endif
}

int MPID_Print_mode( fp, pkt )
FILE        *fp;
MPID_PKT_T  *pkt;
{
    char *modename=0;
    switch (pkt->short_pkt.mode) {
    case MPID_PKT_SHORT:
	fputs( "short", fp );
	break;
    case MPID_PKT_SEND_ADDRESS:
	fputs( "send address", fp );
	break;
    case MPID_PKT_REQUEST_SEND_GET:
	fputs( "do get", fp );
	break; 
    case MPID_PKT_OK_TO_SEND_GET:
	fputs( "ok to send get", fp );
	break;
    case MPID_PKT_CONT_GET:
	fputs( "continue get", fp );
	break;
    default:
	fprintf( fp, "Mode %d is unknown!\n", pkt->short_pkt.mode );
	break;
    }
    /* if (MPID_MODE_HAS_XDR(pkt)) fputs( "xdr", fp ); */

    if (modename) {
	fputs( modename, fp );
    }
    return MPI_SUCCESS;
}
    
void MPID_Print_pkt_data( msg, address, len )
char *msg;
char *address;
int  len;
{
    int i; char *aa = (char *)address;

    if (msg)
	fprintf( MPID_DEBUG_FILE, "[%d]%s\n", MPID_MyWorldRank, msg );
    if (len < 78 && address) {
	for (i=0; i<len; i++) {
	    fprintf( MPID_DEBUG_FILE, "%x", aa[i] );
	}
	fprintf( MPID_DEBUG_FILE, "\n" );
    }
    fflush( MPID_DEBUG_FILE );
}

void MPID_Print_Send_Handle( shandle )
MPIR_SHANDLE *shandle;
{
    fprintf( stdout, "[%d]* dmpi_send_contents:\n\
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
    fprintf( fp, "rhandle at %lx\n\
\tcookie     \t= %lx\n\
\tis_complete\t= %d\n\
\tbuf        \t= %lx\n", 
	     (long)rhandle, 
#ifdef MPIR_HAS_COOKIES
	     rhandle->cookie, 
#else
	     0,
#endif
	     rhandle->is_complete, 
	     (long)rhandle->buf );
}
void MPID_Print_shandle( fp, shandle )
FILE *fp;
MPIR_SHANDLE *shandle;
{
    fprintf( fp, "shandle at %lx\n\
\tcookie     \t= %lx\n\
\tis_complete\t= %d\n\
\tstart      \t= %lx\n\
\tbytes_as_contig\t= %d\n\
", 
	     (long)shandle, 
#ifdef MPIR_HAS_COOKIES
	     shandle->cookie, 
#else
	     0,
#endif
	     shandle->is_complete, 
	     (long)shandle->start,
	     shandle->bytes_as_contig
 );
}
