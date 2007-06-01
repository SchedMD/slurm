/* This is the master include file for the p2p portable shared-memory
 * subsystem.  The files are separated in order to separate the choice of
 * shared-memory scheme from the choice of locking scheme.
 */
#ifndef P2P_INCLUDED
#define P2P_INCLUDED

#include "p2p_common.h"		/* general declarations for p2p */
#include "p2p_special.h"	/* special machine-dependent declarations */
#include "p2p_shmalloc.h"	/* shared memory and its management */
#include "p2p_locks.h"		/* locks and/or semaphores */

#endif

