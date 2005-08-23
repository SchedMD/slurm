/*****************************************************************************\
 *  proctrack_arms.c - process tracking via QsNet rms kernel module
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

#define MAX_IDS 512

int init (void)
{
	/* close librmscall's internal fd to /proc/rms/control */
	pthread_atfork(NULL, NULL, rmsmod_fini);
	return SLURM_SUCCESS;
}

int fini (void)
{
	return SLURM_SUCCESS;
}


/*
 * This plugin does not actually create the rms program description.
 * switch/elan handles program creation.  This just returns the prgid
 * created by switch/elan.
 */
uint32_t slurm_container_create (slurmd_job_t * job)
{
	int prgid = 0;
	/*
	 * Return a handle to the job step manager's existing prgid
	 */
	if (rms_getprgid (job->jmgr_pid, &prgid) < 0) {
		error ("proctrack/rms: rms_getprdid: %m");
		return (0);
	}
	debug2 ("proctrack/rms: prgid = %d, jmgr_pid = %d",
		prgid, job->jmgr_pid);

	return (prgid);
}

extern int slurm_container_add (uint32_t id, pid_t pid)
{
	return SLURM_SUCCESS;
}

/*
 * slurm_singal_container assumes that the slurmd jobstep manager
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

	debug3("proctrack/rms slurm_container_signal id %d, signal %d",
	       id, signal);
	if (id <= 0)
		return -1;

/* 	rms_prgids(MAX_IDS, ids, &nids); */
/* 	for (i = 0; i < nids; i++) { */
/* 		debug3("proctrack/rms prgid[%d] = %d", i, ids[i]); */
/* 		if (ids[i] == id) { */
/* 			cont_exists = true; */
/* 			break; */
/* 		} */
/* 	} */
/* 	if (!cont_exists) { */
/* 		debug3("container (program description) not found"); */
/* 		return -1; */
/* 	} */

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

	debug3("proctrack/rms nids = %d", nids);
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
int slurm_container_destroy (uint32_t id)
{
	pid_t pids[8];
	int nids = 0;
	int i;

	debug2("proctrack/rms: destroying container %u\n", id);
	if (id == 0)
		return SLURM_SUCCESS;

	debug3("proctrack/rms destroy cont calling signal cont signal 0");
	if (slurm_container_signal(id, 0) == -1)
		return SLURM_SUCCESS;

	return SLURM_ERROR;
}

uint32_t slurm_container_find (pid_t pid)
{
	int prgid = 0;

	if (rms_getprgid ((int) pid, &prgid) < 0) {
		error ("rms_getprgid: %m");
		return (0);
	}
	debug2 ("proctrack/rms: rms_getprgid(pid %ld) = %d",  pid, prgid);
	return ((uint32_t) prgid);
}
