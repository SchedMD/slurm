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

static int _parse_checkpoint_args(int argc, char **argv,
				  uint16_t *max_wait, char **image_dir);
static int _parse_restart_args(int argc, char **argv,
			       uint16_t *stick, char **image_dir);
static void _update_job_size(uint32_t job_id);
static int _parse_requeue_flags(char *, uint32_t *state_flags);
static inline bool _is_array_task_id(const char *jobid);
static job_info_msg_t *_get_job_info(const char *jobid, uint32_t *task_id);
static uint32_t *_get_job_ids(const char *jobid, uint32_t *num_ids);

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

/* Return the current time limit of the specified job_id or NO_VAL if the
 * information is not available */
static uint32_t _get_job_time(uint32_t job_id)
{
	uint32_t time_limit = NO_VAL;
	int i, rc;
	job_info_msg_t *resp;

	rc = slurm_load_job(&resp, job_id, SHOW_ALL);
	if (rc == SLURM_SUCCESS) {
		for (i = 0; i < resp->record_count; i++) {
			if (resp->job_array[i].job_id != job_id)
				continue;	/* should not happen */
			time_limit = resp->job_array[i].time_limit;
			break;
		}
		slurm_free_job_info_msg(resp);
	} else {
		error("Could not load state information for job %u: %m",
		      job_id);
	}

	return time_limit;
}

/*
 * scontrol_hold - perform some job hold/release operation
 * IN op - suspend/resume operation
 * IN job_id_str - a job id
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *		error message and returns 0
 */
extern int
scontrol_hold(char *op, char *job_id_str)
{
	int i, rc = SLURM_SUCCESS;
	char *next_str;
	job_desc_msg_t job_msg;
	uint32_t job_id;
	uint32_t array_id;
	job_info_msg_t *resp;
	slurm_job_info_t *job_ptr;

	if (job_id_str) {
		job_id = (uint32_t) strtol(job_id_str, &next_str, 10);
		if (next_str[0] == '_')
			array_id = strtol(next_str+1, &next_str, 10);
		else
			array_id = NO_VAL;
		if ((job_id == 0) || (next_str[0] != '\0')) {
			fprintf(stderr, "Invalid job id specified\n");
			return 1;
		}
	} else {
		fprintf(stderr, "Invalid job id specified\n");
		return 1;
	}

	if (scontrol_load_job(&resp, job_id)) {
		if (quiet_flag == -1)
			slurm_perror ("slurm_load_job error");
		return 1;
	}

	slurm_init_job_desc_msg (&job_msg);
	job_msg.job_id = job_id;
	/* set current user, needed e.g., for AllowGroups checks */
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
	for (i = 0, job_ptr = resp->job_array; i < resp->record_count;
	     i++, job_ptr++) {
		if ((array_id != NO_VAL) &&
		    (job_ptr->array_task_id != array_id))
			continue;

		if (!IS_JOB_PENDING(job_ptr)) {
			if ((array_id == NO_VAL) &&
			    (job_ptr->array_task_id != NO_VAL))
				continue;
			slurm_seterrno(ESLURM_JOB_NOT_PENDING);
			return ESLURM_JOB_NOT_PENDING;
		}

		job_msg.job_id = job_ptr->job_id;
		if (slurm_update_job(&job_msg))
			rc = slurm_get_errno();
	}

	return rc;
}


/*
 * scontrol_suspend - perform some suspend/resume operation
 * IN op - suspend/resume operation
 * IN job_id_str - a job id
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *		error message and returns 0
 */
extern int
scontrol_suspend(char *op, char *job_id_str)
{
	uint32_t job_id = 0;
	char *next_str;

	if (job_id_str) {
		job_id = (uint32_t) strtol (job_id_str, &next_str, 10);
		if (next_str[0] != '\0') {
			fprintf(stderr, "Invalid job id specified\n");
			exit_code = 1;
			return SLURM_SUCCESS;
		}
	} else {
		fprintf(stderr, "Invalid job id specified\n");
		exit_code = 1;
		return SLURM_SUCCESS;
	}

	if (strncasecmp(op, "suspend", MAX(strlen(op), 2)) == 0)
		return slurm_suspend(job_id);
	else
		return slurm_resume(job_id);
}

/*
 * scontrol_requeue - requeue a pending or running batch job
 * IN job_id_str - a job id
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *              error message and returns 0
 */
extern int
scontrol_requeue(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	int i;
	uint32_t *ids;
	uint32_t num_ids;

	if (! argv[0]) {
		exit_code = 1;
		return 0;
	}

	ids = _get_job_ids(argv[0], &num_ids);
	if (ids == NULL) {
		exit_code = 1;
		return 0;
	}

	for (i = 0; i < num_ids; i++) {
		rc = slurm_requeue(ids[i], 0);
		if (rc != SLURM_SUCCESS) {
			fprintf(stderr, "%s  array job_id %u\n",
					slurm_strerror(slurm_get_errno()), ids[i]);
			exit_code = 1;
			break;
		}
	}

	xfree(ids);

	return rc;
}

