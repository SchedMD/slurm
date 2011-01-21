/*****************************************************************************\
 *  bridge_linker.c
 *
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov>, Danny Auble <da@llnl.gov>
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

extern "C" {
#include "bridge_linker.h"
#include "../block_allocator/block_allocator.h"
}

#if defined HAVE_BG_FILES && defined HAVE_BGQ

#include <bgsched/bgsched.h>
#include <bgsched/Block.h>
#include <bgsched/core/core.h>

using namespace std;
using namespace bgsched;
using namespace bgsched::core;

#endif

#define MAX_POLL_RETRIES    220
#define POLL_INTERVAL        3

/* Global variables */

bg_config_t *bg_conf = NULL;
bg_lists_t *bg_lists = NULL;
bool agent_fini = false;
time_t last_bg_update;
pthread_mutex_t block_state_mutex = PTHREAD_MUTEX_INITIALIZER;
int blocks_are_created = 0;
int num_unused_cpus = 0;

/* local vars */
static pthread_mutex_t api_file_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool initialized = false;

static void _b_midplane_del(void *object)
{
	b_midplane_t *b_midplane = (b_midplane_t *)object;

	if (b_midplane) {
		xfree(b_midplane->loc);
		xfree(b_midplane);
	}
}

extern int bridge_init(char *properties_file)
{
	if (initialized)
		return 1;

#if defined HAVE_BG_FILES && defined HAVE_BGQ
	bgsched::init(properties_file);
#endif

	initialized = true;

	return 1;

}

extern int bridge_fini()
{
	initialized = false;

	return SLURM_SUCCESS;
}

/*
 * Convert a BG API error code to a string
 * IN inx - error code from any of the BG Bridge APIs
 * RET - string describing the error condition
 */
extern const char *bridge_err_str(int inx)
{
	// switch (inx) {
	// }

	return "?";
}

extern int bridge_get_size(uint16_t *size)
{
	int i;
#if defined HAVE_BG_FILES && defined HAVE_BGQ
        Midplane::Coordinates bgq_size = core::getMachineSize();
#endif
	if (!bridge_init(NULL))
		return SLURM_ERROR;
	memset(size, 0, sizeof(uint16_t) * SYSTEM_DIMENSIONS);

#if defined HAVE_BG_FILES && defined HAVE_BGQ
	bgq = (ComputeHardware::ConstPtr &)bg;
	for (i=0; i<SYSTEM_DIMENSIONS; i++)
		size[i] = bgq_size[(bgsched::Dimension::Value)i];
#else
	for (i=0; i<SYSTEM_DIMENSIONS; i++)
		size[i] = DIM_SIZE[i];
#endif

	return SLURM_SUCCESS;
}

extern List bridge_get_midplanes()
{
	uint32_t a, x, y, z;
	List b_midplane_list = NULL;
#if defined HAVE_BG_FILES && defined HAVE_BGQ
	ComputeHardware::ConstPtr bgq;
#endif
	if (!bridge_init(NULL))
		return b_midplane_list;

#if defined HAVE_BG_FILES && defined HAVE_BGQ
	bgq = getComputeHardware();
#endif
	b_midplane_list = list_create(_b_midplane_del);

	for (a = 0; a <= DIM_SIZE[A]; ++a)
		for (x = 0; x <= DIM_SIZE[X]; ++x)
			for (y = 0; y <= DIM_SIZE[Y]; ++y)
				for (z = 0; z <= DIM_SIZE[Z]; ++z) {
					b_midplane_t *b_midplane =
						(b_midplane_t *)xmalloc(
							sizeof(b_midplane_t));
#if defined HAVE_BG_FILES && defined HAVE_BGQ
					Midplane::Coordinates coords =
						{{a, x, y, z}};
					Midplane::ConstPtr midplane =
						bgq->getMidplane(coords);
					b_midplane->loc = static_cast<char *>(
						midplane->getLocation());
#endif
					list_append(b_midplane_list,
						    b_midplane);
					b_midplane->coord[A] = a;
					b_midplane->coord[X] = x;
					b_midplane->coord[Y] = y;
					b_midplane->coord[Z] = z;
				}
	return b_midplane_list;
}

