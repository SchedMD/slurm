/*****************************************************************************\
 *  sfree.c - free specified partition or all partitions.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
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

#include "sfree.h"

#define MAX_POLL_RETRIES    110
#define POLL_INTERVAL        3

/* Globals */

char *bgl_part_id = NULL;
int all_parts = 0;

/************
 * Functions *
 ************/
#ifdef HAVE_BGL_FILES

	
static int _free_partition(char *bgl_part_id);
static int _update_bgl_record_state(char *bgl_part_id);
static void _term_jobs_on_part(char *bgl_part_id);
static char *_bgl_err_str(status_t inx);
static int _remove_job(db_job_id_t job_id);

int main(int argc, char *argv[])
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	rm_partition_list_t *part_list = NULL;
	rm_partition_state_flag_t part_state = PARTITION_ALL_FLAG;
	int j, num_parts = 0;
	rm_partition_t *part_ptr = NULL;
	int rc;

	log_init(xbasename(argv[0]), opts, SYSLOG_FACILITY_DAEMON, NULL);
	parse_command_line(argc, argv);
	if(!all_parts) {
		if(!bgl_part_id) {
			error("you need to specify a partition");
			exit(0);
		}
		_free_partition(bgl_part_id);
	} else {
		if ((rc = rm_get_partitions_info(part_state, &part_list))
		    != STATUS_OK) {
			error("rm_get_partitions_info(): %s", _bgl_err_str(rc));
			return -1; 
		}

		if ((rc = rm_get_data(part_list, RM_PartListSize, &num_parts))
		    != STATUS_OK) {
			error("rm_get_data(RM_PartListSize): %s", 
			      _bgl_err_str(rc));
			
			num_parts = 0;
		}
			
		for (j=0; j<num_parts; j++) {
			if (j) {
				if ((rc = rm_get_data(part_list, 
						      RM_PartListNextPart, 
						      &part_ptr)) 
				    != STATUS_OK) {
					error("rm_get_data"
					      "(RM_PartListNextPart): %s",
					      _bgl_err_str(rc));
					
					break;
				}
			} else {
				if ((rc = rm_get_data(part_list, 
						      RM_PartListFirstPart, 
						      &part_ptr)) 
				    != STATUS_OK) {
					error("rm_get_data"
					      "(RM_PartListFirstPart: %s",
					      _bgl_err_str(rc));
					
					break;
				}
			}
			if ((rc = rm_get_data(part_ptr, RM_PartitionID, 
					      &bgl_part_id))
			    != STATUS_OK) {
				error("rm_get_data(RM_PartitionID): %s", 
				      _bgl_err_str(rc));
				
				break;
			}
			if(strncmp("RMP", bgl_part_id, 3))
				continue;
			_free_partition(bgl_part_id);
		}
		if ((rc = rm_free_partition_list(part_list)) != STATUS_OK) {
			error("rm_free_partition_list(): %s",
			      _bgl_err_str(rc));
		}
	}
	return 0;
}

static int _free_partition(char *bgl_part_id)
{
	int state=-1;
	int rc;

	info("freeing partition %s", bgl_part_id);
	_term_jobs_on_part(bgl_part_id);
	while (1) {
		if((state = _update_bgl_record_state(bgl_part_id))
		   == SLURM_ERROR)
			break;
		if (state != RM_PARTITION_FREE 
		    && state != RM_PARTITION_DEALLOCATING) {
			info("pm_destroy %s",bgl_part_id);
			if ((rc = pm_destroy_partition(bgl_part_id)) 
			    != STATUS_OK) {
				if(rc == PARTITION_NOT_FOUND) {
					info("partition %s is not found");
					break;
				}
				error("pm_destroy_partition(%s): %s",
				      bgl_part_id, 
				      _bgl_err_str(rc));
			}
		}
		
		if ((state == RM_PARTITION_FREE)
		    ||  (state == RM_PARTITION_ERROR))
			break;
		sleep(3);
	}
	info("partition %s is freed", bgl_part_id);
	return SLURM_SUCCESS;
}

