#include "mpid.h"
/*
 * Need prototypes for MPID_SetMsgDebugFlag, MPIR_Ref_init
 */
#include <stdio.h>
#include "cmnargs.h"
#ifdef MPIR_MEMDEBUG
#include "tr2.h"
#endif
#ifdef MPID_FLOW_CONTROL
void MPID_FlowDebug ( int );
#endif
/* 
   Includes for control options
 */
#include "ptrcvt.h"

/* prototype for getenv */
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#else
extern char *getenv();
#endif

#ifdef HAVE_UNISTD_H
/* 
   Need to undefine SEEK_SET, SEEK_CUR, and SEEK_END on some Paragon
   platforms 
 */
/* prototype for nice */
#include <unistd.h>
#endif
#ifdef NEEDS_STDLIB_PROTOTYPES
#include "protofix.h"
#endif

/*
 * This file contains common argument handling routines for MPI ADIs
 * It is being augmented to use the environment to provide values;
 * this is particularly useful for initialization options.
 * 
 */

/*
   Get an integer from the environment; otherwise, return defval.
 */
int MPID_GetIntParameter(char * name, int defval )
{
    char *p = getenv( name );

    if (p) 
	return atoi(p);
    return defval;
}

/*
   MPID_ArgSqueeze - Remove all null arguments from an arg vector; 
   update the number of arguments.
 */
void MPID_ArgSqueeze( int *Argc, char **argv )
{
    int argc, i, j;

    /* Compress out the eliminated args */
    argc = *Argc;
    j    = 0;
    i    = 0;
    while (j < argc) {
	while (argv[j] == 0 && j < argc) j++;
	if (j < argc) argv[i++] = argv[j++];
    }
    /* Back off the last value if it is null */
    if (!argv[i-1]) i--;
    *Argc = i;
}

