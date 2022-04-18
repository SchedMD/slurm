/*****************************************************************************\
 *  update_job.c - update job functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include "scontrol.h"
#include "src/common/env.h"
#include "src/common/gres.h"
#include "src/common/proc_args.h"
#include "src/common/uid.h"

typedef struct job_ids {
	uint32_t job_id;
	uint32_t array_job_id;
	uint32_t array_task_id;
	char *   array_task_str;
} job_ids_t;

static uint32_t	_get_job_time(const char *job_id_str);
static bool	_is_job_id(char *job_str);
static bool	_is_single_job(char *job_id_str);
static char *	_job_name2id(char *job_name, uint32_t job_uid);
static char *	_next_job_id(void);
static void	_update_job_size(uint32_t job_id);

/* Local variables for managing job IDs */
static char *local_job_str = NULL;

/* Confirm that contents of job_str is valid comma delimited list of job IDs */
static bool _is_job_id(char *job_str)
{
	bool have_plus = false, have_under = false;
	int bracket_cnt = 0;
	int i;

	if (!job_str)
		return false;

	local_job_str = xstrdup(job_str);
	for (i = 0; local_job_str[i]; i++) {
		if (local_job_str[i] == '+') {
			if (have_plus)
				goto fail;	/* multiple '+' in name */
			have_plus = true;
		} else if (local_job_str[i] == '_') {
			if (have_under)
				goto fail;	/* multiple '_' in name */
			have_under = true;
		} else if (local_job_str[i] == '[') {
			bracket_cnt++;
		} else if (local_job_str[i] == ']') {
			bracket_cnt--;
		} else if (local_job_str[i] == '-') {
			if ((bracket_cnt == 0) && !have_under)
				goto fail;	/* stray '-' */
		} else if ((local_job_str[i] == ',') ||
			   (local_job_str[i] == ' ')) {
			if (bracket_cnt == 0) {
				local_job_str[i] = '^';	/* New separator */
				have_plus  = false;
				have_under = false;
			}
		} else if ((local_job_str[i] < '0') || (local_job_str[i] > '9'))
			goto fail;	/* Unexpected character */
	}

	if (bracket_cnt != 0)
		goto fail;	/* Unbalanced brackets */
	return true;

fail:	xfree(local_job_str);
	debug("Character %d in %s is invalid job ID", i, job_str);
	return false;
}

/* Get the next job ID from local variables set up by _is_job_id() */
static char *_next_job_id(void)
{
	static hostlist_t hl = NULL;
	static char *save_ptr = NULL;
	static char *next_job_id = NULL;
	static char *task_id_spec = NULL;
	char *job_id_str = NULL, *bracket_ptr, *under_ptr;
	char *tmp_str, *end_job_str;
	int i;

	/* Clean up from previous calls */
	xfree(next_job_id);

	if (hl) {
		/* Process job ID regular expression using previously
		 * established hostlist data structure */
		tmp_str = hostlist_shift(hl);
		if (tmp_str) {
			next_job_id = xstrdup(tmp_str);
			free(tmp_str);
			if (task_id_spec) {
				xstrcat(next_job_id, "_");
				xstrcat(next_job_id, task_id_spec);
			}
			return next_job_id;
		}
		hostlist_destroy(hl);
		hl = NULL;
	}

	/* Get next token */
	xfree(task_id_spec);
	if (local_job_str && !save_ptr)	/* Get first token */
		job_id_str = strtok_r(local_job_str, "^", &save_ptr);
	else if (save_ptr)		/* Get next token */
		job_id_str = strtok_r(NULL, "^", &save_ptr);

	if (!job_id_str)	/* No more tokens */
		goto fini;

	under_ptr = strchr(job_id_str, '_');
	if (under_ptr) {
		if (under_ptr[1] == '[') {
			/* Strip brackets from job array task ID spec */
			task_id_spec = xstrdup(under_ptr + 2);
			for (i = 0; task_id_spec[i]; i++) {
				if (task_id_spec[i] == ']') {
					task_id_spec[i] = '\0';
					break;
				}
			}
		} else {
			task_id_spec = xstrdup(under_ptr + 1);
		}
	}

	bracket_ptr = strchr(job_id_str, '[');
	if (bracket_ptr && (!under_ptr || (bracket_ptr < under_ptr))) {
		/* Job ID specification uses regular expression */
		tmp_str = xstrdup(job_id_str);
		if ((end_job_str = strchr(tmp_str, '_')))
			end_job_str[0] = '\0';
		hl = hostlist_create(tmp_str);
		if (!hl) {
			error("Invalid job id: %s", job_id_str);
			xfree(tmp_str);
			goto fini;
		}
		xfree(tmp_str);
		tmp_str = hostlist_shift(hl);
		if (!tmp_str) {
			error("Invalid job id: %s", job_id_str);
			hostlist_destroy(hl);
			goto fini;
		}
		next_job_id = xstrdup(tmp_str);
		free(tmp_str);
	} else if (under_ptr) {
		under_ptr[0] = '\0';
		next_job_id = xstrdup(job_id_str);
		under_ptr[0] = '_';
	} else {
		next_job_id = xstrdup(job_id_str);
	}

	if (task_id_spec) {
		xstrcat(next_job_id, "_");
		xstrcat(next_job_id, task_id_spec);
	}

	return next_job_id;

fini:	xfree(local_job_str);
	save_ptr = NULL;
	return NULL;
}

/*
 * scontrol_hold - perform some job hold/release operation
 * IN op	- hold/release operation
 * IN job_str	- a job ID or job name
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *		error message and returns 0
 */
