/*****************************************************************************\
 *  bridge_linker.c
 *
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov>, Danny Auble <da@llnl.gov>
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


#include "../ba/block_allocator.h"
#include "../bridge_linker.h"
#include "src/common/uid.h"
#include "bridge_status.h"
#include "bridge_switch_connections.h"

#define MAX_ADD_RETRY 2

#if defined HAVE_BG_FILES
typedef struct {
	/* all the rm functions */
	status_t (*set_serial)(const rm_serial_t serial);
	status_t (*get_bg)(my_bluegene_t **bg);
	status_t (*free_bg)(my_bluegene_t *bg);
#ifdef HAVE_BGP
	status_t (*new_ionode)(rm_ionode_t **ionode);
	status_t (*free_ionode)(rm_ionode_t *ionode);
#endif
	status_t (*add_partition)(rm_partition_t *partition);
	status_t (*get_partition)(pm_partition_id_t pid,
				  rm_partition_t **partition);
	status_t (*get_partition_info)(pm_partition_id_t pid,
				       rm_partition_t **partition);
	status_t (*modify_partition)(pm_partition_id_t pid,
				     enum rm_modify_op op, const void *data);
	status_t (*set_part_owner)(pm_partition_id_t pid, const char *name);
	status_t (*add_part_user)(pm_partition_id_t pid, const char *name);
	status_t (*remove_part_user)(pm_partition_id_t pid, const char *name);
	status_t (*remove_partition)(pm_partition_id_t pid);
	status_t (*get_partitions)(rm_partition_state_flag_t flag,
				   rm_partition_list_t **part_list);
	status_t (*get_partitions_info)(rm_partition_state_flag_t flag,
					rm_partition_list_t **part_list);
	status_t (*get_job)(db_job_id_t dbJobId, rm_job_t **job);
	status_t (*get_jobs)(rm_job_state_flag_t flag, rm_job_list_t **jobs);
	status_t (*get_nodecards)(rm_bp_id_t bpid,
				  rm_nodecard_list_t **nc_list);
	status_t (*new_nodecard)(rm_nodecard_t **nodecard);
	status_t (*free_nodecard)(rm_nodecard_t *nodecard);
	status_t (*new_partition)(rm_partition_t **partition);
	status_t (*free_partition)(rm_partition_t *partition);
	status_t (*free_job)(rm_job_t *job);
	status_t (*free_partition_list)(rm_partition_list_t *part_list);
	status_t (*free_job_list)(rm_job_list_t *job_list);
	status_t (*free_nodecard_list)(rm_nodecard_list_t *nc_list);
	status_t (*get_data)(rm_element_t* element,
			     enum rm_specification field, void *data);
	status_t (*set_data)(rm_element_t* element,
			     enum rm_specification field, void *data);

	/* all the jm functions */
	status_t (*signal_job)(db_job_id_t jid, rm_signal_t sig);

	/* all the pm functions */
	status_t (*create_partition)(pm_partition_id_t pid);
	status_t (*destroy_partition)(pm_partition_id_t pid);

	/* set say message stuff */
	void (*set_log_params)(FILE * stream, unsigned int level);

} bridge_api_t;

pthread_mutex_t api_file_mutex = PTHREAD_MUTEX_INITIALIZER;
bridge_api_t bridge_api;
#endif

static bool initialized = false;
bool have_db2 = true;
void *handle = NULL;

#if defined HAVE_BG_FILES
/* translation from the enum to the actual port number */
static int _port_enum(int port)
{
	switch(port) {
	case RM_PORT_S0:
		return 0;
		break;
	case RM_PORT_S1:
		return 1;
		break;
	case RM_PORT_S2:
		return 2;
		break;
	case RM_PORT_S3:
		return 3;
		break;
	case RM_PORT_S4:
		return 4;
		break;
	case RM_PORT_S5:
		return 5;
		break;
	default:
		return -1;
	}
}

static int _bg_errtrans(int in)
{
	switch (in) {
	case STATUS_OK:
		return SLURM_SUCCESS;
	case PARTITION_NOT_FOUND:
		return BG_ERROR_BLOCK_NOT_FOUND;
	case INCOMPATIBLE_STATE:
		return BG_ERROR_INVALID_STATE;
	case CONNECTION_ERROR:
		return BG_ERROR_CONNECTION_ERROR;
	case JOB_NOT_FOUND:
		return BG_ERROR_JOB_NOT_FOUND;
	case BP_NOT_FOUND:
		return BG_ERROR_MP_NOT_FOUND;
	case SWITCH_NOT_FOUND:
		return BG_ERROR_SWITCH_NOT_FOUND;
#ifndef HAVE_BGL
	case PARTITION_ALREADY_DEFINED:
		return BG_ERROR_BLOCK_ALREADY_DEFINED;
#endif
	case JOB_ALREADY_DEFINED:
		return BG_ERROR_JOB_ALREADY_DEFINED;
	case INTERNAL_ERROR:
		return BG_ERROR_INTERNAL_ERROR;
	case INVALID_INPUT:
		return BG_ERROR_INVALID_INPUT;
	case INCONSISTENT_DATA:
		return BG_ERROR_INCONSISTENT_DATA;
	default:
		break;
	}
	return SLURM_ERROR;
}

static status_t _get_job(db_job_id_t dbJobId, rm_job_t **job)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.get_job))(dbJobId, job));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

static status_t _get_jobs(rm_job_state_flag_t flag, rm_job_list_t **jobs)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.get_jobs))(flag, jobs));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;
}

static status_t _free_job(rm_job_t *job)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.free_job))(job));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

static status_t _free_job_list(rm_job_list_t *job_list)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.free_job_list))(job_list));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

static status_t _signal_job(db_job_id_t jid, rm_signal_t sig)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.signal_job))(jid, sig));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

static status_t _remove_block_user(pm_partition_id_t pid,
				   const char *name)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.remove_part_user))(pid, name));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

static status_t _new_block(rm_partition_t **partition)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.new_partition))(partition));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;
}

static status_t _add_block(rm_partition_t *partition)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.add_partition))(partition));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;
}


/* Kill a job and remove its record from MMCS */
static int _remove_job(db_job_id_t job_id, char *block_id)
{
	int rc;
	int count = 0;
	rm_job_t *job_rec = NULL;
	rm_job_state_t job_state;
	bool is_history = false;

	debug("removing job %d from MMCS on block %s", job_id, block_id);
	while (1) {
		if (count)
			sleep(POLL_INTERVAL);
		count++;

		/* Find the job */
		if ((rc = _get_job(job_id, &job_rec)) != SLURM_SUCCESS) {

			if (rc == BG_ERROR_JOB_NOT_FOUND) {
				debug("job %d removed from MMCS", job_id);
				return SLURM_SUCCESS;
			}

			error("bridge_get_job(%d): %s", job_id,
			      bg_err_str(rc));
			continue;
		}

		if ((rc = bridge_get_data(job_rec, RM_JobState, &job_state))
		    != SLURM_SUCCESS) {
			(void) _free_job(job_rec);
			if (rc == BG_ERROR_JOB_NOT_FOUND) {
				debug("job %d not found in MMCS", job_id);
				return SLURM_SUCCESS;
			}

			error("bridge_get_data(RM_JobState) for jobid=%d "
			      "%s", job_id, bg_err_str(rc));
			continue;
		}

		/* If this job is in the history table we
		   should just exit here since it is marked
		   incorrectly */
		if ((rc = bridge_get_data(job_rec, RM_JobInHist,
					  &is_history))
		    != SLURM_SUCCESS) {
			(void) _free_job(job_rec);
			if (rc == BG_ERROR_JOB_NOT_FOUND) {
				debug("job %d removed from MMCS", job_id);
				return SLURM_SUCCESS;
			}

			error("bridge_get_data(RM_JobInHist) for jobid=%d "
			      "%s", job_id, bg_err_str(rc));
			continue;
		}

		if ((rc = _free_job(job_rec)) != SLURM_SUCCESS)
			error("bridge_free_job: %s", bg_err_str(rc));

		debug2("job %d on block %s is in state %d history %d",
		       job_id, block_id, job_state, is_history);

		/* check the state and process accordingly */
		if (is_history) {
			debug2("Job %d on block %s isn't in the "
			       "active job table anymore, final state was %d",
			       job_id, block_id, job_state);
			return SLURM_SUCCESS;
		} else if (job_state == RM_JOB_TERMINATED)
			return SLURM_SUCCESS;
		else if (job_state == RM_JOB_DYING) {
			if (count > MAX_POLL_RETRIES)
				error("Job %d on block %s isn't dying, "
				      "trying for %d seconds", job_id,
				      block_id, count*POLL_INTERVAL);
			continue;
		} else if (job_state == RM_JOB_ERROR) {
			error("job %d on block %s is in a error state.",
			      job_id, block_id);

			//free_bg_block();
			return SLURM_SUCCESS;
		}

		/* we have been told the next 2 lines do the same
		 * thing, but I don't believe it to be true.  In most
		 * cases when you do a signal of SIGTERM the mpirun
		 * process gets killed with a SIGTERM.  In the case of
		 * bridge_cancel_job it always gets killed with a
		 * SIGKILL.  From IBM's point of view that is a bad
		 * deally, so we are going to use signal ;).  Sending
		 * a SIGKILL will kill the mpirun front end process,
		 * and if you kill that jobs will never get cleaned up and
		 * you end up with ciod unreacahble on the next job.
		 */

//		 rc = bridge_cancel_job(job_id);
		rc = _signal_job(job_id, SIGTERM);

		if (rc != SLURM_SUCCESS) {
			if (rc == BG_ERROR_JOB_NOT_FOUND) {
				debug("job %d on block %s removed from MMCS",
				      job_id, block_id);
				return SLURM_SUCCESS;
			}
			if (rc == BG_ERROR_INVALID_STATE)
				debug("job %d on block %s is in an "
				      "INCOMPATIBLE_STATE",
				      job_id, block_id);
			else
				error("bridge_signal_job(%d): %s", job_id,
				      bg_err_str(rc));
		} else if (count > MAX_POLL_RETRIES)
			error("Job %d on block %s is in state %d and "
			      "isn't dying, and doesn't appear to be "
			      "responding to SIGTERM, trying for %d seconds",
			      job_id, block_id, job_state, count*POLL_INTERVAL);

	}

	error("Failed to remove job %d from MMCS", job_id);
	return BG_ERROR_INTERNAL_ERROR;
}
#endif