extern int bridge_block_create(bg_record_t *bg_record)
{
	ListIterator itr = NULL;
	int i;
	int rc = SLURM_SUCCESS;
	b_midplane_t *b_midplane;

#if defined HAVE_BG_FILES && defined HAVE_BGQ
	Block::Ptr block_ptr;
	Block::Midplanes midplanes;
        Block::PassthroughMidplanes pt_midplanes;
        Block::DimensionConnectivity conn_type;
	Midplane::Ptr midplane;
	Dimension dim;
#endif

	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (bg_record->small) {
		info("we can't make small blocks yet");
		return SLURM_ERROR;
	}

	if (!bg_record->bg_midplanes || !list_count(bg_record->bg_midplanes)) {
		error("There are no midplanes in this block?");
		return SLURM_ERROR;
	}

#if defined HAVE_BG_FILES && defined HAVE_BGQ
	itr = list_iterator_create(bg_record->bg_midplanes);
	while ((b_midplane = (b_midplane_t *)list_next(itr)))
		midplanes.push_back(b_midplane->loc);
	list_iterator_destroy(itr);

	itr = list_iterator_create(bg_record->bg_pt_midplanes);
	while ((b_midplane = (b_midplane_t *)list_next(itr)))
		pt_midplanes.push_back(b_midplane->loc);

	list_iterator_destroy(itr);

        for (i=0, dim = Dimension::A; i<SYSTEM_DIMENSIONS; i++, ++dim) {
		switch (bg_record->conn_type[i]) {
		case Block::Connectivity::Mesh:
			conn_type[dim] = Block::Connectivity::Mesh;
			break;
		case Block::Connectivity::Torus:
		default:
			conn_type[dim] = Block::Connectivity::Torus;
			break;
		}
	}
	block_ptr = Block::create(midplanes, pt_midplanes, conn_type);
	block_ptr->setName(bg_record->bg_block_id);
	block_ptr->addUser(bg_record->bg_block_id, bg_record->user_name);
	block_ptr->add(NULL);

	bg_record->block_ptr = (void *)&block_ptr;
#endif

	return rc;
}

/*
 * Boot a block. Block state expected to be FREE upon entry.
 * NOTE: This function does not wait for the boot to complete.
 * the slurm prolog script needs to perform the waiting.
 * NOTE: block_state_mutex needs to be locked before entering.
 */
extern int bridge_block_boot(bg_record_t *bg_record)
{
	int rc = SLURM_SUCCESS;

	if (bg_record->magic != BLOCK_MAGIC) {
		error("boot_block: magic was bad");
		return SLURM_ERROR;
	}

	if (!bg_record || !bg_record->bg_block_id)
		return SLURM_ERROR;

	if (!bridge_init(NULL))
		return SLURM_ERROR;

#if defined HAVE_BG_FILES && defined HAVE_BGQ
	if (bridge_block_set_owner(bg_record->bg_block_id,
				   bg_conf->slurm_user_name) != SLURM_SUCCESS)
		return SLURM_ERROR;

        try {
		Block::initiateBoot(bg_record->bg_block_id);
	} catch(...) {
                error("Boot block request failed ... continuing.");
		rc = SLURM_ERROR;
	}
	/* Set this here just to make sure we know we are suppose to
	   be booting.  Just incase the block goes free before we
	   notice we are configuring.
	*/
	bg_record->boot_state = BG_BLOCK_BOOTING;
#else
	if (!block_ptr_exist_in_list(bg_lists->booted, bg_record))
	 	list_push(bg_lists->booted, bg_record);
	bg_record->state = BG_BLOCK_INITED;
	last_bg_update = time(NULL);
#endif
	return rc;
}

extern int bridge_block_free(char *bg_block_id)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_block_id)
		return SLURM_ERROR;

#if defined HAVE_BG_FILES && defined HAVE_BGQ
       try {
		Block::initiateFree(bg_block_id);
	} catch(...) {
                error("Free block request failed ... continuing.");
		rc = SLURM_ERROR;
	}
