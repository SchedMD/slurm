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


#include "bridge_linker.h"
#include "../block_allocator/block_allocator.h"

#if defined HAVE_BG_FILES && defined HAVE_BGQ

#include <bgsched/bgsched.h>
#include <bgsched/Block.h>
#include <bgsched/core/core.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_POLL_RETRIES    220
#define POLL_INTERVAL        3

using namespace std;
using namespace bgsched;
using namespace bgsched::core;

pthread_mutex_t api_file_mutex = PTHREAD_MUTEX_INITIALIZER;
bool initialized = false;
bool have_db2 = true;
void *handle = NULL;

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

	bgsched::init(properties_file);


	initialized = true;

	return 1;

}

extern int bridge_fini()
{
	initialized = false;

	return SLURM_SUCCESS;
}

extern int bridge_get_bg(my_bluegene_t **bg)
{
	int rc = SLURM_ERROR;
	if (!bridge_init(NULL))
		return rc;
	try {
		ComputeHardware::ConstPtr bgq = getComputeHardware();
		*bg = (my_bluegene_t *)bgq;
		rc = SLURM_SUCCESS;
	} catch (...) { // Handle all exceptions
		error(" Unexpected error calling getComputeHardware");
		*bg = NULL;
	}

	return rc;
}

extern int bridge_get_size(my_bluegene_t *bg, uint32_t *size)
{
	ComputeHardware::ConstPtr bgq;
	int i;

	if (!bridge_init(NULL) || !bg)
		return SLURM_ERROR;
	bgq = (ComputeHardware::ConstPtr)bg;
	memset(size, 0, sizeof(uint32_t) * SYSTEM_DIMENSIONS);
	for (i=0; i<SYSTEM_DIMENSIONS; i++)
		size[i] = bgq->getMidplaneSize((bgsched::Dimension::Value)i);

	return size;
}

extern List bridge_get_map(my_bluegene_t *bg)
{
	ComputeHardware::ConstPtr bgq;
	uint32_t a, b, c, d;
	List b_midplane_list = list_create(_b_midplane_del);

	if (!bridge_init(NULL) || !bg)
		return SLURM_ERROR;
	bgq = (ComputeHardware::ConstPtr)bg;

	for (a = 0; a < bgq->getMachineSize(Dimension::A); ++a)
		for (b = 0; b < bgq->getMachineSize(Dimension::B); ++b)
			for (c = 0; c < bgq->getMachineSize(Dimension::C); ++c)
				for (d = 0;
				     d < bgq->getMachineSize(Dimension::D);
				     ++d) {
					Midplane::Coordinates coords =
						{{a, b, c, d}};
					Midplane::ConstPtr midplane =
						bgq->getMidplane(coords);
					b_midplane_t *b_midplane =
						xmalloc(sizeof(b_midplane_t));

					list_append(bp_map_list, b_midplane);
					b_midplane->midplane = midplane;
					b_midplane->loc =
						midplane->getLocation();
					b_midplane->coord[A] = a;
					b_midplane->coord[X] = b;
					b_midplane->coord[Y] = c;
					b_midplane->coord[Z] = d;
				}
	return b_midplane_list;
}

extern int bridge_create_block(bg_record_t *bg_record)
{
	Block::Ptr block_ptr;
	Block::Midplanes midplanes;
        Block::PassthroughMidplanes pt_midplanes;
        Block::DimensionConnectivity conn_type;
	ListIterator itr = NULL;
	Midplane::ConstPtr midplane;
	int i;
	int rc = SLURM_SUCCESS;

	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (bg_record->block_ptr)
		return SLURM_ERROR;

	if (bg_record->small) {
		info("we can't make small blocks yet");
		return SLURM_ERROR;
	}

	if (!bg_record->bg_midplanes || !list_count(bg_record->bg_midplanes)) {
		error("There are no midplanes in this block?");
		return SLURM_ERROR;
	}

	itr = list_iterator_create(bg_record->bg_midplanes);
	while ((midplane == (Midplane::ConstPtr)list_next(itr))) {
		midplanes.push_back(midplane->getLocation());
	}
	list_iterator_destroy(itr);

	itr = list_iterator_create(bg_record->bg_pt_midplanes);
	while ((midplane == (Midplane::ConstPtr)list_next(itr))) {
		pt_midplanes.push_back(midplane->getLocation());
	}
	list_iterator_destroy(itr);

        for (i=A; i<Z; i++)
		conn_type[i] = bg_record->conn_type[i];

	block_ptr = Block::create(midplanes, pt_midplanes, conn_type);
	block_ptr->setName(bg_record->bg_block_id);
	block_ptr->addUser(bg_record->bg_block_id, bg_record->user_name);
	block_ptr->add(NULL);

	midplanes.clear();
        pt_midplanes.clear();
        conn_type.clear();

	bg_record->block_ptr = (void *)block_ptr;

	return rc;
}

