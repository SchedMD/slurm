/*****************************************************************************\
 *  sfree.c - free specified block or all blocks.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
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

#include "sfree.h"

#define MAX_POLL_RETRIES    110
#define POLL_INTERVAL        3
#define MAX_PTHREAD_RETRIES  1

/* Globals */

int all_blocks = 0;
char *bg_block_id = NULL;
bool wait_full = false;

#ifdef HAVE_BG_FILES

typedef struct bg_records {
	char *bg_block_id;
	int state;
} delete_record_t; 

static int num_block_to_free = 0;
static int num_block_freed = 0;
static pthread_mutex_t freed_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static List delete_record_list = NULL;

/************
 * Functions *
 ************/

	
static int _free_block(delete_record_t *delete_record);
static int _update_bg_record_state();
static void _term_jobs_on_block(char *bg_block_id);
static char *_bg_err_str(status_t inx);
static int _remove_job(db_job_id_t job_id);


static void _clean_destroy_list(void* object)
{
	delete_record_t* delete_record = (delete_record_t*) object;

	if (delete_record) {
		xfree(delete_record->bg_block_id);
		xfree(delete_record);
	}
}

/* Free multiple blocks in parallel */
static void *_mult_free_block(void *args)
{
	delete_record_t *delete_record = (delete_record_t *) args;

	debug("destroying the bgblock %s.", delete_record->bg_block_id);
	_free_block(delete_record);	
	
	slurm_mutex_lock(&freed_cnt_mutex);
	num_block_freed++;
	slurm_mutex_unlock(&freed_cnt_mutex);
	
	return NULL;
}

int main(int argc, char *argv[])
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	rm_partition_list_t *block_list = NULL;
	rm_partition_state_flag_t block_state = PARTITION_ALL_FLAG;
	int j, num_blocks = 0;
	rm_partition_t *block_ptr = NULL;
	int rc;
	pthread_attr_t attr_agent;
	pthread_t thread_agent;
	int retries;
	delete_record_t *delete_record = NULL;
	
	bridge_init();
	if (!have_db2) {
		error("Required libraries can not be found "
		       "to access the Bluegene system.\nPlease "
		       "set your LD_LIBRARY_PATH correctly to "
		       "point to them.");
		exit(0);
	}

	log_init(xbasename(argv[0]), opts, SYSLOG_FACILITY_DAEMON, NULL);
	parse_command_line(argc, argv);

	delete_record_list = list_create(_clean_destroy_list);
	
	if(!all_blocks) {
		if(!bg_block_id) {
			error("you need to specify a bgblock");
			exit(0);
		}
		delete_record = xmalloc(sizeof(delete_record_t));
		delete_record->bg_block_id = xstrdup(bg_block_id);
		delete_record->state = NO_VAL;
		list_push(delete_record_list, delete_record);

		slurm_attr_init(&attr_agent);
		if (pthread_attr_setdetachstate(
			    &attr_agent, 
			    PTHREAD_CREATE_DETACHED))
			error("pthread_attr_setdetach"
			      "state error %m");
		
		retries = 0;
		while (pthread_create(&thread_agent, 
				      &attr_agent, 
				      _mult_free_block, 
				      (void *)delete_record)) {
			error("pthread_create "
			      "error %m");
			if (++retries 
			    > MAX_PTHREAD_RETRIES)
				fatal("Can't create "
				      "pthread");
			/* sleep and retry */
			usleep(1000);	
		}
		slurm_attr_destroy(&attr_agent);
		num_block_to_free++;
	} else {
		if ((rc = bridge_get_blocks_info(block_state, &block_list))
		    != STATUS_OK) {
			error("bridge_get_blocks_info(): %s", 
			      _bg_err_str(rc));
			return -1; 
		}

		if ((rc = bridge_get_data(block_list, RM_PartListSize, 
					  &num_blocks)) != STATUS_OK) {
			error("bridge_get_data(RM_PartListSize): %s", 
			      _bg_err_str(rc));
			
			num_blocks = 0;
		}
			
		for (j=0; j<num_blocks; j++) {
			if (j) {
				if ((rc = bridge_get_data(block_list, 
							  RM_PartListNextPart, 
							  &block_ptr)) 
				    != STATUS_OK) {
					error("bridge_get_data"
					      "(RM_PartListNextPart): %s",
					      _bg_err_str(rc));
					
					break;
				}
			} else {
				if ((rc = bridge_get_data(block_list, 
							  RM_PartListFirstPart,
							  &block_ptr)) 
				    != STATUS_OK) {
					error("bridge_get_data"
					      "(RM_PartListFirstPart: %s",
					      _bg_err_str(rc));
					
					break;
				}
			}
			if ((rc = bridge_get_data(block_ptr, RM_PartitionID, 
						  &bg_block_id))
			    != STATUS_OK) {
				error("bridge_get_data(RM_PartitionID): %s", 
				      _bg_err_str(rc));
				
				break;
			}

			if(!bg_block_id) {
				error("No Part ID was returned from database");
				continue;
			}

			if(strncmp("RMP", bg_block_id, 3)) {
				free(bg_block_id);
				continue;
			}
				
			delete_record = xmalloc(sizeof(delete_record_t));
			delete_record->bg_block_id = xstrdup(bg_block_id);
			
			free(bg_block_id);
			
			if ((rc = bridge_get_data(block_ptr,
						  RM_PartitionState,
						  &delete_record->state))
			    != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_PartitionState): %s",
				      _bg_err_str(rc));
			} 
			
			list_push(delete_record_list, delete_record);

			slurm_attr_init(&attr_agent);
			if (pthread_attr_setdetachstate(
				    &attr_agent, 
				    PTHREAD_CREATE_DETACHED))
				error("pthread_attr_setdetach"
				      "state error %m");
			
			retries = 0;
			while (pthread_create(&thread_agent, 
					      &attr_agent, 
					      _mult_free_block, 
					      (void *)delete_record)) {
				error("pthread_create "
				      "error %m");
				if (++retries 
				    > MAX_PTHREAD_RETRIES)
					fatal("Can't create "
					      "pthread");
				/* sleep and retry */
				usleep(1000);	
			}
			slurm_attr_destroy(&attr_agent);
			num_block_to_free++;
		}
		if ((rc = bridge_free_block_list(block_list)) != STATUS_OK) {
			error("bridge_free_block_list(): %s",
			      _bg_err_str(rc));
		}
	}
	while(num_block_to_free > num_block_freed) {
		info("waiting for all bgblocks to free...");
		_update_bg_record_state();
		sleep(1);
	}
	list_destroy(delete_record_list);
	
	bridge_fini();
	return 0;
}