#endif
	return rc;
}

extern int bridge_block_remove(char *bg_block_id)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_block_id)
		return SLURM_ERROR;

#if defined HAVE_BG_FILES && defined HAVE_BGQ
       try {
		Block::remove(bg_block_id);
	} catch(...) {
                error("Remove block request failed ... continuing.");
		rc = SLURM_ERROR;
	}
#endif
	return rc;
}

extern int bridge_block_add_user(char *bg_block_id, char *user_name)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_block_id || !user_name)
		return SLURM_ERROR;

#if defined HAVE_BG_FILES && defined HAVE_BGQ
        try {
		Block::addUser(bg_block_id, user_name);
	} catch(...) {
                error("Remove block request failed ... continuing.");
		rc = SLURM_ERROR;
	}
#endif
	return rc;
}

extern int bridge_block_remove_user(char *bg_block_id, char *user_name)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_block_id || !user_name)
		return SLURM_ERROR;

#if defined HAVE_BG_FILES && defined HAVE_BGQ
        try {
		Block::removeUser(bg_block_id, user_name);
	} catch(...) {
                error("Remove block request failed ... continuing.");
		rc = REMOVE_USER_ERR;
	}
#endif
	return rc;
}

extern int bridge_block_remove_all_users(char *bg_block_id, char *user_name)
{
	int rc = SLURM_SUCCESS;
#if defined HAVE_BG_FILES && defined HAVE_BGQ
	std::vector<std::string> vec;
#endif

	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_block_id)
		return SLURM_ERROR;

#if defined HAVE_BG_FILES && defined HAVE_BGQ
	vec = Block::getUsers(bg_block_id);
	if (vec.empty())
		return REMOVE_USER_NONE;
	for (iter = vec.begin(); iter != vec.end(); iter++) {
		if (user_name && !strcmp(user_name, iter))
			continue;
		if ((rc = bridge_block_remove_user(bg_block_id, user_name)
		     != SLURM_SUCCESS))
			break;
	}

#endif
	return rc;
}

extern int bridge_block_set_owner(char *bg_block_id, char *user_name)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_block_id || !user_name)
		return SLURM_ERROR;

	if ((rc = bridge_block_remove_all_users(bg_block_id, user_name))
	    == REMOVE_USER_ERR) {
		error("bridge_block_set_owner: Something happened removing "
		      "users from block %s",
		      bg_block_id);
		return SLURM_ERROR;
	} else if (rc == REMOVE_USER_NONE && user_name)
		rc = bridge_block_add_user(bg_block_id, user_name);

	return rc;
}

extern int bridge_block_remove_jobs(char *bg_block_id)
{
#if defined HAVE_BG_FILES && defined HAVE_BGQ
	std::vector<Job::ConstPtr> job_vec;
	JobFilter job_filter;
	JobFilter::Statuses job_statuses;
	vector<Job::ConstPtr>::iterator iter;
	int count = 0;
#endif

	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_block_id) {
		error("no block name given");
		return SLURM_ERROR;
	}

#if defined HAVE_BG_FILES && defined HAVE_BGQ

	job_filter.setComputeBlockName(bg_block_id);

	/* I think these are all the states we need. */
	job_statuses.insert(Job::Loading);
	job_statuses.insert(Job::Starting);
	job_statuses.insert(Job::Running);
	job_statuses.insert(Job::Ending);
	job_filter.setStatuses(&job_statuses);

	while (1) {
		if (count)
			sleep(POLL_INTERVAL);
		count++;
		job_vec = getJobs(job_filter);
		if (job_vec.empty())
			return SLURM_SUCCESS;

		for (iter = job_vec.begin(); iter != job_vec.end(); iter++)
			debug("waiting on job %u to finish on block %s",
			      *(iter)->getId(), bg_block_id);
	}
#endif
	return SLURM_SUCCESS;
}

