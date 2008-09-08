/*****************************************************************************\
 *  job_modify.c - Process Wiki job modify request
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include "./msg.h"
#include <strings.h>
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

/* Given a string, replace the first space found with '\0' */
extern void	null_term(char *str)
{
	char *tmp_ptr;
	for (tmp_ptr=str; ; tmp_ptr++) {
		if (tmp_ptr[0] == '\0')
			break;
		if (isspace(tmp_ptr[0])) {
			tmp_ptr[0] = '\0';
			break;
		}
	}
}

static int	_job_modify(uint32_t jobid, char *bank_ptr, 
			char *depend_ptr, char *new_hostlist,
			uint32_t new_node_cnt, char *part_name_ptr, 
			uint32_t new_time_limit, char *name_ptr,
			char *start_ptr, char *feature_ptr)
{
	struct job_record *job_ptr;
	time_t now = time(NULL);

	job_ptr = find_job_record(jobid);
	if (job_ptr == NULL) {
		error("wiki: MODIFYJOB has invalid jobid %u", jobid);
		return ESLURM_INVALID_JOB_ID;
	}
	if (IS_JOB_FINISHED(job_ptr)) {
		error("wiki: MODIFYJOB jobid %u is finished", jobid);
		return ESLURM_DISABLED;
	}

	if (depend_ptr) {
		int rc = update_job_dependency(job_ptr, depend_ptr);
		if (rc == SLURM_SUCCESS) {
			info("wiki: changed job %u dependency to %s", 
				jobid, depend_ptr);
		} else {
			error("wiki: changing job %u dependency to %s", 
				jobid, depend_ptr);
			return EINVAL;
		}
	}

	if (new_time_limit) {
		time_t old_time = job_ptr->time_limit;
		job_ptr->time_limit = new_time_limit;
		info("wiki: change job %u time_limit to %u",
			jobid, new_time_limit);
		/* Update end_time based upon change
		 * to preserve suspend time info */
		job_ptr->end_time = job_ptr->end_time +
				((job_ptr->time_limit -
				  old_time) * 60);
		last_job_update = now;
	}

	if (bank_ptr &&
	    (update_job_account("wiki", job_ptr, bank_ptr) != SLURM_SUCCESS)) {
		return EINVAL;
	}

	if (feature_ptr) {
		if ((job_ptr->job_state == JOB_PENDING) &&
		    (job_ptr->details)) {
			info("wiki: change job %u features to %s", 
				jobid, feature_ptr);
			job_ptr->details->features = xstrdup(feature_ptr);
			last_job_update = now;
		} else {
			error("wiki: MODIFYJOB features of non-pending "
				"job %u", jobid);
			return ESLURM_DISABLED;
		}
	}

	if (start_ptr) {
		char *end_ptr;
		uint32_t begin_time = strtol(start_ptr, &end_ptr, 10);
		if ((job_ptr->job_state == JOB_PENDING) &&
		    (job_ptr->details)) {
			info("wiki: change job %u begin time to %u", 
				jobid, begin_time);
			job_ptr->details->begin_time = begin_time;
			last_job_update = now;
		} else {
			error("wiki: MODIFYJOB begin_time of non-pending "
				"job %u", jobid);
			return ESLURM_DISABLED;
		}
	}

	if (name_ptr) {
		info("wiki: change job %u name %s", jobid, name_ptr);
		xfree(job_ptr->name);
		job_ptr->name = xstrdup(name_ptr);
		last_job_update = now;
	}

	if (new_hostlist) {
		int i, rc = 0, task_cnt;
		hostlist_t hl;
		char *tasklist;

		if (!job_ptr->details) {
			/* Job is done, nothing to reset */
			if (new_hostlist == '\0')
				goto host_fini;
			error("wiki: MODIFYJOB tasklist of non-pending "
				"job %u", jobid);
			return ESLURM_DISABLED;
		}

		xfree(job_ptr->details->req_nodes);
		FREE_NULL_BITMAP(job_ptr->details->req_node_bitmap);
		if (new_hostlist == '\0')
			goto host_fini;

		tasklist = moab2slurm_task_list(new_hostlist, &task_cnt);
		if (tasklist == NULL) {
			rc = 1;
			goto host_fini;
		}
		hl = hostlist_create(tasklist);
		if (hl == 0) {
			rc = 1;
			goto host_fini;
		}
		hostlist_uniq(hl);
		hostlist_sort(hl);
		i = strlen(new_hostlist) + 16;
		job_ptr->details->req_nodes = xmalloc(i);
		i = hostlist_ranged_string(hl, i, job_ptr->details->req_nodes);
		hostlist_destroy(hl);
		if (i < 0) {
			rc = 1;
			goto host_fini;
		}
		if (node_name2bitmap(job_ptr->details->req_nodes, false,
                                     &job_ptr->details->req_node_bitmap)) {
			rc = 1;
			goto host_fini;
		}

host_fini:	if (rc) {
			info("wiki: change job %u invalid hostlist %s", 
				jobid, new_hostlist);
			xfree(job_ptr->details->req_nodes);
			return EINVAL;
		} else {
			info("wiki: change job %u hostlist %s", 
				jobid, new_hostlist);
		}
	}

	if (part_name_ptr) {
		struct part_record *part_ptr;
		part_ptr = find_part_record(part_name_ptr);
		if (part_ptr == NULL) {
			error("wiki: MODIFYJOB has invalid partition %s",
				part_name_ptr);
			return ESLURM_INVALID_PARTITION_NAME;
		}
		info("wiki: change job %u partition %s",
			jobid, part_name_ptr);
		xfree(job_ptr->partition);
		job_ptr->partition = xstrdup(part_name_ptr);
		job_ptr->part_ptr = part_ptr;
		last_job_update = now;
	}

	if (new_node_cnt) {
		if (IS_JOB_PENDING(job_ptr) && job_ptr->details) {
			job_ptr->details->min_nodes = new_node_cnt;
			if (job_ptr->details->max_nodes
			&&  (job_ptr->details->max_nodes < new_node_cnt))
				job_ptr->details->max_nodes = new_node_cnt;
			info("wiki: change job %u min_nodes to %u",
				jobid, new_node_cnt);
			last_job_update = now;
		} else {
			error("wiki: MODIFYJOB node count of non-pending "
				"job %u", jobid);
			return ESLURM_DISABLED;
		}
	}

	return SLURM_SUCCESS;
}