extern int
scontrol_hold(char *op, char *job_str)
{
	static uint32_t last_job_id = NO_VAL;
	static job_info_msg_t *jobs = NULL;
	job_array_resp_msg_t *resp = NULL;
	int i, rc = SLURM_SUCCESS, rc2;
	int j;
	job_desc_msg_t job_msg;
	uint32_t job_id = 0;
	char *job_name = NULL;
	char *job_id_str = NULL;
	slurm_job_info_t *job_ptr;

	if (job_str && !xstrncasecmp(job_str, "JobID=", 6))
		job_str += 6;
	if (job_str && !xstrncasecmp(job_str, "Job=", 4))
		job_str += 4;

	slurm_init_job_desc_msg (&job_msg);
	if ((xstrncasecmp(op, "holdu", 5) == 0) ||
	    (xstrncasecmp(op, "uhold", 5) == 0)) {
		job_msg.priority = 0;
		job_msg.alloc_sid = ALLOC_SID_USER_HOLD;
	} else if (xstrncasecmp(op, "hold", 4) == 0) {
		job_msg.priority = 0;
		job_msg.alloc_sid = 0;
	} else
		job_msg.priority = INFINITE;

	if (_is_job_id(job_str)) {
		while ((job_msg.job_id_str = _next_job_id())) {
			rc2 = slurm_update_job2(&job_msg, &resp);
			if (rc2 != SLURM_SUCCESS) {
				rc2 = slurm_get_errno();
				rc = MAX(rc, rc2);
				exit_code = 1;
				if (quiet_flag != 1) {
					fprintf(stderr, "%s for job %s\n",
						slurm_strerror(rc2),
						job_msg.job_id_str);
				}
			} else if (resp) {
				for (i = 0; i < resp->job_array_count; i++) {
					if ((resp->error_code[i]==SLURM_SUCCESS)
					    && (resp->job_array_count == 1))
						continue;
					exit_code = 1;
					if (quiet_flag == 1)
						continue;
					fprintf(stderr, "%s: %s\n",
						resp->job_array_id[i],
						slurm_strerror(resp->
							       error_code[i]));
				}
				slurm_free_job_array_resp(resp);
				resp = NULL;
			}
		}
		return rc;
	} else if (job_str) {
		if (!xstrncasecmp(job_str, "Name=", 5)) {
			job_str += 5;
			job_id = 0;
			job_name = job_str;
			last_job_id = NO_VAL;
		} else if (!xstrncasecmp(job_str, "JobName=", 8)) {
			job_str += 8;
			job_id = 0;
			job_name = job_str;
			last_job_id = NO_VAL;
		} else {
			exit_code = 1;
			rc = ESLURM_INVALID_JOB_ID;
			slurm_seterrno(rc);
			if (quiet_flag != 1) {
				fprintf(stderr, "%s for job %s\n",
					slurm_strerror(rc), job_str);
			}
			return rc;
		}
	} else {
		last_job_id = NO_VAL;	/* Refresh cache on next call */
		return 0;
	}

	if (last_job_id != job_id) {
		if (scontrol_load_job(&jobs, job_id)) {
			if (quiet_flag == -1)
				slurm_perror ("slurm_load_job error");
			return 1;
		}
		last_job_id = job_id;
	}

	/* set current user, needed e.g., for AllowGroups checks */
	for (i = 0, job_ptr = jobs->job_array; i < jobs->record_count;
	     i++, job_ptr++) {
		if (xstrcmp(job_name, job_ptr->name))
			continue;

		if (!IS_JOB_PENDING(job_ptr)) {
			if (job_ptr->array_task_id != NO_VAL)
				continue;
			slurm_seterrno(ESLURM_JOB_NOT_PENDING);
			rc = MAX(rc, ESLURM_JOB_NOT_PENDING);
		}

		if (job_ptr->array_task_str) {
			xstrfmtcat(job_id_str, "%u_%s",
				   job_ptr->array_job_id,
				   job_ptr->array_task_str);
		} else if (job_ptr->array_task_id != NO_VAL) {
			xstrfmtcat(job_id_str, "%u_%u",
				   job_ptr->array_job_id,
				   job_ptr->array_task_id);
		} else {
			xstrfmtcat(job_id_str, "%u", job_ptr->job_id);
		}
		job_msg.job_id_str = job_id_str;
		rc2 = slurm_update_job2(&job_msg, &resp);
		if (rc2 != SLURM_SUCCESS) {
			rc2 = slurm_get_errno();
			rc = MAX(rc, rc2);
			exit_code = 1;
			if (quiet_flag != 1) {
				fprintf(stderr, "%s for job %s\n",
					slurm_strerror(rc2),
					job_msg.job_id_str);
			}
		} else if (resp) {
			for (j = 0; j < resp->job_array_count; j++) {
				if ((resp->error_code[j] == SLURM_SUCCESS) &&
				    (resp->job_array_count == 1))
					continue;
				exit_code = 1;
				if (quiet_flag == 1)
					continue;
				fprintf(stderr, "%s: %s\n",
					resp->job_array_id[j],
					slurm_strerror(resp->error_code[j]));
			}
			slurm_free_job_array_resp(resp);
			resp = NULL;
		}
		xfree(job_id_str);
	}

	return rc;
}


/*
 * scontrol_suspend - perform some suspend/resume operation
 * IN op - suspend/resume operation
 * IN job_str - a job id
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *		error message and returns 0
 */
extern void
scontrol_suspend(char *op, char *job_str)
{
	int rc, i;
	job_array_resp_msg_t *resp = NULL;
	char *job_id_str;

	if (xstrncasecmp(job_str, "jobid=", 6) == 0)
		job_str += 6;
	if (xstrncasecmp(job_str, "job=", 4) == 0)
		job_str += 4;

	if (_is_job_id(job_str)) {
		job_id_str = _next_job_id();
		while (job_id_str) {
			if (!xstrncasecmp(op, "suspend", MAX(strlen(op), 2)))
				rc = slurm_suspend2(job_id_str, &resp);
			else
				rc = slurm_resume2(job_id_str, &resp);
			if (rc != SLURM_SUCCESS) {
				exit_code = 1;
				if (quiet_flag != 1) {
					fprintf(stderr, "%s for job %s\n",
						slurm_strerror(slurm_get_errno()),
						job_id_str);
				}
			} else if (resp) {
				for (i = 0; i < resp->job_array_count; i++) {
					if ((resp->error_code[i] == SLURM_SUCCESS)
					    && (resp->job_array_count == 1))
						continue;
					exit_code = 1;
					if (quiet_flag == 1)
						continue;
					fprintf(stderr, "%s: %s\n",
						resp->job_array_id[i],
						slurm_strerror(resp->
							       error_code[i]));
				}
				slurm_free_job_array_resp(resp);
				resp = NULL;
			}
			job_id_str = _next_job_id();
		}
	} else {
		exit_code = 1;
		rc = ESLURM_INVALID_JOB_ID;
		slurm_seterrno(rc);
		if (quiet_flag != 1) {
			fprintf(stderr, "%s for job %s\n",
				slurm_strerror(rc), job_str);
		}
	}
}