static int _free_block(delete_record_t *delete_record)
{
	int rc;
	int i=0;

	info("freeing bgblock %s", delete_record->bg_block_id);
	_term_jobs_on_block(delete_record->bg_block_id);
	while (1) {
		if (delete_record->state != (rm_partition_state_t)NO_VAL
		    && delete_record->state != RM_PARTITION_FREE 
		    && delete_record->state != RM_PARTITION_DEALLOCATING) {
			info("bridge_destroy %s",delete_record->bg_block_id);
			if ((rc = bridge_destroy_block(
				     delete_record->bg_block_id))
			    != STATUS_OK) {
				if(rc == PARTITION_NOT_FOUND) {
					info("block %s is not found");
					break;
				}
				error("bridge_destroy_block(%s): %s",
				      delete_record->bg_block_id,
				      _bg_err_str(rc));
			}
		}
		
		if(!wait_full) {
			if(i>5)
				delete_record->state = RM_PARTITION_FREE;
			
			i++;
		}

		if ((delete_record->state == RM_PARTITION_FREE)
		    ||  (delete_record->state == RM_PARTITION_ERROR))
			break;
		sleep(3);
	}
	info("bgblock %s is freed", delete_record->bg_block_id);
	return SLURM_SUCCESS;
}

static int _update_bg_record_state()
{
	char *name = NULL;
	int rc;
	rm_partition_state_t state = -2;
	rm_partition_t *block_ptr = NULL;
	delete_record_t *delete_record = NULL;
	ListIterator itr;
	
	if(!delete_record_list) {
		return SLURM_SUCCESS;
	}

	itr = list_iterator_create(delete_record_list);
	while ((delete_record = (delete_record_t*) list_next(itr))) {	
		if(!delete_record->bg_block_id) {
			continue;
		}
		
		if ((delete_record->state == RM_PARTITION_FREE)
		    || (delete_record->state == RM_PARTITION_ERROR)) {
			continue;
		}
		name = delete_record->bg_block_id;
		
		if ((rc = bridge_get_block_info(name, &block_ptr)) 
		    != STATUS_OK) {
			if(rc == PARTITION_NOT_FOUND 
			   || rc == INCONSISTENT_DATA) {
				debug("block %s is not found",
				      delete_record->bg_block_id);
				continue;
			}
			
			error("bridge_get_block_info(%s): %s", 
			      name, 
			      _bg_err_str(rc));
			goto finished;
		}
		state = 1;

		if ((rc = bridge_get_data(block_ptr,
					  RM_PartitionState,
					  &delete_record->state))
		    != STATUS_OK) {
			error("bridge_get_data"
			      "(RM_PartitionState): %s",
			      _bg_err_str(rc));
		} 
		if ((rc = bridge_free_block(block_ptr)) 
		    != STATUS_OK) {
			error("bridge_free_block(): %s", 
			      _bg_err_str(rc));
		}
	finished:
		if(state != 1) {
			error("The requested block %s was not "
			      "found in system.",
			      name);
			slurm_mutex_lock(&freed_cnt_mutex);
			num_block_freed++;
			slurm_mutex_unlock(&freed_cnt_mutex);
		}
	}
	list_iterator_destroy(itr);
	
	return state;
}

