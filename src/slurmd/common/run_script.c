/*****************************************************************************\
 * src/slurmd/common/run_script.c - code shared between slurmd and slurmstepd
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher Morrone <morrone2@llnl.gov>
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

#include <glob.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/wait.h>

#include "slurm/slurm_errno.h"
#include "src/common/list.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmd/common/job_container_plugin.h"
#include "src/slurmd/common/run_script.h"

/*
 *  Same as waitpid(2) but kill process group for pid after timeout secs.
 *   Returns 0 for valid status in pstatus, -1 on failure of waitpid(2).
 */
int waitpid_timeout (const char *name, pid_t pid, int *pstatus, int timeout)
{
	int timeout_ms = 1000 * timeout; /* timeout in ms                   */
	int max_delay =  1000;           /* max delay between waitpid calls */
	int delay = 10;                  /* initial delay                   */
	int rc;
	int options = WNOHANG;

	if (timeout <= 0)
		options = 0;

	while ((rc = waitpid (pid, pstatus, options)) <= 0) {
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			error("waitpid: %m");
			return (-1);
		} else if (timeout_ms <= 0) {
			info ("%s%stimeout after %ds: killing pgid %d",
			      name != NULL ? name : "",
			      name != NULL ? ": " : "",
			      timeout, pid);
			killpg(pid, SIGKILL);
			options = 0;
		} else {
			(void) poll(NULL, 0, delay);
			timeout_ms -= delay;
			delay = MIN (timeout_ms, MIN(max_delay, delay*2));
		}
	}

	killpg(pid, SIGKILL);  /* kill children too */
	return (0);
}

/*
 * Run a prolog or epilog script (does NOT drop privileges)
 * name IN: class of program (prolog, epilog, etc.),
 * path IN: pathname of program to run
 * job_id IN: info on associated job
 * max_wait IN: maximum time to wait in seconds, -1 for no limit
 * env IN: environment variables to use on exec, sets minimal environment
 *	if NULL
 * uid IN: user ID of job owner
 * RET 0 on success, -1 on failure.
 */
static int
_run_one_script(const char *name, const char *path, uint32_t job_id,
		int max_wait, char **env, uid_t uid)
{
	int status;
	pid_t cpid;

	xassert(env);
	if (path == NULL || path[0] == '\0')
		return 0;

	if (job_id) {
		debug("[job %u] attempting to run %s [%s]",
			job_id, name, path);
	} else
		debug("attempting to run %s [%s]", name, path);

	if (access(path, R_OK | X_OK) < 0) {
		error("Can not run %s [%s]: %m", name, path);
		return -1;
	}

	if ((cpid = fork()) < 0) {
		error ("executing %s: fork: %m", name);
		return -1;
	}
	if (cpid == 0) {
		char *argv[2];

		/* container_g_join needs to be called in the
		   forked process part of the fork to avoid a race
		   condition where if this process makes a file or
		   detacts itself from a child before we add the pid
		   to the container in the parent of the fork.
		*/
		if (container_g_join(job_id, getuid())
		    != SLURM_SUCCESS)
			error("container_g_join(%u): %m", job_id);

		argv[0] = (char *)xstrdup(path);
		argv[1] = NULL;

		setpgid(0, 0);
		execve(path, argv, env);
		error("execve(%s): %m", path);
		exit(127);
	}

	if (waitpid_timeout(name, cpid, &status, max_wait) < 0)
		return (-1);
	return status;
}

static void _xfree_f (void *x)
{
	xfree (x);
}


static int _ef (const char *p, int errnum)
{
	return error ("run_script: glob: %s: %s", p, strerror (errno));
}

static List _script_list_create (const char *pattern)
{
	glob_t gl;
	size_t i;
	List l = NULL;

	if (pattern == NULL)
		return (NULL);

	int rc = glob (pattern, GLOB_ERR, _ef, &gl);
	switch (rc) {
	case 0:
		l = list_create ((ListDelF) _xfree_f);
		for (i = 0; i < gl.gl_pathc; i++)
			list_push (l, xstrdup (gl.gl_pathv[i]));
		break;
	case GLOB_NOMATCH:
		break;
	case GLOB_NOSPACE:
		error ("run_script: glob(3): Out of memory");
		break;
	case GLOB_ABORTED:
		error ("run_script: cannot read dir %s: %m", pattern);
		break;
	default:
		error ("Unknown glob(3) return code = %d", rc);
		break;
	}

	globfree (&gl);

	return l;
}

int run_script(const char *name, const char *pattern, uint32_t job_id,
	       int max_wait, char **env, uid_t uid)
{
	int rc = 0;
	List l;
	ListIterator i;
	char *s;

	if (pattern == NULL || pattern[0] == '\0')
		return 0;

	l = _script_list_create (pattern);
	if (l == NULL)
		return error ("Unable to run %s [%s]", name, pattern);

	i = list_iterator_create (l);
	while ((s = list_next (i))) {
		rc = _run_one_script (name, s, job_id, max_wait, env, uid);
		if (rc) {
			error ("%s: exited with status 0x%04x\n", s, rc);
			break;
		}

	}
	list_iterator_destroy (i);
	FREE_NULL_LIST (l);

	return rc;
}
