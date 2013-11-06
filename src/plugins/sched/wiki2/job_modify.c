/*****************************************************************************\
 *  job_modify.c - Process Wiki job modify request
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "./msg.h"
#include <strings.h>
#include "src/common/gres.h"
#include "src/common/node_select.h"
#include "src/common/slurm_accounting_storage.h"
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
			char *start_ptr, char *feature_ptr, char *env_ptr,
			char *comment_ptr, char *gres_ptr, char *wckey_ptr)
{
	struct job_record *job_ptr;
	time_t now = time(NULL);
	bool update_accounting = false;

	job_ptr = find_job_record(jobid);
	if (job_ptr == NULL) {
		error("wiki: MODIFYJOB has invalid jobid %u", jobid);
		return ESLURM_INVALID_JOB_ID;
	}
	if (IS_JOB_FINISHED(job_ptr) || (job_ptr->details == NULL)) {
		info("wiki: MODIFYJOB jobid %u is finished", jobid);
		return ESLURM_DISABLED;
	}

	if (comment_ptr) {
		info("wiki: change job %u comment %s", jobid, comment_ptr);
		xfree(job_ptr->comment);
		job_ptr->comment = xstrdup(comment_ptr);
		last_job_update = now;
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

	if (env_ptr) {
		bool have_equal = false;
		char old_sep[1];
		int begin = 0, i;

		if (job_ptr->batch_flag == 0) {
			error("wiki: attempt to set environment variables "
			      "for non-batch job %u", jobid);
			return ESLURM_DISABLED;
		}
		for (i=0; ; i++) {
			if (env_ptr[i] == '=') {
				if (have_equal) {
					error("wiki: setting job %u invalid "
					      "environment variables: %s",
					      jobid, env_ptr);
					return EINVAL;
				}
				have_equal = true;
				if (env_ptr[i+1] == '\"') {
					for (i+=2; ; i++) {
						if (env_ptr[i] == '\0') {
							error("wiki: setting job %u "
							      "invalid environment "
							      "variables: %s",
					 		     jobid, env_ptr);
							return EINVAL;
						}
						if (env_ptr[i] == '\"') {
							i++;
							break;
						}
						if (env_ptr[i] == '\\') {
							i++;
						}
					}
				} else if (env_ptr[i+1] == '\'') {
					for (i+=2; ; i++) {
						if (env_ptr[i] == '\0') {
							error("wiki: setting job %u "
							      "invalid environment "
							      "variables: %s",
					 		     jobid, env_ptr);
							return EINVAL;
						}
						if (env_ptr[i] == '\'') {
							i++;
							break;
						}
						if (env_ptr[i] == '\\') {
							i++;
						}
					}
				}
			}
			if (isspace(env_ptr[i]) || (env_ptr[i] == ',')) {
				if (!have_equal) {
					error("wiki: setting job %u invalid "
					      "environment variables: %s",
					      jobid, env_ptr);
					return EINVAL;
				}
				old_sep[0] = env_ptr[i];
				env_ptr[i] = '\0';
				xrealloc(job_ptr->details->env_sup,
					 sizeof(char *) *
					 (job_ptr->details->env_cnt+1));
				job_ptr->details->env_sup
						[job_ptr->details->env_cnt++] =
						xstrdup(&env_ptr[begin]);
				info("wiki: for job %u add env: %s",
				     jobid, &env_ptr[begin]);
				env_ptr[i] = old_sep[0];
				if (isspace(old_sep[0]))
					break;
				begin = i + 1;
				have_equal = false;
			}
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
		if (IS_JOB_PENDING(job_ptr) && (job_ptr->details)) {
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
		if (IS_JOB_PENDING(job_ptr) && (job_ptr->details)) {
			info("wiki: change job %u begin time to %u",
				jobid, begin_time);
			job_ptr->details->begin_time = begin_time;
			last_job_update = now;
			update_accounting = true;
		} else {
			error("wiki: MODIFYJOB begin_time of non-pending "
				"job %u", jobid);
			return ESLURM_DISABLED;
		}
	}

	if (name_ptr) {
		if (IS_JOB_PENDING(job_ptr)) {
			info("wiki: change job %u name %s", jobid, name_ptr);
			xfree(job_ptr->name);
			job_ptr->name = xstrdup(name_ptr);
			last_job_update = now;
			update_accounting = true;
		} else {
			error("wiki: MODIFYJOB name of non-pending job %u",
			      jobid);
			return ESLURM_DISABLED;
		}
	}

	if (new_hostlist) {
		int rc = 0, task_cnt;
		hostlist_t hl;
		char *tasklist;

		if (!IS_JOB_PENDING(job_ptr) || !job_ptr->details) {
			/* Job is done, nothing to reset */
			if (new_hostlist == '\0')
				goto host_fini;
			error("wiki: MODIFYJOB hostlist of non-pending "
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
		job_ptr->details->req_nodes =
			hostlist_ranged_string_xmalloc(hl);
		hostlist_destroy(hl);
		if (job_ptr->details->req_nodes == NULL) {
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
			update_accounting = true;
		}
	}

	if (part_name_ptr) {
		struct part_record *part_ptr;
		if (!IS_JOB_PENDING(job_ptr)) {
			error("wiki: MODIFYJOB partition of non-pending "
			      "job %u", jobid);
			return ESLURM_DISABLED;
		}

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
		update_accounting = true;
	}

	if (new_node_cnt) {
		job_desc_msg_t job_desc;
#ifdef HAVE_BG
		uint16_t geometry[SYSTEM_DIMENSIONS] = {(uint16_t) NO_VAL};
		static uint16_t cpus_per_node = 0;
		if (!cpus_per_node) {
			select_g_alter_node_cnt(SELECT_GET_NODE_CPU_CNT,
						&cpus_per_node);
		}
#endif
		if (!IS_JOB_PENDING(job_ptr) || !job_ptr->details) {
			error("wiki: MODIFYJOB node count of non-pending "
			      "job %u", jobid);
			return ESLURM_DISABLED;
		}
		memset(&job_desc, 0, sizeof(job_desc_msg_t));

		job_desc.min_nodes = new_node_cnt;
		job_desc.max_nodes = NO_VAL;
		job_desc.select_jobinfo = select_g_select_jobinfo_alloc();

		select_g_alter_node_cnt(SELECT_SET_NODE_CNT, &job_desc);

		select_g_select_jobinfo_free(job_desc.select_jobinfo);

		job_ptr->details->min_nodes = job_desc.min_nodes;
		if (job_ptr->details->max_nodes &&
		    (job_ptr->details->max_nodes < job_desc.min_nodes))
			job_ptr->details->max_nodes = job_desc.min_nodes;
		info("wiki: change job %u min_nodes to %u",
		     jobid, new_node_cnt);
#ifdef HAVE_BG
		job_ptr->details->min_cpus = job_desc.min_cpus;
		job_ptr->details->max_cpus = job_desc.max_cpus;
		job_ptr->details->pn_min_cpus = job_desc.pn_min_cpus;

		new_node_cnt = job_ptr->details->min_cpus;
		if (cpus_per_node)
			new_node_cnt /= cpus_per_node;

		/* This is only set up so accounting is set up correctly */
		select_g_select_jobinfo_set(job_ptr->select_jobinfo,
					    SELECT_JOBDATA_NODE_CNT,
					    &new_node_cnt);
		/* reset geo since changing this makes any geo
		   potentially invalid */
		select_g_select_jobinfo_set(job_ptr->select_jobinfo,
					    SELECT_JOBDATA_GEOMETRY,
					    geometry);
#endif
		last_job_update = now;
		update_accounting = true;
	}

	if (gres_ptr) {
		char *orig_gres;

		if (!IS_JOB_PENDING(job_ptr)) {
			error("wiki: MODIFYJOB GRES of non-pending job %u",
			      jobid);
			return ESLURM_DISABLED;
		}

		orig_gres = job_ptr->gres;
		job_ptr->gres = NULL;
		if (gres_ptr[0])
			job_ptr->gres = xstrdup(gres_ptr);
		if (gres_plugin_job_state_validate(job_ptr->gres,
						   &job_ptr->gres_list)) {
			error("wiki: MODIFYJOB Invalid GRES=%s", gres_ptr);
			xfree(job_ptr->gres);
			job_ptr->gres = orig_gres;
			return ESLURM_INVALID_GRES;
		}
		xfree(orig_gres);
	}

	if (wckey_ptr) {
		int rc = update_job_wckey("update_job", job_ptr, wckey_ptr);
		if (rc != SLURM_SUCCESS) {
			error("wiki: MODIFYJOB Invalid WCKEY=%s", wckey_ptr);
			return rc;
		}
	}

	if (update_accounting) {
		if (job_ptr->details && job_ptr->details->begin_time) {
			/* Update job record in accounting to reflect
			 * the changes */
			jobacct_storage_g_job_start(acct_db_conn, job_ptr);
		}
	}

	return SLURM_SUCCESS;
}