extern int bridge_job_remove(void *job, char *bg_block_id)
{
	int count = 0;
	bgq_job_status_t job_state;
	bool is_history = false;
	uint32_t job_id;
#if defined HAVE_BG_FILES && defined HAVE_BGQ
	Job::ConstPtr job_ptr = (bgsched::Job::ConstPtr &)job;

	if (!job_ptr)
		return SLURM_ERROR;
	job_id = job_ptr->getId();
	debug("removing job %d from MMCS on block %s",
	      job_id, bg_block_id);
	while (1) {
		if (count)
			sleep(POLL_INTERVAL);
		count++;

		job_state = (bgq_job_status_t) job_ptr->getStatus().toValue();;
		is_history = job_ptr->isInHistory();

		/* FIX ME: We need to call something here to end the
		   job. */
		// if ((rc = bridge_free_job(job_rec)) != STATUS_OK)
		// 	error("bridge_free_job: %s", bg_err_str(rc));

		debug2("job %d on block %s is in state %d history %d",
		       job_id, bg_block_id, job_state, is_history);

		/* check the state and process accordingly */
		if (is_history) {
			debug2("Job %d on block %s isn't in the "
			       "active job table anymore, final state was %d",
			       job_id, bg_block_id, job_state);
			return SLURM_SUCCESS;
		} else if (job_state == BG_JOB_TERMINATED)
			return SLURM_SUCCESS;
		else if (job_state == BG_JOB_ENDING) {
			if (count > MAX_POLL_RETRIES)
				error("Job %d on block %s isn't dying, "
				      "trying for %d seconds", job_id,
				      bg_block_id, count*POLL_INTERVAL);
			continue;
		} else if (job_state == BG_JOB_ERROR) {
			error("job %d on block %s is in a error state.",
			      job_id, bg_block_id);

			//free_bg_block();
			return SLURM_ERROR;
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

		/* FIXME: I don't know how to cancel jobs yet. */
//		 rc = bridge_cancel_job(job_id);
		// rc = bridge_signal_job(job_id, SIGTERM);

		return SLURM_SUCCESS;
		// if (rc != STATUS_OK) {
		// 	if (rc == JOB_NOT_FOUND) {
		// 		debug("job %d on block %s removed from MMCS",
		// 		      job_ptr->getId(), bg_block_id);
		// 		return STATUS_OK;
		// 	}
		// 	if (rc == INCOMPATIBLE_STATE)
		// 		debug("job %d on block %s is in an "
		// 		      "INCOMPATIBLE_STATE",
		// 		      job_ptr->getId(), bg_block_id);
		// 	else
		// 		error("bridge_signal_job(%d): %s",
		// 		      job_ptr->getId(),
		// 		      bg_err_str(rc));
		// } else if (count > MAX_POLL_RETRIES)
		// 	error("Job %d on block %s is in state %d and "
		// 	      "isn't dying, and doesn't appear to be "
		// 	      "responding to SIGTERM, trying for %d seconds",
		// 	      job_ptr->getId(), bg_block_id,
		// 	      job_state, count*POLL_INTERVAL);

	}

	error("Failed to remove job %d from MMCS", job_id);
	return SLURM_ERROR;
#else
	return SLURM_SUCCESS;
#endif
}

// extern int bridge_set_log_params(char *api_file_name, unsigned int level)
// {
// 	static FILE *fp = NULL;
//         FILE *fp2 = NULL;
// 	int rc = SLURM_SUCCESS;

// 	if (!bridge_init())
// 		return SLURM_ERROR;

// 	slurm_mutex_lock(&api_file_mutex);
// 	if (fp)
// 		fp2 = fp;

// 	fp = fopen(api_file_name, "a");

// 	if (fp == NULL) {
// 		error("can't open file for bridgeapi.log at %s: %m",
// 		      api_file_name);
// 		rc = SLURM_ERROR;
// 		goto end_it;
// 	}


// 	(*(bridge_api.set_log_params))(fp, level);
// 	/* In the libraries linked to from the bridge there are stderr
// 	   messages send which we would miss unless we dup this to the
// 	   log */
// 	//(void)dup2(fileno(fp), STDERR_FILENO);

// 	if (fp2)
// 		fclose(fp2);
// end_it:
// 	slurm_mutex_unlock(&api_file_mutex);
//  	return rc;
// }


