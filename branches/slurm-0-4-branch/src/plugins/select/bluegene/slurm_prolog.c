/*****************************************************************************\
 * slurm_ prolog.c - Wait until the specified partition is ready and owned by 
 *	this user. This is executed via SLURM to synchronize the user's job 
 *	execution with slurmctld configuration of partitions.
 *
 * NOTE: execute "/bgl/BlueLight/ppcfloor/bglsys/bin/db2profile" first
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-2002-040.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif
#include <stdlib.h>

#ifndef HAVE_BGL_FILES

/* Just a stub, no synchronization to perform */
int main(int argc, char *argv[])
{
	exit(0);
}

#else

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include "src/plugins/select/bluegene/wrap_rm_api.h"

#define _DEBUG 0

/*
 * Check the bglblock's status every POLL_SLEEP seconds. 
 * Retry for a period of MIN_DELAY + (INCR_DELAY * base partition count).
 * For example if MIN_DELAY=300 and INCR_DELAY=20, wait up to 428 seconds
 * for a 16 base partition bglblock to ready (300 + 20 * 16).
 */ 
#define POLL_SLEEP 3			/* retry interval in seconds  */
#define MIN_DELAY  300			/* time in seconds */
#define INCR_DELAY 20			/* time in seconds per BP */

int max_delay = MIN_DELAY;
int cur_delay = 0; 

static char *bgl_err_str(status_t inx);
static char *_part_state_str(rm_partition_state_t state);
static void  _wait_part_ready(char *part_name);
static void  _wait_part_owner(char *part_name, char *user_id);

int main(int argc, char *argv[])
{
	char *job_id = NULL, *part_name = NULL, *user_id = NULL;

	job_id = getenv("SLURM_JOBID");		/* get SLURM job ID */
	if (!job_id)
		fprintf(stderr, "SLURM_JOBID not set\n");

	part_name = getenv("MPIRUN_PARTITION");	/* get partition ID */
	if (!part_name) {
		fprintf(stderr, "MPIRUN_PARTITION not set for job %s\n",
			job_id);
		exit(0);
	}

	_wait_part_ready(part_name);

	user_id = getenv("SLURM_UID");          /* get SLURM user ID */
	if (!user_id)
		fprintf(stderr, "SLURM_UID not set for job %s\n", job_id);
	else
		_wait_part_owner(part_name, user_id);
	exit(0);
}

