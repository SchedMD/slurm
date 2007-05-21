/*
 *  $Id: debugutil.c,v 1.5 1998/01/29 14:27:06 gropp Exp $
 *
 *  (C) 1996 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

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


#include "mpiimpl.h"
#include "sbcnst2.h"
/* Error handlers in pt2pt */
#include "mpipt2pt.h"

/* Include references to the Queues and Communicators here too, 
 * to ensure that the debugger can see their types.
 */
#include "../util/queue.h"
#include "comm.h"
#include "req.h"

#ifndef VOLATILE
#if defined(__STDC__) || defined(__cplusplus) || defined (__sgi)
#define VOLATILE volatile
#else
#define VOLATILE
#endif
#endif

typedef struct MPIR_COMMUNICATOR MPIR_Communicator;

/* Array of procdescs for debugging purposes */
MPIR_PROCDESC *MPIR_proctable = 0;
int MPIR_proctable_size = 0;

/* List of all communicators */
MPIR_Comm_list MPIR_All_communicators;

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

/* With some compilers and debug formats, (e.g. Digital Unix ("the
 * operating system formerly known as OSF1"), and AIX), including
 * the header files is not sufficient to cause the type definitions
 * to be included in the object file debug information.  To cause
 * this to happen you also need to instance an entity of that type.
 * This simplest way to do that (without causing static space to be
 * allocated) is to instance a variables.
 *
 * We stand on our head a bit to prevent the compiler from optimizing
 * out these variables, and therefore discarding the associated type
 * definitions in the debugging information.
 *
 * This also has the useful effect of documenting the 
 * types which are used by TotalView's MPICH support, and here 
 * they are.
 *
 * Note that picky compilers may complain about "declared and not used"
 * variables.  Some compilers may provide a #pragma that can
 * turn off those warnings; others may be quiet if the variables are
 * declared static.
 */
VOLATILE MPIR_SQUEUE       *MPIR_debug_sq;
VOLATILE MPID_QHDR         *MPIR_debug_qh;
VOLATILE MPID_QUEUE        *MPIR_debug_q;
VOLATILE MPID_QEL          *MPIR_debug_qel;
VOLATILE MPIR_SQEL         *MPIR_debug_sqel;
VOLATILE MPIR_RHANDLE      *MPIR_debug_rh;
VOLATILE MPIR_Comm_list    *MPIR_debug_cl;
VOLATILE MPIR_Communicator *MPIR_debug_c;
VOLATILE MPI_Status        *MPIR_debug_s;

/*
   MPIR_Breakpoint - Provide a routine that a debugger can intercept
                     at interesting times.
		     Note that before calling this you should set up
		     MPIR_debug_state, so that the debugger can see
		     what is going on.

*/
void * MPIR_Breakpoint()
{
  /* This routine is only here to have a breakpoint set in it,
   * it doesn't need any contents itself, but we don't want
   * it inlined and removed despite that.
   *
   * Here we initialize and reference the above variables to prevent
   * the compiler from optimizing out the types needed for message
   * queue display in TotalView.  Here we're suffering the overhead
   * of 18 pointers and 18 assignments, which should be minimal.
   */

  static void *dummy_vector[9];
  MPIR_debug_sq   = 0; dummy_vector[0] = &MPIR_debug_sq;
  MPIR_debug_qh   = 0; dummy_vector[1] = &MPIR_debug_qh;
  MPIR_debug_q    = 0; dummy_vector[2] = &MPIR_debug_q;
  MPIR_debug_qel  = 0; dummy_vector[3] = &MPIR_debug_qel;
  MPIR_debug_sqel = 0; dummy_vector[4] = &MPIR_debug_sqel;
  MPIR_debug_rh   = 0; dummy_vector[5] = &MPIR_debug_rh;
  MPIR_debug_cl   = 0; dummy_vector[6] = &MPIR_debug_cl;
  MPIR_debug_c    = 0; dummy_vector[7] = &MPIR_debug_c;
  MPIR_debug_s    = 0; dummy_vector[8] = &MPIR_debug_s;
  return dummy_vector;
}