static int _update_bgl_record_state(char *bgl_part_id)
{
	rm_partition_state_flag_t part_state = PARTITION_ALL_FLAG;
	char *name = NULL;
	rm_partition_list_t *part_list = NULL;
	int j, rc,  num_parts = 0;
	rm_partition_state_t state = -1;
	rm_partition_t *part_ptr = NULL;
	
	if ((rc = rm_get_partitions_info(part_state, &part_list))
	    != STATUS_OK) {
		error("rm_get_partitions_info(): %s", _bgl_err_str(rc));
		return -1;
	}

	if ((rc = rm_get_data(part_list, RM_PartListSize, &num_parts))
	    != STATUS_OK) {
		error("rm_get_data(RM_PartListSize): %s", _bgl_err_str(rc));
		state = -1;
		num_parts = 0;
	}
			
	for (j=0; j<num_parts; j++) {
		if (j) {
			if ((rc = rm_get_data(part_list,
					      RM_PartListNextPart,
					      &part_ptr))
			    != STATUS_OK) {
				error("rm_get_data(RM_PartListNextPart): %s",
				      _bgl_err_str(rc));
				state = -1;
				break;
			}
		} else {
			if ((rc = rm_get_data(part_list,
					      RM_PartListFirstPart,
					      &part_ptr))
			    != STATUS_OK) {
				error("rm_get_data(RM_PartListFirstPart: %s",
				      _bgl_err_str(rc));
				state = -1;
				break;
			}
		}
		if ((rc = rm_get_data(part_ptr,
				      RM_PartitionID,
				      &name))
		    != STATUS_OK) {
			error("rm_get_data(RM_PartitionID): %s",
			      _bgl_err_str(rc));
			state = -1;
			break;
		}
		if(!strcmp(bgl_part_id, name))
			break;
	}
	
	if(state == -1)
		goto clean_up;
	else if(j>=num_parts) {
		error("This partition, %s, doesn't exist in MMCS",
		      bgl_part_id);
		state = -1;
		goto clean_up;
	}

	if ((rc = rm_get_data(part_ptr,
			      RM_PartitionState,
			      &state))
	    != STATUS_OK) {
		error("rm_get_data(RM_PartitionState): %s",
		      _bgl_err_str(rc));
	} 

clean_up:
	if ((rc = rm_free_partition_list(part_list)) != STATUS_OK) {
		error("rm_free_partition_list(): %s", _bgl_err_str(rc));
	}
	return state;
}

/* Perform job termination work */
static void _term_jobs_on_part(char *bgl_part_id)
{
	int i, jobs, rc, job_found = 0;
	rm_job_list_t *job_list;
	int live_states;
	rm_element_t *job_elem;
	pm_partition_id_t part_id;
	db_job_id_t job_id;
	
	//debug("getting the job info");
	live_states = JOB_ALL_FLAG
		& (~JOB_TERMINATED_FLAG)
		& (~JOB_KILLED_FLAG);
	if ((rc = rm_get_jobs(live_states, &job_list)) != STATUS_OK) {
		error("rm_get_jobs(): %s", _bgl_err_str(rc));
		return;
	}
	
	if ((rc = rm_get_data(job_list, RM_JobListSize, &jobs)) != STATUS_OK) {
		error("rm_get_data(RM_JobListSize): %s", _bgl_err_str(rc));
		jobs = 0;
	} else if (jobs > 300)
		fatal("Active job count (%d) invalid, restart MMCS", jobs);
	//debug("job count %d",jobs);
	for (i=0; i<jobs; i++) {
		if (i) {
			if ((rc = rm_get_data(job_list, RM_JobListNextJob,
					&job_elem)) != STATUS_OK) {
				error("rm_get_data(RM_JobListNextJob): %s",
				      _bgl_err_str(rc));
				continue;
			}
		} else {
			if ((rc = rm_get_data(job_list, RM_JobListFirstJob,
					      &job_elem)) != STATUS_OK) {
				error("rm_get_data(RM_JobListFirstJob): %s",
				      _bgl_err_str(rc));
				continue;
			}
		}
		
		if(!job_elem) {
			error("No Job Elem breaking out job count = %d\n",
			      jobs);
			break;
		}
		if ((rc = rm_get_data(job_elem, RM_JobPartitionID, &part_id))
		    != STATUS_OK) {
			error("rm_get_data(RM_JobPartitionID) %s: %s",
			      part_id, _bgl_err_str(rc));
			continue;
		}
		
		if (strcmp(part_id, bgl_part_id) != 0)
			continue;
		job_found = 1;
		if ((rc = rm_get_data(job_elem, RM_JobDBJobID, &job_id))
		    != STATUS_OK) {
			error("rm_get_data(RM_JobDBJobID): %s",
			      _bgl_err_str(rc));
			continue;
		}
		info("got job_id %d",job_id);
		if((rc = _remove_job(job_id))
		   == INTERNAL_ERROR)
			goto not_removed;
		
	}
	if(job_found == 0)
		info("No jobs on partition");
	
not_removed:
	if ((rc = rm_free_job_list(job_list)) != STATUS_OK)
		error("rm_free_job_list(): %s", _bgl_err_str(rc));
}