static void _remove_jobs_on_block_and_reset(rm_job_list_t *job_list,
					    int job_cnt, char *block_id)
{
	bg_record_t *bg_record = NULL;
	int job_remove_failed = 0;

#if defined HAVE_BG_FILES && defined HAVE_BG_L_P
	rm_element_t *job_elem = NULL;
	pm_partition_id_t job_block;
	db_job_id_t job_id;
	int i, rc;
#endif

	if (!job_list)
		job_cnt = 0;

	if (!block_id) {
		error("_remove_jobs_on_block_and_reset: no block name given");
		return;
	}

#if defined HAVE_BG_FILES && defined HAVE_BG_L_P
	for (i=0; i<job_cnt; i++) {
		if (i) {
			if ((rc = bridge_get_data(
				     job_list, RM_JobListNextJob,
				     &job_elem)) != SLURM_SUCCESS) {
				error("bridge_get_data"
				      "(RM_JobListNextJob): %s",
				      bg_err_str(rc));
				continue;
			}
		} else {
			if ((rc = bridge_get_data(
				     job_list, RM_JobListFirstJob,
				     &job_elem)) != SLURM_SUCCESS) {
				error("bridge_get_data"
				      "(RM_JobListFirstJob): %s",
				      bg_err_str(rc));
				continue;
			}
		}

		if (!job_elem) {
			error("No Job Elem breaking out job count = %d", i);
			break;
		}
		if ((rc = bridge_get_data(job_elem, RM_JobPartitionID,
					  &job_block))
		    != SLURM_SUCCESS) {
			error("bridge_get_data(RM_JobPartitionID) %s: %s",
			      job_block, bg_err_str(rc));
			continue;
		}

		if (!job_block) {
			error("No blockID returned from Database");
			continue;
		}

		debug2("looking at block %s looking for %s",
		       job_block, block_id);

		if (strcmp(job_block, block_id)) {
			free(job_block);
			continue;
		}

		free(job_block);

		if ((rc = bridge_get_data(job_elem, RM_JobDBJobID, &job_id))
		    != SLURM_SUCCESS) {
			error("bridge_get_data(RM_JobDBJobID): %s",
			      bg_err_str(rc));
			continue;
		}
		debug2("got job_id %d",job_id);
		if ((rc = _remove_job(job_id, block_id))
		    == BG_ERROR_INTERNAL_ERROR) {
			job_remove_failed = 1;
			break;
		}
	}
#else
	/* Simpulate better job completion since on a real system it
	 * could take up minutes to kill a job. */
	if (job_cnt)
		sleep(2);
#endif
	/* remove the block's users */
	slurm_mutex_lock(&block_state_mutex);
	bg_record = find_bg_record_in_list(bg_lists->main, block_id);
	if (bg_record) {
		if (job_remove_failed) {
			if (bg_record->mp_str)
				slurm_drain_nodes(
					bg_record->mp_str,
					"_term_agent: Couldn't remove job",
					slurm_get_slurm_user_id());
			else
				error("Block %s doesn't have a node list.",
				      block_id);
		}

		bg_reset_block(bg_record, NULL);
	} else if (bg_conf->layout_mode == LAYOUT_DYNAMIC) {
		debug2("Hopefully we are destroying this block %s "
		       "since it isn't in the bg_lists->main",
		       block_id);
	} else if (job_cnt) {
		error("Could not find block %s previously assigned to job.  "
		      "If this is happening at startup and you just changed "
		      "your bluegene.conf this is expected.  Else you should "
		      "probably restart your slurmctld since this shouldn't "
		      "happen outside of that.",
		      block_id);
	}
	slurm_mutex_unlock(&block_state_mutex);

}

/**
 * initialize the BG block in the resource manager
 */
static void _pre_allocate(bg_record_t *bg_record)
{
#if defined HAVE_BG_FILES
	int rc;
	int send_psets=bg_conf->ionodes_per_mp;
	rm_connection_type_t conn_type = bg_record->conn_type[0];
#ifdef HAVE_BGL
	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionBlrtsImg,
				  bg_record->blrtsimage)) != SLURM_SUCCESS)
		error("bridge_set_data(RM_PartitionBlrtsImg): %s",
		      bg_err_str(rc));

	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionLinuxImg,
				  bg_record->linuximage)) != SLURM_SUCCESS)
		error("bridge_set_data(RM_PartitionLinuxImg): %s",
		      bg_err_str(rc));

	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionRamdiskImg,
				  bg_record->ramdiskimage)) != SLURM_SUCCESS)
		error("bridge_set_data(RM_PartitionRamdiskImg): %s",
		      bg_err_str(rc));
#else
	struct tm my_tm;
	struct timeval my_tv;

	if ((rc = bridge_set_data(bg_record->bg_block,
				  RM_PartitionCnloadImg,
				  bg_record->linuximage)) != SLURM_SUCCESS)
		error("bridge_set_data(RM_PartitionLinuxCnloadImg): %s",
		      bg_err_str(rc));

	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionIoloadImg,
				  bg_record->ramdiskimage)) != SLURM_SUCCESS)
		error("bridge_set_data(RM_PartitionIoloadImg): %s",
		      bg_err_str(rc));

	gettimeofday(&my_tv, NULL);
	localtime_r(&my_tv.tv_sec, &my_tm);
	bg_record->bg_block_id = xstrdup_printf(
		"RMP%2.2d%2.2s%2.2d%2.2d%2.2d%3.3ld",
		my_tm.tm_mday, mon_abbr(my_tm.tm_mon),
		my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, my_tv.tv_usec/1000);
	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionID,
				  bg_record->bg_block_id)) != SLURM_SUCCESS)
		error("bridge_set_data(RM_PartitionID): %s",
		      bg_err_str(rc));
#endif
	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionMloaderImg,
				  bg_record->mloaderimage)) != SLURM_SUCCESS)
		error("bridge_set_data(RM_PartitionMloaderImg): %s",
		      bg_err_str(rc));

	/* Don't send a * uint16_t into this it messes things up. */
	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionConnection,
				  &conn_type)) != SLURM_SUCCESS)
		error("bridge_set_data(RM_PartitionConnection): %s",
		      bg_err_str(rc));

	/* rc = bg_conf->mp_cnode_cnt/bg_record->cnode_cnt; */
