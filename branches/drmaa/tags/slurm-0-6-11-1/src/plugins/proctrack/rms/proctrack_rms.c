/*****************************************************************************\
 *  proctrack_rms.c - process tracking via QsNet rms kernel module
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
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

#include <rms/rmscall.h>

#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>
#include "src/common/log.h"
#include "src/slurmd/proctrack.h"

const char plugin_name[] = "Process tracking for QsNet via the rms module";
const char plugin_type[]      = "proctrack/rms";
const uint32_t plugin_version = 1;

static int _prg_destructor_fork(void);
static void _prg_destructor_send(int fd, int prgid);

#define MAX_IDS 512

extern int init (void)
{
	/* close librmscall's internal fd to /proc/rms/control */
	pthread_atfork(NULL, NULL, rmsmod_fini);
	return SLURM_SUCCESS;
}

extern int fini (void)
{
	return SLURM_SUCCESS;
}


/*
 * When proctrack/rms is used in conjunction with switch/elan, 
 * slurm_container_create will not normally create the program description.
 * It just retrieves the prgid created in switch/elan.
 *
 * When the program description cannot be retrieved (switch/elan is not
 * being used, the job step is a batch script, etc.) then rms_prgcreate()
 * is called here.
 */
extern int slurm_container_create (slurmd_job_t *job)
{
	int prgid;
	/*
	 * Return a handle to an existing prgid or create a new one
	 */
	if (rms_getprgid (job->jmgr_pid, &prgid) < 0) {
		int fd = _prg_destructor_fork();
		/* Use slurmd job-step manager's pid as a unique identifier */
		prgid = job->jmgr_pid;
		if ((rms_prgcreate (prgid, job->uid, 1)) < 0) {
			error ("ptrack/rms: rms_prgcreate: %m");
			_prg_destructor_send(fd, -1);
			return SLURM_ERROR;
		}
		_prg_destructor_send(fd, prgid);
	}
        debug3("proctrack/rms: prgid = %d", prgid);

	job->cont_id = (uint32_t)prgid;
	return SLURM_SUCCESS;
}

extern int slurm_container_add (slurmd_job_t *job, pid_t pid)
{
	return SLURM_SUCCESS;
}

/*
 * slurm_container_signal assumes that the slurmd jobstep manager
 * is always the last process in the rms program description.
 * No signals are sent to the last process.
 */
extern int slurm_container_signal  (uint32_t id, int signal)
{
	pid_t *pids;
	int nids = 0;
	int i;
	int rc;
	int ids[MAX_IDS];
	bool cont_exists = false;

	if (id <= 0)
		return -1;

        pids = malloc(MAX_IDS * sizeof(pid_t));
	if (!pids) {
		error("proctrack/rms container signal: malloc failed: %m");
		return -1;
	}
        if ((rc = rms_prginfo((int)id, MAX_IDS, pids, &nids)) < 0) {
		error("proctrack/rms rms_prginfo failed %d: %m", rc);
		free(pids);
		/*
		 * Ignore errors, program desc has probably already
		 * been cleaned up.
		 */
                return -1;
        }

        rc = -1;
        for (i = nids-2; i >= 0 ; i--) {
		debug3("proctrack/rms(pid %d) Sending signal %d to process %d",
		       getpid(), signal, pids[i]);
		rc &= kill(pids[i], signal);
		debug("rc = %d", rc);
	}
        free(pids);
	debug3("proctrack/rms signal container returning %d", rc);
        return rc;
}


/*
 * The switch/elan plugin is really responsible for creating and
 * destroying rms program descriptions.  slurm_destroy_container simply
 * returns SLURM_SUCCESS when the program description contains one and
 * only one process, assumed to be the slurmd jobstep manager.
 */
extern int slurm_container_destroy (uint32_t id)
{
	pid_t pids[8];
	int nids = 0;
	int i;

	debug2("proctrack/rms: destroying container %u\n", id);
	if (id == 0)
		return SLURM_SUCCESS;

	if (slurm_container_signal(id, 0) == -1)
		return SLURM_SUCCESS;

	return SLURM_ERROR;
}

extern uint32_t slurm_container_find (pid_t pid)
{
	int prgid = 0;

	if (rms_getprgid ((int) pid, &prgid) < 0)
		return (uint32_t) 0;
	return (uint32_t) prgid;
}


static void
_close_all_fd_except(int fd)
{
        int openmax;
        int i;

        openmax = sysconf(_SC_OPEN_MAX);
        for (i = 0; i <= openmax; i++) {
                if (i != fd)
                        close(i);
        }
}


/*
 * Fork a child process that waits for a pipe to close, signalling that the
 * parent process has exited.  Then call rms_prgdestroy.
 */
static int
_prg_destructor_fork()
{
	pid_t pid;
	int fdpair[2];
	int prgid;
	int i;
	int dummy;

	if (pipe(fdpair) < 0) {
		error("_prg_destructor_fork: failed creating pipe");
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		error("_prg_destructor_fork: failed to fork program destructor");
	} else if (pid > 0) {
		/* parent */
		close(fdpair[0]);
		waitpid(pid, (int *)NULL, 0);
		return fdpair[1];
	}
	
	/****************************************/
	/* fork again so the destructor process
         * will not be a child of the slurmd
	 */
	pid = fork();
	if (pid < 0) {
		error("_prg_destructor_fork: second fork failed");
	} else if (pid > 0) {
		exit(0);
	}

	/* child */
	close(fdpair[1]);

	/* close librmscall's internal fd to /proc/rms/control */
	rmsmod_fini();

	_close_all_fd_except(fdpair[0]);
        /* Wait for the program description id from the child */
        if (read(fdpair[0], &prgid, sizeof(prgid)) != sizeof(prgid)) {
		error("_prg_destructor_fork read failed: %m");
		exit(1);
	}

	if (prgid == -1)
		exit(1);

	/*
	 * Wait for the pipe to close, signalling that the parent
	 * has exited.
	 */
	while (read(fdpair[0], &dummy, sizeof(dummy)) > 0) {}

	/*
	 * Verify that program description is empty.  If not, send a SIGKILL.
	 */
	for (i = 0; i < 30; i++) {
		int maxids = 8;
		pid_t pids[8];
		int nids = 0;

		if (rms_prginfo(prgid, maxids, pids, &nids) < 0) {
			error("_prg_destructor_fork: rms_prginfo: %m");
		}
		if (nids == 0)
			break;
		if (rms_prgsignal(prgid, SIGKILL) < 0) {
			error("_prg_destructor_fork: rms_prgsignal: %m");
		}
		sleep(1);
	}

	if (rms_prgdestroy(prgid) < 0) {
		error("rms_prgdestroy");
	}
	exit(0);
}



/*
 * Send the prgid of the newly created program description to the process
 * forked earlier by _prg_destructor_fork(), using the file descriptor
 * "fd" which was returned by the call to _prg_destructor_fork().
 */
static void
_prg_destructor_send(int fd, int prgid)
{
	debug3("_prg_destructor_send %d", prgid);
	if (write (fd, &prgid, sizeof(prgid)) != sizeof(prgid)) {
		error ("_prg_destructor_send failed: %m"); 
	}
	/* Deliberately avoid closing fd.  When this process exits, it
	   will close fd signalling to the child process that it is
	   time to call rms_prgdestroy */
	/*close(fd);*/
}
