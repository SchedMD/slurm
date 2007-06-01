/* mpe_log.c - the externally callable functions in MPE_Log

   New version to use CLOG - Bill Gropp and Rusty Lusk

*/

#include <stdio.h>

#include "mpeconf.h"
#include "clog.h"
#include "clog_merge.h"
#include "mpi.h"		/* Needed for MPI routines */
#include "mpe.h"		/* why? */
#include "mpe_log.h"

#ifdef HAVE_STDLIB_H
/* Needed for getenv */
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

/* we want to use the PMPI routines instead of the MPI routines for all of 
   the logging calls internal to the mpe_log package */
#ifdef USE_PMPI
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
#endif

/* temporarily borrowed from mpe_log_genproc.h */ /* try to replace by CLOG */
int    MPE_Log_hasBeenInit = 0;
int    MPE_Log_hasBeenClosed = 0;
int    MPE_Log_clockIsRunning = 0;
int    MPE_Log_isLockedOut = 0;
int    MPE_Log_AdjustedTimes = 0;
#define MPE_HAS_PROCID
int    MPE_Log_procid;
/* end of borrowing */

/*@
    MPE_Init_log - Initialize for logging

    Notes:
    Initializes the MPE logging package.  This must be called before any of
    the other MPE logging routines.  It is collective over 'MPI_COMM_WORLD'

.seealso: MPE_Finish_log
@*/
int MPE_Init_log()
{

    if (!MPE_Log_hasBeenInit || MPE_Log_hasBeenClosed) {
	MPI_Comm_rank( MPI_COMM_WORLD, &MPE_Log_procid ); /* get process ID */
	CLOG_Init();
	CLOG_LOGCOMM(INIT, -1, (int) MPI_COMM_WORLD);
	MPE_Log_hasBeenInit = 1;	/* set MPE_Log as being initialized */
	MPE_Log_hasBeenClosed = 0;
	MPE_Log_isLockedOut = 0;
	if ( MPE_Log_procid == 0 ) {
	    CLOG_LOGRAW( LOG_CONST_DEF, MPI_PROC_NULL, "MPI_PROC_NULL" );
	    CLOG_LOGRAW( LOG_CONST_DEF, MPI_ANY_SOURCE, "MPI_ANY_SOURCE" );
	    CLOG_LOGRAW( LOG_CONST_DEF, MPI_ANY_TAG, "MPI_ANY_TAG" );
	}
    }
    return MPE_Log_OK;
}

/*@
    MPE_Start_log - Begin logging of events
@*/
int MPE_Start_log()
{
  if (!MPE_Log_hasBeenInit) return MPE_Log_NOT_INITIALIZED;
  CLOG_status = 0;
  MPE_Log_isLockedOut = 0;
  return MPE_Log_OK;
}

/*@
    MPE_Stop_log - Stop logging events
@*/
int MPE_Stop_log()
{
  if (!MPE_Log_hasBeenInit) return MPE_Log_NOT_INITIALIZED;
  MPE_Log_isLockedOut = 1;
  CLOG_status = 1;
  return MPE_Log_OK;
}

/*@
  MPE_Initialized_logging - Indicate whether MPE_Init_log or MPE_Finish_log
  have been called.

  Returns:
  0 if MPE_Init_log has not been called, 1 if MPE_Init_log has been called
  but MPE_Finish_log has not been called, and 2 otherwise.
@*/
int MPE_Initialized_logging ()
{
    return MPE_Log_hasBeenInit + MPE_Log_hasBeenClosed;
}

/*@
    MPE_Describe_state - Create log record describing a state

    Input Parameters:
. start - event number for the start of the state
. end   - event number for the end of the state
. name  - Name of the state
. color - color to display the state in

    Notes:
    Adds string containing a state def to the logfile.  The format of the
    definition is (in ALOG)
.vb
    (LOG_STATE_DEF) 0 sevent eevent 0 0 "color" "name"
.ve
    States are added to a log file by calling 'MPE_Log_event' for the start and
    end event numbers.

.seealso: MPE_Log_get_event_number
@*/
int MPE_Describe_state( start, end, name, color )
int start, end;
char *name, *color;
{
    int stateid;

    if (!MPE_Log_hasBeenInit) return MPE_Log_NOT_INITIALIZED;

    stateid = CLOG_get_new_state();
    CLOG_LOGSTATE( stateid, start, end, color, name);

    return MPE_Log_OK;
}

