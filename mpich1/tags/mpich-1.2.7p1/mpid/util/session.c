/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  $Id: session.c,v 1.2 2002/05/13 18:06:53 gropp Exp $
 *
 *  (C) 2002 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpid.h"
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include "session.h"

/* On some systems (SGI IRIX 6), process exit sometimes kills all processes
   in the process GROUP.  This code attempts to fix that.  
   We DON'T do it if stdin (0) is connected to a terminal, because that
   disconnects the process from the terminal.
 */

#if defined(HAVE_SETSID) && defined(HAVE_ISATTY) && defined(SET_NEW_PGRP)
/*
  MPID_Process_group_init - Create a separate process group for this
  session.  This will keep any runtime loaded with this executable that
  kills the process group from killing any shell script or program that is
  running this program.  This is unfortunately more widespread than you'd like;
  it is an area that is poorly documented and hard to debug, since user
  scripts and programs are aborted seemingly at random.

  This should be called only once for a group of processes that are created
  together.  For example, if a collection of processes is created with
  fork/exec (or just fork), then this routine should be called once
  before the fork step.

  To use this, you must include in configure.in
  AC_CHECK_FUNCS(setsid isatty)
  AC_ARG_ENABLE(processgroup,
  [--enable-processgroup - Use a separate process group for
  the MPICH processes],,enable_processgroup=default)
  ...
  if test "$enable_processgroup" = "default" -o \
          "$enable_processgroup" = "yes" ; then
      AC_DEFINE(SET_NEW_PGRP,,[Define to force a new process group])
  fi

  This routine also checks the value of the environment variable

  MPICH_PROCESS_GROUP

  whose values are no and yes, with the default being yes.
 */
int MPID_Process_group_init( void )
{
    pid_t rc = 0;
    if (!isatty(0)) {
	char *name = getenv( "MPICH_PROCESS_GROUP" );
	if (!name || strcmp( name, "yes" ) == 0) {
	    rc = setsid();
	}
    }
    /* Else there is nothing to do */
    return rc;
}
#else
int MPID_Process_group_init( void )
{
    return 0;
}
#endif
