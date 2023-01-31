/*****************************************************************************\
 *  jobcomp_common.c - common functions for jobcomp plugins
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC
 *  Written by Alejandro Sanchez <alex@schedmd.com>
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

#include "src/common/assoc_mgr.h"
#include "src/common/data.h"
#include "src/common/fd.h"
#include "src/common/parse_time.h"
#include "src/common/uid.h"
#include "src/plugins/jobcomp/common/jobcomp_common.h"
#include "src/slurmctld/slurmctld.h"

/*
 * Open jobcomp state file, or backup if necessary.
 *
 * IN: char pointer to state file name.
 * RET: buffer with the loaded file or NULL.
 */
extern buf_t *jobcomp_common_load_state_file(char *state_file)
{
	char *absolute_file = NULL;
	buf_t *buf;

	xassert(state_file);

	xstrfmtcat(absolute_file, "%s/%s",
		   slurm_conf.state_save_location, state_file);

	if ((buf = create_mmap_buf(absolute_file))) {
		xfree(absolute_file);
		return buf;
	}

	error("Could not open jobcomp state file %s: %m", absolute_file);
	error("NOTE: Trying backup jobcomp state save file. Finished jobs may be lost!");

	xstrcat(absolute_file, ".old");

	if (!(buf = create_mmap_buf(absolute_file)))
		error("Could not open backup jobcomp state file %s: %m", absolute_file);

	xfree(absolute_file);

	return buf;
}

extern void jobcomp_common_write_state_file(buf_t *buffer, char *state_file)
{
	int fd;
	bool do_close = true;
	char *reg_file = NULL, *new_file = NULL, *old_file = NULL;
	char *tmp_str = NULL;

	xstrfmtcat(reg_file, "%s/%s", slurm_conf.state_save_location,
		   state_file);
	xstrfmtcat(old_file, "%s.old", reg_file);
	xstrfmtcat(new_file, "%s.new", reg_file);

	if ((fd = creat(new_file, 0600)) < 0) {
		xstrfmtcat(tmp_str, "creating");
		goto rwfail;
	}

	xstrfmtcat(tmp_str, "writing");
	safe_write(fd, get_buf_data(buffer), get_buf_offset(buffer));
	xfree(tmp_str);

	do_close = false;
	if (fsync_and_close(fd, state_file))
		goto rwfail;

	(void) unlink(old_file);
	if (link(reg_file, old_file))
		debug2("unable to create link for %s -> %s: %m",
		       reg_file, old_file);

	(void) unlink(reg_file);
	if (link(new_file, reg_file))
		debug2("unable to create link for %s -> %s: %m",
		       new_file, reg_file);

rwfail:
	if (tmp_str)
		error("Can't save state, error %s file %s: %m",
		      tmp_str, new_file);
	if (do_close && fsync_and_close(fd, state_file))
		;
	(void) unlink(new_file);
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);
	xfree(tmp_str);
}