/* Modify a job:
 *	CMD=MODIFYJOB ARG=<jobid>
 *		[BANK=<name>;]
 *		[COMMENT=<whatever>;]
 *		[DEPEND=afterany:<jobid>;]
 *		[JOBNAME=<name>;]
 *		[MINSTARTTIME=<uts>;]
 *		[NODES=<number>;]
 *		[PARTITION=<name>;]
 *		[RFEATURES=<features>;]
 *		[TIMELIMT=<seconds>;]
 *		[VARIABLELIST=<env_vars>;]
 *		[GRES=<name:value>;]
 *		[WCKEY=<name>;]
 *
 * RET 0 on success, -1 on failure */
extern int	job_modify_wiki(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *bank_ptr, *depend_ptr, *nodes_ptr, *start_ptr;
	char *host_ptr, *name_ptr, *part_ptr, *time_ptr, *tmp_char;
	char *comment_ptr, *feature_ptr, *env_ptr, *gres_ptr, *wckey_ptr;
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
	comment_ptr = strstr(cmd_ptr, "COMMENT=");
	depend_ptr  = strstr(cmd_ptr, "DEPEND=");
	gres_ptr    = strstr(cmd_ptr, "GRES=");
	host_ptr    = strstr(cmd_ptr, "HOSTLIST=");
	name_ptr    = strstr(cmd_ptr, "JOBNAME=");
	start_ptr   = strstr(cmd_ptr, "MINSTARTTIME=");
	nodes_ptr   = strstr(cmd_ptr, "NODES=");
	part_ptr    = strstr(cmd_ptr, "PARTITION=");
	feature_ptr = strstr(cmd_ptr, "RFEATURES=");
	time_ptr    = strstr(cmd_ptr, "TIMELIMIT=");
	env_ptr     = strstr(cmd_ptr, "VARIABLELIST=");
	wckey_ptr   = strstr(cmd_ptr, "WCKEY=");
	if (bank_ptr) {
		bank_ptr[4] = ':';
		bank_ptr += 5;
		null_term(bank_ptr);
	}
	if (comment_ptr) {
		comment_ptr[7] = ':';
		comment_ptr += 8;
		if (comment_ptr[0] == '\"') {
			comment_ptr++;
			for (i=0; ; i++) {
				if (comment_ptr[i] == '\0')
					break;
				if (comment_ptr[i] == '\"') {
					comment_ptr[i] = '\0';
					break;
				}
			}
		} else if (comment_ptr[0] == '\'') {
			comment_ptr++;
			for (i=0; ; i++) {
				if (comment_ptr[i] == '\0')
					break;
				if (comment_ptr[i] == '\'') {
					comment_ptr[i] = '\0';
					break;
				}
			}
		} else
			null_term(comment_ptr);
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
	if (gres_ptr) {
		gres_ptr[4] = ':';
		gres_ptr += 5;
		null_term(gres_ptr);
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
	if (env_ptr) {
		env_ptr[12] = ':';
		env_ptr += 13;
		null_term(env_ptr);
	}
	if (wckey_ptr) {
		wckey_ptr[5] = ':';
		wckey_ptr += 6;
		null_term(wckey_ptr);
	}

	/* Look for any un-parsed "=" ignoring anything after VARIABLELIST
	 * which is expected to contain "=" in its value*/
	tmp_char = strchr(cmd_ptr, '=');
	if (tmp_char && (!env_ptr || (env_ptr > tmp_char))) {
		tmp_char[0] = '\0';
		while (tmp_char[-1] && (!isspace(tmp_char[-1])))
			tmp_char--;
		error("wiki: Invalid MODIFYJOB option %s", tmp_char);
	}

	lock_slurmctld(job_write_lock);
	slurm_rc = _job_modify(jobid, bank_ptr, depend_ptr, host_ptr,
			new_node_cnt, part_ptr, new_time_limit, name_ptr,
			start_ptr, feature_ptr, env_ptr, comment_ptr,
			gres_ptr, wckey_ptr);
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
