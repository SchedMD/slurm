/*****************************************************************************\
 *  update_step.c - update step functions for scontrol.
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
#include "src/common/proc_args.h"
#include "src/common/slurm_jobacct_gather.h"

#define MAX_RECORD_FIELDS 100
#define BUFFER_SIZE 4096

/* Return the current time limit of the specified job/step_id or NO_VAL if the
 * information is not available */
static uint32_t _get_step_time(uint32_t job_id, uint32_t step_id)
{
	uint32_t time_limit = NO_VAL;
	int i, rc;
	job_step_info_response_msg_t *resp;

	rc = slurm_get_job_steps((time_t) 0, job_id, step_id, &resp, SHOW_ALL);
	if (rc == SLURM_SUCCESS) {
		for (i = 0; i < resp->job_step_count; i++) {
			if ((resp->job_steps[i].job_id != job_id) ||
			    (resp->job_steps[i].step_id != step_id))
				continue;	/* should not happen */
			time_limit = resp->job_steps[i].time_limit;
			break;
		}
		slurm_free_job_step_info_response_msg(resp);
	} else {
		error("Could not load state information for step %u.%u: %m",
		      job_id, step_id);
	}

	return time_limit;
}

static int _parse_comp_file(
	char *file, step_update_request_msg_t *step_msg)
{
	int i;
	FILE *fd = fopen(file, "r");
	char line[BUFFER_SIZE];
	char *fptr;
	int version;
	char *update[MAX_RECORD_FIELDS+1];    /* End list with null entry and,
						 possibly, more data than we
						 expected */

	enum {	UPDATE_STEP_VERSION,
		UPDATE_STEP_EXTRA,
		UPDATE_STEP_INBLOCKS,
		UPDATE_STEP_OUTBLOCKS,
		UPDATE_STEP_EXITCODE,
		UPDATE_STEP_CPU_ALLOC,
		UPDATE_STEP_START,
		UPDATE_STEP_END,
		UPDATE_STEP_USER_SEC,
		UPDATE_STEP_SYS_SEC,
		UPDATE_STEP_MAX_RSS,
		UPDATE_STEP_UID,
		UPDATE_STEP_STEPNAME,
		UPDATE_STEP_VER1_LENGTH
	};

	if (fd == NULL) {
		perror(file);
		return SLURM_ERROR;
	}

	if (!fgets(line, BUFFER_SIZE, fd)) {
		fprintf(stderr, "Empty step update completion file\n");
		return SLURM_ERROR;
	}

	fptr = line;	/* break the record into NULL-terminated strings */
	for (i = 0; i < MAX_RECORD_FIELDS; i++) {
		update[i] = fptr;
		fptr = strstr(fptr, " ");
		if (fptr == NULL) {
			fptr = strstr(update[i], "\n");
			if (fptr)
				*fptr = 0;
			break;
		} else
			*fptr++ = 0;
	}

	if (i < MAX_RECORD_FIELDS)
		i++;
	update[i] = 0;

	version = atoi(update[UPDATE_STEP_VERSION]);
	switch (version) {
	case 1:
		if (i != UPDATE_STEP_VER1_LENGTH) {
			fprintf(stderr,
				"Bad step update completion file length\n");
			return SLURM_ERROR;
		}
		step_msg->jobacct = jobacctinfo_create(NULL);
		step_msg->exit_code = atoi(update[UPDATE_STEP_EXITCODE]);
		step_msg->start_time = atoi(update[UPDATE_STEP_START]);
		step_msg->end_time = atoi(update[UPDATE_STEP_END]);
		step_msg->jobacct->user_cpu_sec =
			atoi(update[UPDATE_STEP_USER_SEC]);
		step_msg->jobacct->sys_cpu_sec =
			atoi(update[UPDATE_STEP_SYS_SEC]);
		step_msg->jobacct->min_cpu =
			step_msg->jobacct->user_cpu_sec
			+ step_msg->jobacct->sys_cpu_sec;
		step_msg->jobacct->max_rss = atoi(update[UPDATE_STEP_MAX_RSS]);
		step_msg->name =
			xstrdup(xbasename(update[UPDATE_STEP_STEPNAME]));
		break;
	default:
		fprintf(stderr, "Unsupported step update "
			"completion file version: %d\n",
			version);
		return SLURM_ERROR;
		break;
	}


	return SLURM_SUCCESS;
}

/*
 * scontrol_update_step - update the slurm step configuration per the supplied
 *	arguments
 * IN argc - count of arguments
 * IN argv - list of arguments
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *			error message and returns 0
 */
extern int scontrol_update_step (int argc, char *argv[])
{
	int i, update_cnt = 0;
	char *tag, *val;
	int taglen;
	step_update_request_msg_t step_msg;

	slurm_init_update_step_msg (&step_msg);

	for (i=0; i<argc; i++) {
		tag = argv[i];
		val = strchr(argv[i], '=');
		if (val) {
			taglen = val - argv[i];
			val++;
		} else {
			exit_code = 1;
			fprintf (stderr, "Invalid input: %s\n", argv[i]);
			fprintf (stderr, "Request aborted\n");
			return -1;
		}

		if (strncasecmp(tag, "StepId", MAX(taglen, 4)) == 0) {
			char *end_ptr;
			step_msg.job_id = (uint32_t) strtol(val, &end_ptr, 10);
			if (end_ptr[0] == '.') {
				step_msg.step_id = (uint32_t)
					strtol(end_ptr+1, (char **) NULL, 10);
			} else if (end_ptr[0] != '\0') {
				exit_code = 1;
				fprintf (stderr, "Invalid StepID parameter: "
					 "%s\n", argv[i]);
				fprintf (stderr, "Request aborted\n");
				return 0;
			} /* else apply to all steps of this job_id */
		} else if (strncasecmp(tag, "TimeLimit", MAX(taglen, 2)) == 0) {
			bool incr, decr;
			uint32_t step_current_time, time_limit;

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
				step_current_time = _get_step_time(
							step_msg.job_id,
							step_msg.step_id);
				if (step_current_time == NO_VAL) {
					exit_code = 1;
					return 0;
				}
				if (incr) {
					time_limit += step_current_time;
				} else if (time_limit > step_current_time) {
					error("TimeLimit decrement larger than"
					      " current time limit (%u > %u)",
					      time_limit, step_current_time);
					exit_code = 1;
					return 0;
				} else {
					time_limit = step_current_time -
						     time_limit;
				}
			}
			step_msg.time_limit = time_limit;
			update_cnt++;
		} else if (strncasecmp(tag, "CompFile", MAX(taglen, 2)) == 0) {
			if (_parse_comp_file(val, &step_msg)) {
				exit_code = 1;
				fprintf(stderr,
					"Bad completion file (%s) given\n"
					"Request aborted\n", val);
				return 0;
			}
			update_cnt++;
		} else {
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

	if (slurm_update_step(&step_msg))
		return slurm_get_errno ();
	else
		return 0;
}