/* 	if (rc > 1) */
/* 		send_psets = bg_conf->ionodes_per_mp/rc; */

	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionPsetsPerBP,
				  &send_psets)) != SLURM_SUCCESS)
		error("bridge_set_data(RM_PartitionPsetsPerBP): %s",
		      bg_err_str(rc));

	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionUserName,
				  bg_conf->slurm_user_name))
	    != SLURM_SUCCESS)
		error("bridge_set_data(RM_PartitionUserName): %s",
		      bg_err_str(rc));

#endif
}

/**
 * add the block record to the DB
 */
static int _post_allocate(bg_record_t *bg_record)
{
	int rc = SLURM_SUCCESS;
#if defined HAVE_BG_FILES
	int i;
	pm_partition_id_t block_id;

	/* Add partition record to the DB */
	debug2("adding block");

	for(i=0;i<MAX_ADD_RETRY; i++) {
		if ((rc = _add_block(bg_record->bg_block))
		    != SLURM_SUCCESS) {
			error("bridge_add_block(): %s", bg_err_str(rc));
			rc = SLURM_ERROR;
		} else {
			rc = SLURM_SUCCESS;
			break;
		}
		sleep(3);
	}
	if (rc == SLURM_ERROR) {
		info("going to free it");
		if ((rc = bridge_free_block(bg_record->bg_block))
		    != SLURM_SUCCESS)
			error("bridge_free_block(): %s", bg_err_str(rc));
		fatal("couldn't add last block.");
	}
	debug2("done adding");

	/* Get back the new block id */
	if ((rc = bridge_get_data(bg_record->bg_block, RM_PartitionID,
				  &block_id))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_PartitionID): %s",
		      bg_err_str(rc));
		bg_record->bg_block_id = xstrdup("UNKNOWN");
	} else {
		if (!block_id) {
			error("No Block ID was returned from database");
			return SLURM_ERROR;
		}
		bg_record->bg_block_id = xstrdup(block_id);

		free(block_id);

	}
	/* We are done with the block */
	if ((rc = bridge_free_block(bg_record->bg_block)) != SLURM_SUCCESS)
		error("bridge_free_block(): %s", bg_err_str(rc));
#else
	/* We are just looking for a real number here no need for a
	   base conversion
	*/
	if (!bg_record->bg_block_id) {
		struct tm my_tm;
		struct timeval my_tv;
		gettimeofday(&my_tv, NULL);
		localtime_r(&my_tv.tv_sec, &my_tm);
		bg_record->bg_block_id = xstrdup_printf(
			"RMP%2.2d%2.2s%2.2d%2.2d%2.2d%3.3ld",
			my_tm.tm_mday, mon_abbr(my_tm.tm_mon),
			my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec,
			my_tv.tv_usec/1000);
		/* Since we divide by 1000 here we need to sleep that
		   long to get a unique id. It takes longer than this
		   in a real system so we don't worry about it. */
		usleep(1000);
	}
#endif

	return rc;
}

#if defined HAVE_BG_FILES

static int _set_ionodes(bg_record_t *bg_record, int io_start, int io_nodes)
{
	char bitstring[BITSIZE];

	if (!bg_record)
		return SLURM_ERROR;

	bg_record->ionode_bitmap = bit_alloc(bg_conf->ionodes_per_mp);
	/* Set the correct ionodes being used in this block */
	bit_nset(bg_record->ionode_bitmap, io_start, io_start+io_nodes);
	bit_fmt(bitstring, BITSIZE, bg_record->ionode_bitmap);
	bg_record->ionode_str = xstrdup(bitstring);
	return SLURM_SUCCESS;
}

static int _get_syms(int n_syms, const char *names[], void *ptrs[])
{
        int i, count;
#ifdef HAVE_BGL
#ifdef BG_DB2_SO
	void *db_handle = NULL;
	db_handle = dlopen (BG_DB2_SO, RTLD_LAZY);
	if (!db_handle) {
		have_db2 = false;
		debug("%s", dlerror());
		return 0;
	}
	dlclose(db_handle);
#else
	fatal("No BG_DB2_SO is set, can't run.");
#endif
#endif // HAVE_BGL

#ifdef BG_BRIDGE_SO
	handle = dlopen (BG_BRIDGE_SO, RTLD_LAZY);
	if (!handle) {
		have_db2 = false;
		debug("%s", dlerror());
		return 0;
	}
#else
	fatal("No BG_BRIDGE_SO is set, can't run.");
#endif

	dlerror();    /* Clear any existing error */
        count = 0;
        for ( i = 0; i < n_syms; ++i ) {
                ptrs[i] = dlsym(handle, names[i]);
                if (ptrs[i]) {
			++count;
		} else
			fatal("Can't find %s in api", names[i]);
	}
        return count;
}

static int _block_get_and_set_mps(bg_record_t *bg_record)
{
	int rc, i, j;
	int cnt = 0;
	int switch_cnt = 0;
	rm_switch_t *curr_switch = NULL;
	rm_BP_t *curr_mp = NULL;
	char *switchid = NULL;
	rm_connection_t curr_conn;
	int dim;
	ba_mp_t *ba_node = NULL;
	ba_switch_t *ba_switch = NULL;
	ba_mp_t *ba_mp = NULL;
	ListIterator itr = NULL;
	rm_partition_t *block_ptr = (rm_partition_t *)bg_record->bg_block;

	debug2("getting info for block %s", bg_record->bg_block_id);

	if ((rc = bridge_get_data(block_ptr, RM_PartitionSwitchNum,
				  &switch_cnt)) != SLURM_SUCCESS) {
		error("bridge_get_data(RM_PartitionSwitchNum): %s",
		      bg_err_str(rc));
		goto end_it;
	}
	if (!switch_cnt) {
		debug3("no switch_cnt");
		if ((rc = bridge_get_data(block_ptr,
					  RM_PartitionFirstBP,
					  &curr_mp))
		    != SLURM_SUCCESS) {
			error("bridge_get_data: "
			      "RM_PartitionFirstBP: %s",
			      bg_err_str(rc));
			goto end_it;
		}
		if ((rc = bridge_get_data(curr_mp, RM_BPID, &switchid))
		    != SLURM_SUCCESS) {
			error("bridge_get_data: RM_SwitchBPID: %s",
			      bg_err_str(rc));
			goto end_it;
		}

		ba_mp = loc2ba_mp(switchid);
		if (!ba_mp) {
			error("find_bp_loc: bpid %s not known", switchid);
			goto end_it;
		}
		ba_node = ba_copy_mp(ba_mp);
		ba_setup_mp(ba_node, 0, 0);
		ba_node->used = BA_MP_USED_TRUE;
		if (!bg_record->ba_mp_list)
			bg_record->ba_mp_list = list_create(destroy_ba_mp);
		list_push(bg_record->ba_mp_list, ba_node);
		return SLURM_SUCCESS;
	}
	for (i=0; i<switch_cnt; i++) {
		if (i) {
			if ((rc = bridge_get_data(block_ptr,
						  RM_PartitionNextSwitch,
						  &curr_switch))
			    != SLURM_SUCCESS) {
				error("bridge_get_data: "
				      "RM_PartitionNextSwitch: %s",
				      bg_err_str(rc));
				goto end_it;
			}
		} else {
			if ((rc = bridge_get_data(block_ptr,
						  RM_PartitionFirstSwitch,
						  &curr_switch))
			    != SLURM_SUCCESS) {
				error("bridge_get_data: "
				      "RM_PartitionFirstSwitch: %s",
				      bg_err_str(rc));
				goto end_it;
			}
		}
		if ((rc = bridge_get_data(curr_switch, RM_SwitchDim, &dim))
		    != SLURM_SUCCESS) {
			error("bridge_get_data: RM_SwitchDim: %s",
			      bg_err_str(rc));
			goto end_it;
		}
		if ((rc = bridge_get_data(curr_switch, RM_SwitchBPID,
					  &switchid))
		    != SLURM_SUCCESS) {
			error("bridge_get_data: RM_SwitchBPID: %s",
			      bg_err_str(rc));
			goto end_it;
		}

		ba_mp = loc2ba_mp(switchid);
		if (!ba_mp) {
			error("find_bp_loc: bpid %s not known", switchid);
			goto end_it;
		}

		if ((rc = bridge_get_data(curr_switch, RM_SwitchConnNum, &cnt))
		    != SLURM_SUCCESS) {
			error("bridge_get_data: RM_SwitchBPID: %s",
			      bg_err_str(rc));
			goto end_it;
		}
		debug2("switch id = %s dim %d conns = %d",
		       switchid, dim, cnt);

		if (bg_record->ba_mp_list) {
			itr = list_iterator_create(bg_record->ba_mp_list);
			while ((ba_node = list_next(itr))) {
				if (ba_node->coord[X] == ba_mp->coord[X] &&
				    ba_node->coord[Y] == ba_mp->coord[Y] &&
				    ba_node->coord[Z] == ba_mp->coord[Z])
					break;	/* we found it */
			}
			list_iterator_destroy(itr);
		}

		if (!ba_node) {
			ba_node = ba_copy_mp(ba_mp);
			ba_setup_mp(ba_node, 0, 0);
			if (!bg_record->ba_mp_list)
				bg_record->ba_mp_list =
					list_create(destroy_ba_mp);
			list_push(bg_record->ba_mp_list, ba_node);
		}
		ba_switch = &ba_node->axis_switch[dim];
		for (j=0; j<cnt; j++) {
			if (j) {
				if ((rc = bridge_get_data(
					     curr_switch,
					     RM_SwitchNextConnection,
					     &curr_conn))
				    != SLURM_SUCCESS) {
					error("bridge_get_data: "
					      "RM_SwitchNextConnection: %s",
					      bg_err_str(rc));
					goto end_it;
				}
			} else {
				if ((rc = bridge_get_data(
					     curr_switch,
					     RM_SwitchFirstConnection,
					     &curr_conn))
				    != SLURM_SUCCESS) {
					error("bridge_get_data: "
					      "RM_SwitchFirstConnection: %s",
					      bg_err_str(rc));
					goto end_it;
				}
			}

			if (curr_conn.p1 == 1 && dim == X) {
				if (ba_node->used) {
					debug("I have already been to "
					      "this node %s",
					      ba_node->coord_str);
					goto end_it;
				}
				ba_node->used = true;
			}
			debug3("connection going from %d -> %d",
			       curr_conn.p1, curr_conn.p2);

			if (ba_switch->int_wire[curr_conn.p1].used) {
				debug("%s dim %d port %d "
				      "is already in use",
				      ba_node->coord_str,
				      dim,
				      curr_conn.p1);
				goto end_it;
			}
			ba_switch->int_wire[curr_conn.p1].used = 1;
			ba_switch->int_wire[curr_conn.p1].port_tar
				= curr_conn.p2;

			if (ba_switch->int_wire[curr_conn.p2].used) {
				debug("%s dim %d port %d "
				      "is already in use",
				      ba_node->coord_str,
				      dim,
				      curr_conn.p2);
				goto end_it;
			}
			ba_switch->int_wire[curr_conn.p2].used = 1;
			ba_switch->int_wire[curr_conn.p2].port_tar
				= curr_conn.p1;
		}
	}
	return SLURM_SUCCESS;
end_it:
	if (bg_record->ba_mp_list) {
		list_destroy(bg_record->ba_mp_list);
		bg_record->ba_mp_list = NULL;
	}
	return SLURM_ERROR;
}

