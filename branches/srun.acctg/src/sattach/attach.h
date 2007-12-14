/****************************************************************************\
 *  attach.h - definitions needed for TotalView interactions
 *****************************************************************************
 *  This file was supplied by James Cownie <jcownie@etnus.com> and provides 
 *  information required to interface Slurm to the TotalView debugger from 
 *  the Etnus Corporation. For more information about TotalView, see
 *  http://www.etnus.com/
\*****************************************************************************/

/*  $Id$
 */

/* This file contains support for bringing processes up stopped, so that
 * a debugger can attach to them     (done for TotalView)
 */

/* Update log
 *
 * Nov 27 1996 jcownie@dolphinics.com: Added the executable_name to MPIR_PROCDESC
 */

#ifndef _ATTACH_INCLUDE
#define _ATTACH_INCLUDE

#ifndef VOLATILE
#if defined(__STDC__) || defined(__cplusplus)
#define VOLATILE volatile
#else
#define VOLATILE
#endif
#endif
/*****************************************************************************
*                                DEBUGGING SUPPORT                           *
*****************************************************************************/


/* A little struct to hold the target processor name and pid for
 * each process which forms part of the MPI program.
 * We may need to think more about this once we have dynamic processes...
 *
 * DO NOT change the name of this structure or its fields. The debugger knows
 * them, and will be confused if you change them.
 */
typedef struct {
  char * host_name;           /* Something we can pass to inet_addr */
  char * executable_name;     /* The name of the image */
  int    pid;		      /* The pid of the process */
} MPIR_PROCDESC;

/* Array of procdescs for debugging purposes */
extern MPIR_PROCDESC *MPIR_proctable;
extern int MPIR_proctable_size;

/* Various global variables which a debugger can use for 
 * 1) finding out what the state of the program is at
 *    the time the magic breakpoint is hit.
 * 2) inform the process that it has been attached to and is
 *    now free to run.
 */
extern VOLATILE int MPIR_debug_state;
extern VOLATILE int MPIR_debug_gate;
extern int          MPIR_being_debugged; /* Cause extra info on internal state
					  * to be maintained
					  */
 
/* Values for the debug_state, this seems to be all we need at the moment
 * but that may change... 
 */
#define MPIR_DEBUG_SPAWNED   1
#define MPIR_DEBUG_ABORTING  2

/* SLURM specific declarations */
extern int MPIR_i_am_starter;
extern int MPIR_acquired_pre_main;

extern void MPIR_Breakpoint(void);

/* Value for totalview %J expansion in bulk launch string
 */
extern char *totalview_jobid; 

#endif