/*
 * Convert a BGL API error code to a string
 * IN inx - error code from any of the BGL Bridge APIs
 * RET - string describing the error condition
 */
static char *_bgl_err_str(status_t inx)
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

/* Kill a job and remove its record from MMCS */
static int _remove_job(db_job_id_t job_id)
{
	int i, rc;
	rm_job_t *job_rec = NULL;
	rm_job_state_t job_state;

	info("removing job %d from MMCS", job_id);
	for (i=0; i<MAX_POLL_RETRIES; i++) {
		if (i > 0)
			sleep(POLL_INTERVAL);

		/* Find the job */
		if ((rc = rm_get_job(job_id, &job_rec)) != STATUS_OK) {
			if (rc == JOB_NOT_FOUND) {
				debug("job %d removed from MMCS", job_id);
				return STATUS_OK;
			} 

			error("rm_get_job(%d): %s", job_id, 
			      _bgl_err_str(rc));
			continue;
		}

		if ((rc = rm_get_data(job_rec, RM_JobState, &job_state)) != 
				STATUS_OK) {
			(void) rm_free_job(job_rec);
			if (rc == JOB_NOT_FOUND) {
				debug("job %d not found in MMCS", job_id);
				return STATUS_OK;
			} 

			error("rm_get_data(RM_JobState) for jobid=%d "
			      "%s", job_id, _bgl_err_str(rc));
			continue;
		}
		if ((rc = rm_free_job(job_rec)) != STATUS_OK)
			error("rm_free_job: %s", _bgl_err_str(rc));

		info("job %d is in state %d", job_id, job_state);
		
		/* check the state and process accordingly */
		if(job_state == RM_JOB_TERMINATED)
			return STATUS_OK;
		else if(job_state == RM_JOB_DYING)
			continue;
		else if(job_state == RM_JOB_ERROR) {
			error("job %d is in a error state.", job_id);
			
			//free_bgl_partition();
			return STATUS_OK;
		} else {
			(void) jm_signal_job(job_id, SIGKILL);
			rc = jm_cancel_job(job_id);
		}
		
		if (rc != STATUS_OK) {
			if (rc == JOB_NOT_FOUND) {
				debug("job %d removed from MMCS", job_id);
				return STATUS_OK;
			} 
			if(rc == INCOMPATIBLE_STATE)
				debug("job %d is in an INCOMPATIBLE_STATE",
				      job_id);
			else
				error("rm_cancel_job(%d): %s", job_id, 
				      _bgl_err_str(rc));
		}
	}
	error("Failed to remove job %d from MMCS", job_id);
	return INTERNAL_ERROR;
}

#else 

int main(int argc, char *argv[])
{
	printf("Only can be ran on the service node of a BGL system.\n");
	return 0;
}

#endif
