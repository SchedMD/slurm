/*****************************************************************************\
 * src/slurmd/common/setproctitle.c - argv manipulation 
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under 
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

/*
 * Based on src/backend/utils/misc/pg_status.c from 
 * PostgreSQL Database Management System
 * 
 * Portions Copyright (c) 1996-2001, The PostgreSQL Global Development Group
 * 
 * Portions Copyright (c) 1994, The Regents of the University of California
 * 
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written agreement
 * is hereby granted, provided that the above copyright notice and this
 * paragraph and the following two paragraphs appear in all copies.
 * 
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS
 * DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

/*--------------------------------------------------------------------
 * ps_status.c
 *
 * Routines to support changing the ps display of PostgreSQL backends
 * to contain some useful information. Mechanism differs wildly across
 * platforms.
 *
 * $Header: /var/cvs/openssh/openbsd-compat/setproctitle.c,v 1.5 2003/01/20 02:1
 *
 * Copyright 2000 by PostgreSQL Global Development Group
 * various details abducted from various places
 *--------------------------------------------------------------------
 */

#if HAVE_CONFIG_H
#  include "config.h"
#  if !HAVE_MALLOC
#    include "src/common/malloc.h"
#  endif
#endif

#ifndef HAVE_SETPROCTITLE
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include "src/common/strlcpy.h"
#ifdef HAVE_SYS_PSTAT_H
#include <sys/pstat.h>		/* for HP-UX */
#endif
#ifdef HAVE_PS_STRINGS
#include <machine/vmparam.h>	/* for old BSD */
#include <sys/exec.h>
#endif

/*------
 * Alternative ways of updating ps display:
 *
 * SETPROCTITLE_STRATEGY == PS_USE_PSTAT
 *	   use the pstat(PSTAT_SETCMD, )
 *	   (HPUX)
 * SETPROCTITLE_STRATEGY == PS_USE_PS_STRINGS
 *	   assign PS_STRINGS->ps_argvstr = "string"
 *	   (some BSD systems)
 * SETPROCTITLE_STRATEGY == PS_USE_CHANGE_ARGV
 *	   assign argv[0] = "string"
 *	   (some other BSD systems)
 * SETPROCTITLE_STRATEGY == PS_USE_CLOBBER_ARGV
 *	   write over the argv and environment area
 *	   (most SysV-like systems)
 * SETPROCTITLE_STRATEGY == PS_USE_NONE
 *	   don't update ps display
 *	   (This is the default, as it is safest.)
 */

#define PS_USE_NONE			0
#define PS_USE_PSTAT			1
#define PS_USE_PS_STRINGS		2
#define PS_USE_CHANGE_ARGV		3
#define PS_USE_CLOBBER_ARGV		4

#ifndef SETPROCTITLE_STRATEGY
# define SETPROCTITLE_STRATEGY	PS_USE_NONE 
#endif

#ifndef SETPROCTITLE_PS_PADDING
# define SETPROCTITLE_PS_PADDING	' '
#endif
#endif /* HAVE_SETPROCTITLE */

extern char **environ;

/*
 * argv clobbering uses existing argv space, all other methods need a buffer
 */
#if SETPROCTITLE_STRATEGY != PS_USE_CLOBBER_ARGV
static char ps_buffer[256];
static const size_t ps_buffer_size = sizeof(ps_buffer);
#else
static char *ps_buffer;			/* will point to argv area */
static size_t ps_buffer_size;		/* space determined at run time */
#endif

/* save the original argv[] location here */
static int	save_argc;
static char **save_argv;

#if HAVE__PROGNAME
extern char *__progname;
#else
static char __progname[64];
#endif

#ifndef HAVE_SETPROCTITLE
/*
 * Call this to update the ps status display to a fixed prefix plus an
 * indication of what you're currently doing passed in the argument.
 */