/* Modify a job:
 *	CMD=MODIFYJOB ARG=<jobid> PARTITION=<name> NODES=<number>
 *		DEPEND=afterany:<jobid> TIMELIMT=<seconds> BANK=<name>
 *		MINSTARTTIME=<uts> RFEATURES=<features>
 * RET 0 on success, -1 on failure */
extern int	job_modify_wiki(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *bank_ptr, *depend_ptr, *nodes_ptr, *start_ptr;
	char *host_ptr, *name_ptr, *part_ptr, *time_ptr, *tmp_char;
	char *feature_ptr;
	int i, slurm_rc;
	uint32_t jobid, new_node_cnt = 0, new_time_limit = 0;
	static char reply_msg[128];
	/* Locks: write job, read node and partition info */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };

	arg_ptr = strstr(cmd_ptr, "ARG=");
	if (arg_ptr == NULL) {
		*err_code = -300;
		*err_msg = "MODIFYJOB lacks ARG=";
		error("wiki: MODIFYJOB lacks ARG=");
		return -1;
	}
	/* Change all parsed "=" to ":" then search for remaining "="
	 * and report results as unrecognized options */
	arg_ptr[3] = ':';
	arg_ptr += 4;
	jobid = strtoul(arg_ptr, &tmp_char, 10);
	if ((tmp_char[0] != '\0') && (!isspace(tmp_char[0]))) {
		*err_code = -300;
		*err_msg = "Invalid ARG value";
		error("wiki: MODIFYJOB has invalid jobid");
		return -1;
	}
	bank_ptr    = strstr(cmd_ptr, "BANK=");
	depend_ptr  = strstr(cmd_ptr, "DEPEND=");
	host_ptr    = strstr(cmd_ptr, "HOSTLIST=");
	name_ptr    = strstr(cmd_ptr, "JOBNAME=");
	start_ptr   = strstr(cmd_ptr, "MINSTARTTIME=");
	nodes_ptr   = strstr(cmd_ptr, "NODES=");
	part_ptr    = strstr(cmd_ptr, "PARTITION=");
	feature_ptr = strstr(cmd_ptr, "RFEATURES=");
	time_ptr    = strstr(cmd_ptr, "TIMELIMIT=");
	if (bank_ptr) {
		bank_ptr[4] = ':';
		bank_ptr += 5;
		null_term(bank_ptr);
	}
	if (depend_ptr) {
		depend_ptr[6] = ':';
		depend_ptr += 7;
		null_term(depend_ptr);
	}
	if (feature_ptr) {
		feature_ptr[9] = ':';
		feature_ptr += 10;
		null_term(feature_ptr);
	}
	if (host_ptr) {
		host_ptr[8] = ':';
		host_ptr += 9;
		null_term(host_ptr);
	}
	if (name_ptr) {
		name_ptr[7] = ':';
		name_ptr += 8;
		if (name_ptr[0] == '\"') {
			name_ptr++;
			for (i=0; ; i++) {
				if (name_ptr[i] == '\0')
					break;
				if (name_ptr[i] == '\"') {
					name_ptr[i] = '\0';
					break;
				}
			}
		} else if (name_ptr[0] == '\'') {
			name_ptr++;
			for (i=0; ; i++) {
				if (name_ptr[i] == '\0')
					break;
				if (name_ptr[i] == '\'') {
					name_ptr[i] = '\0';
					break;
				}
			}
		} else
			null_term(name_ptr);
	}
	if (start_ptr) {
		start_ptr[12] = ':';
		start_ptr += 13;
		null_term(start_ptr);
	}	
	if (nodes_ptr) {
		nodes_ptr[5] = ':';
		nodes_ptr += 6;
		new_node_cnt = strtoul(nodes_ptr, NULL, 10);
	}
	if (part_ptr) {
		part_ptr[9] = ':';
		part_ptr += 10;
		null_term(part_ptr);
	}
	if (time_ptr) {
		time_ptr[9] = ':';
		time_ptr += 10;
		new_time_limit = strtoul(time_ptr, NULL, 10);
	}

	/* Look for any un-parsed "=" */
	tmp_char = strchr(cmd_ptr, '=');
	if (tmp_char) {
		tmp_char[0] = '\0';
		while (tmp_char[-1] && (!isspace(tmp_char[-1])))
			tmp_char--;
		error("wiki: Invalid MODIFYJOB option %s", tmp_char);
	}

	lock_slurmctld(job_write_lock);
	slurm_rc = _job_modify(jobid, bank_ptr, depend_ptr, host_ptr,
			new_node_cnt, part_ptr, new_time_limit, name_ptr,
			start_ptr, feature_ptr);
	unlock_slurmctld(job_write_lock);
	if (slurm_rc != SLURM_SUCCESS) {
		*err_code = -700;
		*err_msg = slurm_strerror(slurm_rc);
		error("wiki: Failed to modify job %u (%m)", jobid);
		return -1;
	}

	snprintf(reply_msg, sizeof(reply_msg),
		"job %u modified successfully", jobid);
	*err_msg = reply_msg;
	return 0;
}
