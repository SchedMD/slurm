/*****************************************************************************\
 *  update_job.c - update job functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
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

#include "scontrol.h"
#include "src/common/env.h"
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
static int	_parse_checkpoint_args(int argc, char **argv,
				       uint16_t *max_wait, char **image_dir);
static int	_parse_requeue_flags(char *, uint32_t *state_flags);
static int	_parse_restart_args(int argc, char **argv,
				    uint16_t *stick, char **image_dir);
static void	_update_job_size(uint32_t job_id);

/* Local variables for managing job IDs */
static char *local_job_str = NULL;

/* Confirm that contents of job_str is valid comma delimited list of job IDs */
static bool _is_job_id(char *job_str)
{
	bool have_under = false;
	int bracket_cnt = 0;
	int i;

	if (!job_str)
		return false;

	local_job_str = xstrdup(job_str);
	for (i = 0; local_job_str[i]; i++) {
		if (local_job_str[i] == '_') {
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
				have_under = false;
			}
		} else if ((local_job_str[i] < '0') || (local_job_str[i] > '9'))
			goto fail;	/* Unexpected character */
	}

	if (bracket_cnt != 0)
		goto fail;	/* Unbalanced brackets */
	return true;

fail:	xfree(local_job_str);
	debug("Character %d in %s is not a valid job ID", i, job_str);
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
 * scontrol_checkpoint - perform some checkpoint/resume operation
 * IN op - checkpoint operation
 * IN job_step_id_str - either a job name (for all steps of the given job) or
 *			a step name: "<jid>.<step_id>"
 * IN argc - argument count
 * IN argv - arguments of the operation
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *			error message and returns 0
 */
extern int
scontrol_checkpoint(char *op, char *job_step_id_str, int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	uint32_t job_id = 0, step_id = 0;
	char *next_str;
	uint32_t ckpt_errno;
	char *ckpt_strerror = NULL;
	int oplen = strlen(op);
	uint16_t max_wait = CKPT_WAIT, stick = 0;
	char *image_dir = NULL;

	if (job_step_id_str) {
		job_id = (uint32_t) strtol (job_step_id_str, &next_str, 10);
		if (next_str[0] == '.') {
			step_id = (uint32_t) strtol (&next_str[1], &next_str,
						     10);
		} else
			step_id = NO_VAL;
		if (next_str[0] != '\0') {
			fprintf(stderr, "Invalid job step name\n");
			return 0;
		}
	} else {
		fprintf(stderr, "Invalid job step name\n");
		return 0;
	}

	if (strncasecmp(op, "able", MAX(oplen, 1)) == 0) {
		time_t start_time;
		rc = slurm_checkpoint_able (job_id, step_id, &start_time);
		if (rc == SLURM_SUCCESS) {
			if (start_time) {
				char time_str[32];
				slurm_make_time_str(&start_time, time_str,
						    sizeof(time_str));
				printf("Began at %s\n", time_str);
			} else
				printf("Yes\n");
		} else if (slurm_get_errno() == ESLURM_DISABLED) {
			printf("No\n");
			rc = SLURM_SUCCESS;	/* not real error */
		}
	}
	else if (strncasecmp(op, "complete", MAX(oplen, 2)) == 0) {
		/* Undocumented option used for testing purposes */
		static uint32_t error_code = 1;
		char error_msg[64];
		sprintf(error_msg, "test error message %d", error_code);
		rc = slurm_checkpoint_complete(job_id, step_id, (time_t) 0,
			error_code++, error_msg);
	}
	else if (strncasecmp(op, "disable", MAX(oplen, 1)) == 0)
		rc = slurm_checkpoint_disable (job_id, step_id);
	else if (strncasecmp(op, "enable", MAX(oplen, 2)) == 0)
		rc = slurm_checkpoint_enable (job_id, step_id);
	else if (strncasecmp(op, "create", MAX(oplen, 2)) == 0) {
		if (_parse_checkpoint_args(argc, argv, &max_wait, &image_dir)){
			return 0;
		}
		rc = slurm_checkpoint_create (job_id, step_id, max_wait,
					      image_dir);

	} else if (strncasecmp(op, "requeue", MAX(oplen, 2)) == 0) {
		if (_parse_checkpoint_args(argc, argv, &max_wait, &image_dir)){
			return 0;
		}
		rc = slurm_checkpoint_requeue (job_id, max_wait, image_dir);

	} else if (strncasecmp(op, "vacate", MAX(oplen, 2)) == 0) {
		if (_parse_checkpoint_args(argc, argv, &max_wait, &image_dir)){
			return 0;
		}
		rc = slurm_checkpoint_vacate (job_id, step_id, max_wait,
					      image_dir);

	} else if (strncasecmp(op, "restart", MAX(oplen, 2)) == 0) {
		if (_parse_restart_args(argc, argv, &stick, &image_dir)) {
			return 0;
		}
		rc = slurm_checkpoint_restart (job_id, step_id, stick,
					       image_dir);

	} else if (strncasecmp(op, "error", MAX(oplen, 2)) == 0) {
		rc = slurm_checkpoint_error (job_id, step_id,
			&ckpt_errno, &ckpt_strerror);
		if (rc == SLURM_SUCCESS) {
			printf("error(%u): %s\n", ckpt_errno, ckpt_strerror);
			free(ckpt_strerror);
		}
	}

	else {
		fprintf (stderr, "Invalid checkpoint operation: %s\n", op);
		return 0;
	}

	return rc;
}

