/*****************************************************************************\
 *  bgl_part_info.c - blue gene partition information from the db2 database.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  endif
#  if WITH_PTHREADS
#    include <pthread.h>
#  endif
#endif

#include <signal.h>
#include <unistd.h>

#include <slurm/slurm_errno.h>

#include <pwd.h>
#include <sys/types.h>
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/slurmctld/proc_req.h"
#include "bluegene.h"

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

static int _wait_part_ready(char *part_name);
static int _wait_part_owner(char *part_name, int user_id);

/* Pack all relevent information about a partition */
extern void pack_partition(bgl_record_t *bgl_record, Buf buffer)
{
	pack_time(bgl_record->time_last_change, buffer);
	packstr(bgl_record->nodes, buffer);
	packstr(bgl_record->owner_name, buffer);
	packstr(bgl_record->bgl_part_id, buffer);
	pack32(bgl_record->state, buffer);
	pack32(bgl_record->conn_type, buffer);
	pack32(bgl_record->node_use, buffer);	
}

/* unpack all relevent information about a partition */
extern void unpack_partition(bgl_record_t *bgl_record, Buf buffer)
{
	uint16_t uint16_tmp;
	safe_unpack_time(&bgl_record->time_last_change, buffer);
unpack_error:
	safe_unpackstr_xmalloc(&bgl_record->nodes, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc(&bgl_record->owner_name, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc(&bgl_record->bgl_part_id, &uint16_tmp, buffer);
	safe_unpack32(&bgl_record->state, buffer);
	safe_unpack32(&bgl_record->conn_type, buffer);
	safe_unpack32(&bgl_record->node_use, buffer); 
	
}

/*
 * check to see if partition is ready to execute.  Meaning
 * User is added to the list of users able to run, and no one 
 * else is running on the partition.
 *
 * NOTE: This happens in parallel with srun and slurmd spawning
 * the job. A prolog script is expected to defer initiation of
 * the job script until the BGL block is available for use.
 */
extern int part_ready(struct job_record *job_ptr)
{
	int rc1 = 0, rc2 = 0;
	
#ifdef HAVE_BGL_FILES
	
	/* bgl_update_ptr->uid = job_ptr->user_id; */
/* 	bgl_update_ptr->job_id = job_ptr->job_id; */
	
	rc1 = _wait_part_ready(job_ptr->partition);
		
	rc2 = _wait_part_owner(job_ptr->partition, job_ptr->user_id);

	if(!rc1 && !rc2)
		return 0;
	else if(rc1 && rc2)
		return 1;
	else
		return -1;

#endif
	return 0;
}

static int _wait_part_ready(char *part_name)
{
	int j, rc, num_parts;
	rm_partition_t *part_ptr;
	rm_partition_state_t state;
	rm_partition_state_flag_t part_state = PARTITION_ALL_FLAG;
	int is_ready = 0;
	char *name;
	rm_partition_list_t *part_list;

	if ((rc = rm_get_partitions_info(part_state, &part_list))
	    != STATUS_OK) {
		error("rm_get_partitions(): %s\n", 
			bgl_err_str(rc));
		is_ready = -1; 
	} 
	if ((rc = rm_get_data(part_list, RM_PartListSize, &num_parts))
	    != STATUS_OK) {
		error("rm_get_data(RM_PartListSize): %s\n",
			bgl_err_str(rc));
		is_ready = -1;
		num_parts = 0;
	}

	for (j=0; j<num_parts; j++) {
		if (j) {
			if ((rc = rm_get_data(part_list,
					      RM_PartListNextPart, &part_ptr))
			    != STATUS_OK) {
				error("rm_get_data("
					"RM_PartListNextPart): %s\n",
					bgl_err_str(rc));
				is_ready = -1;
				break;
			}
		} else {
			if ((rc = rm_get_data(part_list,
					      RM_PartListFirstPart, &part_ptr))
			    != STATUS_OK) {
				error("rm_get_data("
					"RM_PartListFirstPart: %s\n",
					bgl_err_str(rc));
				is_ready = -1;
				break;
			}
		}

		if ((rc = rm_get_data(part_ptr, RM_PartitionID, &name))
		    != STATUS_OK) {
			fprintf(stderr,
				"rm_get_data(RM_PartitionID): %s\n",
				bgl_err_str(rc));
			is_ready = -1;
			break;
		}

		if (!strcmp(part_name, name))
			break;
	}
	if(is_ready != -1) {
		if ((rc = rm_get_data(part_ptr, RM_PartitionState, &state))
		    != STATUS_OK) {
			fprintf(stderr,
				"rm_get_data(RM_PartitionState): %s\n",
				bgl_err_str(rc));
			is_ready = -1;
		}
		
		if ((state == RM_PARTITION_READY)
		    ||  (state == RM_PARTITION_ERROR))
			is_ready = 1;
	}
	if ((rc = rm_free_partition_list(part_list)) 
	    != STATUS_OK) {
		error("rm_free_partition_list(): %s\n",
			bgl_err_str(rc));
	}

	return is_ready;
			
}

/* Partition owner should be set when partition is ready, don't 
 * have long delays */
static int _wait_part_owner(char *part_name, int user_id)
{
	int j, rc, num_parts;
	rm_partition_t *part_ptr;
	char *name;
	struct passwd *pw_ent;
	int is_ready = 0;
	rm_partition_state_flag_t part_state = PARTITION_ALL_FLAG;
	rm_partition_list_t *part_list;
	
	
	if ((rc = rm_get_partitions_info(part_state, &part_list))
	    != STATUS_OK) {
		error("rm_get_partitions(): %s\n", 
			bgl_err_str(rc));
		is_ready = -1;
	}
	
	if ((rc = rm_get_data(part_list, RM_PartListSize, &num_parts))
	    != STATUS_OK) {
		error("rm_get_data(RM_PartListSize): %s\n",
			bgl_err_str(rc)); 
		is_ready = -1;
		num_parts = 0; 
	}
	
	for (j=0; j<num_parts; j++) {
		if (j) {
			if ((rc = rm_get_data(part_list,
					      RM_PartListNextPart, &part_ptr))
			    != STATUS_OK) {
				error("rm_get_data(RM_PartListNextPart): %s\n",
				      bgl_err_str(rc));
				is_ready = -1;
				break;
			}
		} else {
			if ((rc = rm_get_data(part_list,
					      RM_PartListFirstPart, &part_ptr))
			    != STATUS_OK) {
				error("rm_get_data(RM_PartListFirstPart: %s\n",
				      bgl_err_str(rc));
				is_ready = -1;
				break;
			}
		}

		if ((rc = rm_get_data(part_ptr, RM_PartitionID, &name))
		    != STATUS_OK) {
			error("rm_get_data(RM_PartitionID): %s\n",
			      bgl_err_str(rc));
			is_ready = -1;
			break;
		}
		if (!strcmp(part_name, name))
			break;
	}
	if(is_ready != -1) {
		if ((rc = rm_get_data(part_ptr, RM_PartitionUserName, 
				      &name)) != STATUS_OK) {
			fprintf(stderr,
				"rm_get_data(RM_PartitionUserName): %s\n",
				bgl_err_str(rc));
			is_ready = -1;
		}
		
		if (name[0] == '\0')
			is_ready = -1;
		if ((pw_ent = getpwnam(name)) == NULL) {
			error("getpwnam(%s) errno=%d\n",
			      name, errno);
			is_ready = -1;
		}
		if (pw_ent->pw_uid == user_id)
			is_ready = 1;
	}
	
	if ((rc = rm_free_partition_list(part_list)) != STATUS_OK) {
		error("rm_free_partition_list(): %s\n",
			bgl_err_str(rc));
	}

	return is_ready;
}