static bg_record_t *_translate_object_to_block(rm_partition_t *block_ptr,
					       char *bg_block_id)
{
	int mp_cnt, i, nc_cnt, io_cnt, rc;
	rm_element_t *mp_ptr = NULL;
	rm_bp_id_t mpid;
	char node_name_tmp[255], *user_name = NULL;

	ba_mp_t *ba_mp = NULL;
	char *tmp_char = NULL;

	rm_nodecard_t *ncard = NULL;
	int nc_id, io_start;

	bool small = false;
	hostlist_t hostlist;		/* expanded form of hosts */
	bg_record_t *bg_record = (bg_record_t *)xmalloc(sizeof(bg_record_t));

	bg_record->magic = BLOCK_MAGIC;
	bg_record->bg_block = block_ptr;
	bg_record->bg_block_id = xstrdup(bg_block_id);

	/* we don't need anything else since we are just getting rid
	   of the thing.
	*/
	if (!bg_recover)
		return bg_record;

#ifndef HAVE_BGL
	if ((rc = bridge_get_data(block_ptr, RM_PartitionSize, &mp_cnt))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_PartitionSize): %s",
		      bg_err_str(rc));
		goto end_it;
	}

	if (mp_cnt==0) {
		error("it appear we have 0 cnodes in block %s", bg_block_id);
		goto end_it;
	}
	bg_record->cnode_cnt = mp_cnt;
	bg_record->cpu_cnt = bg_conf->cpu_ratio * bg_record->cnode_cnt;