static int
_parse_checkpoint_args(int argc, char **argv, uint16_t *max_wait,
		       char **image_dir)
{
	int i;

	for (i=0; i< argc; i++) {
		if (strncasecmp(argv[i], "MaxWait=", 8) == 0) {
			*max_wait = (uint16_t) strtol(&argv[i][8],
						      (char **) NULL, 10);
		} else if (strncasecmp(argv[i], "ImageDir=", 9) == 0) {
			*image_dir = &argv[i][9];
		} else {
			exit_code = 1;
			error("Invalid input: %s", argv[i]);
			error("Request aborted");
			return -1;
		}
	}
	return 0;
}

static int
_parse_restart_args(int argc, char **argv, uint16_t *stick, char **image_dir)
{
	int i;

	for (i=0; i< argc; i++) {
		if (strncasecmp(argv[i], "StickToNodes", 5) == 0) {
			*stick = 1;
		} else if (strncasecmp(argv[i], "ImageDir=", 9) == 0) {
			*image_dir = &argv[i][9];
		} else {
			exit_code = 1;
			error("Invalid input: %s", argv[i]);
			error("Request aborted");
			return -1;
		}
	}
	return 0;
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
	job_desc_msg_t job_msg;
	uint32_t job_id = 0;
	char *job_name = NULL;
	char *job_id_str = NULL;
	slurm_job_info_t *job_ptr;

	if (job_str && !strncasecmp(job_str, "JobID=", 6))
		job_str += 6;
	if (job_str && !strncasecmp(job_str, "Job=", 4))
		job_str += 4;

	slurm_init_job_desc_msg (&job_msg);
	job_msg.user_id = getuid();
	if ((strncasecmp(op, "holdu", 5) == 0) ||
	    (strncasecmp(op, "uhold", 5) == 0)) {
		job_msg.priority = 0;
		job_msg.alloc_sid = ALLOC_SID_USER_HOLD;
	} else if (strncasecmp(op, "hold", 4) == 0) {
		job_msg.priority = 0;
		job_msg.alloc_sid = 0;
	} else
		job_msg.priority = INFINITE;

	if (_is_job_id(job_str)) {
		job_msg.job_id_str = _next_job_id();
		while (job_msg.job_id_str) {
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
			}
			job_msg.job_id_str = _next_job_id();
		}
		return rc;
	} else if (job_str) {
		if (!strncasecmp(job_str, "Name=", 5)) {
			job_str += 5;
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
		if (job_name &&
		    ((job_ptr->name == NULL) ||
		     strcmp(job_name, job_ptr->name)))
			continue;

		if (!IS_JOB_PENDING(job_ptr)) {
			if (job_ptr->array_task_id != NO_VAL)
				continue;
			if (job_name)
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
			for (i = 0; i < resp->job_array_count; i++) {
				if ((resp->error_code[i] == SLURM_SUCCESS) &&
				    (resp->job_array_count == 1))
					continue;
				exit_code = 1;
				if (quiet_flag == 1)
					continue;
				fprintf(stderr, "%s: %s\n",
					resp->job_array_id[i],
					slurm_strerror(resp->error_code[i]));
			}
			slurm_free_job_array_resp(resp);
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

	if (strncasecmp(job_str, "jobid=", 6) == 0)
		job_str += 6;
	if (strncasecmp(job_str, "job=", 4) == 0)
		job_str += 4;

	if (_is_job_id(job_str)) {
		job_id_str = _next_job_id();
		while (job_id_str) {
			if (!strncasecmp(op, "suspend", MAX(strlen(op), 2)))
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
scontrol_requeue(int argc, char **argv)
{
	char *job_id_str, *job_str;
	int rc, i;
	job_array_resp_msg_t *resp = NULL;

	if (!argv[0]) {
		exit_code = 1;
		return;
	}

	job_str = argv[0];
	if (strncasecmp(argv[0], "jobid=", 6) == 0)
		job_str += 6;
	if (strncasecmp(argv[0], "job=", 4) == 0)
		job_str += 4;

	if (_is_job_id(job_str)) {
		job_id_str = _next_job_id();
		while (job_id_str) {
			rc = slurm_requeue2(job_id_str, 0, &resp);
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
scontrol_requeue_hold(int argc, char **argv)
{
	int rc, i;
	uint32_t state_flag;
	char *job_id_str, *job_str;
	job_array_resp_msg_t *resp = NULL;

	state_flag = 0;

	if (argc == 1)
		job_str = argv[0];
	else
		job_str = argv[1];

	if (strncasecmp(job_str, "jobid=", 6) == 0)
		job_str += 6;
	if (strncasecmp(job_str, "job=", 4) == 0)
		job_str += 4;

	if (argc == 2) {
		if (_parse_requeue_flags(argv[0], &state_flag) < 0) {
			error("Invalid state specification %s", argv[0]);
			exit_code = 1;
			return;
		}
	}
	state_flag |= JOB_REQUEUE_HOLD;

	if (_is_job_id(job_str)) {
		job_id_str = _next_job_id();
		while (job_id_str) {
			rc = slurm_requeue2(job_id_str, state_flag, &resp);
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
 * scontrol_update_job - update the slurm job configuration per the supplied
 *	arguments
 * IN argc - count of arguments
 * IN argv - list of arguments
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *			error message and returns 0
 */
extern int
scontrol_update_job (int argc, char *argv[])
{
	bool update_size = false;
	int i, update_cnt = 0, rc = SLURM_SUCCESS, rc2;
	char *tag, *val;
	int taglen, vallen;
	job_desc_msg_t job_msg;
	job_array_resp_msg_t *resp = NULL;
	uint32_t job_uid = NO_VAL;

	slurm_init_job_desc_msg (&job_msg);

	/* set current user, needed e.g., for AllowGroups checks */
	job_msg.user_id = getuid();

	for (i = 0; i < argc; i++) {
		tag = argv[i];
		val = strchr(argv[i], '=');
		if (val) {
			taglen = val - argv[i];
			val++;
			vallen = strlen(val);
		} else if (strncasecmp(tag, "Nice", MAX(strlen(tag), 2)) == 0){
			/* "Nice" is the only tag that might not have an
			   equal sign, so it is handled specially. */
			job_msg.nice = NICE_OFFSET + 100;
			update_cnt++;
			continue;
		} else {
			exit_code = 1;
			fprintf (stderr, "Invalid input: %s\n", argv[i]);
			fprintf (stderr, "Request aborted\n");
			return -1;
		}

		if (strncasecmp(tag, "JobId", MAX(taglen, 3)) == 0) {
			job_msg.job_id_str = val;
		}
		else if (strncasecmp(tag, "Comment", MAX(taglen, 3)) == 0) {
			job_msg.comment = val;
			update_cnt++;
		}
		else if (strncasecmp(tag, "TimeLimit", MAX(taglen, 5)) == 0) {
			bool incr, decr;
			uint32_t job_current_time, time_limit;

			incr = (val[0] == '+');
			decr = (val[0] == '-');
			if (incr || decr)
				val++;
			time_limit = time_str2mins(val);
			if (time_limit == NO_VAL) {
				error("Invalid TimeLimit value");
				exit_code = 1;
				return 0;
			}
			if (incr || decr) {
				if (!job_msg.job_id_str) {
					error("JobId must preceed TimeLimit "
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
				if (incr) {
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
			}
			job_msg.time_limit = time_limit;
			update_cnt++;
		}
		else if (strncasecmp(tag, "TimeMin", MAX(taglen, 5)) == 0) {
			int time_min = time_str2mins(val);
			if ((time_min < 0) && (time_min != INFINITE)) {
				error("Invalid TimeMin value");
				exit_code = 1;
				return 0;
			}
			job_msg.time_min = time_min;
			update_cnt++;
		}
		else if (strncasecmp(tag, "Priority", MAX(taglen, 2)) == 0) {
			if (parse_uint32(val, &job_msg.priority)) {
				error ("Invalid Priority value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(tag, "Nice", MAX(taglen, 2)) == 0) {
			int nice;
			nice = strtoll(val, (char **) NULL, 10);
			if (abs(nice) > NICE_OFFSET) {
				error("Invalid nice value, must be between "
					"-%d and %d", NICE_OFFSET,
					NICE_OFFSET);
				exit_code = 1;
				return 0;
			}
			job_msg.nice = NICE_OFFSET + nice;
			update_cnt++;
		}
		else if (strncasecmp(tag, "NumCPUs", MAX(taglen, 6)) == 0) {
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
		/* ReqProcs was removed in SLURM version 2.1 */
		else if ((strncasecmp(tag, "NumTasks", MAX(taglen, 8)) == 0) ||
			 (strncasecmp(tag, "ReqProcs", MAX(taglen, 8)) == 0)) {
			if (parse_uint32(val, &job_msg.num_tasks)) {
				error ("Invalid NumTasks value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(tag, "Requeue", MAX(taglen, 4)) == 0) {
			if (parse_uint16(val, &job_msg.requeue)) {
				error ("Invalid Requeue value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		/* ReqNodes was replaced by NumNodes in SLURM version 2.1 */
		else if ((strncasecmp(tag, "ReqNodes", MAX(taglen, 8)) == 0) ||
		         (strncasecmp(tag, "NumNodes", MAX(taglen, 8)) == 0)) {
			int min_nodes, max_nodes, rc;
			if (strcmp(val, "0") == 0) {
				job_msg.min_nodes = 0;
			} else if (strcasecmp(val, "ALL") == 0) {
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
		else if (strncasecmp(tag, "ReqSockets", MAX(taglen, 4)) == 0) {
			if (parse_uint16(val, &job_msg.sockets_per_node)) {
				error ("Invalid ReqSockets value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(tag, "ReqCores", MAX(taglen, 4)) == 0) {
			if (parse_uint16(val, &job_msg.cores_per_socket)) {
				error ("Invalid ReqCores value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
                else if (strncasecmp(tag, "TasksPerNode", MAX(taglen, 2))==0) {
			if (parse_uint16(val, &job_msg.ntasks_per_node)) {
				error ("Invalid TasksPerNode value: %s", val);
				exit_code = 1;
				return 0;
			}
                        update_cnt++;
                }
		else if (strncasecmp(tag, "ReqThreads", MAX(taglen, 4)) == 0) {
			if (parse_uint16(val, &job_msg.threads_per_core)) {
				error ("Invalid ReqThreads value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(tag, "MinCPUsNode", MAX(taglen, 4)) == 0) {
			if (parse_uint16(val, &job_msg.pn_min_cpus)) {
				error ("Invalid MinCPUsNode value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(tag, "MinMemoryNode",
				     MAX(taglen, 10)) == 0) {
			if (parse_uint32(val, &job_msg.pn_min_memory)) {
				error ("Invalid MinMemoryNode value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(tag, "MinMemoryCPU",
				     MAX(taglen, 10)) == 0) {
			if (parse_uint32(val, &job_msg.pn_min_memory)) {
				error ("Invalid MinMemoryCPU value: %s", val);
				exit_code = 1;
				return 0;
			}
			job_msg.pn_min_memory |= MEM_PER_CPU;
			update_cnt++;
		}
		else if (strncasecmp(tag, "MinTmpDiskNode",
				     MAX(taglen, 5)) == 0) {
			if (parse_uint32(val, &job_msg.pn_min_tmp_disk)) {
				error ("Invalid MinTmpDiskNode value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(tag, "Partition", MAX(taglen, 2)) == 0) {
			job_msg.partition = val;
			update_cnt++;
		}
		else if (strncasecmp(tag, "QOS", MAX(taglen, 2)) == 0) {
			job_msg.qos = val;
			update_cnt++;
		}
		else if (strncasecmp(tag, "ReservationName",
				     MAX(taglen, 3)) == 0) {
			job_msg.reservation = val;
			update_cnt++;
		}
		else if (!strncasecmp(tag, "Name", MAX(taglen, 2)) ||
			 !strncasecmp(tag, "JobName", MAX(taglen, 4))) {
			job_msg.name = val;
			update_cnt++;
		}
		else if (strncasecmp(tag, "WCKey", MAX(taglen, 1)) == 0) {
			job_msg.wckey = val;
			update_cnt++;
		}
		else if (strncasecmp(tag, "StdOut", MAX(taglen, 6)) == 0) {
			job_msg.std_out = val;
			update_cnt++;
		}
		else if (strncasecmp(tag, "Switches", MAX(taglen, 5)) == 0) {
			char *sep_char;
			job_msg.req_switch =
				(uint32_t) strtol(val, &sep_char, 10);
			update_cnt++;
			if (sep_char && sep_char[0] == '@') {
				job_msg.wait4switch = time_str2mins(sep_char+1)
						      * 60;
			}
		}
		else if (strncasecmp(tag, "wait-for-switch", MAX(taglen, 5))
			 == 0) {
			if (parse_uint32(val, &job_msg.wait4switch)) {
				error ("Invalid wait-for-switch value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(tag, "Shared", MAX(taglen, 2)) == 0) {
			if (strncasecmp(val, "YES", MAX(vallen, 1)) == 0)
				job_msg.shared = 1;
			else if (strncasecmp(val, "NO", MAX(vallen, 1)) == 0)
				job_msg.shared = 0;
			else if (parse_uint16(val, &job_msg.shared)) {
				error ("Invalid wait-for-switch value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(tag, "Contiguous", MAX(taglen, 3)) == 0) {
			if (strncasecmp(val, "YES", MAX(vallen, 1)) == 0)
				job_msg.contiguous = 1;
			else if (strncasecmp(val, "NO", MAX(vallen, 1)) == 0)
				job_msg.contiguous = 0;
			else if (parse_uint16(val, &job_msg.contiguous)) {
				error ("Invalid Contiguous value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(tag, "CoreSpec", MAX(taglen, 4)) == 0) {
			if (!strcmp(val, "-1") || !strcmp(val, "*"))
				job_msg.core_spec = (uint16_t) INFINITE;
			else if (parse_uint16(val, &job_msg.core_spec)) {
				error ("Invalid CoreSpec value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(tag, "ExcNodeList", MAX(taglen, 3)) == 0){
			job_msg.exc_nodes = val;
			update_cnt++;
		}
		else if (!strncasecmp(tag, "NodeList",    MAX(taglen, 8)) ||
			 !strncasecmp(tag, "ReqNodeList", MAX(taglen, 8))) {
			job_msg.req_nodes = val;
			update_size = true;
			update_cnt++;
		}
		else if (strncasecmp(tag, "Features", MAX(taglen, 1)) == 0) {
			job_msg.features = val;
			update_cnt++;
		}
		else if (strncasecmp(tag, "Gres", MAX(taglen, 2)) == 0) {
			if (!strcasecmp(val, "help") ||
			    !strcasecmp(val, "list")) {
				print_gres_help();
			} else {
				job_msg.gres = val;
				update_cnt++;
			}
		}
		else if (strncasecmp(tag, "Account", MAX(taglen, 1)) == 0) {
			job_msg.account = val;
			update_cnt++;
		}
		else if (strncasecmp(tag, "Dependency", MAX(taglen, 1)) == 0) {
			job_msg.dependency = val;
			update_cnt++;
		}
		else if (strncasecmp(tag, "Geometry", MAX(taglen, 2)) == 0) {
			char* token, *delimiter = ",x", *next_ptr;
			int j, rc = 0;
			int dims = slurmdb_setup_cluster_dims();
			uint16_t geo[dims];
			char* geometry_tmp = xstrdup(val);
			char* original_ptr = geometry_tmp;
			token = strtok_r(geometry_tmp, delimiter, &next_ptr);
			for (j=0; j<dims; j++) {
				if (token == NULL) {
					error("insufficient dimensions in "
						"Geometry");
					rc = -1;
					break;
				}
				geo[j] = (uint16_t) atoi(token);
				if (geo[j] <= 0) {
					error("invalid --geometry argument");
					rc = -1;
					break;
				}
				geometry_tmp = next_ptr;
				token = strtok_r(geometry_tmp, delimiter,
					&next_ptr);
			}
			if (token != NULL) {
				error("too many dimensions in Geometry");
				rc = -1;
			}

			if (original_ptr)
				xfree(original_ptr);
			if (rc != 0)
				exit_code = 1;
			else {
				for (j=0; j<dims; j++)
					job_msg.geometry[j] = geo[j];
				update_cnt++;
			}
		}

		else if (strncasecmp(tag, "Rotate", MAX(taglen, 2)) == 0) {
			if (strncasecmp(val, "YES", MAX(vallen, 1)) == 0)
				job_msg.rotate = 1;
			else if (strncasecmp(val, "NO", MAX(vallen, 1)) == 0)
				job_msg.rotate = 0;
			else if (parse_uint16(val, &job_msg.rotate)) {
				error ("Invalid rotate value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(tag, "Conn-Type", MAX(taglen, 2)) == 0) {
			verify_conn_type(val, job_msg.conn_type);
			if (job_msg.conn_type[0] != (uint16_t)NO_VAL)
				update_cnt++;
		}
		else if (strncasecmp(tag, "Licenses", MAX(taglen, 1)) == 0) {
			job_msg.licenses = val;
			update_cnt++;
		}
		else if (!strncasecmp(tag, "EligibleTime", MAX(taglen, 2)) ||
			 !strncasecmp(tag, "StartTime",    MAX(taglen, 2))) {
			if ((job_msg.begin_time = parse_time(val, 0))) {
				if (job_msg.begin_time < time(NULL))
					job_msg.begin_time = time(NULL);
				update_cnt++;
			}
		}
		else if (!strncasecmp(tag, "EndTime", MAX(taglen, 2))) {
			job_msg.end_time = parse_time(val, 0);
			update_cnt++;
		}
		else if (!strncasecmp(tag, "Reboot", MAX(taglen, 3))) {
			if (strncasecmp(val, "YES", MAX(vallen, 1)) == 0)
				job_msg.reboot = 1;
			else if (strncasecmp(val, "NO", MAX(vallen, 1)) == 0)
				job_msg.reboot = 0;
			else if (parse_uint16(val, &job_msg.reboot)) {
				error ("Invalid reboot value: %s", val);
				exit_code = 1;
				return 0;
			}
			update_cnt++;
		}
		else if (!strncasecmp(tag, "UserID", MAX(taglen, 3))) {
			uid_t user_id = 0;
			if (uid_from_string(val, &user_id) < 0) {
				exit_code = 1;
				fprintf (stderr, "Invalid UserID: %s\n", val);
				fprintf (stderr, "Request aborted\n");
				return 0;
			}
			job_uid = (uint32_t) user_id;
		}
		else {
			exit_code = 1;
			fprintf (stderr, "Update of this parameter is not "
				 "supported: %s\n", argv[i]);
			fprintf (stderr, "Request aborted\n");
			return 0;
		}
	}

	if (update_cnt == 0) {
		exit_code = 1;
		fprintf (stderr, "No changes specified\n");
		return 0;
	}

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
	} else if (update_size) {
		/* See check above for one job ID */
		job_msg.job_id = slurm_atoul(job_msg.job_id_str);
		_update_job_size(job_msg.job_id);
	}

	if (_is_job_id(job_msg.job_id_str)) {
		job_msg.job_id_str = _next_job_id();
		while (job_msg.job_id_str) {
			rc2 = slurm_update_job2(&job_msg, &resp);
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
			}
			job_msg.job_id_str = _next_job_id();
		}
	}

	return rc;
}

/*
 * Send message to stdout of specified job
 * argv[0] == jobid
 * argv[1]++ the message
 */
extern int
scontrol_job_notify(int argc, char *argv[])
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
		return;		/*No job environment here to update */

	if (slurm_allocation_lookup_lite(job_id, &alloc_info) !=
	    SLURM_SUCCESS) {
		slurm_perror("slurm_allocation_lookup_lite");
		return;
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
	chmod(fname_csh, 0700);	/* Make file executable */
	chmod(fname_sh,  0700);

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
		/* We don't have sufficient information to recreate this */
		fprintf(resize_sh, "unset SLURM_TASKS_PER_NODE\n");
		fprintf(resize_csh, "unsetenv SLURM_TASKS_PER_NODE\n");
	}

	printf("To reset SLURM environment variables, execute\n");
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

/* _parse_requeue_args()
 */
static int
_parse_requeue_flags(char *s, uint32_t *state)
{
	char *p;
	char *p0;
	char *z;

	p0 = p = xstrdup(s);
	/* search for =
	 */
	z = strchr(p, '=');
	if (!z) {
		return -1;
	}
	*z = 0;

	/* validate flags keyword
	 */
	if (strncasecmp(p, "state", 5) != 0) {
		return -1;
	}
	++z;

	p = z;
	if (strncasecmp(p, "specialexit", 11) == 0
	    || strncasecmp(p, "se", 2) == 0) {
		*state = JOB_SPECIAL_EXIT;
		xfree(p0);
		return 0;
	}

	xfree(p0);
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
			    ((task_id < bit_size(array_bitmap)) &&
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
			if (!job_ptr->name || strcmp(job_name, job_ptr->name))
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
