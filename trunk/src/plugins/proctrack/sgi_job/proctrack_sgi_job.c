/*****************************************************************************\
 *  proctrack_sgi_job.c - process tracking via SGI's "job" module.
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>
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

#if HAVE_CONFIG_H
#   include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>
#include "src/common/log.h"

#include "src/slurmd/slurmstepd/slurmstepd_job.h"


const char plugin_name[]      = "Process tracking via SGI job module";
const char plugin_type[]      = "proctrack/sgi_job";
const uint32_t plugin_version = 90;

/* 
 * We can't include <job.h> since its prototypes conflict with some
 *  of SLURM's. Instead, put important function protypes and
 *  the jid_t typedef here:
 */
typedef uint64_t jid_t;

typedef jid_t (*create_f)    (jid_t jid_requested, uid_t uid, int options);
typedef jid_t (*getjid_f)    (pid_t pid);
typedef jid_t (*waitjid_f)   (jid_t jid, int *status, int options);
typedef int   (*killjid_f)   (jid_t jid, int sig);
typedef jid_t (*detachpid_f) (pid_t pid);
typedef jid_t (*attachpid_f) (pid_t pid, jid_t jid_requested);
typedef int   (*getpidlist_f)(jid_t jid, pid_t *pid, int bufsize);
typedef int   (*getpidcnt_f) (jid_t jid);

/*
 *  Handle to libjob.so 
 */
static void *libjob_handle = NULL;

/*
 *  libjob operations we'll need in this plugin
 */
static struct job_operations {
	create_f     create;
	getjid_f     getjid;
	waitjid_f    waitjid;
	killjid_f    killjid;
	detachpid_f  detachpid;
	attachpid_f  attachpid;
	getpidlist_f getpidlist;
	getpidcnt_f  getpidcnt;
} job_ops;


/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init (void)
{
	/*  We dlopen() libjob.so instead of directly linking to it
	 *   because of symbols like "job_create" in libjob which
	 *   conflict with symbols in slurmd. dlopening the library
	 *   prevents these symbols from going into the global namespace.
	 */
	if ((libjob_handle = dlopen ("libjob.so", RTLD_LAZY)) == NULL) {
		error ("Unable to open libjob.so: %m");
		return SLURM_ERROR;
	}

	job_ops.create    = dlsym (libjob_handle, "job_create");
	job_ops.getjid    = dlsym (libjob_handle, "job_getjid");
	job_ops.waitjid   = dlsym (libjob_handle, "job_waitjid");
	job_ops.killjid   = dlsym (libjob_handle, "job_killjid");
	job_ops.detachpid = dlsym (libjob_handle, "job_detachpid");
	job_ops.attachpid = dlsym (libjob_handle, "job_attachpid");
	job_ops.getpidlist= dlsym (libjob_handle, "job_getpidlist");
	job_ops.getpidcnt = dlsym (libjob_handle, "job_getpidcnt");

	if (!job_ops.create)
		error ("Unable to resolve job_create in libjob.so");
	if (!job_ops.getjid)
		error ("Unable to resolve job_getjid in libjob.so");
	if (!job_ops.waitjid)
		error ("Unable to resolve job_waitjid in libjob.so");
	if (!job_ops.killjid)
		error ("Unable to resolve job_killjid in libjob.so");
	if (!job_ops.detachpid)
		error ("Unable to resolve job_detachpid in libjob.so");
	if (!job_ops.attachpid)
		error ("Unable to resolve job_attachpid in libjob.so");
	if (!job_ops.getpidlist)
		error ("Unable to resolve job_getpidlist in libjob.so");
	if (!job_ops.getpidcnt)
		error ("Unable to resolve job_getpidcnt in libjob.so");

	info ("successfully loaded libjob.so");
	return SLURM_SUCCESS;
}

int fini (void)
{
	dlclose (libjob_handle);
	return SLURM_SUCCESS;
}

jid_t _job_create (jid_t jid, uid_t uid, int options)
{
	return ((*job_ops.create) (jid, uid, options));
}

