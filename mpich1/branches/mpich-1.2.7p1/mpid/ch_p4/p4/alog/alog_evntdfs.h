/****************************************************************************
These are the reserved event types for logfile header records.  Unspecified
fields are either 0 or (in the case of string data) null.

e_type	proc_id  task_id  int_data  cycle  timestamp  string_data

 -1                                                    creator and date
 -2                       # events
 -3                       # procs
 -4                       # tasks
 -5                       # event types
 -6                                         start_time
 -7                                         end_time
 -8                       # timer_cycles
 -9                       event_type                   description
-10                       event_type                   printf string

*************************************************************************/

#define SYSTEM_TYPE	-1
#define NUM_EVENTS	-2
#define NUM_PROCS	-3
#define NUM_TASKS	-4
#define NUM_EVTYPES	-5
#define START_TIME	-6
#define END_TIME	-7
#define NUM_CYCLES	-8
#define EVTYPE_DESC	-9
#define EPRINT_FORMAT  -10

#define ALOG_EVENT_SYNC        -101
#define ALOG_EVENT_PAIR_A1     -102
#define ALOG_EVENT_PAIR_A2     -103
#define ALOG_EVENT_PAIR_B1     -104