static void _wait_part_ready(char *part_name)
{
	int i, j, rc, num_parts;
	rm_partition_t *part_ptr;
	rm_partition_state_t state;
	rm_partition_state_flag_t part_state = PARTITION_ALL_FLAG;
	int is_ready = 0;
	char *name;
	rm_partition_list_t *part_list;
	
#if _DEBUG
	printf("Waiting for partition %s to become ready.", part_name);
#endif

	for (i=0; (cur_delay < max_delay); i++) {
		if (i) {
			sleep(POLL_SLEEP);
			cur_delay += POLL_SLEEP;
#if _DEBUG
			printf(".");
#endif
		}
		if (max_delay != MIN_DELAY) {	/* already have partition size */
			if ((rc = rm_get_partitions_info(part_state, &part_list))
					!= STATUS_OK) {
				fprintf(stderr, "rm_get_partitions(): %s\n", 
					bgl_err_str(rc));
				continue; 
			} 
			if ((rc = rm_get_data(part_list, RM_PartListSize, &num_parts))
					!= STATUS_OK) {
				fprintf(stderr, "rm_get_data(RM_PartListSize): %s\n",
					bgl_err_str(rc));
				num_parts = 0;
			}

			for (j=0; j<num_parts; j++) {
				if (j) {
					if ((rc = rm_get_data(part_list,
							RM_PartListNextPart, &part_ptr))
							!= STATUS_OK) {
						fprintf(stderr, "rm_get_data("
							"RM_PartListNextPart): %s\n",
							bgl_err_str(rc));
						break;
					}
				} else {
					if ((rc = rm_get_data(part_list,
							RM_PartListFirstPart, &part_ptr))
							!= STATUS_OK) {
						fprintf(stderr, "rm_get_data("
							"RM_PartListFirstPart: %s\n",
							bgl_err_str(rc));
						break;
					}
				}

				if ((rc = rm_get_data(part_ptr, RM_PartitionID, &name))
						!= STATUS_OK) {
					fprintf(stderr,
						"rm_get_data(RM_PartitionID): %s\n",
						bgl_err_str(rc));
					continue;
				}

				if (strcmp(part_name, name) != 0)
					continue;

				if ((rc = rm_get_data(part_ptr, RM_PartitionState, &state))
						!= STATUS_OK) {
					fprintf(stderr,
						"rm_get_data(RM_PartitionState): %s\n",
						bgl_err_str(rc));
					break;
				}
				
				if ((state == RM_PARTITION_READY)
				||  (state == RM_PARTITION_ERROR))
					is_ready = 1;
				break;
			}
			if ((rc = rm_free_partition_list(part_list)) 
					!= STATUS_OK) {
				fprintf(stderr, 
					"rm_free_partition_list(): %s\n",
					bgl_err_str(rc));
			}
			
		} else {	/* Need to get partition size */
			int bp;
	       
			if ((rc = rm_get_partition(part_name, &part_ptr)) 
					!= STATUS_OK) {
				fprintf(stderr, "rm_get_partition(%s): %s\n", 
					part_name, bgl_err_str(rc));
				continue;
			}
			
			if ((rc = rm_get_data(part_ptr, RM_PartitionBPNum, &bp))
					!= STATUS_OK) {
				fprintf(stderr, "rm_get_data(%s, "
					"RM_PartitionBPNum) %s\n",
					part_name, bgl_err_str(rc));
			} else {
				max_delay += (INCR_DELAY * bp);
				if (max_delay == MIN_DELAY)
					max_delay++;	/* avoid re-test */
			}
			if ((rc = rm_get_data(part_ptr, RM_PartitionState, &state))
					!= STATUS_OK) {
				fprintf(stderr,
					"rm_get_data(RM_PartitionState): %s\n",
					bgl_err_str(rc));
			} else {
				if ((state == RM_PARTITION_READY)
				||  (state == RM_PARTITION_ERROR))
					is_ready = 1;
			}
			if ((rc = rm_free_partition(part_ptr)) != STATUS_OK) {
				fprintf(stderr, "rm_free_partition(): %s\n", 
					bgl_err_str(rc));
			}
#if (_DEBUG > 1)
			printf("\nstate=%s\n",_part_state_str(state));
#endif
		}

		if (is_ready)
			break;
	}

#if _DEBUG
	if (is_ready == 0)
		printf("\n");
	else
     	   printf("\nPartition %s is ready.\n", part_name);
#endif
	if (is_ready == 0)
		fprintf(stderr, "Partition state not ready (%s)\n",
			_part_state_str(state));

}

static char *_part_state_str(rm_partition_state_t state)
{
	static char tmp[16];

	switch (state) {
		case RM_PARTITION_BUSY: 
			return "RM_PARTITION_BUSY";
		case RM_PARTITION_CONFIGURING:
			return "RM_PARTITION_CONFIGURING";
		case RM_PARTITION_DEALLOCATING:
			return "RM_PARTITION_DEALLOCATING";
		case RM_PARTITION_ERROR:
			return "RM_PARTITION_ERROR";
		case RM_PARTITION_FREE:
			return "RM_PARTITION_FREE";
		case RM_PARTITION_NAV:
			return "RM_PARTITION_NAV";
		case RM_PARTITION_READY:
			return "RM_PARTITION_READY";
		default:
			snprintf(tmp, sizeof(tmp), "%d", state);
			return tmp;
	}
}

/* Partition owner should be set when partition is ready, don't 
 * have long delays */