extern data_t *jobcomp_common_job_record_to_data(job_record_t *job_ptr) {
	char start_str[32], end_str[32], time_str[32];
	char *usr_str = NULL, *grp_str = NULL, *state_string = NULL;
	char *exit_code_str = NULL, *derived_ec_str = NULL;
	buf_t *script = NULL;
	enum job_states job_state;
	int i, tmp_int, tmp_int2;
	time_t elapsed_time;
	uint32_t time_limit;
	data_t *record = NULL;

	usr_str = uid_to_string_or_null(job_ptr->user_id);
	grp_str = gid_to_string_or_null(job_ptr->group_id);

	if ((job_ptr->time_limit == NO_VAL) && job_ptr->part_ptr)
		time_limit = job_ptr->part_ptr->max_time;
	else
		time_limit = job_ptr->time_limit;

	if (job_ptr->job_state & JOB_RESIZING) {
		time_t now = time(NULL);
		state_string = job_state_string(job_ptr->job_state);
		if (job_ptr->resize_time) {
			parse_time_make_str_utc(&job_ptr->resize_time,
						start_str, sizeof(start_str));
		} else {
			parse_time_make_str_utc(&job_ptr->start_time, start_str,
						sizeof(start_str));
		}
		parse_time_make_str_utc(&now, end_str, sizeof(end_str));
	} else {
		/* Job state will typically have JOB_COMPLETING or JOB_RESIZING
		 * flag set when called. We remove the flags to get the eventual
		 * completion state: JOB_FAILED, JOB_TIMEOUT, etc. */
		job_state = job_ptr->job_state & JOB_STATE_BASE;
		state_string = job_state_string(job_state);
		if (job_ptr->resize_time) {
			parse_time_make_str_utc(&job_ptr->resize_time,
						start_str, sizeof(start_str));
		} else if (job_ptr->start_time > job_ptr->end_time) {
			/* Job cancelled while pending and
			 * expected start time is in the future. */
			snprintf(start_str, sizeof(start_str), "Unknown");
		} else {
			parse_time_make_str_utc(&job_ptr->start_time, start_str,
						sizeof(start_str));
		}
		parse_time_make_str_utc(&job_ptr->end_time, end_str,
					sizeof(end_str));
	}

	elapsed_time = job_ptr->end_time - job_ptr->start_time;

	tmp_int = tmp_int2 = 0;
	if (job_ptr->derived_ec == NO_VAL)
		;
	else if (WIFSIGNALED(job_ptr->derived_ec))
		tmp_int2 = WTERMSIG(job_ptr->derived_ec);
	else if (WIFEXITED(job_ptr->derived_ec))
		tmp_int = WEXITSTATUS(job_ptr->derived_ec);
	xstrfmtcat(derived_ec_str, "%d:%d", tmp_int, tmp_int2);

	tmp_int = tmp_int2 = 0;
	if (job_ptr->exit_code == NO_VAL)
		;
	else if (WIFSIGNALED(job_ptr->exit_code))
		tmp_int2 = WTERMSIG(job_ptr->exit_code);
	else if (WIFEXITED(job_ptr->exit_code))
		tmp_int = WEXITSTATUS(job_ptr->exit_code);
	xstrfmtcat(exit_code_str, "%d:%d", tmp_int, tmp_int2);

	record = data_set_dict(data_new());

	data_set_int(data_key_set(record, "jobid"), job_ptr->job_id);
	data_set_string(data_key_set(record, "container"), job_ptr->container);
	data_set_string(data_key_set(record, "username"), usr_str);
	data_set_int(data_key_set(record, "user_id"), job_ptr->user_id);
	data_set_string(data_key_set(record, "groupname"), grp_str);
	data_set_int(data_key_set(record, "group_id"), job_ptr->group_id);
	data_set_string(data_key_set(record, "@start"), start_str);
	data_set_string(data_key_set(record, "@end"), end_str);
	data_set_int(data_key_set(record, "elapsed"), elapsed_time);
	data_set_string(data_key_set(record, "partition"), job_ptr->partition);
	data_set_string(data_key_set(record, "alloc_node"),
			job_ptr->alloc_node);
	data_set_string(data_key_set(record, "nodes"), job_ptr->nodes);
	data_set_int(data_key_set(record, "total_cpus"), job_ptr->total_cpus);
	data_set_int(data_key_set(record, "total_nodes"), job_ptr->total_nodes);
	data_set_string_own(data_key_set(record, "derived_ec"), derived_ec_str);
	derived_ec_str = NULL;
	data_set_string_own(data_key_set(record, "exit_code"), exit_code_str);
	exit_code_str = NULL;
	data_set_string(data_key_set(record, "state"), state_string);
	data_set_string(data_key_set(record, "failed_node"),
			job_ptr->failed_node);
	data_set_float(data_key_set(record, "cpu_hours"),
		       ((elapsed_time * job_ptr->total_cpus) / 3600.0f));

	if (job_ptr->array_task_id != NO_VAL) {
		data_set_int(data_key_set(record, "array_job_id"),
			     job_ptr->array_job_id);
		data_set_int(data_key_set(record, "array_task_id"),
			     job_ptr->array_task_id);
	}

	if (job_ptr->het_job_id != NO_VAL) {
		/* Continue supporting the old terms. */
		data_set_int(data_key_set(record, "pack_job_id"),
			     job_ptr->het_job_id);
		data_set_int(data_key_set(record, "pack_job_offset"),
			     job_ptr->het_job_offset);
		data_set_int(data_key_set(record, "het_job_id"),
			     job_ptr->het_job_id);
		data_set_int(data_key_set(record, "het_job_offset"),
			     job_ptr->het_job_offset);
	}

	if (job_ptr->details && job_ptr->details->submit_time) {
		parse_time_make_str_utc(&job_ptr->details->submit_time,
					time_str, sizeof(time_str));
		data_set_string(data_key_set(record, "@submit"), time_str);
	}

	if (job_ptr->details && job_ptr->details->begin_time) {
		parse_time_make_str_utc(&job_ptr->details->begin_time, time_str,
					sizeof(time_str));
		data_set_string(data_key_set(record, "@eligible"), time_str);
		if (job_ptr->start_time) {
			int64_t queue_wait = (int64_t)difftime(
				job_ptr->start_time,
				job_ptr->details->begin_time);
			data_set_int(data_key_set(record, "@queue_wait"),
				     queue_wait);
		}
	}

	if (job_ptr->details && job_ptr->details->work_dir)
		data_set_string(data_key_set(record, "work_dir"),
				job_ptr->details->work_dir);

	if (job_ptr->details && job_ptr->details->std_err)
		data_set_string(data_key_set(record, "std_err"),
				job_ptr->details->std_err);

	if (job_ptr->details && job_ptr->details->std_in)
		data_set_string(data_key_set(record, "std_in"),
				job_ptr->details->std_in);

	if (job_ptr->details && job_ptr->details->std_out)
		data_set_string(data_key_set(record, "std_out"),
				job_ptr->details->std_out);

	if (job_ptr->assoc_ptr && job_ptr->assoc_ptr->cluster)
		data_set_string(data_key_set(record, "cluster"),
				job_ptr->assoc_ptr->cluster);

	if (job_ptr->qos_ptr && job_ptr->qos_ptr->name)
		data_set_string(data_key_set(record, "qos"),
				job_ptr->qos_ptr->name);

	if (job_ptr->details && (job_ptr->details->num_tasks != NO_VAL))
		data_set_int(data_key_set(record, "ntasks"),
			     job_ptr->details->num_tasks);

	if (job_ptr->details && (job_ptr->details->ntasks_per_node != NO_VAL16))
		data_set_int(data_key_set(record, "ntasks_per_node"),
			     job_ptr->details->ntasks_per_node);

	if (job_ptr->details && (job_ptr->details->ntasks_per_tres != NO_VAL16))
		data_set_int(data_key_set(record, "ntasks_per_tres"),
			     job_ptr->details->ntasks_per_tres);

	if (job_ptr->details && (job_ptr->details->cpus_per_task != NO_VAL16))
		data_set_int(data_key_set(record, "cpus_per_task"),
			     job_ptr->details->cpus_per_task);

	if (job_ptr->details && job_ptr->details->orig_dependency)
		data_set_string(data_key_set(record, "orig_dependency"),
				job_ptr->details->orig_dependency);

	if (job_ptr->details && job_ptr->details->exc_nodes)
		data_set_string(data_key_set(record, "excluded_nodes"),
				job_ptr->details->exc_nodes);

	if (job_ptr->details && job_ptr->details->features)
		data_set_string(data_key_set(record, "features"),
				job_ptr->details->features);

	if (time_limit != INFINITE)
		data_set_int(data_key_set(record, "time_limit"),
			     (time_limit * 60));

	if (job_ptr->name)
		data_set_string(data_key_set(record, "job_name"),
				job_ptr->name);

	if (job_ptr->resv_name)
		data_set_string(data_key_set(record, "reservation_name"),
				job_ptr->resv_name);

	if (job_ptr->wckey)
		data_set_string(data_key_set(record, "wc_key"), job_ptr->wckey);

	if (job_ptr->tres_fmt_req_str)
		data_set_string(data_key_set(record, "tres_req"),
				job_ptr->tres_fmt_req_str);

	if (job_ptr->tres_fmt_alloc_str)
		data_set_string(data_key_set(record, "tres_alloc"),
				job_ptr->tres_fmt_alloc_str);

	if (job_ptr->account)
		data_set_string(data_key_set(record, "account"),
				job_ptr->account);

	if ((script = get_job_script(job_ptr)))
		data_set_string(data_key_set(record, "script"),
				get_buf_data(script));
	FREE_NULL_BUFFER(script);

	if (job_ptr->assoc_ptr) {
		assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
					   NO_LOCK, NO_LOCK, NO_LOCK };
		slurmdb_assoc_rec_t *assoc_ptr = job_ptr->assoc_ptr;
		char *parent_accounts = NULL;
		char **acc_aux = NULL;
		int nparents = 0;

		assoc_mgr_lock(&locks);

		/* Start at the first parent and go up. When studying
		 * this code it was slightly faster to do 2 loops on
		 * the association linked list and only 1 xmalloc but
		 * we opted for cleaner looking code and going with a
		 * realloc. */
		while (assoc_ptr) {
			if (assoc_ptr->acct) {
				acc_aux = xrealloc(acc_aux,
						   sizeof(char *) *
						   (nparents + 1));
				acc_aux[nparents++] = assoc_ptr->acct;
			}
			assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
		}

		for (i = nparents - 1; i >= 0; i--)
			xstrfmtcat(parent_accounts, "/%s", acc_aux[i]);
		xfree(acc_aux);

		data_set_string(data_key_set(record, "parent_accounts"),
				parent_accounts);

		xfree(parent_accounts);

		assoc_mgr_unlock(&locks);
	}

	xfree(usr_str);
	xfree(grp_str);

	return record;
}