#endif

	if ((rc = bridge_get_data(block_ptr, RM_PartitionBPNum, &mp_cnt))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_BPNum): %s",
		      bg_err_str(rc));
		goto end_it;
	}

	if (mp_cnt==0) {
		error("it appear we have 0 Midplanes in block %s", bg_block_id);
		goto end_it;
	}
	bg_record->mp_count = mp_cnt;

	debug3("has %d MPs", bg_record->mp_count);

	if ((rc = bridge_get_data(block_ptr, RM_PartitionSwitchNum,
				  &bg_record->switch_count))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_PartitionSwitchNum): %s",
		      bg_err_str(rc));
		goto end_it;
	}

	if ((rc = bridge_get_data(block_ptr, RM_PartitionSmall,
				  &small))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_PartitionSmall): %s",
		      bg_err_str(rc));
		goto end_it;
	}

	if (small) {
		if ((rc = bridge_get_data(block_ptr,
					  RM_PartitionOptions,
					  &tmp_char))
		    != SLURM_SUCCESS) {
			error("bridge_get_data(RM_PartitionOptions): "
			      "%s", bg_err_str(rc));
			goto end_it;
		} else if (tmp_char) {
			switch(tmp_char[0]) {
			case 's':
				bg_record->conn_type[0] = SELECT_HTC_S;
				break;
			case 'd':
				bg_record->conn_type[0] = SELECT_HTC_D;
				break;
			case 'v':
				bg_record->conn_type[0] = SELECT_HTC_V;
				break;
			case 'l':
				bg_record->conn_type[0] = SELECT_HTC_L;
				break;
			default:
				bg_record->conn_type[0] = SELECT_SMALL;
				break;
			}

			free(tmp_char);
		} else
			bg_record->conn_type[0] = SELECT_SMALL;

		if ((rc = bridge_get_data(block_ptr,
					  RM_PartitionFirstNodeCard,
					  &ncard))
		    != SLURM_SUCCESS) {
			error("bridge_get_data("
			      "RM_PartitionFirstNodeCard): %s",
			      bg_err_str(rc));
			goto end_it;
		}

		if ((rc = bridge_get_data(block_ptr,
					  RM_PartitionNodeCardNum,
					  &nc_cnt))
		    != SLURM_SUCCESS) {
			error("bridge_get_data("
			      "RM_PartitionNodeCardNum): %s",
			      bg_err_str(rc));
			goto end_it;
		}
#ifdef HAVE_BGL
		/* Translate nodecard count to ionode count */
		if ((io_cnt = nc_cnt * bg_conf->io_ratio))
			io_cnt--;

		nc_id = 0;
		if (nc_cnt == 1)
			bridge_find_nodecard_num(
				block_ptr, ncard, &nc_id);

		bg_record->cnode_cnt =
			nc_cnt * bg_conf->nodecard_cnode_cnt;
		bg_record->cpu_cnt =
			bg_conf->cpu_ratio * bg_record->cnode_cnt;

		if ((rc = bridge_get_data(ncard,
					  RM_NodeCardQuarter,
					  &io_start))
		    != SLURM_SUCCESS) {
			error("bridge_get_data(CardQuarter): %d",rc);
			goto end_it;
		}
		io_start *= bg_conf->quarter_ionode_cnt;
		io_start += bg_conf->nodecard_ionode_cnt * (nc_id%4);
#else
		/* Translate nodecard count to ionode count */
		if ((io_cnt = nc_cnt * bg_conf->io_ratio))
			io_cnt--;

		if ((rc = bridge_get_data(ncard,
					  RM_NodeCardID,
					  &tmp_char))
		    != SLURM_SUCCESS) {
			error("bridge_get_data(RM_NodeCardID): %d",rc);
			goto end_it;
		}

		if (!tmp_char)
			goto end_it;

		/* From the first nodecard id we can figure
		   out where to start from with the alloc of ionodes.
		*/
		nc_id = atoi((char*)tmp_char+1);
		free(tmp_char);
		io_start = nc_id * bg_conf->io_ratio;
		if (bg_record->cnode_cnt <
		    bg_conf->nodecard_cnode_cnt) {
			rm_ionode_t *ionode;

			/* figure out the ionode we are using */
			if ((rc = bridge_get_data(
				     ncard,
				     RM_NodeCardFirstIONode,
				     &ionode)) != SLURM_SUCCESS) {
				error("bridge_get_data("
				      "RM_NodeCardFirstIONode): %d",
				      rc);
				goto end_it;
			}
			if ((rc = bridge_get_data(ionode,
						  RM_IONodeID,
						  &tmp_char))
			    != SLURM_SUCCESS) {
				error("bridge_get_data("
				      "RM_NodeCardIONodeNum): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				goto end_it;
			}

			if (!tmp_char)
				goto end_it;
			/* just add the ionode num to the
			 * io_start */
			io_start += atoi((char*)tmp_char+1);
			free(tmp_char);
			/* make sure i is 0 since we are only using
			 * 1 ionode */
			io_cnt = 0;
		}
#endif
		if (_set_ionodes(bg_record, io_start, io_cnt)
		    == SLURM_ERROR)
			error("couldn't create ionode_bitmap "
			      "for ionodes %d to %d",
			      io_start, io_start+io_cnt);
		debug3("%s uses ionodes %s",
		       bg_record->bg_block_id,
		       bg_record->ionode_str);
	} else {
		rm_connection_type_t conn_type;
#ifdef HAVE_BGL
		bg_record->cpu_cnt = bg_conf->cpus_per_mp
			* bg_record->mp_count;
		bg_record->cnode_cnt =  bg_conf->mp_cnode_cnt
			* bg_record->mp_count;
#endif
		/* Don't send a * uint16_t into this it messes things up. */
		if ((rc = bridge_get_data(block_ptr,
					  RM_PartitionConnection,
					  &conn_type))
		    != SLURM_SUCCESS) {
			error("bridge_get_data"
			      "(RM_PartitionConnection): %s",
			      bg_err_str(rc));
			goto end_it;
		}
		bg_record->conn_type[0] = conn_type;
		/* Set the bitmap blank here if it is a full
		   node we don't want anything set we also
		   don't want the bg_record->ionodes set.
		*/
		bg_record->ionode_bitmap =
			bit_alloc(bg_conf->ionodes_per_mp);
	}

	_block_get_and_set_mps(bg_record);

	if (!bg_record->ba_mp_list)
		fatal("couldn't get the wiring info for block %s",
		      bg_record->bg_block_id);

	hostlist = hostlist_create(NULL);

	for (i=0; i<mp_cnt; i++) {
		if (i) {
			if ((rc = bridge_get_data(block_ptr,
						  RM_PartitionNextBP,
						  &mp_ptr))
			    != SLURM_SUCCESS) {
				error("bridge_get_data(RM_NextBP): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				break;
			}
		} else {
			if ((rc = bridge_get_data(block_ptr,
						  RM_PartitionFirstBP,
						  &mp_ptr))
			    != SLURM_SUCCESS) {
				error("bridge_get_data"
				      "(RM_FirstBP): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				break;
			}
		}
		if ((rc = bridge_get_data(mp_ptr, RM_BPID, &mpid))
		    != SLURM_SUCCESS) {
			error("bridge_get_data(RM_BPID): %s",
			      bg_err_str(rc));
			rc = SLURM_ERROR;
			break;
		}

		if (!mpid) {
			error("No MP ID was returned from database");
			goto end_it;
		}

		ba_mp = loc2ba_mp(mpid);

		if (!ba_mp) {
			fatal("Could not find coordinates for "
			      "MP ID %s", (char *) mpid);
		}
		free(mpid);


		snprintf(node_name_tmp,
			 sizeof(node_name_tmp),
			 "%s%s",
			 bg_conf->slurm_node_prefix,
			 ba_mp->coord_str);


		hostlist_push_host(hostlist, node_name_tmp);
	}
	bg_record->mp_str = hostlist_ranged_string_xmalloc(hostlist);
	hostlist_destroy(hostlist);
	debug3("got nodes of %s", bg_record->mp_str);
	// need to get the 000x000 range for nodes
	// also need to get coords

#ifdef HAVE_BGL
	if ((rc = bridge_get_data(block_ptr, RM_PartitionMode,
				  &bg_record->node_use))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_PartitionMode): %s",
		      bg_err_str(rc));
	}
#endif
	process_nodes(bg_record, true);
#ifdef HAVE_BGL
	/* get the images of the block */
	if ((rc = bridge_get_data(block_ptr,
				  RM_PartitionBlrtsImg,
				  &user_name))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_PartitionBlrtsImg): %s",
		      bg_err_str(rc));
		goto end_it;
	}
	if (!user_name) {
		error("No BlrtsImg was returned from database");
		goto end_it;
	}
	bg_record->blrtsimage = xstrdup(user_name);

	if ((rc = bridge_get_data(block_ptr,
				  RM_PartitionLinuxImg,
				  &user_name))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_PartitionLinuxImg): %s",
		      bg_err_str(rc));
		goto end_it;
	}
	if (!user_name) {
		error("No LinuxImg was returned from database");
		goto end_it;
	}
	bg_record->linuximage = xstrdup(user_name);

	if ((rc = bridge_get_data(block_ptr,
				  RM_PartitionRamdiskImg,
				  &user_name))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_PartitionRamdiskImg): %s",
		      bg_err_str(rc));
		goto end_it;
	}
	if (!user_name) {
		error("No RamdiskImg was returned from database");
		goto end_it;
	}
	bg_record->ramdiskimage = xstrdup(user_name);

#else
	if ((rc = bridge_get_data(block_ptr,
				  RM_PartitionCnloadImg,
				  &user_name))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_PartitionCnloadImg): %s",
		      bg_err_str(rc));
		goto end_it;
	}
	if (!user_name) {
		error("No CnloadImg was returned from database");
		goto end_it;
	}
	bg_record->linuximage = xstrdup(user_name);

	if ((rc = bridge_get_data(block_ptr,
				  RM_PartitionIoloadImg,
				  &user_name))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_PartitionIoloadImg): %s",
		      bg_err_str(rc));
		goto end_it;
	}
	if (!user_name) {
		error("No IoloadImg was returned from database");
		goto end_it;
	}
	bg_record->ramdiskimage = xstrdup(user_name);

#endif
	if ((rc = bridge_get_data(block_ptr,
				  RM_PartitionMloaderImg,
				  &user_name))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_PartitionMloaderImg): %s",
		      bg_err_str(rc));
		goto end_it;
	}
	if (!user_name) {
		error("No MloaderImg was returned from database");
		goto end_it;
	}
	bg_record->mloaderimage = xstrdup(user_name);
	/* This needs to happen or it will be trash after the
	   free_block_list */
	bg_record->bg_block = NULL;

	return bg_record;
end_it:
	error("Something bad happened with load of %s", bg_block_id);
	if (bg_recover) {
		error("Can't use %s not adding", bg_block_id);
		destroy_bg_record(bg_record);
		bg_record = NULL;
	}
	return bg_record;
}
#endif

extern int bridge_init(char *properties_file)
{
#ifdef HAVE_BG_FILES
	static const char *syms[] = {
		"rm_set_serial",
#ifdef HAVE_BGP
		"rm_get_BG",
		"rm_free_BG",
		"rm_new_ionode",
		"rm_free_ionode",
#else
		"rm_get_BGL",
		"rm_free_BGL",
#endif
		"rm_add_partition",
		"rm_get_partition",
		"rm_get_partition_info",
		"rm_modify_partition",
		"rm_set_part_owner",
		"rm_add_part_user",
		"rm_remove_part_user",
		"rm_remove_partition",
		"rm_get_partitions",
		"rm_get_partitions_info",
		"rm_get_job",
		"rm_get_jobs",
		"rm_get_nodecards",
		"rm_new_nodecard",
		"rm_free_nodecard",
		"rm_new_partition",
		"rm_free_partition",
		"rm_free_job",
		"rm_free_partition_list",
		"rm_free_job_list",
		"rm_free_nodecard_list",
		"rm_get_data",
		"rm_set_data",
		"jm_signal_job",
		"pm_create_partition",
		"pm_destroy_partition",
		"setSayMessageParams"
	};
	int n_syms;
	int rc;

	if (initialized)
		return 1;

	n_syms = sizeof( syms ) / sizeof( char * );

	initialized = true;
	if (!_get_syms(n_syms, syms, (void **) &bridge_api))
		return 0;
#ifdef BG_SERIAL
	debug("setting the serial to %s", BG_SERIAL);
	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.set_serial))(BG_SERIAL));
	slurm_mutex_unlock(&api_file_mutex);
	debug2("done %d", rc);
