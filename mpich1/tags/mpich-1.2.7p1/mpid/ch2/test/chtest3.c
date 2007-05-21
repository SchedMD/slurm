#include "mpid.h"
#include <stdio.h>
/* Set to stdout to get trace */
static FILE *MPID_TRACE_FILE = 0; 

#define MAX_BUF 10000

/*
   This version tests the nonblocking routines used for non-blocking
   eager transfers.
 */
int main(argc,argv)
int argc;
char **argv;
{
MPID_PKT_SHORT_T pkt;
int        from, ntest, i;
int        bufsize = 100;
char       buf[MAX_BUF];
ntest = 10000;
ASYNCSendId_t sid;
ASYNCRecvId_t rid;

PIiInit( &argc, &argv );

if (MPID_WorldSize != 2) {
    fprintf( stderr, "\n", MPID_WorldSize );
    }
for (i=0; i<ntest; i++) {
    if (MPID_MyWorldRank == 0) {
	MPID_SendControl( &pkt, sizeof(MPID_PKT_SHORT_T), 1 );
	MPID_ISendChannel( buf, bufsize, 1, sid );
	from = -1;
	MPID_RecvAnyControl( &pkt, sizeof(MPID_PKT_SHORT_T), &from );
	if (from != 1) {
	    fprintf( stderr, 
		"0 received message from %d, expected 1\n", from );
	    }
	MPID_WSendChannel( buf, bufsize, 1, sid );
	}
    else {
	from = -1;
	MPID_RecvAnyControl( &pkt, sizeof(MPID_PKT_SHORT_T), &from );
	if (from != 0) {
	    fprintf( stderr, 
		"1 received message from %d, expected 0\n", from );
	    }
	MPID_IRecvChannel( buf, bufsize, 0, rid );
	MPID_SendControl( &pkt, sizeof(MPID_PKT_SHORT_T), 0 );
	MPID_WRecvChannel( buf, bufsize, 0, rid );
	}
    }
PIiFinish();
return 0;
}