static void  _wait_part_owner(char *part_name, char *user_id)
{
	uid_t target_uid;
	int i, j, rc, num_parts;
	rm_partition_t *part_ptr;
	char *name;
	struct passwd *pw_ent;
	int is_ready = 0;
	rm_partition_state_flag_t part_state = PARTITION_ALL_FLAG;
	rm_partition_list_t *part_list;
	
	target_uid = atoi(user_id);

#if _DEBUG
	printf("Waiting for partition %s owner to become %d.", part_name, 
		target_uid);
#endif

	for (i=0; (cur_delay < max_delay); i++) {
		if (i) {
			sleep(POLL_SLEEP);
			cur_delay += POLL_SLEEP;
#if _DEBUG
			printf(".");
#endif
		}
		if ((rc = rm_get_partitions_info(part_state, &part_list))
				!= STATUS_OK) {
			fprintf(stderr, "rm_get_partitions(): %s\n", 
				bgl_err_str(rc));
			continue;
		}

		if ((rc = rm_get_data(part_list, RM_PartListSize, &num_parts))
				!= STATUS_OK) {
			fprintf(stderr, "rm_get_data(RM_PartListSize): %s\n",
				bgl_err_str(rc)); 
			num_parts = 0; 
		}

		for (j=0; j<num_parts; j++) {
			if (j) {
				if ((rc = rm_get_data(part_list,
						RM_PartListNextPart, &part_ptr))
						!= STATUS_OK) {
					fprintf(stderr, 
						"rm_get_data(RM_PartListNextPart): %s\n",
						bgl_err_str(rc));
					break;
				}
			} else {
				if ((rc = rm_get_data(part_list,
						RM_PartListFirstPart, &part_ptr))
						!= STATUS_OK) {
					fprintf(stderr, 
						"rm_get_data(RM_PartListFirstPart: %s\n",
						bgl_err_str(rc));
					break;
				}
			}

			if ((rc = rm_get_data(part_ptr, RM_PartitionID, &name))
					!= STATUS_OK) {
				fprintf(stderr, 
					"rm_get_data(RM_PartitionID): %s\n",
					bgl_err_str(rc));
				continue;
			}
			if (strcmp(part_name, name) != 0)
				continue;

			if ((rc = rm_get_data(part_ptr, RM_PartitionUserName, 
					&name)) != STATUS_OK) {
				fprintf(stderr,
					"rm_get_data(RM_PartitionUserName): %s\n",
					bgl_err_str(rc));
				break;
			}

			if (name[0] == '\0')
				break;
			if ((pw_ent = getpwnam(name)) == NULL) {
				fprintf(stderr, "getpwnam(%s) errno=%d\n",
					name, errno);
				break;
			}
#if (_DEBUG > 1)
			printf("\nowner = %s(%d)\n", name, pw_ent->pw_uid);
#endif
			if (pw_ent->pw_uid == target_uid)
				is_ready = 1;
			break;
		}

		if ((rc = rm_free_partition_list(part_list)) != STATUS_OK) {
			fprintf(stderr, "rm_free_partition_list(): %s\n",
				bgl_err_str(rc));
		}

		if (is_ready)
			break;
	}

#if _DEBUG
	if (is_ready == 0)
		printf("\n");
	else
		printf("\nPartition %s owner is %d.\n", part_name, target_uid);
#endif
	if (is_ready == 0)
		fprintf(stderr, "Partition %s owner not changed (%s)\n", 
			part_name, name);
}

/* Temporary static function.
 * This module will get completely re-written for driver 140 */
static char *bgl_err_str(status_t inx)
{
        switch (inx) {
                case STATUS_OK:
                        return "Status OK";
                case PARTITION_NOT_FOUND:
                        return "Partition not found";
                case JOB_NOT_FOUND:
                        return "Job not found";
                case BP_NOT_FOUND:
                        return "Base partition not found";
                case SWITCH_NOT_FOUND:
                        return "Switch not found";
                case JOB_ALREADY_DEFINED:
                        return "Job already defined";
                case CONNECTION_ERROR:
                        return "Connection error";
                case INTERNAL_ERROR:
                        return "Internal error";
                case INVALID_INPUT:
                        return "Invalid input";
                case INCOMPATIBLE_STATE:
                        return "Incompatible state";
                case INCONSISTENT_DATA:
                        return "Inconsistent data";
        }

        return "?";
}

#endif  /* HAVE_BGL_FILES */
