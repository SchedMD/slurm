/*****************************************************************************\
 *  job_modify.c - Process Wiki job modify request
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-226842.
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
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

static void	_null_term(char *str)
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

/* return -1 on error */
static int32_t _get_depend_id(char *str)
{
	/* stand-alone job_id */
	if (isdigit(str[0]))
		return (int32_t) atol(str);

	if (strncasecmp(str, "afterany:", 9) != 0)	/* invalid spec */
		return (int32_t) -1;

	str += 9;
	if (!isdigit(str[0]))
		return (int32_t) -1;
	return (int32_t) atol(str);
}

static int	_job_modify(uint32_t jobid, char *bank_ptr, 
			int32_t depend_id,
			uint32_t new_node_cnt, char *part_name_ptr, 
			uint32_t new_time_limit)
{
	struct job_record *job_ptr;

	job_ptr = find_job_record(jobid);
	if (job_ptr == NULL) {
		error("wiki: MODIFYJOB has invalid jobid %u", jobid);
		return ESLURM_INVALID_JOB_ID;
	}
	if (IS_JOB_FINISHED(job_ptr)) {
		error("wiki: MODIFYJOB jobid %u is finished", jobid);
		return ESLURM_DISABLED;
	}

	if (depend_id != -1) {
		info("wiki: changing job dependency to %d", depend_id);
		job_ptr->dependency = depend_id;
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
		last_job_update = time(NULL);
	}
	if (bank_ptr) {
		info("wiki: change job %u bank %s", jobid, bank_ptr);
		xfree(job_ptr->account);
		job_ptr->account = xstrdup(bank_ptr);
		last_job_update = time(NULL);
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
		strncpy(job_ptr->partition, part_name_ptr, MAX_SLURM_NAME);
		job_ptr->part_ptr = part_ptr;
		last_job_update = time(NULL);
	}

	if (new_node_cnt) {
		if (IS_JOB_PENDING(job_ptr) && job_ptr->details) {
			job_ptr->details->min_nodes = new_node_cnt;
			if (job_ptr->details->max_nodes
			&&  (job_ptr->details->max_nodes < new_node_cnt))
				job_ptr->details->max_nodes = new_node_cnt;
			info("wiki: change job %u min_nodes to %u",
				jobid, new_node_cnt);
			last_job_update = time(NULL);
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
 * RET 0 on success, -1 on failure */
extern int	job_modify_wiki(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *bank_ptr, *depend_ptr, *nodes_ptr;
	char *part_ptr, *time_ptr, *tmp_char;
	int slurm_rc;
	int depend_id = -1;
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
	bank_ptr   = strstr(cmd_ptr, "BANK=");
	depend_ptr = strstr(cmd_ptr, "DEPEND=");
	nodes_ptr  = strstr(cmd_ptr, "NODES=");
	part_ptr   = strstr(cmd_ptr, "PARTITION=");
	time_ptr   = strstr(cmd_ptr, "TIMELIMIT=");
	if (bank_ptr) {
		bank_ptr[4] = ':';
		bank_ptr += 5;
		_null_term(bank_ptr);
	}
	if (depend_ptr) {
		depend_ptr[6] = ':';
		depend_ptr += 7;
		depend_id = _get_depend_id(depend_ptr);
		if (depend_id == -1) {
			*err_code = -300;
			*err_msg = "MODIFYJOB has invalid DEPEND specificiation";
			error("wiki: MODIFYJOB has invalid DEPEND spec: %s",
				depend_ptr);
			return -1;
		}
	}
	if (nodes_ptr) {
		nodes_ptr[5] = ':';
		nodes_ptr += 6;
		new_node_cnt = strtoul(nodes_ptr, NULL, 10);
	}
	if (part_ptr) {
		part_ptr[9] = ':';
		part_ptr += 10;
		_null_term(part_ptr);
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
	slurm_rc = _job_modify(jobid, bank_ptr, depend_id, 
			new_node_cnt, part_ptr, new_time_limit);
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