void MPID_ProcessArgs( int *argc, char ***argv )
{
int i;
int active_rank = -1;

char **str;

if (argv && *argv) {
    for (i=1; i<*argc; i++) {
	str = (*argv)+i;
	if (str && *str) {
	    if (strcmp(*str,"-mpipktsize" ) == 0) {
		int len;
		*str = 0;
		i++;
		if (i <*argc) {
		    len = atoi( (*argv)[i] );
		    MPID_SetPktSize( len );
		    (*argv)[i] = 0;
		    }
		else {
		    printf( "Missing argument for -mpipktsize\n" );
		    }
		}
#ifdef HAVE_NICE
	    else if (strcmp(*str,"-mpinice" ) == 0) {
		int niceincr;
		*str = 0;
		i++;
		if (i <*argc) {
		    niceincr = atoi( (*argv)[i] );
		    nice(niceincr);
		    (*argv)[i] = 0;
		    }
		else {
		    printf( "Missing argument for -mpinice\n" );
		    }
		}
#endif
#ifdef MPID_HAS_DEBUG
	    else if (strcmp(*str,"-mpichdebug") == 0) {
		MPID_SetDebugFlag( 1 );
		*str = 0;
		}
	    else if (strcmp(*str,"-mpidbfile" ) == 0) {
		MPID_SetDebugFlag( 1 );
		*str = 0;
		i++;
		if (i <*argc) {
		    MPID_SetDebugFile( (*argv)[i] );
		    (*argv)[i] = 0;
		    }
		else {
		    printf( "Missing filename for -mpdbfile\n" );
		    }
		}
	    else if (strcmp(*str,"-chmemdebug" ) == 0) {
		MPID_SetSpaceDebugFlag( 1 );
		*str = 0;
		}
	    else if (strcmp(*str,"-mpichmsg" ) == 0) {
		MPID_SetMsgDebugFlag( 1 );
		*str = 0;
		}
	    else if (strcmp(*str,"-mpitrace" ) == 0) {
		*str = 0;
		i++;
		if (i <*argc) {
		    MPID_Set_tracefile( (*argv)[i] );
		    (*argv)[i] = 0;
		    }
		else {
		    printf( "Missing filename for -mpitrace\n" );
		    }
		}
#endif
#ifdef MPIR_MEMDEBUG
	    else if (strcmp(*str,"-mpimem" ) == 0) {
		MPID_trDebugLevel( 1 );
		*str = 0;
	    }
#endif
	    else if (strcmp(*str, "-mpidb") == 0) { 
		*str = 0;
		i++;
		str = *argv + i;
		if (i < *argc) {
		    if (strcmp( *str, "mem" ) == 0) {
#ifdef MPIR_MEMDEBUG
			if (active_rank == -1 ||
			    active_rank == MPID_MyWorldRank) 
			    MPID_trDebugLevel( 1 );
			*str = 0;
#else
			printf( "-mpidb mem not available\n" );
#endif
		    }
		    else if (strcmp( *str, "memdump" ) == 0) {
#ifdef MPIR_MEMDEBUG
			extern int MPIR_Dump_Mem;
			MPIR_Dump_Mem = 1;
			*str = 0;
#else
			printf( "-mpidb memdump not available\n" );
#endif
		    }
		    else if (strcmp( *str, "-memdump" ) == 0) {
#ifdef MPIR_MEMDEBUG
			extern int MPIR_Dump_Mem;
			MPIR_Dump_Mem = 0;
			*str = 0;
#endif
		    }
		    else if (strcmp( *str, "memall" ) == 0) {
#ifdef MPIR_MEMDEBUG
			if (active_rank == -1 ||
			    active_rank == MPID_MyWorldRank) 
			    MPID_trlevel( 3 );
			*str = 0;
#else
			printf( "-mpidb mem not available\n" );
#endif
		    }
		    else if (strcmp( *str, "queue" ) == 0) {
			extern int MPID_Print_queues;
			MPID_Print_queues = 1;
		    }
		    else if (strcmp( *str, "ref" ) == 0) {
#ifdef MPIR_OBJDEBUG
			if (active_rank == -1 ||
			    active_rank == MPID_MyWorldRank) 
			    MPIR_Ref_init( 1, (char *)0 );
			*str = 0;
#else
			printf( "-mpidb ref not available\n" );
#endif
		    }
		    else if (strcmp( *str, "reffile" ) == 0) {
#ifdef MPIR_OBJDEBUG
			*str = 0;
			i++;
			str = *argv + i;
			if (active_rank == -1 ||
			    active_rank == MPID_MyWorldRank) 
			    MPIR_Ref_init( 1, *str );
			*str = 0;
#else
			printf( "-mpidb ref not available\n" );
#endif
		    }
		    else if (strcmp( *str, "ptr" ) == 0) {
			if (active_rank == -1 ||
			    active_rank == MPID_MyWorldRank) 
			    MPIR_PointerOpts( 1 );
			*str = 0;
		    }
		    else if (strcmp( *str, "rank" ) == 0) {
			*str = 0;
			i++;
			str = *argv + i;
			active_rank = atoi( *str );
			*str = 0;
		    }
		    else if (strcmp( *str, "trace" ) == 0) {
			*str = 0;
#ifdef DEBUG_TRACE
			if (active_rank == -1 ||
			    active_rank == MPID_MyWorldRank) 
			    TR_stack_init( 1 );
#else
			printf( "Trace debugging is not enabled\n" );
#endif
		    }
		    else {
			printf( "%s is unknown -mpidb option\n", *str );
		    }
		}
		else {
		    printf( "Missing argument for -mpidb\n" );
		}
	    }
#ifdef MPID_FLOW_CONTROL
	    else if (strcmp( *str, "-mpidbflow" ) == 0) {
		MPID_FlowDebug( 1 );
	    }
#endif
	    }
	}
    /* Remove the null arguments */
    MPID_ArgSqueeze( argc, *argv ); 
    }
}
