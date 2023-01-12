/*****************************************************************************\
 *  jobcomp_filetxt.c - text file slurm job completion logging plugin.
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#include "config.h"

#include <fcntl.h>
#include <grp.h>
#include <inttypes.h>
#include <pwd.h>
#include <unistd.h>

#include "src/common/slurm_xlator.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/interfaces/jobcomp.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_time.h"
#include "src/common/uid.h"
#include "filetxt_jobcomp_process.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobcomp" for Slurm job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobcomp/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]       	= "Job completion text file logging plugin";
const char plugin_type[]       	= "jobcomp/filetxt";
const uint32_t plugin_version	= SLURM_VERSION_NUMBER;

const char default_job_comp_loc[] = "/var/log/slurm_jobcomp.log";

#define JOB_FORMAT "JobId=%lu UserId=%s(%lu) GroupId=%s(%lu) Name=%s JobState=%s Partition=%s "\
		"TimeLimit=%s StartTime=%s EndTime=%s NodeList=%s NodeCnt=%u ProcCnt=%u "\
		"WorkDir=%s ReservationName=%s Tres=%s Account=%s QOS=%s "\
		"WcKey=%s Cluster=%s SubmitTime=%s EligibleTime=%s%s%s "\
		"DerivedExitCode=%s ExitCode=%s \n"

/* File descriptor used for logging */
static pthread_mutex_t  file_lock = PTHREAD_MUTEX_INITIALIZER;
static char *           log_name  = NULL;
static int              job_comp_fd = -1;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init ( void )
{
	return SLURM_SUCCESS;
}

