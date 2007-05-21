/*
 *  $Id: sendq.h,v 1.2 1998/01/29 14:25:23 gropp Exp $
 *
 *  (C) 1996 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 *
 *   	Written by James Cownie (BBN) Jun 21 1996. The structures maintained
 *	by these functions are used by TotalView to display the message passing
 * 	state of MPI programs.
 */

#ifndef _SENDQ_INCLUDED
#define _SENDQ_INCLUDED

/* These macros are used to keep the shadow data structures if we 
 * want to provide more debug information for the user about pending
 * sends.
 *
 * If you don't even want the capability to do that (which will save
 * one test on a global variable in isend, wait and so on)
 * then change the definition below.
 */
/*
 * This definition is now set by the configure process
 */
/* #define MPI_KEEP_SEND_QUEUE */

#ifdef MPI_KEEP_SEND_QUEUE
/* Useful definitions */
#define MPIR_REMEMBER_SEND(shandle, buf, count, datatype, dest, tag, comm) \
   (MPIR_being_debugged && \
    (MPIR_Remember_send((shandle), (buf), (count), (datatype), (dest), (tag), (comm)),1))
#define MPIR_FORGET_SEND(shandle) \
   (MPIR_being_debugged && \
    (MPIR_Forget_send((shandle)),1))
#define MPIR_SENDQ_INIT()     MPIR_Sendq_init()
#define MPIR_SENDQ_FINALIZE() MPIR_Sendq_finalize()

#if defined(__STDC__) || defined(__cplusplus) || defined(HAVE_PROTOTYPES)
extern void MPIR_Remember_send(MPIR_SHANDLE *, void *, int, MPI_Datatype, int, int, struct MPIR_COMMUNICATOR *);
extern void MPIR_Forget_send(MPIR_SHANDLE *);
#else
extern void MPIR_Remember_send();
extern void MPIR_Forget_send();
#endif	/* STDC */

#else

/* Null definitions, but don't leave dangling ; */
#define MPIR_REMEMBER_SEND(request, buf, count, datatype, dest, tag, comm) ((void)0)
#define MPIR_FORGET_SEND(request) ((void)0)
#define MPIR_SENDQ_INIT()         ((void)0)
#define MPIR_SENDQ_FINALIZE()     ((void)0)
#endif /* KEEP_SEND_QUEUE */

#endif /* SENDQ_INCLUDED */
