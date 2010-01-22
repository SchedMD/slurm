/*****************************************************************************\
 *  update_job.c - update job functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

static int _parse_checkpoint_args(int argc, char **argv,
				  uint16_t *max_wait, char **image_dir);
static int _parse_restart_args(int argc, char **argv,
			       uint16_t *stick, char **image_dir);

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
	uint32_t job_id = 0, step_id = 0, step_id_set = 0;
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
			step_id_set = 1;
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
 * scontrol_suspend - perform some suspend/resume operation
 * IN op - suspend/resume operation
 * IN job_id_str - a job id
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *		error message and returns 0
 */
extern int
scontrol_suspend(char *op, char *job_id_str)
{
	int rc = SLURM_SUCCESS;
	uint32_t job_id = 0;
	char *next_str;

	if (job_id_str) {
		job_id = (uint32_t) strtol (job_id_str, &next_str, 10);
		if (next_str[0] != '\0') {
			fprintf(stderr, "Invalid job id specified\n");
			exit_code = 1;
			return 0;
		}
	} else {
		fprintf(stderr, "Invalid job id specified\n");
		exit_code = 1;
		return 0;
	}

	if (strncasecmp(op, "suspend", MAX(strlen(op), 2)) == 0)
		rc = slurm_suspend (job_id);
	else
		rc = slurm_resume (job_id);

	return rc;
}

/*
 * scontrol_requeue - requeue a pending or running batch job
 * IN job_id_str - a job id
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *              error message and returns 0
 */