int fini ( void )
{
	if (job_comp_fd >= 0)
		close(job_comp_fd);
	xfree(log_name);
	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard Slurm job completion
 * logging API.
 */

extern int jobcomp_p_set_location(void)
{
	int rc = SLURM_SUCCESS;

	if (!slurm_conf.job_comp_loc)
		slurm_conf.job_comp_loc = xstrdup(default_job_comp_loc);

	xfree(log_name);
	log_name = xstrdup(slurm_conf.job_comp_loc);

	slurm_mutex_lock( &file_lock );
	if (job_comp_fd >= 0)
		close(job_comp_fd);
	job_comp_fd = open(log_name, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (job_comp_fd == -1) {
		fatal("open %s: %m", log_name);
		rc = SLURM_ERROR;
	} else
		fchmod(job_comp_fd, 0644);
	slurm_mutex_unlock( &file_lock );
	return rc;
}

/* This is a variation of slurm_make_time_str() in src/common/parse_time.h
 * This version uses ISO8601 format by default. */
static void _make_time_str (time_t *time, char *string, int size)
{
	struct tm time_tm;

	if ( *time == (time_t) 0 ) {
		snprintf(string, size, "Unknown");
	} else {
		/* Format YYYY-MM-DDTHH:MM:SS, ISO8601 standard format */
		localtime_r(time, &time_tm);
		strftime(string, size, "%FT%T", &time_tm);
	}
}

extern int jobcomp_p_log_record(job_record_t *job_ptr)
{
	int rc = SLURM_SUCCESS, tmp_int, tmp_int2;
	char job_rec[1024];
	char start_str[32], end_str[32], lim_str[32];
	char *usr_str = NULL, *grp_str = NULL;
	char *resv_name, *tres, *account, *qos, *wckey, *cluster;
	char *exit_code_str = NULL, *derived_ec_str = NULL;
	char submit_time[32], eligible_time[32], array_id[64], het_id[64];
	char *state_string, *work_dir;
	size_t offset = 0, tot_size, wrote;
	uint32_t job_state;
	uint32_t time_limit;

	if ((log_name == NULL) || (job_comp_fd < 0)) {
		error("JobCompLoc log file %s not open", log_name);
		return SLURM_ERROR;
	}

	slurm_mutex_lock( &file_lock );
	usr_str = uid_to_string_or_null(job_ptr->user_id);
	grp_str = gid_to_string_or_null(job_ptr->group_id);

	if ((job_ptr->time_limit == NO_VAL) && job_ptr->part_ptr)
		time_limit = job_ptr->part_ptr->max_time;
	else
		time_limit = job_ptr->time_limit;
	if (time_limit == INFINITE)
		strcpy(lim_str, "UNLIMITED");
	else {
		snprintf(lim_str, sizeof(lim_str), "%lu",
			 (unsigned long) time_limit);
	}

	if (job_ptr->job_state & JOB_RESIZING) {
		time_t now = time(NULL);
		state_string = job_state_string(job_ptr->job_state);
		if (job_ptr->resize_time) {
			_make_time_str(&job_ptr->resize_time, start_str,
				       sizeof(start_str));
		} else {
			_make_time_str(&job_ptr->start_time, start_str,
				       sizeof(start_str));
		}
		_make_time_str(&now, end_str, sizeof(end_str));
	} else {
		/* Job state will typically have JOB_COMPLETING or JOB_RESIZING
		 * flag set when called. We remove the flags to get the eventual
		 * completion state: JOB_FAILED, JOB_TIMEOUT, etc. */
		job_state = job_ptr->job_state & JOB_STATE_BASE;
		state_string = job_state_string(job_state);
		if (job_ptr->resize_time) {
			_make_time_str(&job_ptr->resize_time, start_str,
				       sizeof(start_str));
		} else if (job_ptr->start_time > job_ptr->end_time) {
			/* Job cancelled while pending and
			 * expected start time is in the future. */
			snprintf(start_str, sizeof(start_str), "Unknown");
		} else {
			_make_time_str(&job_ptr->start_time, start_str,
				       sizeof(start_str));
		}
		_make_time_str(&job_ptr->end_time, end_str, sizeof(end_str));
	}

	if (job_ptr->details && job_ptr->details->work_dir)
		work_dir = job_ptr->details->work_dir;
	else
		work_dir = "unknown";

	if (job_ptr->resv_name && job_ptr->resv_name[0])
		resv_name = job_ptr->resv_name;
	else
		resv_name = "";

	if (job_ptr->tres_fmt_req_str && job_ptr->tres_fmt_req_str[0])
		tres = job_ptr->tres_fmt_req_str;
	else
		tres = "";

	if (job_ptr->account && job_ptr->account[0])
		account = job_ptr->account;
	else
		account = "";

	if (job_ptr->qos_ptr != NULL) {
		qos = job_ptr->qos_ptr->name;
	} else
		qos = "";

	if (job_ptr->wckey && job_ptr->wckey[0])
		wckey = job_ptr->wckey;
	else
		wckey = "";

	if (job_ptr->assoc_ptr != NULL)
		cluster = job_ptr->assoc_ptr->cluster;
	else
		cluster = "unknown";

	if (job_ptr->details && job_ptr->details->submit_time) {
		_make_time_str(&job_ptr->details->submit_time,
			       submit_time, sizeof(submit_time));
	} else {
		snprintf(submit_time, sizeof(submit_time), "unknown");
	}

	if (job_ptr->details && job_ptr->details->begin_time) {
		_make_time_str(&job_ptr->details->begin_time,
			       eligible_time, sizeof(eligible_time));
	} else {
		snprintf(eligible_time, sizeof(eligible_time), "unknown");
	}

	if (job_ptr->array_task_id != NO_VAL) {
		snprintf(array_id, sizeof(array_id),
			 " ArrayJobId=%u ArrayTaskId=%u",
			 job_ptr->array_job_id, job_ptr->array_task_id);
	} else {
		array_id[0] = '\0';
	}

	if (job_ptr->het_job_id) {
		snprintf(het_id, sizeof(het_id),
			 " HetJobId=%u HetJobOffset=%u",
			 job_ptr->het_job_id, job_ptr->het_job_offset);
	} else {
		het_id[0] = '\0';
	}

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

	snprintf(job_rec, sizeof(job_rec), JOB_FORMAT,
		 (unsigned long) job_ptr->job_id, usr_str,
		 (unsigned long) job_ptr->user_id, grp_str,
		 (unsigned long) job_ptr->group_id, job_ptr->name,
		 state_string, job_ptr->partition, lim_str, start_str,
		 end_str, job_ptr->nodes, job_ptr->node_cnt,
		 job_ptr->total_cpus, work_dir, resv_name, tres, account, qos,
		 wckey, cluster, submit_time, eligible_time, array_id, het_id,
		 derived_ec_str, exit_code_str);
	tot_size = strlen(job_rec);

	while (offset < tot_size) {
		wrote = write(job_comp_fd, job_rec + offset,
			tot_size - offset);
		if (wrote == -1) {
			if (errno == EAGAIN)
				continue;
			else {
				rc = SLURM_ERROR;
				break;
			}
		}
		offset += wrote;
	}
	xfree(usr_str);
	xfree(grp_str);
	xfree(derived_ec_str);
	xfree(exit_code_str);
	slurm_mutex_unlock( &file_lock );
	return rc;
}

/*
 * get info from the database
 * in/out job_list List of job_rec_t *
 * note List needs to be freed when called
 */
extern List jobcomp_p_get_jobs(slurmdb_job_cond_t *job_cond)
{
	return filetxt_jobcomp_process_get_jobs(job_cond);
}