void
setproctitle(const char *fmt, ...)
{
#if SETPROCTITLE_STRATEGY == PS_USE_PSTAT
	union pstun pst;
#endif
#if SETPROCTITLE_STRATEGY != PS_USE_NONE
	ssize_t used;
	va_list ap;

	/* no ps display if you didn't call save_ps_display_args() */
	if (save_argv == NULL)
		return;
#if SETPROCTITLE_STRATEGY == PS_USE_CLOBBER_ARGV
	/* If ps_buffer is a pointer, it might still be null */
	if (ps_buffer == NULL)
		return;
#endif /* PS_USE_CLOBBER_ARGV */

	/*
	 * Overwrite argv[] to point at appropriate space, if needed
	 */
#if SETPROCTITLE_STRATEGY == PS_USE_CHANGE_ARGV
	save_argv[0] = ps_buffer;
	save_argv[1] = NULL;
#endif /* PS_USE_CHANGE_ARGV */

#if SETPROCTITLE_STRATEGY == PS_USE_CLOBBER_ARGV
	save_argv[1] = NULL;
#endif /* PS_USE_CLOBBER_ARGV */

	/*
	 * Make fixed prefix of ps display.
	 */

	va_start(ap, fmt);
	if (fmt == NULL)
		snprintf(ps_buffer, ps_buffer_size, "%s", __progname);
	else {
		used = snprintf(ps_buffer, ps_buffer_size, "%s: ", __progname);
		if (used == -1 || used >= ps_buffer_size)
			used = ps_buffer_size;
		vsnprintf(ps_buffer + used, ps_buffer_size - used, fmt, ap);
	}
	va_end(ap);

#if SETPROCTITLE_STRATEGY == PS_USE_PSTAT
	pst.pst_command = ps_buffer;
	pstat(PSTAT_SETCMD, pst, strlen(ps_buffer), 0, 0);
#endif   /* PS_USE_PSTAT */

#if SETPROCTITLE_STRATEGY == PS_USE_PS_STRINGS
	PS_STRINGS->ps_nargvstr = 1;
	PS_STRINGS->ps_argvstr = ps_buffer;
#endif   /* PS_USE_PS_STRINGS */

#if SETPROCTITLE_STRATEGY == PS_USE_CLOBBER_ARGV
	/* pad unused memory */
	used = strlen(ps_buffer);
	memset(ps_buffer + used, SETPROCTITLE_PS_PADDING, 
	    ps_buffer_size - used);
#endif   /* PS_USE_CLOBBER_ARGV */

#endif /* PS_USE_NONE */
}

static void _init__progname (const char *argv0)
{
#if !HAVE__PROGNAME
	char *start = strrchr (argv0, '/');
	strlcpy (__progname, start ? (start + 1) : argv0, sizeof (__progname));
	return;
#endif /* !HAVE__PROGNAME */
}

#endif /* HAVE_SETPROCTITLE */

/*
 * Call this early in startup to save the original argc/argv values.
 *
 * argv[] will not be overwritten by this routine, but may be overwritten
 * during setproctitle. Also, the physical location of the environment
 * strings may be moved, so this should be called before any code that
 * might try to hang onto a getenv() result.
 */
void
init_setproctitle(int argc, char *argv[])
{
#if SETPROCTITLE_STRATEGY == PS_USE_CLOBBER_ARGV
	char *end_of_area = NULL;
	char **new_environ;
	int i;
#endif

	save_argc = argc;
	save_argv = argv;

	_init__progname (argv[0]);

#if SETPROCTITLE_STRATEGY == PS_USE_CLOBBER_ARGV
	/*
	 * If we're going to overwrite the argv area, count the available
	 * space.  Also move the environment to make additional room.
	 */

	/*
	 * check for contiguous argv strings
	 */
	for (i = 0; i < argc; i++) {
		if (i == 0 || end_of_area + 1 == argv[i])
			end_of_area = argv[i] + strlen(argv[i]);
	}

	/* probably can't happen? */
	if (end_of_area == NULL) {
		ps_buffer = NULL;
		ps_buffer_size = 0;
		return;
	}

	/*
	 * check for contiguous environ strings following argv
	 */
	for (i = 0; environ[i] != NULL; i++) {
		if (end_of_area + 1 == environ[i])
			end_of_area = environ[i] + strlen(environ[i]);
	}

	ps_buffer = argv[0];
	ps_buffer_size = end_of_area - argv[0] - 1;

	/*
	 * Duplicate and move the environment out of the way
	 */
	new_environ = malloc(sizeof(char *) * (i + 1));
	for (i = 0; environ[i] != NULL; i++) {
		new_environ[i] = strdup(environ[i]);
		//free(environ[i]);
	}
	/* if(environ) */
/* 		free(environ); */
	new_environ[i] = NULL;
	environ = new_environ;
#endif /* PS_USE_CLOBBER_ARGV */
}