jid_t _job_getjid (pid_t pid)
{
	return ((*job_ops.getjid) (pid));
}

jid_t _job_waitjid (jid_t jid, int *status, int options)
{
	return ((*job_ops.waitjid) (jid, status, options));
}

int _job_killjid (jid_t jid, int sig)
{
	return ((*job_ops.killjid) (jid, sig));
}

int _job_detachpid (pid_t pid)
{
	return ((*job_ops.detachpid) (pid));
}

int _job_attachpid (pid_t pid, jid_t jid)
{
	return ((*job_ops.attachpid) (pid, jid));
}

int _job_getpidlist (jid_t jid, pid_t *pid, int bufsize)
{
	return ((*job_ops.getpidlist) (jid, pid, bufsize));
}

int _job_getpidcnt (jid_t jid)
{
	return ((*job_ops.getpidcnt) (jid));
}

int slurm_container_create (slurmd_job_t *job)
{
	jid_t jid;
	job->cont_id = (uint32_t) -1;

	if (!libjob_handle)
		init();

	if ((jid = _job_create (0, job->uid, 0)) == (jid_t) -1) {
		error ("Failed to create job container: %m");
		return SLURM_ERROR;
	}
	debug ("created jid 0x%08lx\n", jid);
	
	return SLURM_SUCCESS;
}

int slurm_container_add (slurmd_job_t *job, pid_t pid)
{
	if (job->cont_id == (uint32_t) -1) {
		job->cont_id = (uint32_t) _job_getjid (getpid());
		/*
		 *  Detach ourselves from the job container now that there
		 *   is at least one other process in it.
		 */
		if (_job_detachpid (getpid()) == (jid_t) -1) {
			error ("Failed to detach from job container: %m");
			return SLURM_ERROR;
		}
	}
	return SLURM_SUCCESS;
}

int slurm_container_signal (uint32_t id, int sig)
{
	if ( (_job_killjid ((jid_t) id, sig) < 0) 
	   && (errno != ENODATA) && (errno != EBADF) )
		return (SLURM_ERROR);
	return (SLURM_SUCCESS);
}

int slurm_container_destroy (uint32_t id)
{
	int status;
	_job_waitjid ((jid_t) id, &status, 0);
	/*  Assume any error means job doesn't exist. Therefore,
	 *   return SUCCESS to slurmd so it doesn't retry continuously
	 */
	return SLURM_SUCCESS;
}

uint32_t slurm_container_find (pid_t pid)
{
	jid_t jid;

	if ((jid = _job_getjid (pid)) == (jid_t) -1)
		return ((uint32_t) 0);
	
	return ((uint32_t) jid);
}

bool slurm_container_has_pid (uint32_t cont_id, pid_t pid)
{
	jid_t jid;

	if ((jid = _job_getjid (pid)) == (jid_t) -1)
		return false;
	if ((uint32_t)jid != cont_id)
		return false;

	return true;
}

int slurm_container_wait (uint32_t id)
{
	int status;
	if (_job_waitjid ((jid_t) id, &status, 0) == (jid_t)-1)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

int slurm_container_get_pids(uint32_t cont_id, pid_t **pids, int *npids)
{
	int pidcnt, bufsize;
	pid_t *p;

	pidcnt = _job_getpidcnt((jid_t)cont_id);
	if (pidcnt > 0) {
		/* 
		 * FIXME - The "+ 128" is a rough attempt to allow for
		 * the fact that _job_getpidcnt() followed by _job_get_pidlist
		 * is not atomic.
		 */
		bufsize = sizeof(pid_t) * (pidcnt + 128);
		p = (pid_t *)xmalloc(bufsize);
		pidcnt = _job_getpidlist((jid_t)cont_id, p, bufsize);
		if (pidcnt == -1) {
			error("job_getpidlist() failed: %m");
			*pids = NULL;
			*npids = 0;
			xfree(p);
			return SLURM_ERROR;
		}
		*pids = p;
		*npids = pidcnt;
	} else {
		*pids = NULL;
		*npids = 0;
	}

	return SLURM_SUCCESS;
}
