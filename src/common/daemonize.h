/*****************************************************************************\
 * daemonize.h - daemonization routine
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/


#ifndef _HAVE_DAEMONIZE_H
#define _HAVE_DAEMONIZE_H

#include <stdbool.h>
#include <sys/types.h>

/*
 * Fork process into background and inherit new session.
 *
 * Returns -1 on error.
 */
extern int xdaemon(void);

/* Write pid into file pidfile if uid is not 0 change the owner of the
 * pidfile to that user.
 */
extern int create_pidfile(const char *pidfilename, uid_t uid);
extern int update_pidfile(int fd);

/*
 * Attempt to read an old pid from the configured pidfile
 * Returns 0 if no pidfile exists (No running process)
 * If pidfilefd is not NULL, returns open file descriptor for
 * pidfile (when pid != 0).
 */
extern pid_t read_pidfile(const char *pidfilename, int *pidfilefd);

/*
 * Test for a core file limit. If small then log it.
 */
extern void test_core_limit(void);

/*
 * Setup process to run as SlurmUser.
 */
extern void become_slurm_user(void);

/*
 * Disable privilege escalation for the calling process: set
 * PR_SET_NO_NEW_PRIVS and clear keepcaps so the kernel drops the
 * permitted/effective/ambient capability sets when the uid later transitions
 * away from 0. A no-op on platforms without prctl() support.
 *
 * RET SLURM_SUCCESS or an errno on failure
 */
extern int restrict_privileges(void);

/*
 * Start a new session (setsid()) to detach from the controlling terminal.
 * Being already a session leader (EPERM) is treated as success.
 *
 * RET SLURM_SUCCESS or an errno on failure
 */
extern int start_new_session(void);

/*
 * Request that the calling process is sent sig when its parent process exits.
 * A no-op on platforms without prctl() support.
 *
 * IN sig - signal to deliver on parent death
 * RET SLURM_SUCCESS or an errno on failure
 */
extern int set_parent_death_signal(int sig);

/*
 * Restrict process to given user/group.
 *
 * Drops the calling (privileged) process down to the target uid/gid as a
 * lockdown step before exec()ing untrusted code.
 *
 * A root target is permitted, but since dropping capabilities relies on the
 * uid changing away from 0, a root target keeps its capabilities.
 *
 * IN uid - target user (SLURM_AUTH_NOBODY is rejected with ESLURM_AUTH_NOBODY)
 * IN gid - target group (SLURM_AUTH_NOBODY resolves to the user's group)
 * IN gids - if non-NULL, set these supplementary groups (overrides drop_groups)
 * IN gids_count - number of entries in gids
 * IN drop_groups - when gids is NULL, drop all inherited supplementary groups
 *	(via drop_supplementary_groups()); if false, leave them intact
 * IN unshare_sysv - unshare the System V semaphore namespace
 * IN unshare_files - unshare the file descriptor table
 * IN drop_priv - disable new privileges (PR_SET_NO_NEW_PRIVS) and ensure
 *	capabilities are dropped on the uid change (keepcaps off)
 * IN kill_child_on_exit - request SIGKILL when the parent process exits
 * IN reset_signals - reset the blocked signal mask and signal dispositions to
 *	their defaults
 * IN new_session - setsid() to detach from the controlling terminal
 * RET SLURM_SUCCESS or an errno on failure
 */
extern int become_user(uid_t uid, gid_t gid, gid_t *gids, int gids_count,
		       bool drop_groups, bool unshare_sysv, bool unshare_files,
		       bool drop_priv, bool kill_child_on_exit,
		       bool reset_signals, bool new_session);

#endif /* !_HAVE_DAEMONIZE_H */