extern int
scontrol_requeue(char *job_id_str)
{
	int rc = SLURM_SUCCESS;
	uint32_t job_id = 0;
	char *next_str;

	if (job_id_str) {
		job_id = (uint32_t) strtol (job_id_str, &next_str, 10);
		if (next_str[0] != '\0') {
			fprintf(stderr, "Invalid job id specified\n");
			exit_code = 1;
			return 0;
		}
	} else {
		fprintf(stderr, "Invalid job id specified\n");
		exit_code = 1;
		return 0;
	}

	rc = slurm_requeue (job_id);
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
	int i, update_cnt = 0;
	char *tag, *val;
	int taglen, vallen;
	job_desc_msg_t job_msg;

	slurm_init_job_desc_msg (&job_msg);

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

		if (strncasecmp(tag, "JobId", MAX(taglen, 1)) == 0) {
			job_msg.job_id =
				(uint32_t) strtol(val, (char **) NULL, 10);
		}
		else if (strncasecmp(tag, "Comment", MAX(taglen, 3)) == 0) {
			job_msg.comment = val;
			update_cnt++;
		}
		else if (strncasecmp(tag, "TimeLimit", MAX(taglen, 2)) == 0) {
			int time_limit = time_str2mins(val);
			if ((time_limit < 0) && (time_limit != INFINITE)) {
				error("Invalid TimeLimit value");
				exit_code = 1;
				return 0;
			}
			job_msg.time_limit = time_limit;
			update_cnt++;
		}
		else if (strncasecmp(tag, "Priority", MAX(taglen, 2)) == 0) {
			job_msg.priority =
				(uint32_t) strtoll(val, (char **) NULL, 10);
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
		/* ReqProcs was replaced by NumTasks in SLURM version 2.1 */
		else if ((strncasecmp(tag, "ReqProcs", MAX(taglen, 4)) == 0) ||
			 (strncasecmp(tag, "NumTasks", MAX(taglen, 8)) == 0)) {
			job_msg.num_tasks =
				(uint32_t) strtol(val, (char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(tag, "Requeue", MAX(taglen, 4)) == 0) {
			job_msg.requeue =
				(uint16_t) strtol(val, (char **) NULL, 10);
			update_cnt++;
		}
		/* ReqNodes was replaced by NumNodes in SLURM version 2.1 */
		else if ((strncasecmp(tag, "ReqNodes", MAX(taglen, 8)) == 0) ||
		         (strncasecmp(tag, "NumNodes", MAX(taglen, 8)) == 0)) {
			int rc = get_resource_arg_range(
				val,
				"requested node count",
				(int *)&job_msg.min_nodes,
				(int *)&job_msg.max_nodes,
				false);
			if(!rc)
				return rc;
			update_cnt++;
		}
		else if (strncasecmp(tag, "ReqSockets", MAX(taglen, 4)) == 0) {
			job_msg.min_sockets =
				(uint16_t) strtol(val, (char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(tag, "ReqCores", MAX(taglen, 4)) == 0) {
			job_msg.min_cores =
				(uint16_t) strtol(val, (char **) NULL, 10);
			update_cnt++;
		}
                else if (strncasecmp(tag, "TasksPerNode", MAX(taglen, 2))==0) {
                        job_msg.ntasks_per_node =
                                (uint16_t) strtol(val, (char **) NULL, 10);
                        update_cnt++;
                }
		else if (strncasecmp(tag, "ReqThreads", MAX(taglen, 4)) == 0) {
			job_msg.min_threads =
				(uint16_t) strtol(val, (char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(tag, "MinCPUsNode", MAX(taglen, 4)) == 0) {
			job_msg.job_min_cpus =
				(uint32_t) strtol(val, (char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(tag, "MinMemoryNode",
				     MAX(taglen, 10)) == 0) {
			job_msg.job_min_memory =
				(uint32_t) strtol(val, (char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(tag, "MinMemoryCPU",
				     MAX(taglen, 10)) == 0) {
			job_msg.job_min_memory =
				(uint32_t) strtol(val, (char **) NULL, 10);
			job_msg.job_min_memory |= MEM_PER_CPU;
			update_cnt++;
		}
		else if (strncasecmp(tag, "MinTmpDiskNode",
				     MAX(taglen, 5)) == 0) {
			job_msg.job_min_tmp_disk =
				(uint32_t) strtol(val, (char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(tag, "PartitionName",
				     MAX(taglen, 2)) == 0) {
			job_msg.partition = val;
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
		else if (strncasecmp(tag, "Shared", MAX(taglen, 2)) == 0) {
			if (strncasecmp(val, "YES", MAX(vallen, 1)) == 0)
				job_msg.shared = 1;
			else if (strncasecmp(val, "NO", MAX(vallen, 1)) == 0)
				job_msg.shared = 0;
			else
				job_msg.shared =
					(uint16_t) strtol(val,
							(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(tag, "Contiguous", MAX(taglen, 3)) == 0) {
			if (strncasecmp(val, "YES", MAX(vallen, 1)) == 0)
				job_msg.contiguous = 1;
			else if (strncasecmp(val, "NO", MAX(vallen, 1)) == 0)
				job_msg.contiguous = 0;
			else
				job_msg.contiguous =
					(uint16_t) strtol(val,
							(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(tag, "ExcNodeList", MAX(taglen, 1)) == 0){
			job_msg.exc_nodes = val;
			update_cnt++;
		}
		else if (strncasecmp(tag, "ReqNodeList", MAX(taglen, 8)) == 0){
			job_msg.req_nodes = val;
			update_cnt++;
		}
		else if (strncasecmp(tag, "Features", MAX(taglen, 1)) == 0) {
			job_msg.features = val;
			update_cnt++;
		}
		else if (strncasecmp(tag, "Account", MAX(taglen, 1)) == 0) {
			job_msg.account = val;
			update_cnt++;
		}
		else if (strncasecmp(tag, "Dependency", MAX(taglen, 1)) == 0) {
			job_msg.dependency = val;
			update_cnt++;
		}
#ifdef HAVE_BG
		else if (strncasecmp(tag, "Geometry", MAX(taglen, 1)) == 0) {
			char* token, *delimiter = ",x", *next_ptr;
			int j, rc = 0;
			uint16_t geo[SYSTEM_DIMENSIONS];
			char* geometry_tmp = xstrdup(val);
			char* original_ptr = geometry_tmp;
			token = strtok_r(geometry_tmp, delimiter, &next_ptr);
			for (j=0; j<SYSTEM_DIMENSIONS; j++) {
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
				for (j=0; j<SYSTEM_DIMENSIONS; j++)
					job_msg.geometry[j] = geo[j];
				update_cnt++;
			}
		}

		else if (strncasecmp(tag, "Rotate", MAX(taglen, 2)) == 0) {
			uint16_t rotate;
			if (strncasecmp(val, "YES", MAX(vallen, 1)) == 0)
				rotate = 1;
			else if (strncasecmp(val, "NO", MAX(vallen, 1)) == 0)
				rotate = 0;
			else
				rotate = (uint16_t) strtol(val,
							   (char **) NULL, 10);
			job_msg.rotate = rotate;
			update_cnt++;
		}
		else if (strncasecmp(tag, "Conn-Type", MAX(taglen, 2)) == 0) {
			job_msg.conn_type = verify_conn_type(val);
			if(job_msg.conn_type != (uint16_t)NO_VAL)
				update_cnt++;
		}
#endif
		else if (strncasecmp(tag, "Licenses", MAX(taglen, 1)) == 0) {
			job_msg.licenses = val;
			update_cnt++;
		}
		else if (!strncasecmp(tag, "EligibleTime", MAX(taglen, 2)) ||
			 !strncasecmp(tag, "StartTime",    MAX(taglen, 2))) {
			job_msg.begin_time = parse_time(val, 0);
			if(job_msg.begin_time < time(NULL))
				job_msg.begin_time = time(NULL);
			update_cnt++;
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
	else
		return 0;
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

