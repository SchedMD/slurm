#include "mpdattach.h"
/* N.B. 
 * We want to compile this file for debugging under all circumstances.
 * That way we guarantee to pass the structure definition of MPIR_PROCDESC
 * over to the debugger in the debug information, so it doesn't have to make
 * any presumptions about the size or layout of the fields therein.
 * This way it can work on all the different targets without a problem.
 *
 * Since the only function that gets called in here simply returns (and
 * it's only called on spawning processes), the run time cost of compiling
 * this with debugging and without optimisation is negligible.
 */

/* Array of procdescs for debugging purposes */
MPIR_PROCDESC *MPIR_proctable = 0;
int MPIR_proctable_size = 0;

/* Two global variables which a debugger can use for 
 * 1) finding out what the state of the program is at
 *    the time the magic breakpoint is hit.
 * 2) informing the process that it has been attached to and is
 *    now free to run.
 */
VOLATILE int MPIR_debug_state = 0;
VOLATILE int MPIR_debug_gate  = 0;
char * MPIR_debug_abort_string= 0;
int MPIR_being_debugged       = 0;

/*
   MPIR_Breakpoint - Provide a routine that a debugger can intercept
                     at interesting times.
		     Note that before calling this you should set up
		     MPIR_debug_state, so that the debugger can see
		     what is going on.

*/
void * MPIR_Breakpoint( void )
{
  /* This routine is only here to have a breakpoint set in it,
   * it doesn't need any contents itself, but we don't want
   * it inlined and removed despite that.
   */
    return (void *) 0;
}