/* Perform job termination work */
static void _term_jobs_on_block(char *bg_block_id)
{
	int i, jobs, rc, job_found = 0;
	rm_job_list_t *job_list;
	int live_states;
	rm_element_t *job_elem;
	pm_partition_id_t block_id;
	db_job_id_t job_id;
	
	//debug("getting the job info");
	live_states = JOB_ALL_FLAG
		& (~JOB_TERMINATED_FLAG)
		& (~JOB_ERROR_FLAG)
		& (~JOB_KILLED_FLAG);
	if ((rc = bridge_get_jobs(live_states, &job_list)) != STATUS_OK) {
		error("bridge_get_jobs(): %s", _bg_err_str(rc));
		return;
	}
	
	if ((rc = bridge_get_data(job_list, RM_JobListSize, &jobs)) 
	    != STATUS_OK) {
		error("bridge_get_data(RM_JobListSize): %s", _bg_err_str(rc));
		jobs = 0;
	} else if (jobs > 300)
		fatal("Active job count (%d) invalid, restart MMCS", jobs);
	//debug("job count %d",jobs);
	for (i=0; i<jobs; i++) {
		if (i) {
			if ((rc = bridge_get_data(job_list, RM_JobListNextJob,
						  &job_elem)) != STATUS_OK) {
				error("bridge_get_data(RM_JobListNextJob): %s",
				      _bg_err_str(rc));
				continue;
			}
		} else {
			if ((rc = bridge_get_data(job_list, RM_JobListFirstJob,
						  &job_elem)) != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_JobListFirstJob): %s",
				      _bg_err_str(rc));
				continue;
			}
		}
		
		if(!job_elem) {
			error("No Job Elem breaking out job count = %d\n",
			      jobs);
			break;
		}
		if ((rc = bridge_get_data(job_elem, RM_JobPartitionID, 
					  &block_id)) != STATUS_OK) {
			error("bridge_get_data(RM_JobPartitionID) %s: %s",
			      block_id, _bg_err_str(rc));
			continue;
		}

		if(!block_id) {
			error("No Block ID was returned from database");
			continue;
		}

		if (strcmp(block_id, bg_block_id) != 0) {
			free(block_id);
			continue;
		}
		free(block_id);
		job_found = 1;
		if ((rc = bridge_get_data(job_elem, RM_JobDBJobID, &job_id))
		    != STATUS_OK) {
			error("bridge_get_data(RM_JobDBJobID): %s",
			      _bg_err_str(rc));
			continue;
		}
		info("got job_id %d",job_id);
		if((rc = _remove_job(job_id)) == INTERNAL_ERROR) {
			goto not_removed;
		}
	}
	if(job_found == 0)
		info("No jobs on bgblock %s", bg_block_id);
	
not_removed:
	if ((rc = bridge_free_job_list(job_list)) != STATUS_OK)
		error("bridge_free_job_list(): %s", _bg_err_str(rc));
}

/*
 * Convert a BG API error code to a string
 * IN inx - error code from any of the BG Bridge APIs
 * RET - string describing the error condition
 */
static char *_bg_err_str(status_t inx)
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
		if ((rc = bridge_get_job(job_id, &job_rec)) != STATUS_OK) {
			if (rc == JOB_NOT_FOUND) {
				debug("job %d removed from MMCS", job_id);
				return STATUS_OK;
			} 

			error("bridge_get_job(%d): %s", job_id, 
			      _bg_err_str(rc));
			continue;
		}

		if ((rc = bridge_get_data(job_rec, RM_JobState, &job_state)) 
		    != STATUS_OK) {
			(void) bridge_free_job(job_rec);
			if (rc == JOB_NOT_FOUND) {
				debug("job %d not found in MMCS", job_id);
				return STATUS_OK;
			} 

			error("bridge_get_data(RM_JobState) for jobid=%d "
			      "%s", job_id, _bg_err_str(rc));
			continue;
		}
		if ((rc = bridge_free_job(job_rec)) != STATUS_OK)
			error("bridge_free_job: %s", _bg_err_str(rc));

		info("job %d is in state %d", job_id, job_state);
		
		/* check the state and process accordingly */
		if(job_state == RM_JOB_TERMINATED)
			return STATUS_OK;
		else if(job_state == RM_JOB_DYING)
			continue;
		else if(job_state == RM_JOB_ERROR) {
			error("job %d is in a error state.", job_id);
			
			//free_bg_block();
			return STATUS_OK;
		}

		(void) bridge_signal_job(job_id, SIGKILL);
		rc = bridge_cancel_job(job_id);
		if (rc != STATUS_OK) {
			if (rc == JOB_NOT_FOUND) {
				debug("job %d removed from MMCS", job_id);
				return STATUS_OK;
			} 
			if(rc == INCOMPATIBLE_STATE)
				debug("job %d is in an INCOMPATIBLE_STATE",
				      job_id);
			else
				error("bridge_cancel_job(%d): %s", job_id, 
				      _bg_err_str(rc));
		}
	}
	error("Failed to remove job %d from MMCS", job_id);
	return INTERNAL_ERROR;
}

#else 

int main(int argc, char *argv[])
{
	printf("Only can be ran on the service node of a Bluegene system.\n");
	return 0;
}

#endif
