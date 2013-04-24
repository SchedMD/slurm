/*****************************************************************************\
 *  deallocate.c  - complete job resource allocation
 *****************************************************************************
 *  Copyright (C) 2012-2013 Los Alamos National Security, LLC.
 *  Written by Jimmy Cao <Jimmy.Cao@emc.com>, Ralph Castain <rhc@open-mpi.org>
 *  All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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
#include "src/slurmctld/locks.h"
#include "src/slurmctld/port_mgr.h"
#include "src/slurmctld/state_save.h"

#include "deallocate.h"
#include "argv.h"
#include "constants.h"
#include "job_ports_list.h"


/**
 * deallocate the resources for slurm jobs.
 *
 * the deallocate msg can be like "deallocate slurm_jobid=123
 * job_return_code=0:slurm_jobid=124 job_return_code=0"
 *
 * IN:
 *	msg: the deallocate msg
 *
 */
extern void deallocate(const char *msg)
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
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK
	};

	jobid_argv = argv_split(msg, ':');
	/* jobid_argv will be freed */
	tmp_jobid_argv = jobid_argv;

	while (*tmp_jobid_argv) {
		/* to identify the slurm_job */
		if (NULL != (pos = strstr(*tmp_jobid_argv, "slurm_jobid="))) {
			pos = pos + strlen("slurm_jobid=");  /* step over */
			sscanf(pos, "%u", &slurm_jobid);
		}

		if (NULL != (pos = strstr(*tmp_jobid_argv,"job_return_code="))){
			pos = pos + strlen("job_return_code=");  /* step over*/
			sscanf(pos, "%u", &job_return_code);
		}

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
			(void) schedule_job_save();	/* Has own locking */
			(void) schedule_node_save();	/* Has own locking */
		}

		/* deallocate port */
		deallocate_port(slurm_jobid);

		/*step to the next */
		tmp_jobid_argv++;
	}
	/* free app_argv */
	argv_free(jobid_argv);
}

/**
 * deallocate the ports for a slurm job.
 *
 * deallocate the ports and remove the entry from List.
 *
 * IN:
 *	slurm_jobid: slurm jobid
 *
 */
extern void deallocate_port(uint32_t slurm_jobid)
{
	job_ports_t *item = NULL;
	ListIterator it = NULL;
	struct job_record *job_ptr = NULL;
	struct step_record step;

	if (NULL == job_ports_list)
		return;

	it = list_iterator_create(job_ports_list);
	item = (job_ports_t *) list_find(it, find_job_ports_item_func,
					 &slurm_jobid);
	if (NULL == item) {
		info ("slurm_jobid = %u not found in List.", slurm_jobid);
		return;
	}

	job_ptr = find_job_record(slurm_jobid);
	step.job_ptr = job_ptr;
	step.step_node_bitmap = job_ptr->node_bitmap;
	step.step_id = 0;
	step.resv_port_cnt = item->port_cnt;
	step.resv_ports =item->resv_ports;
	step.resv_port_array = xmalloc(sizeof(int) * step.resv_port_cnt);
	memcpy(step.resv_port_array, item->port_array,
					sizeof(int) * step.resv_port_cnt);
	/* call resv_port_free in port_mgr.c */
	resv_port_free(&step);

	/* delete the item from list and automatically
	 * call 'free_job_ports_item_func' */
	list_delete_item (it);
	/* destroy iterator */
	list_iterator_destroy(it);
}
