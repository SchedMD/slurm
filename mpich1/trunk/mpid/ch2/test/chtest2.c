/*
** This channel-test is extended in the way that it fills up the
** header with junk, sends it as a header, sends is with the
** SendChannel-call, and compares the contents.
*/
#include "mpid.h"
#include <stdio.h>
/* Set to stdout to get trace */
static FILE *MPID_TRACE_FILE = 0; 

int main(argc,argv)
int argc;
char **argv;
{
MPID_PKT_SHORT_T pkt,pkt2;
int        from, ntest, i,j, percent;

ntest = 100000;  percent = ntest/50;

PIiInit( &argc, &argv );
if (MPID_WorldSize != 2) {
    fprintf( stderr, "\n", MPID_WorldSize );
    }
for (i=0; i<ntest; i++) {
    if (MPID_MyWorldRank == 0) {

    /* fill in some junk and send */
    for(j=0;j<sizeof(MPID_PKT_SHORT_T);j++)
        ((char *)&pkt)[j] = (char)i%(j+1);
    MPID_SendControl( &pkt, sizeof(MPID_PKT_SHORT_T), 1 );
    MPID_SendChannel( &pkt, sizeof(MPID_PKT_SHORT_T), 1);

    /* receive the two blocks */
	from = -1;
	MPID_RecvAnyControl( &pkt, sizeof(MPID_PKT_SHORT_T), &from );
	if (from != 1) {
	    fprintf( stderr, 
		"0 iteration %d : received message from %d, expected 1\n",
		    i,from );
	    }
    MPID_RecvFromChannel(&pkt2 , sizeof(MPID_PKT_SHORT_T), 1);

    /* compare them */
    for(j=0;j<sizeof(MPID_PKT_SHORT_T);j++)
       if(((char *)&pkt)[j] != ((char *)&pkt2)[j]) {
	      fprintf( stderr, 
		  "1 iteration %d : messages differ in byte %d\n",i,j );
	    }
	}
    else {
	from = -1;

    /* receive the two blocks */
	MPID_RecvAnyControl( &pkt, sizeof(MPID_PKT_SHORT_T), &from );
	if (from != 0) {
	    fprintf( stderr, 
		"1 iteration %d : received message from %d, expected 0\n",
		    i,from );
	    }
    MPID_RecvFromChannel(&pkt2 , sizeof(MPID_PKT_SHORT_T), 0);

    /* compare them */
    for(j=0;j<sizeof(MPID_PKT_SHORT_T);j++)
       if(((char *)&pkt)[j] != ((char *)&pkt2)[j]) {
	      fprintf( stderr, 
		  "1 iteration %d : messages differ in byte %d\n",i,j );
	    }

    /* fill in some junk and send */
    for(j=0;j<sizeof(MPID_PKT_SHORT_T);j++)
        ((char *)&pkt)[j] = (char)i%(j+1);
    MPID_SendControl( &pkt, sizeof(MPID_PKT_SHORT_T), 0 );
    MPID_SendChannel( &pkt, sizeof(MPID_PKT_SHORT_T), 0);

	}
    if(i % percent == 0)
       fprintf( stderr, "%d iterations (of %d) done\n",i,ntest);
    }
PIiFinish();
return 0;
}