extern int bridge_boot_block(char *name)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!name)
		return SLURM_ERROR;

        try {
		Block::initiateBoot(name);
	} catch(...) {
                error("Boot block request failed ... continuing.");
		rc = SLURM_ERROR;
	}

	return rc;
}

extern int bridge_free_block(char *name)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!name)
		return SLURM_ERROR;

        try {
		Block::initiateFree(name);
	} catch(...) {
                error("Free block request failed ... continuing.");
		rc = SLURM_ERROR;
	}

	return rc;
}

extern int bridge_remove_block(char *name)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!name)
		return SLURM_ERROR;

        try {
		Block::remove(name);
	} catch(...) {
                error("Remove block request failed ... continuing.");
		rc = SLURM_ERROR;
	}

	return rc;
}

extern int bridge_set_block_owner(char *bg_block_id, char *user_name)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!name)
		return SLURM_ERROR;

        try {
		Block::addUser(bg_block_id, user_name);
	} catch(...) {
                error("Remove block request failed ... continuing.");
		rc = SLURM_ERROR;
	}

	return rc;
}

extern List bridge_block_get_jobs(bg_record_t *bg_record)
{
	List ret_list = NULL;
	std::vector<Job::Id> job_vec;
	Block::Ptr block_ptr;
	vector<Job::Id>::iterator iter;

	if (!bridge_init(NULL))
		return NULL;

	xassert(bg_record);
	xassert(bg_record->block_ptr);
	block_ptr = (Block::Ptr)bg_record->block_ptr;

	job_vec = block_ptr->getJobIds();
	ret_list = list_create(NULL);
	if (job_vec.empty())
		return ret_list;

	for (iter = job_vec.begin(); iter != job_vec.end(); iter++)
		list_append(ret_list, iter);

	return ret_list;
}

extern int bridge_job_remove(void *job, char *bg_block_id)
{
	int rc;
	int count = 0;
	bgq_job_status_t job_state;
	bool is_history = false;
	uint32_t job_id;
	/* I don't think this is correct right now. */
	Job::ConstPtr job_ptr = job;

	if (!job_ptr)
		return SLURM_ERROR;
	job_id = job_ptr->getId();
	debug("removing job %d from MMCS on block %s",
	      job_id, block_id);
	while (1) {
		if (count)
			sleep(POLL_INTERVAL);
		count++;

		job_state = job_ptr->getStatus();
		is_history = job_ptr->isInHistory();

		/* FIX ME: We need to call something here to end the
		   job. */
		// if ((rc = bridge_free_job(job_rec)) != STATUS_OK)
		// 	error("bridge_free_job: %s", bg_err_str(rc));

		debug2("job %d on block %s is in state %d history %d",
		       job_id, block_id, job_state, is_history);

		/* check the state and process accordingly */
		if (is_history) {
			debug2("Job %d on block %s isn't in the "
			       "active job table anymore, final state was %d",
			       job_id, block_id, job_state);
			return SLURM_SUCCESS;
		} else if (job_state == BG_JOB_TERMINATED)
			return SLURM_SUCCESS;
		else if (job_state == BG_JOB_ENDING) {
			if (count > MAX_POLL_RETRIES)
				error("Job %d on block %s isn't dying, "
				      "trying for %d seconds", job_id,
				      block_id, count*POLL_INTERVAL);
			continue;
		} else if (job_state == BG_JOB_ERROR) {
			error("job %d on block %s is in a error state.",
			      job_id, block_id);

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
		// 		      job_ptr->getId(), block_id);
		// 		return STATUS_OK;
		// 	}
		// 	if (rc == INCOMPATIBLE_STATE)
		// 		debug("job %d on block %s is in an "
		// 		      "INCOMPATIBLE_STATE",
		// 		      job_ptr->getId(), block_id);
		// 	else
		// 		error("bridge_signal_job(%d): %s",
		// 		      job_ptr->getId(),
		// 		      bg_err_str(rc));
		// } else if (count > MAX_POLL_RETRIES)
		// 	error("Job %d on block %s is in state %d and "
		// 	      "isn't dying, and doesn't appear to be "
		// 	      "responding to SIGTERM, trying for %d seconds",
		// 	      job_ptr->getId(), block_id,
		// 	      job_state, count*POLL_INTERVAL);

	}

	error("Failed to remove job %d from MMCS", job_id);
	return SLURM_ERROR;
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

#ifdef __cplusplus
}
#endif

#endif /* HAVE_BG_FILES */