#else
	fatal("No BG_SERIAL is set, can't run.");
#endif
#endif
	return 1;

}

extern int bridge_fini()
{
	if (handle)
		dlclose(handle);
	bridge_status_fini();

	initialized = false;

	return SLURM_SUCCESS;
}

extern int bridge_get_size(int *size)
{
#ifdef HAVE_BG_FILES
	rm_size3D_t mp_size;
	int rc = SLURM_ERROR;

	if (!bg)
		return rc;

	if ((rc = bridge_get_data(bg, RM_Msize, &mp_size)) != SLURM_SUCCESS) {
		error("bridge_get_data(RM_Msize): %d", rc);
		return rc;
	}

	size[X] = mp_size.X;
	size[Y] = mp_size.Y;
	size[Z] = mp_size.Z;

#endif /* HAVE_BG_FILES */
	return SLURM_SUCCESS;
}

extern int bridge_setup_system()
{
#if defined HAVE_BG_FILES
	static bool inited = false;
	int rc;
	rm_BP_t *my_mp = NULL;
	int mp_num, i;
	char *mp_id = NULL;
	rm_location_t mp_loc;
	ba_mp_t *curr_mp;

	if (inited)
		return SLURM_SUCCESS;

	if (!bridge_init(NULL))
		return SLURM_ERROR;

	inited = true;

	if (!have_db2) {
		error("Can't access DB2 library, run from service node");
		return -1;
	}

#ifdef HAVE_BGL
	if (!getenv("DB2INSTANCE") || !getenv("VWSPATH")) {
		error("Missing DB2INSTANCE or VWSPATH env var.  "
		      "Execute 'db2profile'");
		return -1;
	}
#endif

	if (!bg) {
		if ((rc = bridge_get_bg(&bg)) != SLURM_SUCCESS) {
			error("bridge_get_BG(): %d", rc);
			return -1;
		}
	}

	if ((rc = bridge_get_data(bg, RM_BPNum, &mp_num)) != SLURM_SUCCESS) {
		error("bridge_get_data(RM_BPNum): %d", rc);
		mp_num = 0;
	}

	for (i=0; i<mp_num; i++) {

		if (i) {
			if ((rc = bridge_get_data(bg, RM_NextBP, &my_mp))
			    != SLURM_SUCCESS) {
				error("bridge_get_data(RM_NextBP): %d", rc);
				break;
			}
		} else {
			if ((rc = bridge_get_data(bg, RM_FirstBP, &my_mp))
			    != SLURM_SUCCESS) {
				error("bridge_get_data(RM_FirstBP): %d", rc);
				break;
			}
		}

		if ((rc = bridge_get_data(my_mp, RM_BPID, &mp_id))
		    != SLURM_SUCCESS) {
			error("bridge_get_data(RM_BPID): %d", rc);
			continue;
		}

		if (!mp_id) {
			error("No BP ID was returned from database");
			continue;
		}

		if ((rc = bridge_get_data(my_mp, RM_BPLoc, &mp_loc))
		    != SLURM_SUCCESS) {
			error("bridge_get_data(RM_BPLoc): %d", rc);
			continue;
		}

		if (mp_loc.X > DIM_SIZE[X]
		    || mp_loc.Y > DIM_SIZE[Y]
		    || mp_loc.Z > DIM_SIZE[Z]) {
			error("This location %c%c%c is not possible "
			      "in our system %c%c%c",
			      alpha_num[mp_loc.X],
			      alpha_num[mp_loc.Y],
			      alpha_num[mp_loc.Z],
			      alpha_num[DIM_SIZE[X]],
			      alpha_num[DIM_SIZE[Y]],
			      alpha_num[DIM_SIZE[Z]]);
			return 0;
		}

		curr_mp = &ba_main_grid[mp_loc.X][mp_loc.Y][mp_loc.Z];
		curr_mp->loc = xstrdup(mp_id);

		free(mp_id);
	}
#endif

	return SLURM_SUCCESS;
}

extern int bridge_block_create(bg_record_t *bg_record)
{
	int rc = SLURM_SUCCESS;

#if defined HAVE_BG_FILES
	_new_block((rm_partition_t **)&bg_record->bg_block);
#endif
	_pre_allocate(bg_record);

	if (bg_record->cpu_cnt < bg_conf->cpus_per_mp)
		rc = configure_small_block(bg_record);
	else
		rc = configure_block_switches(bg_record);

	if (rc == SLURM_SUCCESS)
		rc = _post_allocate(bg_record);

	return rc;
}

extern int bridge_block_boot(bg_record_t *bg_record)
{
#if defined HAVE_BG_FILES
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	if ((rc = _bg_errtrans((*(bridge_api.set_part_owner))(
				  bg_record->bg_block_id,
				  bg_conf->slurm_user_name)))
	    != SLURM_SUCCESS) {
		error("bridge_set_block_owner(%s,%s): %s",
		      bg_record->bg_block_id,
		      bg_conf->slurm_user_name,
		      bg_err_str(rc));
		slurm_mutex_unlock(&api_file_mutex);
		return rc;
	}

	rc = _bg_errtrans((*(bridge_api.create_partition))
			  (bg_record->bg_block_id));
	/* if (rc == BG_ERROR_INVALID_STATE) */
	/* 	rc = BG_ERROR_BOOT_ERROR; */

	slurm_mutex_unlock(&api_file_mutex);
	return rc;
#else
	info("block %s is ready", bg_record->bg_block_id);
	if (!block_ptr_exist_in_list(bg_lists->booted, bg_record))
	 	list_push(bg_lists->booted, bg_record);
	bg_record->state = BG_BLOCK_INITED;
	last_bg_update = time(NULL);
	return SLURM_SUCCESS;
#endif
}

extern int bridge_block_free(bg_record_t *bg_record)
{
#if defined HAVE_BG_FILES
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.destroy_partition))
			  (bg_record->bg_block_id));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;
#else
	return SLURM_SUCCESS;
#endif
}

extern int bridge_block_remove(bg_record_t *bg_record)
{
#if defined HAVE_BG_FILES
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.remove_partition))
			  (bg_record->bg_block_id));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;
#else
	return SLURM_SUCCESS;
#endif
}

extern int bridge_block_add_user(bg_record_t *bg_record, const char *user_name)
{
#if defined HAVE_BG_FILES
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.add_part_user))
			  (bg_record->bg_block_id, user_name));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;
#else
	return SLURM_SUCCESS;
#endif
}

extern int bridge_block_remove_user(bg_record_t *bg_record,
				    const char *user_name)
{
#if defined HAVE_BG_FILES
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.remove_part_user))
			  (bg_record->bg_block_id, user_name));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;
#else
	return SLURM_SUCCESS;
#endif
}