/*@
    MPE_Describe_event - Create log record describing an event type
    
    Input Parameters:
+   event - Event number
-   name  - String describing the event. 

.seealso: MPE_Log_get_event_number 
@*/
int MPE_Describe_event( event, name )
int event;
char *name;
{
    if (!MPE_Log_hasBeenInit) return MPE_Log_NOT_INITIALIZED;

    CLOG_LOGEVENT( event, name);

    return MPE_Log_OK;
}

/*@
  MPE_Log_get_event_number - Gets an unused event number

  Returns:
  A value that can be provided to MPE_Describe_event or MPE_Describe_state
  which will define an event or state not used before.  

  Notes: 
  This routine is provided to allow packages to ensure that they are 
  using unique event numbers.  It relies on all packages using this
  routine.
@*/
int MPE_Log_get_event_number( )

{
    return CLOG_get_new_event();
}

/*@
    MPE_Log_send - Logs the sending of a message
@*/
int MPE_Log_send( otherParty, tag, size )
int otherParty, tag, size;
{
    char comment[20];
    if (otherParty != MPI_PROC_NULL) {
	sprintf(comment, "%d %d", tag, size);
	CLOG_LOGRAW( LOG_MESG_SEND, otherParty, comment );
    }
    return MPE_Log_OK;
}

/*@
    MPE_Log_receive - log the sending of a message
@*/
int MPE_Log_receive( otherParty, tag, size )
int otherParty, tag, size;
{
    char comment[20];
    if (otherParty != MPI_PROC_NULL) {
	sprintf(comment, "%d %d", tag, size);
	CLOG_LOGRAW( LOG_MESG_RECV, otherParty, comment );
    }
    return MPE_Log_OK;
}

/*@
    MPE_Log_event - Logs an event

    Input Parameters:
+   event - Event number
.   data  - Integer data value
-   string - Optional string describing event
@*/
int MPE_Log_event(event,data,string)
int event, data;
char *string;
{
    CLOG_LOGRAW( event, data, string );
    return MPE_Log_OK;
}

/*@
    MPE_Finish_log - Send log to master, who writes it out

    Notes:
    This routine dumps a logfile in alog or clog format.  It is collective over
    'MPI_COMM_WORLD'.  The default is alog format.  To generate clog output,
    set the environment variable MPE_LOG_FORMAT to CLOG.

@*/
int MPE_Finish_log( filename )
char *filename;
{
/*  The environment variable MPE_LOG_FORMAT may be set to ALOG to generate
    ALOG format log files (CLOG is the default). 
 */
    char *env_log_format;
    char *env_logfile_prefix;
    int shift, log_format, final_log_format;
    int *is_globalp, flag;

    if (MPE_Log_hasBeenClosed == 0) {
	CLOG_Finalize();

	PMPI_Attr_get( MPI_COMM_WORLD, MPI_WTIME_IS_GLOBAL,
                       &is_globalp, &flag );

	if (!flag || (is_globalp && !*is_globalp))
	    shift = CMERGE_SHIFT;
	else
	    shift = CMERGE_NOSHIFT;
        /*
            Ignore what MPI says if the clock is sync.,
            force synchronization of the clocks
        */
        /*  
            printf( "Forcing the synchronization of the clock\n" );
            shift = CMERGE_SHIFT;
        */

	log_format = CLOG_LOG;
	env_log_format = (char *) getenv( "MPE_LOG_FORMAT" );

        /*
        if ( env_log_format != NULL )
            printf( "MPE_LOG_FORMAT = %s\n", env_log_format );
        */
 
        if ( env_log_format != NULL ) {
            if (strcmp(env_log_format,"ALOG") == 0)
                log_format = ALOG_LOG;
            else if (strcmp(env_log_format,"SLOG") == 0)
                log_format = SLOG_LOG;
	}
             

	/* 
           We should do a compare across all processes to choose the format,
	   in case the environment is not the same on all processes.  We use
           MPI_MAX since SLOG_LOG > ALOG_LOG > CLOG_LOG.
           Since log_format is initialized to CLOG, then CLOG will be 
           the default logfile format unless MPE_LOG_FORMAT is set.
        */
	PMPI_Allreduce( &log_format, &final_log_format, 1, MPI_INT,
                        MPI_MAX, MPI_COMM_WORLD );

        /*  printf( "final_log_format = %d\n", final_log_format );  */


	env_logfile_prefix = (char *) getenv( "MPE_LOGFILE_PREFIX" );
	if ( env_logfile_prefix != NULL )
	    CLOG_mergelogs(shift, env_logfile_prefix, final_log_format); 
	else
	    CLOG_mergelogs(shift, filename, final_log_format); 

	MPE_Log_hasBeenClosed = 1;
        MPE_Stop_log();
    }
    return MPE_Log_OK;
}