extern int
scontrol_requeue_hold(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	int i;
	uint32_t state_flag;
	uint32_t *ids;
	uint32_t num_ids;
	char *job_id_str;

	state_flag = 0;

	if (argc == 1)
		job_id_str = argv[0];
	else
		job_id_str = argv[1];

	ids = _get_job_ids(job_id_str, &num_ids);
	if (ids == NULL) {
		exit_code = 1;
		return 0;
	}

	if (argc == 2) {
		rc = _parse_requeue_flags(argv[0], &state_flag);
		if (rc < 0) {
			error("Invalid state specification %s", argv[0]);
			exit_code = 1;
			xfree(ids);
			return 0;
		}
	}
	state_flag |= JOB_REQUEUE_HOLD;

	/* Go and requeue the state either in
	 * JOB_SPECIAL_EXIT or HELD state.
	 */
	for (i = 0; i < num_ids; i++) {
		rc = slurm_requeue(ids[i], state_flag);
		if (rc != SLURM_SUCCESS) {
			fprintf(stderr, "%s  array job_id %u\n",
					slurm_strerror(slurm_get_errno()), ids[i]);
			exit_code = 1;
			break;
		}
	}

	xfree(ids);

	return rc;
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
	int i, update_cnt = 0;
	char *tag, *val;
	int taglen, vallen;
	job_desc_msg_t job_msg;

	slurm_init_job_desc_msg (&job_msg);

	/* set current user, needed e.g., for AllowGroups checks */
	job_msg.user_id = getuid();

	for (i=0; i<argc; i++) {
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
			job_msg.job_id = slurm_xlate_job_id(val);
			if (job_msg.job_id == 0) {
				error ("Invalid JobId value: %s", val);
				exit_code = 1;
				return 0;
			}
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
			if ((time_limit < 0) && (time_limit != INFINITE)) {
				error("Invalid TimeLimit value");
				exit_code = 1;
				return 0;
			}
			if (incr || decr) {
				job_current_time = _get_job_time(job_msg.
								 job_id);
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
		else if (strncasecmp(tag, "ReqProcs", MAX(taglen, 8)) == 0) {
			if (parse_uint32(val, &job_msg.num_tasks)) {
				error ("Invalid ReqProcs value: %s", val);
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
		else if (strncasecmp(tag, "Name", MAX(taglen, 2)) == 0) {
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
			if (parse_uint16(val, &job_msg.core_spec)) {
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
				error ("Invalid wait-for-switch value: %s", val);
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

	if (slurm_update_job(&job_msg))
		return slurm_get_errno ();

	if (update_size)
		_update_job_size(job_msg.job_id);

	return SLURM_SUCCESS;
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

/* is_job_array()
 * Detect the _ jobid separator.
 */
static inline bool
_is_array_task_id(const char *jobid)
{
	int cc;

	cc = 0;
	while (*jobid) {
		if (*jobid == '_')
			++cc;
		++jobid;
	}

	if (cc == 1)
		return true;

	return false;
}

/* _get_job_info()
 */
static job_info_msg_t *
_get_job_info(const char *jobid, uint32_t *task_id)
{
	char buf[64];
	char *taskid;
	char *next_str;
	uint32_t job_id;
	int cc;
	job_info_msg_t *job_info;

	if (strlen(jobid) > 63)
		return NULL;

	strcpy(buf, jobid);

	taskid = strchr(buf, '_');
	if (taskid) {

		*taskid = 0;
		++taskid;

		*task_id = (uint32_t)strtol(taskid, &next_str, 10);
		if (next_str[0] != '\0') {
			fprintf(stderr, "Invalid task_id specified\n");
			return NULL;
		}
	}

	job_id = (uint32_t)strtol(buf, &next_str, 10);
	if (next_str[0] != '\0') {
		fprintf(stderr, "Invalid job_id specified\n");
		return NULL;
	}

	cc = slurm_load_job(&job_info, job_id, SHOW_ALL);
	if (cc < 0) {
		slurm_perror("slurm_load_job");
		return NULL;
	}

	return job_info;
}

/* _get_job_ids()
 */
static uint32_t *
_get_job_ids(const char *jobid, uint32_t *num_ids)
{
	job_info_msg_t *job_info;
	uint32_t *job_ids;
	uint32_t task_id;
	int i;
	int cc;

	task_id = 0;
	job_info = _get_job_info(jobid, &task_id);
	if (job_info == NULL)
		return NULL;

	if (_is_array_task_id(jobid)) {

		job_ids = xmalloc(sizeof(uint32_t));
		*num_ids = 1;

		/* Search for the job_id of the specified
		 * task.
		 */
		for (cc = 0; cc < job_info->record_count; cc++) {
			if (task_id == job_info->job_array[cc].array_task_id) {
				job_ids[0] = job_info->job_array[cc].job_id;
				break;
			}
		}

		slurm_free_job_info_msg(job_info);
		return job_ids;
	}

	if (job_info->record_count == 1) {
		/* No task elements beside the
		 * job itself so it cannot be
		 * a job array.
		 */
		job_ids = xmalloc(sizeof(uint32_t));
		*num_ids = 1;
		job_ids[0] = job_info->job_array[0].job_id;
		slurm_free_job_info_msg(job_info);

		return job_ids;
	}

	*num_ids = job_info->record_count;
	job_ids = xmalloc((*num_ids) * sizeof(uint32_t));
	/* First save the pending jobs
	 */
	i = 0;
	for (cc = 0; cc < job_info->record_count; cc++) {
		if (job_info->job_array[cc].job_state == JOB_PENDING) {
			job_ids[i] = job_info->job_array[cc].job_id;
			++i;
		}
	}
	/* then the rest of the states
	 */
	for (cc = 0; cc < job_info->record_count; cc++) {
		if (job_info->job_array[cc].job_state != JOB_PENDING) {
			job_ids[i] = job_info->job_array[cc].job_id;
			++i;
		}
	}

	xassert(i == *num_ids);
	slurm_free_job_info_msg(job_info);

	return job_ids;
}
