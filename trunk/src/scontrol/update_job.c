/*****************************************************************************\
 *  update_job.c - update job functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#include "scontrol.h"

/* 
 * scontrol_checkpoint - perform some checkpoint/resume operation
 * IN op - checkpoint operation
 * IN job_step_id_str - either a job name (for all steps of the given job) or 
 *			a step name: "<jid>.<step_id>"
 * RET 0 if no slurm error, errno otherwise. parsing error prints 
 *			error message and returns 0
 */
extern int 
scontrol_checkpoint(char *op, char *job_step_id_str)
{
	int rc = SLURM_SUCCESS;
	uint32_t job_id = 0, step_id = 0, step_id_set = 0;
	char *next_str;
	uint32_t ckpt_errno;
	char *ckpt_strerror = NULL;

	if (job_step_id_str) {
		job_id = (uint32_t) strtol (job_step_id_str, &next_str, 10);
		if (next_str[0] == '.') {
			step_id = (uint32_t) strtol (&next_str[1], &next_str, 10);
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

	if (strncasecmp(op, "able", 2) == 0) {
		time_t start_time;
		rc = slurm_checkpoint_able (job_id, step_id, &start_time);
		if (rc == SLURM_SUCCESS) {
			if (start_time) {
				char buf[128], time_str[32];
				slurm_make_time_str(&start_time, time_str,
					sizeof(time_str));
				snprintf(buf, sizeof(buf), 
					"Began at %s\n", time_str); 
				printf(buf);
			} else
				printf("Yes\n");
		} else if (slurm_get_errno() == ESLURM_DISABLED) {
			printf("No\n");
			rc = SLURM_SUCCESS;	/* not real error */
		}
	}
	else if (strncasecmp(op, "complete", 3) == 0) {
		/* Undocumented option used for testing purposes */
		static uint32_t error_code = 1;
		char error_msg[64];
		sprintf(error_msg, "test error message %d", error_code);
		rc = slurm_checkpoint_complete(job_id, step_id, (time_t) 0,
			error_code++, error_msg);
	}
	else if (strncasecmp(op, "disable", 3) == 0)
		rc = slurm_checkpoint_disable (job_id, step_id);
	else if (strncasecmp(op, "enable", 2) == 0)
		rc = slurm_checkpoint_enable (job_id, step_id);
	else if (strncasecmp(op, "create", 2) == 0)
		rc = slurm_checkpoint_create (job_id, step_id, CKPT_WAIT);
	else if (strncasecmp(op, "vacate", 2) == 0)
		rc = slurm_checkpoint_vacate (job_id, step_id, CKPT_WAIT);
	else if (strncasecmp(op, "restart", 2) == 0)
		rc = slurm_checkpoint_restart (job_id, step_id);
	else if (strncasecmp(op, "error", 2) == 0) {
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

	if (strncasecmp(op, "suspend", 3) == 0)
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
 * scontrol_update_job - update the slurm job configuration per the supplied arguments 
 * IN argc - count of arguments
 * IN argv - list of arguments
 * RET 0 if no slurm error, errno otherwise. parsing error prints 
 *			error message and returns 0
 */
extern int
scontrol_update_job (int argc, char *argv[]) 
{
	int i, update_cnt = 0;
	job_desc_msg_t job_msg;

	slurm_init_job_desc_msg (&job_msg);	

	for (i=0; i<argc; i++) {
		if (strncasecmp(argv[i], "JobId=", 6) == 0)
			job_msg.job_id = 
				(uint32_t) strtol(&argv[i][6], 
						 (char **) NULL, 10);
		else if (strncasecmp(argv[i], "Comment=", 8) == 0) {
			job_msg.comment = &argv[i][8];
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "TimeLimit=", 10) == 0) {
			int time_limit = time_str2mins(&argv[i][10]);
			if ((time_limit < 0) && (time_limit != INFINITE)) {
				error("Invalid TimeLimit value");
				exit_code = 1;
				return 0;
			}
			job_msg.time_limit = time_limit;
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Priority=", 9) == 0) {
			job_msg.priority = 
				(uint32_t) strtoll(&argv[i][9], 
						(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Nice=", 5) == 0) {
			int nice;
			nice = strtoll(&argv[i][5], (char **) NULL, 10);
			if (abs(nice) > NICE_OFFSET) {
				error("Invalid nice value, must be between "
					"-%d and %d", NICE_OFFSET, NICE_OFFSET);
				exit_code = 1;
				return 0;
			}
			job_msg.nice = NICE_OFFSET + nice;
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Nice", 4) == 0) {
			job_msg.nice = NICE_OFFSET + 100;
			update_cnt++;
		}		
		else if (strncasecmp(argv[i], "ReqProcs=", 9) == 0) {
			job_msg.num_procs = 
				(uint32_t) strtol(&argv[i][9], 
						(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Requeue=", 8) == 0) {
			job_msg.requeue = 
				(uint16_t) strtol(&argv[i][8], 
						(char **) NULL, 10);
			update_cnt++;
		}
		else if ((strncasecmp(argv[i], "MinNodes=", 9) == 0) ||
		         (strncasecmp(argv[i], "ReqNodes=", 9) == 0)) {
			char *tmp;
			job_msg.min_nodes = 
				(uint32_t) strtol(&argv[i][9],
						 &tmp, 10);
			if (tmp[0] == '-') {
				job_msg.max_nodes = (uint32_t)
					strtol(&tmp[1], (char **) NULL, 10);
				if (job_msg.max_nodes < job_msg.min_nodes) {
					error("Maximum node count less than "
						"minimum value (%u < %u)",
						job_msg.max_nodes,
						job_msg.min_nodes);
				}
			}
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "ReqSockets=", 11) == 0) {
			job_msg.min_sockets = 
				(uint16_t) strtol(&argv[i][11],
						 (char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "ReqCores=", 9) == 0) {
			job_msg.min_cores = 
				(uint16_t) strtol(&argv[i][9],
						 (char **) NULL, 10);
			update_cnt++;
		}
                else if (strncasecmp(argv[i], "TasksPerNode=", 13) == 0) {
                        job_msg.ntasks_per_node =
                                (uint16_t) strtol(&argv[i][13],
                                                 (char **) NULL, 10);
                        update_cnt++;
                }
		else if (strncasecmp(argv[i], "ReqThreads=", 11) == 0) {
			job_msg.min_threads = 
				(uint16_t) strtol(&argv[i][11],
						 (char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "MinProcs=", 9) == 0) {
			job_msg.job_min_procs = 
				(uint32_t) strtol(&argv[i][9], 
						(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "MinSockets=", 11) == 0) {
			job_msg.job_min_sockets = 
				(uint16_t) strtol(&argv[i][11], 
						(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "MinCores=", 9) == 0) {
			job_msg.job_min_cores = 
				(uint16_t) strtol(&argv[i][9], 
						(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "MinThreads=", 11) == 0) {
			job_msg.job_min_threads = 
				(uint16_t) strtol(&argv[i][11], 
						(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "MinMemoryNode=", 14) == 0) {
			job_msg.job_min_memory = 
				(uint32_t) strtol(&argv[i][14], 
						(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "MinMemoryCPU=", 13) == 0) {
			job_msg.job_min_memory =
				(uint32_t) strtol(&argv[i][13],
						(char **) NULL, 10);
			job_msg.job_min_memory |= MEM_PER_CPU;
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "MinTmpDisk=", 11) == 0) {
			job_msg.job_min_tmp_disk = 
				(uint32_t) strtol(&argv[i][11], 
						(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Partition=", 10) == 0) {
			job_msg.partition = &argv[i][10];
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Name=", 5) == 0) {
			job_msg.name = &argv[i][5];
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Shared=", 7) == 0) {
			if (strcasecmp(&argv[i][7], "YES") == 0)
				job_msg.shared = 1;
			else if (strcasecmp(&argv[i][7], "NO") == 0)
				job_msg.shared = 0;
			else
				job_msg.shared = 
					(uint16_t) strtol(&argv[i][7], 
							(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Contiguous=", 11) == 0) {
			if (strcasecmp(&argv[i][11], "YES") == 0)
				job_msg.contiguous = 1;
			else if (strcasecmp(&argv[i][11], "NO") == 0)
				job_msg.contiguous = 0;
			else
				job_msg.contiguous = 
					(uint16_t) strtol(&argv[i][11], 
							(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "ExcNodeList=", 12) == 0) {
			job_msg.exc_nodes = &argv[i][12];
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "ReqNodeList=", 12) == 0) {
			job_msg.req_nodes = &argv[i][12];
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Features=", 9) == 0) {
			job_msg.features = &argv[i][9];
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Account=", 8) == 0) {
			job_msg.account = &argv[i][8];
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Dependency=", 11) == 0) {
			job_msg.dependency = &argv[i][11];
			update_cnt++;
		}
#ifdef HAVE_BG
		else if (strncasecmp(argv[i], "Geometry=", 9) == 0) {
			char* token, *delimiter = ",x", *next_ptr;
			int j, rc = 0;
			uint16_t geo[SYSTEM_DIMENSIONS];
			char* geometry_tmp = xstrdup(&argv[i][9]);
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

		else if (strncasecmp(argv[i], "Rotate=", 7) == 0) {
			uint16_t rotate;
			if (strcasecmp(&argv[i][7], "yes") == 0)
				rotate = 1;
			else if (strcasecmp(&argv[i][7], "no") == 0)
				rotate = 0;
			else
				rotate = (uint16_t) strtol(&argv[i][7], 
							   (char **) NULL, 10);
			job_msg.rotate = rotate;
			update_cnt++;
		}
#endif
		else if (strncasecmp(argv[i], "Licenses=", 9) == 0) {
			job_msg.licenses = &argv[i][9];
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "StartTime=", 10) == 0) {
			job_msg.begin_time = parse_time(&argv[i][10]);
			update_cnt++;
		}
		else {
			exit_code = 1;
			fprintf (stderr, "Invalid input: %s\n", argv[i]);
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
	char message[256];

	job_id = atoi(argv[0]);
	if (job_id <= 0) {
		fprintf(stderr, "Invalid job_id %s", argv[0]);
		return 1;
	}

	message[0] = '\0';
	for (i=1; i<argc; i++) {
		if (i > 1)
			strncat(message, " ", sizeof(message));
		strncat(message, argv[i], sizeof(message));
	}
			
	if (slurm_notify_job(job_id, message))
		return slurm_get_errno ();
	else
		return 0;
}

