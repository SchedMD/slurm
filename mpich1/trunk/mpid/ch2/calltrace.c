
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifndef FPRINTF
#define FPRINTF fprintf
#endif

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include <stdio.h>
/* 
   Some gcc installations have out-of-date include files and need these
   definitions to handle the "missing" prototypes.  This is NOT
   autodetected, but is provided and can be selected by using a switch
   on the options line.

   These are from stdlib.h, stdio.h, and unistd.h
 */
extern int FPRINTF(FILE*,const char*,...);
extern int fflush(FILE *);
#endif

#include "calltrace.h"

/* Declarations */
#ifdef DEBUG_TRACE
char *(TR_stack[TR_MAX_STACK]);
int  TR_stack_sp = 0, TR_stack_debug = 0;

void TR_stack_init( int flag )
{
    TR_stack_debug = flag;
}

/* Generate a stack trace */
void TR_stack_print( 
	FILE *fp, 
	int dir )
{
    int i;

    if (dir == 1) {
	for (i=0; i<TR_stack_sp; i++) {
	    FPRINTF( fp, "(%d) %s\n", i, TR_stack[i] );
	}
    }
    else {
	for (i=TR_stack_sp-1; i>=0; i--) {
	    FPRINTF( fp, "(%d) %s\n", i, TR_stack[i] );
	}
    }
}

#else
void TR_stack_init( int flag )
{
}
#endif
