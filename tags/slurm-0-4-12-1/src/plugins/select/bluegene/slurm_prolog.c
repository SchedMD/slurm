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
int max_delay=MIN_DELAY;
int cur_delay=0; 

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
	int i, j, rc1, rc2, num_parts;
	rm_partition_t *part_ptr;
	rm_partition_state_t state;
	rm_partition_state_flag_t part_state = 7;
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
			
			if ((rc1 = rm_get_partitions_info(part_state, &part_list))
			    != STATUS_OK) {
				fprintf(stderr, "rm_get_partitions() errno=%d\n", 
					rc1);
				
			}
			rm_get_data(part_list, RM_PartListSize, &num_parts);
			for(j=0; j<num_parts; j++) {
				if(j)
					rm_get_data(part_list, RM_PartListNextPart, &part_ptr);
				else
					rm_get_data(part_list, RM_PartListFirstPart, &part_ptr);
				rm_get_data(part_ptr, RM_PartitionID, &name);
				if(!strcasecmp(part_name, name)) {
					rc1 = rm_get_data(part_ptr, RM_PartitionState, &state);
					break;
				}
			}
			rm_free_partition_list(part_list);
			
		} else {	       
			rc1 = rm_get_partition(part_name, &part_ptr);
			if (rc1 == PARTITION_NOT_FOUND)
				continue;	/* wait for creation */
			if (rc1 != STATUS_OK) {
				fprintf(stderr, "rm_get_partition(%s) errno=%d\n", 
					part_name, rc1);
				return;
			}
			
			if (max_delay == MIN_DELAY) {
				int bp;
				rc1 = rm_get_data(part_ptr, RM_PartitionBPNum, &bp);
				if (rc1 != STATUS_OK)
					fprintf(stderr, "rm_get_data(%s, "
						"RM_PartitionBPNum) errno=%d\n",
						part_name, rc1);
				else {
					max_delay += (INCR_DELAY * bp);
					if (max_delay == MIN_DELAY)
						max_delay++;	/* avoid re-test */
				}
			}
			rc1 = rm_get_data(part_ptr, RM_PartitionState, &state);
			rc2 = rm_free_partition(part_ptr);
			if (rc1 != STATUS_OK) {
				fprintf(stderr, 
					"rm_get_data(%s, RM_PartitionState) errno=%d\n", 
					part_name, rc1);
				return;
			}
			if (rc2 != STATUS_OK)
				fprintf(stderr, "rm_free_partition() errno=%d\n", rc2);
		
#if (_DEBUG > 1)
			printf("\nstate=%s\n",_part_state_str(state));
#endif
		}

		if ((state == RM_PARTITION_READY) 
		    ||  (state == RM_PARTITION_ERROR)) {
			is_ready = 1;
			break;
		}
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
	int i, j, rc1, num_parts;
	rm_partition_t *part_ptr;
	char *name;
	struct passwd *pw_ent;
	int is_ready = 0;
	rm_partition_state_flag_t part_state = 7;
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
		if ((rc1 = rm_get_partitions_info(part_state, &part_list))
		    != STATUS_OK) {
			fprintf(stderr, "rm_get_partitions() errno=%d\n", 
				rc1);
				
		}
		rm_get_data(part_list, RM_PartListSize, &num_parts);
		for(j=0; j<num_parts; j++) {
			if(j)
				rm_get_data(part_list, RM_PartListNextPart, &part_ptr);
			else
				rm_get_data(part_list, RM_PartListFirstPart, &part_ptr);
			rm_get_data(part_ptr, RM_PartitionID, &name);
			if(!strcasecmp(part_name, name)) {
				rc1 = rm_get_data(part_ptr, RM_PartitionUserName, &name);
				if (name[0] == '\0')
					continue;
				if ((pw_ent = getpwnam(name)) == NULL) {
					fprintf(stderr, "getpwnam(%s) errno=%d\n", name, 
						errno);
					continue;
				}
#if (_DEBUG > 1)
				printf("\nowner = %s(%d)\n", name, pw_ent->pw_uid);
#endif
				if (pw_ent->pw_uid == target_uid) {
					is_ready = 1;
					break;
				}
			}
		}
		rm_free_partition_list(part_list);
		if(is_ready)
			break;
	
		/* if ((rc1 = rm_get_partition(part_name, &part_ptr)) !=  */
/* 				STATUS_OK) { */
/* 			fprintf(stderr, "rm_get_partition(%s) errno=%d\n", */
/* 				part_name, rc1); */
/* 			return; */
/* 		} */
/* 		rc1 = rm_get_data(part_ptr, RM_PartitionUserName, &name); */
/* 		rc2 = rm_free_partition(part_ptr); */
/* 		if (rc1 != STATUS_OK) { */
/* 			fprintf(stderr, */
/* 				"rm_get_data(%s, RM_PartitionUserName) " */
/* 				"errno=%d\n", part_name, rc1); */
/* 			return; */
/* 		} */
/* 		if (rc2 != STATUS_OK) */
/* 			fprintf(stderr, "rm_free_partition() errno=%d\n", rc2); */

		/* Now test this owner */
		/* if (name[0] == '\0') */
/* 			continue; */
/* 		if ((pw_ent = getpwnam(name)) == NULL) { */
/* 			fprintf(stderr, "getpwnam(%s) errno=%d\n", part_name,  */
/* 				errno); */
/* 			continue; */
/* 		} */
/* #if (_DEBUG > 1) */
/* 		printf("\nowner = %s(%d)\n", name, pw_ent->pw_uid); */
/* #endif */
/* 		if (pw_ent->pw_uid == target_uid) { */
/* 			is_ready = 1; */
/* 			break; */
/* 		} */
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

#endif  /* HAVE_BGL_FILES */
