/*****************************************************************************\
 *  deallocate.c  - complete job resource allocation
 *****************************************************************************
 *  Copyright (C) 2012-2013 Los Alamos National Security, LLC.
 *  Written by Jimmy Cao <Jimmy.Cao@emc.com>, Ralph Castain <rhc@open-mpi.org>
 *  All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
 *  Please also read the included file: DISCLAIMER.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmctld/locks.h"

#include "deallocate.h"
#include "argv.h"
#include "constants.h"

int deallocate(slurm_fd_t new_fd, const char *msg)
{
	char **jobid_argv = NULL, **tmp_jobid_argv;
	char *pos = NULL;
	/* params to complete a job allocation */
	uint32_t slurm_jobid;
	uid_t uid = 0;
	bool job_requeue = false;
	bool node_fail = false;
	uint32_t job_return_code = NO_VAL;
	int  rc = SLURM_SUCCESS;

	jobid_argv = argv_split(msg, ':');
	/* jobid_argv will be freed */
	tmp_jobid_argv = jobid_argv;

	while(*tmp_jobid_argv){
		/* to identify the slurm_job */
		if (NULL != (pos = strstr(*tmp_jobid_argv, "slurm_jobid="))) {
			pos = pos + strlen("slurm_jobid=");  /* step over */
			sscanf(pos, "%u", &slurm_jobid);
		}

		if (NULL != (pos = strstr(*tmp_jobid_argv, "job_return_code="))) {
			pos = pos + strlen("job_return_code=");  /* step over*/
			sscanf(pos, "%u", &job_return_code);
		}

		/* Locks: Write job, write node */
		slurmctld_lock_t job_write_lock = {
			NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK
		};
		lock_slurmctld(job_write_lock);
		rc = job_complete(slurm_jobid, uid, job_requeue,
				  node_fail, job_return_code);
		unlock_slurmctld(job_write_lock);

		/* return result */
		if (rc) {
			info("deallocate JobId=%u: %s ",
			     slurm_jobid, slurm_strerror(rc));
		} else {
			debug2("deallocate JobId=%u ", slurm_jobid);
			(void) schedule_job_save();		/* Has own locking */
			(void) schedule_node_save();	/* Has own locking */
		}

		/*step to the next */
		tmp_jobid_argv++;
	}
	/* free app_argv */
	argv_free(jobid_argv);

	return SLURM_SUCCESS;
}