/*
 * scontrol_requeue - requeue a pending or running batch job
 * IN job_id_str - a job id
 */
extern void
scontrol_requeue(uint32_t flags, char *job_str)
{
	char *job_id_str;
	int rc, i;
	job_array_resp_msg_t *resp = NULL;

	if (!job_str[0]) {
		exit_code = 1;
		return;
	}

	if (xstrncasecmp(job_str, "jobid=", 6) == 0)
		job_str += 6;
	if (xstrncasecmp(job_str, "job=", 4) == 0)
		job_str += 4;

	if (_is_job_id(job_str)) {
		job_id_str = _next_job_id();
		while (job_id_str) {
			rc = slurm_requeue2(job_id_str, flags, &resp);
			if (rc != SLURM_SUCCESS) {
				exit_code = 1;
				if (quiet_flag != 1) {
					fprintf(stderr, "%s for job %s\n",
						slurm_strerror(slurm_get_errno()),
						job_id_str);
				}
			} else if (resp) {
				for (i = 0; i < resp->job_array_count; i++) {
					if ((resp->error_code[i] == SLURM_SUCCESS)
					    && (resp->job_array_count == 1))
						continue;
					exit_code = 1;
					if (quiet_flag == 1)
						continue;
					fprintf(stderr, "%s: %s\n",
						resp->job_array_id[i],
						slurm_strerror(resp->
							       error_code[i]));
				}
				slurm_free_job_array_resp(resp);
				resp = NULL;
			}
			job_id_str = _next_job_id();
		}
	} else {
		exit_code = 1;
		rc = ESLURM_INVALID_JOB_ID;
		slurm_seterrno(rc);
		if (quiet_flag != 1) {
			fprintf(stderr, "%s for job %s\n",
				slurm_strerror(rc), job_str);
		}
	}
}

extern void
scontrol_requeue_hold(uint32_t flags, char *job_str)
{
	int rc, i;
	char *job_id_str;
	job_array_resp_msg_t *resp = NULL;

	flags |= JOB_REQUEUE_HOLD;

	if (_is_job_id(job_str)) {
		job_id_str = _next_job_id();
		while (job_id_str) {
			rc = slurm_requeue2(job_id_str, flags, &resp);
			if (rc != SLURM_SUCCESS) {
				exit_code = 1;
				if (quiet_flag != 1) {
					fprintf(stderr, "%s for job %s\n",
						slurm_strerror(slurm_get_errno()),
						job_id_str);
				}
			} else if (resp) {
				for (i = 0; i < resp->job_array_count; i++) {
					if ((resp->error_code[i] == SLURM_SUCCESS)
					    && (resp->job_array_count == 1))
						continue;
					exit_code = 1;
					if (quiet_flag == 1)
						continue;
					fprintf(stderr, "%s: %s\n",
						resp->job_array_id[i],
						slurm_strerror(resp->
							       error_code[i]));
				}
				slurm_free_job_array_resp(resp);
				resp = NULL;
			}
			job_id_str = _next_job_id();
		}
	} else {
		exit_code = 1;
		rc = ESLURM_INVALID_JOB_ID;
		slurm_seterrno(rc);
		if (quiet_flag != 1) {
			fprintf(stderr, "%s for job %s\n",
				slurm_strerror(rc), job_str);
		}
	}
}

/*
 * scontrol_top_job - Move the specified job ID to the top of the queue for
 *	a given user ID, partition, account, and QOS.
 * IN job_str - a job id
 */
extern void
scontrol_top_job(char *job_id_str)
{
	int rc;

	if (xstrncasecmp(job_id_str, "jobid=", 6) == 0)
		job_id_str += 6;
	if (xstrncasecmp(job_id_str, "job=", 4) == 0)
		job_id_str += 4;

	rc = slurm_top_job(job_id_str);
	if (rc != SLURM_SUCCESS) {
		exit_code = 1;
		if (quiet_flag != 1) {
			fprintf(stderr, "%s for job %s\n",
				slurm_strerror(slurm_get_errno()), job_id_str);
		}
	}
}

/*
 * scontrol_update_job - update the slurm job configuration per the supplied
 *	arguments
 * IN argc - count of arguments
 * IN argv - list of arguments
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *			error message and returns 0
 */
