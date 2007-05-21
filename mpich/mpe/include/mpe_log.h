/**\ --MPE_Log--
*  * mpe_log.h - prototypes for externally callable routines, and a few
*  *             return values
*  *
*  * MPE_Log currently represents some code written by Dr. William
*  * Gropp, stolen from Chameleon's 'blog' logging package and
*  * modified by Ed Karrels, as well as some fresh code written
*  * by Ed Karrels.
*  *
*  * All work funded by Argonne National Laboratory
\**/

#ifndef MPE_NOMPI
#include "mpi.h"
#endif

#ifndef _MPE_LOG_H_
#define _MPE_LOG_H_

/* function return values */
#define MPE_Log_OK         0
  /* no problems */
#define MPE_Log_LOCKED_OUT 1
  /* logs are are being worked on, cannot insert any new entries */
#define MPE_Log_NO_MEMORY  2
  /* could not allocate memory for logging data */
#define MPE_Log_FILE_PROB  3
  /* cound not open file for writing out the logged info */
#define MPE_Log_NOT_INITIALIZED 4
  /* logging not initialized */

int MPE_Init_log (void);
  /* call before calling any other logging functions */

int MPE_Initialized_logging (void);

int MPE_Describe_state ( int start, int end, char *name, char *color );
  /* create state description lines */

int MPE_Describe_event ( int event, char *name);
  /* create event description lines */

int MPE_Log_get_event_number ( void );
  /* Get a new event number */
int MPE_Start_log (void);
  /* set timer to 0 */

int MPE_Log_event ( int event, int data, char *string );
  /* log one event */

int MPE_Log_send ( int receiver, int tag, int size );
  /* log the sending of a message */

int MPE_Log_receive ( int sender, int tag, int size );
  /* log the receiving of a message */

int MPE_Stop_log (void);
  /* stop logging events */

int MPE_Finish_log ( char *filename );
  /* write out data to a file */

#endif



/* The format:
Each line:
  type process task data cycle timestamp [comment]

    type - nonnegative integer representing a user-defined event type
    process - an integer representing the process in which the event occurred
    task - an integer representing a different notion of task.  Usually 
           ignored.
    data - an integer representing user data for the event
    cycle - an integer representing a time cycle, used to distinguish
            between time returned by a timer that "rolls over" during
            the run
    timestamp - an integer representing (when considered in conjuction
                with the cycle number) a time for the event.  Upshot treats
                the units as microseconds
    comment - an optional character string representing user data.  Currently
              12 character maximum, will soon hopefully be any length (really!)

All events from -100 to -1 are reserved header information events.  When
a log is produced, all [-100,-1] events will be moved to the top of the
logfile and have their timestamps set to 0.

All event from -101 and below are reserved system events.  This is to
provide some standardization for the logfiles, so various interpreting
programs can glean similar data from the same logfile.  All [-101,...)
events will have valid timestamps and will be left in time-sorted
order in the logfile.

Formats for reserved types:

  -1 Creation data                *not used*
     Comment: Creator and date

  -2 Number of events in the logfile   *not used*
     Data: number of events

  -3 Number of processors in the run
     Data: number of processes

  -4 Number of tasks used in the run  *not used*
     Task: number of tasks

  -5 Number of event types used        *not used*
     Data: number event types

  -6 Start time of the run
     Timestamp: start time

  -7 End time of the run
     Timestamp: end time

  -8 Number of times the timer cycled
     For example, if the timer's units are in microseconds, and it has a
     range of 0 - 2^32, and a run lasts 3 hours (range=4294 seconds, 3 hours=
     10800 seconds), the timer would have cycled at least twice.
     Data: number of timer cycles

  -9 Decription of event types     *not used*
     Data: event type
     Comment: Description

  -10 printf string for event types   *not used*
      Data: event type
      Comment: printf string

  -11 Rollover point
      The point at which the timer values 'rollover'
      Timestamp: rollover point

  -13 State definition
      Define a state based on the events that signal the beginning and end
      of the state.  Also, define what to call the state and what color/
      stipple pattern to give it in a graphical visualization tool.
      Task: start event
      Data: end event
      Comment: color:bitmap state name

      example:  -13 0 3 4 0 0 Green:boxes Rhode Island
      An event with type 3 will signify the entrance into a 'Rhode Island'
      state.  An event wil type 4 will signify the exit of the 'Rhode Island'
      state.

      States may be overlapped (enter a 'Rhode Island' state while in a
      'Wisconsin' state while in a 'Nevada' state), and the state name may
      have whitspace in it.
      
   -100 Synchronization event
        Sync events are used internally to sychronize timers on the various
        processes.  They do not appear in the logfiles.

   -101 Send message
        Represents the sending of a message
	Data: process ID of the receiving process
	Comment: <message-type tag of message> <size of the message, in bytes>

   -102 Receive message
        Represents the receiving of a message
	Data: process ID of the sending process
	Comment: <message-type tag of message> <size of the message, in bytes>

*/