extern int bridge_block_sync_users(bg_record_t *bg_record)
{
	int returnc = SLURM_SUCCESS;
#ifdef HAVE_BG_FILES
	char *user;
	rm_partition_t *block_ptr = NULL;
	int rc, i, user_count, found=0;
	char *user_name = NULL;

	/* We can't use bridge_get_block_info here because users are
	   filled in there.  This function is very slow but necessary
	   here to get the correct block count and the users. */
	if ((rc = bridge_get_block(bg_record->bg_block_id, &block_ptr))
	    != SLURM_SUCCESS) {
		if (rc == BG_ERROR_INCONSISTENT_DATA
		    && bg_conf->layout_mode == LAYOUT_DYNAMIC)
			return SLURM_SUCCESS;

		error("bridge_get_block(%s): %s",
		      bg_record->bg_block_id,
		      bg_err_str(rc));
		return REMOVE_USER_ERR;
	}

	if ((rc = bridge_get_data(block_ptr, RM_PartitionUsersNum,
				  &user_count))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_PartitionUsersNum): %s",
		      bg_err_str(rc));
		returnc = REMOVE_USER_ERR;
		user_count = 0;
	} else
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("got %d users for %s", user_count,
			     bg_record->bg_block_id);

	if (bg_record->job_ptr) {
		select_jobinfo_t *jobinfo =
			bg_record->job_ptr->select_jobinfo->data;
		user_name = jobinfo->user_name;
	}

	for(i=0; i<user_count; i++) {
		if (i) {
			if ((rc = bridge_get_data(block_ptr,
						  RM_PartitionNextUser,
						  &user))
			    != SLURM_SUCCESS) {
				error("bridge_get_data"
				      "(RM_PartitionNextUser): %s",
				      bg_err_str(rc));
				returnc = REMOVE_USER_ERR;
				break;
			}
		} else {
			if ((rc = bridge_get_data(block_ptr,
						  RM_PartitionFirstUser,
						  &user))
			    != SLURM_SUCCESS) {
				error("bridge_get_data"
				      "(RM_PartitionFirstUser): %s",
				      bg_err_str(rc));
				returnc = REMOVE_USER_ERR;
				break;
			}
		}

		if (!user) {
			error("No user was returned from database");
			continue;
		}

		/* It has been found on L the block owner is not
		   needed as a regular user so we are now removing
		   it.  It is unknown if this is the case for P but we
		   believe it is.  If a problem does arise on P please
		   report and just uncomment this check.
		*/
		/* if (!strcmp(user, bg_conf->slurm_user_name)) { */
		/* 	free(user); */
		/* 	continue; */
		/* } */

		if (user_name && !strcmp(user, user_name)) {
			found=1;
			free(user);
			continue;
		}

		info("Removing user %s from Block %s",
		     user, bg_record->bg_block_id);
		if ((rc = _remove_block_user(bg_record->bg_block_id, user))
		    != SLURM_SUCCESS) {
			debug("user %s isn't on block %s",
			      user,
			      bg_record->bg_block_id);
		}
		free(user);
	}

	// no users currently, or we didn't find outselves in the lookup
	if (!found && user_name) {
		returnc = REMOVE_USER_FOUND;
		if ((rc = bridge_block_add_user(bg_record, user_name))
		    != SLURM_SUCCESS) {
			debug("couldn't add user %s to block %s",
			      user, bg_record->bg_block_id);
		}
	}

	if ((rc = bridge_free_block(block_ptr)) != SLURM_SUCCESS) {
		error("bridge_free_block(): %s", bg_err_str(rc));
	}
#endif
	return returnc;
}

/*
 * Download from MMCS the initial BG block information
 */
extern int bridge_blocks_load_curr(List curr_block_list)
{
	int rc = SLURM_SUCCESS;
#if defined HAVE_BG_FILES

	int mp_cnt;
	rm_partition_t *block_ptr = NULL;
	bg_record_t *bg_record = NULL;

	int block_number, block_count;
	char *bg_block_id = NULL;

	rm_partition_list_t *block_list = NULL;
	rm_partition_state_flag_t state = PARTITION_ALL_FLAG;

	bridge_setup_system();

	if (bg_recover) {
		if ((rc = bridge_get_blocks(state, &block_list))
		    != SLURM_SUCCESS) {
			error("2 rm_get_blocks(): %s", bg_err_str(rc));
			return SLURM_ERROR;
		}
	} else {
		if ((rc = bridge_get_blocks_info(state, &block_list))
		    != SLURM_SUCCESS) {
			error("2 rm_get_blocks_info(): %s", bg_err_str(rc));
			return SLURM_ERROR;
		}
	}

	if ((rc = bridge_get_data(block_list, RM_PartListSize, &block_count))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_PartListSize): %s",
		      bg_err_str(rc));
		block_count = 0;
	}

	info("querying the system for existing blocks");
	for(block_number=0; block_number<block_count; block_number++) {
		int state;
		if (block_number) {
			if ((rc = bridge_get_data(block_list,
						  RM_PartListNextPart,
						  &block_ptr))
			    != SLURM_SUCCESS) {
				error("bridge_get_data"
				      "(RM_PartListNextPart): %s",
				      bg_err_str(rc));
				break;
			}
		} else {
			if ((rc = bridge_get_data(block_list,
						  RM_PartListFirstPart,
						  &block_ptr))
			    != SLURM_SUCCESS) {
				error("bridge_get_data"
				      "(RM_PartListFirstPart): %s",
				      bg_err_str(rc));
				break;
			}
		}

		if ((rc = bridge_get_data(block_ptr, RM_PartitionID,
					  &bg_block_id))
		    != SLURM_SUCCESS) {
			error("bridge_get_data(RM_PartitionID): %s",
			      bg_err_str(rc));
			continue;
		}

		if (!bg_block_id) {
			error("No Block ID was returned from database");
			continue;
		}

		if (strncmp("RMP", bg_block_id, 3)) {
			free(bg_block_id);
			continue;
		}

		/* find BG Block record */
		if (!(bg_record = find_bg_record_in_list(
			      curr_block_list, bg_block_id))) {
			info("%s not found in the state file, adding",
			     bg_block_id);
			bg_record = _translate_object_to_block(
				block_ptr, bg_block_id);
			if (bg_record)
				list_push(curr_block_list, bg_record);
		}
		free(bg_block_id);
		bg_record->modifying = 1;
		/* New BG Block record */

		bg_record->job_running = NO_JOB_RUNNING;
		if ((rc = bridge_get_data(block_ptr, RM_PartitionState,
					  &state))
		    != SLURM_SUCCESS) {
			error("bridge_get_data(RM_PartitionState): %s",
			      bg_err_str(rc));
			continue;
		} else if (state == BG_BLOCK_BOOTING)
			bg_record->boot_state = 1;
		if (bg_record->state & BG_BLOCK_ERROR_FLAG)
			state |= BG_BLOCK_ERROR_FLAG;
		bg_record->state = state;
		debug3("Block %s is in state %s",
		       bg_record->bg_block_id,
		       bg_block_state_string(bg_record->state));

		if ((rc = bridge_get_data(block_ptr, RM_PartitionUsersNum,
					  &mp_cnt)) != SLURM_SUCCESS) {
			error("bridge_get_data(RM_PartitionUsersNum): %s",
			      bg_err_str(rc));
			continue;
		}
	}
	bridge_free_block_list(block_list);
#endif
	return rc;
}

extern void bridge_block_post_job(char *bg_block_id, struct job_record *job_ptr)
{
	int jobs = 0;
	rm_job_list_t *job_list = NULL;

#if defined HAVE_BG_FILES
	int live_states, rc;

	debug2("getting the job info");
	live_states = JOB_ALL_FLAG
		& (~JOB_TERMINATED_FLAG)
		& (~JOB_KILLED_FLAG)
		& (~JOB_ERROR_FLAG);

	if ((rc = _get_jobs(live_states, &job_list)) != SLURM_SUCCESS) {
		error("bridge_get_jobs(): %s", bg_err_str(rc));

		return;
	}

	if ((rc = bridge_get_data(job_list, RM_JobListSize, &jobs))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_JobListSize): %s",
		      bg_err_str(rc));
		jobs = 0;
	}
	debug2("job count %d",jobs);
#else
	/* simulate jobs running and need to be cleared from MMCS */
	jobs = 1;
#endif
	_remove_jobs_on_block_and_reset(job_list, jobs,	bg_block_id);
	if (job_ptr) {
		slurmctld_lock_t job_read_lock =
			{ NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
		lock_slurmctld(job_read_lock);
		if (job_ptr->magic == JOB_MAGIC) {
			/* This signals the job purger that the job
			   actually finished in the system.
			*/
			select_jobinfo_t *jobinfo =
				job_ptr->select_jobinfo->data;
			jobinfo->bg_record = NULL;
		}
		unlock_slurmctld(job_read_lock);
	}

#if defined HAVE_BG_FILES
	if ((rc = _free_job_list(job_list)) != SLURM_SUCCESS)
		error("bridge_free_job_list(): %s", bg_err_str(rc));
#endif
}

#if defined HAVE_BG_FILES
extern status_t bridge_get_bg(my_bluegene_t **bg)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.get_bg))(bg));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;
}

extern status_t bridge_free_bg(my_bluegene_t *bg)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.free_bg))(bg));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern uint16_t bridge_block_get_action(char *bg_block_id)
{
	return BG_BLOCK_ACTION_NONE;
}

extern int bridge_check_nodeboards(char *mp_loc)
{
	return 0;
}