extern int scontrol_update_job(int argc, char **argv)
{
	bool update_size = false;
	int i, update_cnt = 0, rc = SLURM_SUCCESS, rc2;
	char *tag, *val;
	int taglen, vallen;
	job_desc_msg_t job_msg;
	job_array_resp_msg_t *resp = NULL;
	uint32_t job_uid = NO_VAL;

	slurm_init_job_desc_msg (&job_msg);
	for (i = 0; i < argc; i++) {
		char *add_info = NULL;
		tag = argv[i];
		val = strchr(argv[i], '=');
		if (val) {
			taglen = val - argv[i];
			if ((taglen > 0) && ((val[-1] == '+') ||
					     (val[-1] == '-'))) {
				add_info = val - 1;
				taglen--;
			}
			val++;
			vallen = strlen(val);
		}
		/* Handle any tags that might not have an equal sign here */
		else if (xstrncasecmp(tag, "Nice", MAX(strlen(tag), 2)) == 0) {
			job_msg.nice = NICE_OFFSET + 100;
			update_cnt++;
			continue;
		} else if (!xstrncasecmp(tag, "ResetAccrueTime",
					 MAX(strlen(tag), 3))) {
			job_msg.bitflags |= RESET_ACCRUE_TIME;
			update_cnt++;
			continue;
		} else if (!val && argv[i + 1]) {
			tag = argv[i];
			taglen = strlen(tag);
			val = argv[++i];
			vallen = strlen(val);
		} else {
			exit_code = 1;
			fprintf (stderr, "Invalid input: %s\n", argv[i]);
			fprintf (stderr, "Request aborted\n");
			return -1;
		}

		if (xstrncasecmp(tag, "JobId", MAX(taglen, 3)) == 0) {
			job_msg.job_id_str = val;
		}
		else if (xstrncasecmp(tag, "AdminComment",
				      MAX(taglen, 6)) == 0) {
			if (add_info) {
				if (add_info[0] == '-') {
					error("Invalid syntax, AdminComment can not be subtracted from.");
					exit_code = 1;
					return 0;
				}
				job_msg.admin_comment = add_info;
				/*
				 * Mark as unset so we know we handled this
				 * correctly as there is a check later to make
				 * sure we know we got a +-.
				 */
				add_info = NULL;
			} else
				job_msg.admin_comment = val;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "SiteFactor",
				      MAX(taglen, 5)) == 0) {
			long long tmp_prio;
			tmp_prio = strtoll(val, (char **)NULL, 10);
			if (llabs(tmp_prio) > (NICE_OFFSET - 3)) {
				error("SiteFactor value out of range (+/- %u). Value ignored",
				      NICE_OFFSET - 3);
				exit_code = 1;
				return 0;
			}
			job_msg.site_factor = NICE_OFFSET + tmp_prio;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "ArrayTaskThrottle",
				      MAX(taglen, 10)) == 0) {
			int throttle;
			throttle = strtoll(val, (char **) NULL, 10);
			if (throttle < 0) {
				error("Invalid ArrayTaskThrottle value");
				exit_code = 1;
				return 0;
			}
			job_msg.array_inx = val;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "Comment", MAX(taglen, 3)) == 0) {
			job_msg.comment = val;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "Clusters", MAX(taglen, 8)) == 0) {
			job_msg.clusters = val;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "ClusterFeatures",
				      MAX(taglen, 8)) == 0) {
			job_msg.cluster_features = val;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "DelayBoot", MAX(taglen, 5)) == 0) {
			int time_sec = time_str2secs(val);
			if (time_sec == NO_VAL) {
				error("Invalid DelayBoot value");
				exit_code = 1;
				return 0;
			}
			job_msg.delay_boot = time_sec;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "TimeLimit", MAX(taglen, 5)) == 0) {
			uint32_t job_current_time, time_limit;

			if (val && ((val[0] == '+') || (val[0] == '-'))) {
				if (add_info) {
					error("Invalid syntax, variations of +=- are not accepted.");
					exit_code = 1;
					return 0;
				}
				add_info = val;
				val++;
			}

			time_limit = time_str2mins(val);
			if (time_limit == NO_VAL) {
				error("Invalid TimeLimit value");
				exit_code = 1;
				return 0;
			}
			if (add_info) {
				if (!job_msg.job_id_str) {
					error("JobId must precede TimeLimit "
					      "increment or decrement");
					exit_code = 1;
					return 0;
				}

				job_current_time = _get_job_time(job_msg.
								 job_id_str);
				if (job_current_time == NO_VAL) {
					exit_code = 1;
					return 0;
				}
				if (add_info[0] == '+') {
					time_limit += job_current_time;
				} else if (time_limit > job_current_time) {
					error("TimeLimit decrement larger than"
					      " current time limit (%u > %u)",
					      time_limit, job_current_time);
					exit_code = 1;
					return 0;
				} else {
					time_limit = job_current_time -
						     time_limit;
				}
				/*
				 * Mark as unset so we know we handled this
				 * correctly as there is a check later to make
				 * sure we know we got a +-.
				 */
				add_info = NULL;
			}
			job_msg.time_limit = time_limit;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "TimeMin", MAX(taglen, 5)) == 0) {
			int time_min = time_str2mins(val);
			if ((time_min < 0) && (time_min != INFINITE)) {
				error("Invalid TimeMin value");
				exit_code = 1;
				return 0;
			}
			job_msg.time_min = time_min;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "Priority", MAX(taglen, 2)) == 0) {
			if (parse_uint32(val, &job_msg.priority)) {
				error ("Invalid Priority value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "Nice", MAX(taglen, 2)) == 0) {
			long long tmp_nice;
			tmp_nice = strtoll(val, (char **)NULL, 10);
			if (llabs(tmp_nice) > (NICE_OFFSET - 3)) {
				error("Nice value out of range (+/- %u). Value "
				      "ignored", NICE_OFFSET - 3);
				exit_code = 1;
				return 0;
			}
			job_msg.nice = NICE_OFFSET + tmp_nice;
			update_cnt++;
		}
		else if (!xstrncasecmp(tag, "CPUsPerTask", MAX(taglen, 9))) {
			if (parse_uint16(val, &job_msg.cpus_per_task)) {
				error("Invalid CPUsPerTask value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (!xstrncasecmp(tag, "CpusPerTres", MAX(taglen, 9))) {
			job_msg.cpus_per_tres = val;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "NumCPUs", MAX(taglen, 6)) == 0) {
			int min_cpus, max_cpus=0;
			if (!get_resource_arg_range(val, "NumCPUs", &min_cpus,
						   &max_cpus, false) ||
			    (min_cpus <= 0) ||
			    (max_cpus && (max_cpus < min_cpus))) {
				error ("Invalid NumCPUs value: %s", val);
				exit_code = 1;
				return 0;
			}
			job_msg.min_cpus = min_cpus;
			if (max_cpus)
				job_msg.max_cpus = max_cpus;
			update_cnt++;
		}
		/* ReqProcs was removed in Slurm version 2.1 */
		else if ((xstrncasecmp(tag, "NumTasks", MAX(taglen, 8)) == 0) ||
			 (xstrncasecmp(tag, "ReqProcs", MAX(taglen, 8)) == 0)) {
			if (parse_uint32(val, &job_msg.num_tasks)) {
				error ("Invalid NumTasks value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "Requeue", MAX(taglen, 4)) == 0) {
			if (parse_uint16(val, &job_msg.requeue)) {
				error ("Invalid Requeue value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		/* ReqNodes was replaced by NumNodes in Slurm version 2.1 */
		else if ((xstrncasecmp(tag, "ReqNodes", MAX(taglen, 8)) == 0) ||
		         (xstrncasecmp(tag, "NumNodes", MAX(taglen, 8)) == 0)) {
			int min_nodes, max_nodes, rc;
			if (xstrcmp(val, "0") == 0) {
				job_msg.min_nodes = 0;
			} else if (xstrcasecmp(val, "ALL") == 0) {
				job_msg.min_nodes = INFINITE;
			} else {
				min_nodes = (int) job_msg.min_nodes;
				max_nodes = (int) job_msg.max_nodes;
				rc = get_resource_arg_range(
						val, "requested node count",
						&min_nodes, &max_nodes, false);
				if (!rc)
					return rc;
				job_msg.min_nodes = (uint32_t) min_nodes;
				job_msg.max_nodes = (uint32_t) max_nodes;
			}
			update_size = true;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "ReqSockets", MAX(taglen, 4)) == 0) {
			if (parse_uint16(val, &job_msg.sockets_per_node)) {
				error ("Invalid ReqSockets value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "ReqCores", MAX(taglen, 4)) == 0) {
			if (parse_uint16(val, &job_msg.cores_per_socket)) {
				error ("Invalid ReqCores value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
                else if (xstrncasecmp(tag, "TasksPerNode", MAX(taglen, 2))==0) {
			if (parse_uint16(val, &job_msg.ntasks_per_node)) {
				error ("Invalid TasksPerNode value: %s", val);
				exit_code = 1;
				return 0;
			}
                        update_cnt++;
                }
		else if (xstrncasecmp(tag, "ReqThreads", MAX(taglen, 4)) == 0) {
			if (parse_uint16(val, &job_msg.threads_per_core)) {
				error ("Invalid ReqThreads value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "MinCPUsNode", MAX(taglen, 4)) == 0) {
			if (parse_uint16(val, &job_msg.pn_min_cpus)) {
				error ("Invalid MinCPUsNode value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "MinMemoryNode",
				     MAX(taglen, 10)) == 0) {
			if (parse_uint64(val, &job_msg.pn_min_memory)) {
				error ("Invalid MinMemoryNode value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "MinMemoryCPU",
				     MAX(taglen, 10)) == 0) {
			if (parse_uint64(val, &job_msg.pn_min_memory)) {
				error ("Invalid MinMemoryCPU value: %s", val);
				exit_code = 1;
				return 0;
			}
			job_msg.pn_min_memory |= MEM_PER_CPU;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "MinTmpDiskNode",
				     MAX(taglen, 5)) == 0) {
			if (parse_uint32(val, &job_msg.pn_min_tmp_disk)) {
				error ("Invalid MinTmpDiskNode value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "Partition", MAX(taglen, 2)) == 0) {
			job_msg.partition = val;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "QOS", MAX(taglen, 2)) == 0) {
			job_msg.qos = val;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "ReservationName",
				     MAX(taglen, 3)) == 0) {
			job_msg.reservation = val;
			update_cnt++;
		}
		else if (!xstrncasecmp(tag, "Name", MAX(taglen, 2)) ||
			 !xstrncasecmp(tag, "JobName", MAX(taglen, 4))) {
			job_msg.name = val;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "WCKey", MAX(taglen, 1)) == 0) {
			job_msg.wckey = val;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "StdOut", MAX(taglen, 6)) == 0) {
			job_msg.std_out = val;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "Switches", MAX(taglen, 5)) == 0) {
			char *sep_char;
			job_msg.req_switch =
				(uint32_t) strtol(val, &sep_char, 10);
			update_cnt++;
			if (sep_char && sep_char[0] == '@') {
				job_msg.wait4switch = time_str2mins(sep_char+1)
						      * 60;
			}
		}
		else if (xstrncasecmp(tag, "wait-for-switch", MAX(taglen, 5))
			 == 0) {
			if (parse_uint32(val, &job_msg.wait4switch)) {
				error ("Invalid wait-for-switch value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (!xstrncasecmp(tag, "OverSubscribe", MAX(taglen, 2)) ||
			 !xstrncasecmp(tag, "Shared", MAX(taglen, 2))) {
			if (xstrncasecmp(val, "YES", MAX(vallen, 1)) == 0)
				job_msg.shared = 1;
			else if (xstrncasecmp(val, "NO", MAX(vallen, 1)) == 0)
				job_msg.shared = 0;
			else if (parse_uint16(val, &job_msg.shared)) {
				error("Invalid OverSubscribe value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "Contiguous", MAX(taglen, 3)) == 0) {
			if (xstrncasecmp(val, "YES", MAX(vallen, 1)) == 0)
				job_msg.contiguous = 1;
			else if (xstrncasecmp(val, "NO", MAX(vallen, 1)) == 0)
				job_msg.contiguous = 0;
			else if (parse_uint16(val, &job_msg.contiguous)) {
				error ("Invalid Contiguous value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "CoreSpec", MAX(taglen, 4)) == 0) {
			if (!xstrcmp(val, "-1") || !xstrcmp(val, "*"))
				job_msg.core_spec = INFINITE16;
			else if (parse_uint16(val, &job_msg.core_spec)) {
				error ("Invalid CoreSpec value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (!xstrncasecmp(tag, "MemPerTres", MAX(taglen, 5))) {
			job_msg.mem_per_tres = val;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "ThreadSpec", MAX(taglen, 4)) == 0) {
			if (!xstrcmp(val, "-1") || !xstrcmp(val, "*"))
				job_msg.core_spec = INFINITE16;
			else if (parse_uint16(val, &job_msg.core_spec)) {
				error ("Invalid ThreadSpec value: %s", val);
				exit_code = 1;
				return 0;
			} else
				job_msg.core_spec |= CORE_SPEC_THREAD;
			update_cnt++;
		}
		else if (!xstrncasecmp(tag, "TresBind", MAX(taglen, 5))) {
			job_msg.tres_bind = val;
			update_cnt++;
		}
		else if (!xstrncasecmp(tag, "TresFreq", MAX(taglen, 5))) {
			job_msg.tres_freq = val;
			update_cnt++;
		}
		else if (!xstrncasecmp(tag, "TresPerJob", MAX(taglen, 8))) {
			job_msg.tres_per_job = val;
			update_cnt++;
		}
		else if (!xstrncasecmp(tag, "TresPerNode", MAX(taglen, 8))) {
			/* "gres" replaced by "tres_per_node" in v18.08 */
			if (job_msg.tres_per_node)
				xstrfmtcat(job_msg.tres_per_node, ",%s", val);
			else
				job_msg.tres_per_node = xstrdup(val);
			update_cnt++;
		}
		else if (!xstrncasecmp(tag, "TresPerSocket", MAX(taglen, 8))) {
			job_msg.tres_per_socket = val;
			update_cnt++;
		}
		else if (!xstrncasecmp(tag, "TresPerTask", MAX(taglen, 8))) {
			job_msg.tres_per_task = val;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "ExcNodeList", MAX(taglen, 3)) == 0){
			job_msg.exc_nodes = val;
			update_cnt++;
		}
		else if (!xstrncasecmp(tag, "NodeList",    MAX(taglen, 8)) ||
			 !xstrncasecmp(tag, "ReqNodeList", MAX(taglen, 8))) {
			job_msg.req_nodes = val;
			update_size = true;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "Features", MAX(taglen, 1)) == 0) {
			job_msg.features = val;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "Gres", MAX(taglen, 2)) == 0) {
			/* "gres" replaced by "tres_per_node" in v18.08 */
			if (!xstrcasecmp(val, "help") ||
			    !xstrcasecmp(val, "list")) {
				print_gres_help();
			} else {
				char *tmp = gres_prepend_tres_type(val);
				if (job_msg.tres_per_node) {
					xstrfmtcat(job_msg.tres_per_node, ",%s",
						   tmp);
					xfree(tmp);
				} else {
					job_msg.tres_per_node = tmp;
				}
				update_cnt++;
			}
		}
		else if (xstrncasecmp(tag, "Account", MAX(taglen, 1)) == 0) {
			job_msg.account = val;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "BurstBuffer", MAX(taglen, 1)) == 0) {
			job_msg.burst_buffer = val;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "Dependency", MAX(taglen, 1)) == 0) {
			job_msg.dependency = val;
			update_cnt++;
		}
		else if (xstrncasecmp(tag, "Licenses", MAX(taglen, 1)) == 0) {
			job_msg.licenses = val;
			update_cnt++;
		}
		else if (!xstrncasecmp(tag, "EligibleTime", MAX(taglen, 2)) ||
			 !xstrncasecmp(tag, "StartTime",    MAX(taglen, 2))) {
			if ((job_msg.begin_time = parse_time(val, 0))) {
				if (job_msg.begin_time < time(NULL))
					job_msg.begin_time = time(NULL);
				update_cnt++;
			}
		}
		else if (!xstrncasecmp(tag, "EndTime", MAX(taglen, 2))) {
			job_msg.end_time = parse_time(val, 0);
			update_cnt++;
		}
		else if (!xstrncasecmp(tag, "Reboot", MAX(taglen, 3))) {
			if (xstrncasecmp(val, "YES", MAX(vallen, 1)) == 0)
				job_msg.reboot = 1;
			else if (xstrncasecmp(val, "NO", MAX(vallen, 1)) == 0)
				job_msg.reboot = 0;
			else if (parse_uint16(val, &job_msg.reboot)) {
				error ("Invalid reboot value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (!xstrncasecmp(tag, "UserID", MAX(taglen, 3))) {
			uid_t user_id = 0;
			if (uid_from_string(val, &user_id) < 0) {
				exit_code = 1;
				fprintf (stderr, "Invalid UserID: %s\n", val);
				fprintf (stderr, "Request aborted\n");
				return 0;
			}
			job_uid = (uint32_t) user_id;
		}
		else if (!xstrncasecmp(tag, "Deadline", MAX(taglen, 3))) {
			if ((job_msg.deadline = parse_time(val, 0))) {
				update_cnt++;
			}
		} else if (!xstrncasecmp(tag, "WorkDir", MAX(taglen, 2))) {
			job_msg.work_dir = val;
			update_cnt++;
		} else if (!xstrncasecmp(tag, "MailType", MAX(taglen, 5))) {
			job_msg.mail_type = parse_mail_type(val);
			if (job_msg.mail_type == INFINITE16) {
				fprintf(stderr, "Invalid MailType: %s\n", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		} else if (!xstrncasecmp(tag, "MailUser", MAX(taglen, 5))) {
			job_msg.mail_user = val;
			update_cnt++;
		}
		else {
			exit_code = 1;
			fprintf (stderr, "Update of this parameter is not "
				 "supported: %s\n", argv[i]);
			fprintf (stderr, "Request aborted\n");
			return 0;
		}

		if (add_info) {
			error("Option %s does not accept [+|-]= syntax", tag);
			exit_code = 1;
			return 0;
		}
	}

	if (update_cnt == 0) {
		exit_code = 1;
		fprintf (stderr, "No changes specified\n");
		return 0;
	}

	/* If specified, override uid with effective uid provided by
	 * -u <uid> or --uid=<uid> */
	if (euid != NO_VAL)
		job_msg.user_id = euid;

	if (!job_msg.job_id_str && job_msg.name) {
		/* Translate name to job ID string */
		job_msg.job_id_str = _job_name2id(job_msg.name, job_uid);
		if (!job_msg.job_id_str) {
			exit_code = 1;
			return 0;
		}
	}

	if (!job_msg.job_id_str) {
		error("No job ID specified");
		exit_code = 1;
		return 0;
	}

	if (update_size && !_is_single_job(job_msg.job_id_str)) {
		exit_code = 1;
		return 0;
	}

	if (_is_job_id(job_msg.job_id_str)) {
		job_msg.job_id_str = _next_job_id();
		while (job_msg.job_id_str) {
			rc2 = slurm_update_job2(&job_msg, &resp);
			if (update_size && (rc2 == SLURM_SUCCESS)) {
				/* See check above for one job ID */
				job_msg.job_id = slurm_atoul(job_msg.job_id_str);
				_update_job_size(job_msg.job_id);
			}
			if (rc2 != SLURM_SUCCESS) {
				rc2 = slurm_get_errno();
				rc = MAX(rc, rc2);
				exit_code = 1;
				if (quiet_flag != 1) {
					fprintf(stderr, "%s for job %s\n",
						slurm_strerror(slurm_get_errno()),
						job_msg.job_id_str);
				}
			} else if (resp) {
				for (i = 0; i < resp->job_array_count; i++) {
					if ((resp->error_code[i] == SLURM_SUCCESS)
					    && (resp->job_array_count == 1))
						continue;
					exit_code = 1;
					if (quiet_flag == 1)
						continue;
					fprintf(stderr, "%s: %s\n",
						resp->job_array_id[i],
						slurm_strerror(resp->
							       error_code[i]));
				}
				slurm_free_job_array_resp(resp);
				resp = NULL;
			}
			job_msg.job_id_str = _next_job_id();
		}
	} else if (job_msg.job_id_str) {
		exit_code = 1;
		rc = ESLURM_INVALID_JOB_ID;
		slurm_seterrno(rc);
		if (quiet_flag != 1) {
			fprintf(stderr, "%s for job %s\n",
				slurm_strerror(rc), job_msg.job_id_str);
		}
	}

	return rc;
}

/*
 * Send message to stdout of specified job
 * argv[0] == jobid
 * argv[1]++ the message
 */
extern int scontrol_job_notify(int argc, char **argv)
{
	int i;
	uint32_t job_id;
	char *message = NULL;

	job_id = atoi(argv[0]);
	if (job_id <= 0) {
		fprintf(stderr, "Invalid job_id %s", argv[0]);
		return 1;
	}

	for (i=1; i<argc; i++) {
		if (message)
			xstrfmtcat(message, " %s", argv[i]);
		else
			xstrcat(message, argv[i]);
	}

	i = slurm_notify_job(job_id, message);
	xfree(message);

	if (i)
		return slurm_get_errno ();
	else
		return 0;
}

static void _update_job_size(uint32_t job_id)
{
	resource_allocation_response_msg_t *alloc_info;
	char *fname_csh = NULL, *fname_sh = NULL;
	FILE *resize_csh = NULL, *resize_sh = NULL;

	if (!getenv("SLURM_JOBID"))
		return;		/* No job environment here to update */

	if (slurm_allocation_lookup(job_id, &alloc_info) !=
	    SLURM_SUCCESS) {
		if (slurm_get_errno() != ESLURM_ALREADY_DONE) {
			slurm_perror("slurm_allocation_lookup");
			return;
		}
		/* Job size reset to zero, not an error */
		alloc_info = xmalloc(sizeof(resource_allocation_response_msg_t));
		alloc_info->node_list = xstrdup("");
	}

	xstrfmtcat(fname_csh, "slurm_job_%u_resize.csh", job_id);
	xstrfmtcat(fname_sh,  "slurm_job_%u_resize.sh", job_id);
	(void) unlink(fname_csh);
	(void) unlink(fname_sh);
 	if (!(resize_csh = fopen(fname_csh, "w"))) {
		fprintf(stderr, "Could not create file %s: %s\n", fname_csh,
			strerror(errno));
		goto fini;
	}
 	if (!(resize_sh = fopen(fname_sh, "w"))) {
		fprintf(stderr, "Could not create file %s: %s\n", fname_sh,
			strerror(errno));
		goto fini;

	}
	/*
	 * Make files executable
	 */
	if (chmod(fname_csh, 0700) == -1)
		error("%s: chmod(%s): %m", __func__, fname_csh);
	if (chmod(fname_sh, 0700) == -1)
		error("%s: chmod(%s): %m", __func__, fname_sh);

	if (getenv("SLURM_NODELIST")) {
		fprintf(resize_sh, "export SLURM_NODELIST=\"%s\"\n",
			alloc_info->node_list);
		fprintf(resize_csh, "setenv SLURM_NODELIST \"%s\"\n",
			alloc_info->node_list);
	}
	if (getenv("SLURM_JOB_NODELIST")) {
		fprintf(resize_sh, "export SLURM_JOB_NODELIST=\"%s\"\n",
			alloc_info->node_list);
		fprintf(resize_csh, "setenv SLURM_JOB_NODELIST \"%s\"\n",
			alloc_info->node_list);
	}
	if (getenv("SLURM_NNODES")) {
		fprintf(resize_sh, "export SLURM_NNODES=%u\n",
			alloc_info->node_cnt);
		fprintf(resize_csh, "setenv SLURM_NNODES %u\n",
			alloc_info->node_cnt);
	}
	if (getenv("SLURM_JOB_NUM_NODES")) {
		fprintf(resize_sh, "export SLURM_JOB_NUM_NODES=%u\n",
			alloc_info->node_cnt);
		fprintf(resize_csh, "setenv SLURM_JOB_NUM_NODES %u\n",
			alloc_info->node_cnt);
	}
	if (getenv("SLURM_JOB_CPUS_PER_NODE")) {
		char *tmp;
		tmp = uint32_compressed_to_str(alloc_info->num_cpu_groups,
					       alloc_info->cpus_per_node,
					       alloc_info->cpu_count_reps);
		fprintf(resize_sh, "export SLURM_JOB_CPUS_PER_NODE=\"%s\"\n",
			tmp);
		fprintf(resize_csh, "setenv SLURM_JOB_CPUS_PER_NODE \"%s\"\n",
			tmp);
		xfree(tmp);
	}
	if (getenv("SLURM_TASKS_PER_NODE")) {
		/* We don't have sufficient information to recreate these */
		fprintf(resize_sh, "unset SLURM_NPROCS\n");
		fprintf(resize_csh, "unsetenv SLURM_NPROCS\n");

		fprintf(resize_sh, "unset SLURM_NTASKS\n");
		fprintf(resize_csh, "unsetenv SLURM_NTASKS\n");

		fprintf(resize_sh, "unset SLURM_TASKS_PER_NODE\n");
		fprintf(resize_csh, "unsetenv SLURM_TASKS_PER_NODE\n");
	}

	printf("To reset Slurm environment variables, execute\n");
	printf("  For bash or sh shells:  . ./%s\n", fname_sh);
	printf("  For csh shells:         source ./%s\n", fname_csh);

fini:	slurm_free_resource_allocation_response_msg(alloc_info);
	xfree(fname_csh);
	xfree(fname_sh);
	if (resize_csh)
		fclose(resize_csh);
	if (resize_sh)
		fclose(resize_sh);
}

/*
 * parse_requeue_args()
 * IN s - string to parse
 * OUT flags - flags to set based upon argument
 * RET 0 on successful parse, -1 otherwise
 */
extern int parse_requeue_flags(char *s, uint32_t *flags)
{
	int len;

	len = strlen(s);
	if (!xstrncasecmp(s, "incomplete", len)) {
		*flags |= JOB_RUNNING;
		return 0;
	}

	if (xstrncasecmp(s, "state=", 6))
		return -1;
	s += 6;
	if (!xstrncasecmp(s, "specialexit", 11) || !xstrncasecmp(s, "se", 2)) {
		*flags |= JOB_SPECIAL_EXIT;
		return 0;
	}

	return -1;
}

/* Return the current time limit of the specified job_id or NO_VAL if the
 * information is not available */
static uint32_t _get_job_time(const char *job_id_str)
{
	uint32_t job_id, task_id;
	char *next_str = NULL;
	uint32_t time_limit = NO_VAL;
	int i, rc;
	job_info_msg_t *resp;
	bitstr_t *array_bitmap;

	job_id = (uint32_t)strtol(job_id_str, &next_str, 10);
	if (next_str[0] == '_') {
		task_id = (uint32_t)strtol(next_str+1, &next_str, 10);
		if (next_str[0] != '\0') {
			error("Invalid job ID %s", job_id_str);
			return time_limit;
		}
	} else if (next_str[0] != '\0') {
		error("Invalid job ID %s", job_id_str);
		return time_limit;
	} else {
		task_id = NO_VAL;
	}

	rc = slurm_load_job(&resp, job_id, SHOW_ALL);
	if (rc == SLURM_SUCCESS) {
		if (resp->record_count == 0) {
			error("Job ID %s not found", job_id_str);
			slurm_free_job_info_msg(resp);
			return time_limit;
		}
		if ((resp->record_count > 1) && (task_id == NO_VAL)) {
			error("TimeLimit increment/decrement not supported "
			      "for job arrays");
			slurm_free_job_info_msg(resp);
			return time_limit;
		}
		for (i = 0; i < resp->record_count; i++) {
			if ((resp->job_array[i].job_id == job_id) &&
			    (resp->job_array[i].array_task_id == NO_VAL) &&
			    (resp->job_array[i].array_bitmap == NULL)) {
				/* Regular job match */
				time_limit = resp->job_array[i].time_limit;
				break;
			}
			if (resp->job_array[i].array_job_id != job_id)
				continue;
			array_bitmap = (bitstr_t *)
				       resp->job_array[i].array_bitmap;
			if ((task_id == NO_VAL) ||
			    (resp->job_array[i].array_task_id == task_id) ||
			    (array_bitmap &&
			     (task_id < bit_size(array_bitmap)) &&
			     bit_test(array_bitmap, task_id))) {
				/* Array job with task_id match */
				time_limit = resp->job_array[i].time_limit;
				break;
			}
		}
		slurm_free_job_info_msg(resp);
	} else {
		error("Could not load state information for job %s: %m",
		      job_id_str);
	}

	return time_limit;
}

static bool _is_single_job(char *job_id_str)
{
	uint32_t job_id, task_id;
	char *next_str = NULL;
	int rc;
	job_info_msg_t *resp;
	bool is_single = false;

	job_id = (uint32_t)strtol(job_id_str, &next_str, 10);
	if (next_str[0] == '_') {
		task_id = (uint32_t)strtol(next_str+1, &next_str, 10);
		if (next_str[0] != '\0') {
			error("Invalid job ID %s", job_id_str);
			return is_single;
		}
	} else if (next_str[0] != '\0') {
		error("Invalid job ID %s", job_id_str);
		return is_single;
	} else {
		task_id = NO_VAL;
	}

	rc = slurm_load_job(&resp, job_id, SHOW_ALL);
	if (rc == SLURM_SUCCESS) {
		if (resp->record_count == 0) {
			error("Job ID %s not found", job_id_str);
			slurm_free_job_info_msg(resp);
			return is_single;
		}
		if ((resp->record_count > 1) && (task_id == NO_VAL)) {
			error("Job resizing not supported for job arrays");
			slurm_free_job_info_msg(resp);
			return is_single;
		}
		is_single = true;	/* Do not bother to validate */
		slurm_free_job_info_msg(resp);
	} else {
		error("Could not load state information for job %s: %m",
		      job_id_str);
	}

	return is_single;
}

/* Translate a job name to relevant job IDs
 * NOTE: xfree the return value to avoid memory leak */
static char *_job_name2id(char *job_name, uint32_t job_uid)
{
	int i, rc;
	job_info_msg_t *resp;
	slurm_job_info_t *job_ptr;
	char *job_id_str = NULL, *sep = "";

	xassert(job_name);

	rc = scontrol_load_job(&resp, 0);
	if (rc == SLURM_SUCCESS) {
		if (resp->record_count == 0) {
			error("JobName %s not found", job_name);
			slurm_free_job_info_msg(resp);
			return job_id_str;
		}
		for (i = 0, job_ptr = resp->job_array; i < resp->record_count;
		     i++, job_ptr++) {
			if ((job_uid != NO_VAL) &&
			    (job_uid != job_ptr->user_id))
				continue;
			if (!job_ptr->name || xstrcmp(job_name, job_ptr->name))
				continue;
			if (job_ptr->array_task_id != NO_VAL) {
				xstrfmtcat(job_id_str, "%s%u_%u", sep,
					   job_ptr->array_job_id,
					   job_ptr->array_task_id);
			} else {
				xstrfmtcat(job_id_str, "%s%u", sep,
					   job_ptr->job_id);
			}
			sep = ",";
		}
		if (!job_id_str) {
			if (job_uid == NO_VAL) {
				error("No jobs with name \'%s\'", job_name);
			} else {
				error("No jobs with user ID %u and name \'%s\'",
				      job_uid, job_name);
			}
		}
	} else {
		error("Could not load state information: %m");
	}

	return job_id_str;
}