extern int bridge_set_log_params(char *api_file_name, unsigned int level)
{
	static FILE *fp = NULL;
        FILE *fp2 = NULL;
	int rc = SLURM_SUCCESS;

	if (!bridge_init(NULL))
		return SLURM_ERROR;

	slurm_mutex_lock(&api_file_mutex);
	if (fp)
		fp2 = fp;

	fp = fopen(api_file_name, "a");

	if (fp == NULL) {
		error("can't open file for bridgeapi.log at %s: %m",
		      api_file_name);
		rc = SLURM_ERROR;
		goto end_it;
	}


	(*(bridge_api.set_log_params))(fp, level);
	/* In the libraries linked to from the bridge there are stderr
	   messages send which we would miss unless we dup this to the
	   log */
	//(void)dup2(fileno(fp), STDERR_FILENO);

	if (fp2)
		fclose(fp2);
end_it:
	slurm_mutex_unlock(&api_file_mutex);
	return rc;
}

extern status_t bridge_get_data(rm_element_t* element,
				enum rm_specification field, void *data)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	int *state = (int *) data;
	rm_connection_t *curr_conn = (rm_connection_t *)data;

	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.get_data))(element, field, data));

	/* Since these like to change from system to system, we have a
	   nice enum that doesn't in bg_enums.h, convert now. */
	switch (field) {
	case RM_PartitionState:
		state = (int *) data;
		switch (*state) {
		case RM_PARTITION_FREE:
			*state = BG_BLOCK_FREE;
			break;
		case RM_PARTITION_CONFIGURING:
			*state = BG_BLOCK_BOOTING;
			break;
#ifdef HAVE_BGL
		case RM_PARTITION_BUSY:
			*state = BG_BLOCK_BUSY;
			break;
#else
		case RM_PARTITION_REBOOTING:
			*state = BG_BLOCK_REBOOTING;
			break;
#endif
		case RM_PARTITION_READY:
			*state = BG_BLOCK_INITED;
			break;
		case RM_PARTITION_DEALLOCATING:
			*state = BG_BLOCK_TERM;
			break;
		case RM_PARTITION_ERROR:
			*state = BG_BLOCK_ERROR_FLAG;
			break;
		case RM_PARTITION_NAV:
			*state = BG_BLOCK_NAV;
			break;
		default:
			break;
		}
		break;
	case RM_PartitionOptions:
		break;
#ifdef HAVE_BGL
	case RM_PartitionMode:
		break;
#endif
	case RM_SwitchFirstConnection:
	case RM_SwitchNextConnection:
		curr_conn = (rm_connection_t *)data;
		switch(curr_conn->p1) {
		case RM_PORT_S1:
			curr_conn->p1 = 1;
			break;
		case RM_PORT_S2:
			curr_conn->p1 = 2;
			break;
		case RM_PORT_S4:
			curr_conn->p1 = 4;
			break;
		default:
			error("1 unknown port %d",
			      _port_enum(curr_conn->p1));
			return SLURM_ERROR;
		}

		switch(curr_conn->p2) {
		case RM_PORT_S0:
			curr_conn->p2 = 0;
			break;
		case RM_PORT_S3:
			curr_conn->p2 = 3;
			break;
		case RM_PORT_S5:
			curr_conn->p2 = 5;
			break;
		default:
			error("2 unknown port %d",
			      _port_enum(curr_conn->p2));
			return SLURM_ERROR;
		}
		break;
	case RM_PortID:
		state = (int *) data;
		(*state) = _port_enum(*state);
		break;
	default:
		break;
	}

	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_set_data(rm_element_t* element,
				enum rm_specification field, void *data)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.set_data))(element, field, data));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_free_nodecard_list(rm_nodecard_list_t *nc_list)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.free_nodecard_list))(nc_list));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_free_block(rm_partition_t *partition)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.free_partition))(partition));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_block_modify(char *bg_block_id,
				    int op, const void *data)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.modify_partition))
			  (bg_block_id, op, data));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_get_block(char *bg_block_id,
				 rm_partition_t **partition)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.get_partition))
			  (bg_block_id, partition));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_get_block_info(char *bg_block_id,
				      rm_partition_t **partition)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	/* this is here to make sure we don't lock up things with
	   polling and the long running get_BG call */
	rc = pthread_mutex_trylock(&api_file_mutex);
	if (rc == EBUSY)
		return rc;
	else if (rc) {
		errno = rc;
		error("%s:%d %s: pthread_mutex_trylock(): %m",
		      __FILE__, __LINE__, __CURRENT_FUNC__);
	}

	//slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.get_partition_info))
			  (bg_block_id, partition));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_get_blocks(rm_partition_state_flag_t flag,
				  rm_partition_list_t **part_list)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.get_partitions))(flag, part_list));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_get_blocks_info(rm_partition_state_flag_t flag,
				       rm_partition_list_t **part_list)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.get_partitions_info))(flag, part_list));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_free_block_list(rm_partition_list_t *part_list)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.free_partition_list))(part_list));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_new_nodecard(rm_nodecard_t **nodecard)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.new_nodecard))(nodecard));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_free_nodecard(rm_nodecard_t *nodecard)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.free_nodecard))(nodecard));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_get_nodecards(rm_bp_id_t bpid,
				     rm_nodecard_list_t **nc_list)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.get_nodecards))(bpid, nc_list));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

#ifdef HAVE_BGP
extern status_t bridge_new_ionode(rm_ionode_t **ionode)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.new_ionode))(ionode));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_free_ionode(rm_ionode_t *ionode)
{
	int rc = BG_ERROR_CONNECTION_ERROR;
	if (!bridge_init(NULL))
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = _bg_errtrans((*(bridge_api.free_ionode))(ionode));
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}
#else
extern int bridge_find_nodecard_num(rm_partition_t *block_ptr,
				    rm_nodecard_t *ncard,
				    int *nc_id)
{
	char *my_card_name = NULL;
	char *card_name = NULL;
	rm_bp_id_t mp_id = NULL;
	int num = 0;
	int i=0;
	int rc;
	rm_nodecard_list_t *ncard_list = NULL;
	rm_BP_t *curr_mp = NULL;
	rm_nodecard_t *ncard2;

	xassert(block_ptr);
	xassert(nc_id);

	if ((rc = bridge_get_data(ncard,
				  RM_NodeCardID,
				  &my_card_name))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_NodeCardID): %s",
		      bg_err_str(rc));
	}

	if ((rc = bridge_get_data(block_ptr,
				  RM_PartitionFirstBP,
				  &curr_mp))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_PartitionFirstBP): %s",
		      bg_err_str(rc));
	}
	if ((rc = bridge_get_data(curr_mp, RM_BPID, &mp_id))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_BPID): %d", rc);
		return SLURM_ERROR;
	}

	if ((rc = bridge_get_nodecards(mp_id, &ncard_list))
	    != SLURM_SUCCESS) {
		error("bridge_get_nodecards(%s): %d",
		      mp_id, rc);
		free(mp_id);
		return SLURM_ERROR;
	}
	free(mp_id);
	if ((rc = bridge_get_data(ncard_list, RM_NodeCardListSize, &num))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_NodeCardListSize): %s",
		      bg_err_str(rc));
		return SLURM_ERROR;
	}

	for(i=0; i<num; i++) {
		if (i) {
			if ((rc =
			     bridge_get_data(ncard_list,
					     RM_NodeCardListNext,
					     &ncard2)) != SLURM_SUCCESS) {
				error("bridge_get_data"
				      "(RM_NodeCardListNext): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				goto cleanup;
			}
		} else {
			if ((rc = bridge_get_data(ncard_list,
						  RM_NodeCardListFirst,
						  &ncard2)) != SLURM_SUCCESS) {
				error("bridge_get_data"
				      "(RM_NodeCardListFirst: %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				goto cleanup;
			}
		}
		if ((rc = bridge_get_data(ncard2,
					  RM_NodeCardID,
					  &card_name)) != SLURM_SUCCESS) {
			error("bridge_get_data(RM_NodeCardID: %s",
			      bg_err_str(rc));
			rc = SLURM_ERROR;
			goto cleanup;
		}
		if (strcmp(my_card_name, card_name)) {
			free(card_name);
			continue;
		}
		free(card_name);
		(*nc_id) = i;
		break;
	}
cleanup:
	free(my_card_name);
	return SLURM_SUCCESS;
}
#endif

#endif /* HAVE_BG_FILES */
