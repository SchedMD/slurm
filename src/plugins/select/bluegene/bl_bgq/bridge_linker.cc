/*****************************************************************************\
 *  bridge_linker.cc
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#if HAVE_CONFIG_H
/* needed to figure out if HAVE_BG_FILES is set */
#  include "config.h"
#endif

#ifdef HAVE_BG_FILES
/* These need to be the first declared since on line 187 of
 * /bgsys/drivers/ppcfloor/extlib/include/log4cxx/helpers/transcoder.h
 * there is a nice generic BUFSIZE declared and the BUFSIZE declared
 * elsewhere in SLURM will cause errors when compiling.
 */
#include <log4cxx/fileappender.h>
#include <log4cxx/logger.h>
#include <log4cxx/patternlayout.h>

#endif

extern "C" {
#include "../ba_bgq/block_allocator.h"
#include "src/common/parse_time.h"
#include "src/common/uid.h"
}

#include "bridge_status.h"

/* local vars */
//static pthread_mutex_t api_file_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool initialized = false;


#ifdef HAVE_BG_FILES

// For future code
//
// static int _check_version(
// 	const unsigned major, const unsigned minor, const unsigned micro)
// {
// 	if ((version::major > major)
// 	    || (version::major == major
// 		&& version::minor > minor)
// 	    || (version::major == major
// 		&& version::minor == minor
// 		&& version::mod >= micro))
// 		return true;

// 	return false;
// }

/* ba_system_mutex needs to be locked before coming here */
static void _setup_ba_mp(int level, uint16_t *coords,
			 ComputeHardware::ConstPtr bgqsys)
{
	ba_mp_t *ba_mp;
	Midplane::ConstPtr mp_ptr;
	int i;

	if (!bgqsys) {
		if (bg_recover != NOT_FROM_CONTROLLER)
			fatal("_setup_ba_mp: No ComputeHardware ptr");
		else {
			error("_setup_ba_mp: can't talk to the database");
			return;
		}
	}
	if (level > SYSTEM_DIMENSIONS)
		return;

	if (level < SYSTEM_DIMENSIONS) {
		for (coords[level] = 0;
		     coords[level] < DIM_SIZE[level];
		     coords[level]++) {
			/* handle the outter dims here */
			_setup_ba_mp(level+1, coords, bgqsys);
		}
		return;
	}

	if (!(ba_mp = coord2ba_mp(coords))
	    || !(mp_ptr = bridge_get_midplane(bgqsys, ba_mp)))
		return;

	ba_mp->loc = xstrdup(mp_ptr->getLocation().c_str());

	ba_mp->nodecard_loc =
		(char **)xmalloc(sizeof(char *) * bg_conf->mp_nodecard_cnt);
	for (i=0; i<bg_conf->mp_nodecard_cnt; i++) {
		NodeBoard::ConstPtr nb_ptr = bridge_get_nodeboard(mp_ptr, i);
		if (nb_ptr)
			ba_mp->nodecard_loc[i] =
				xstrdup(nb_ptr->getLocation().c_str());
	}
}

static bg_record_t * _translate_object_to_block(const Block::Ptr &block_ptr)
{
	bg_record_t *bg_record = (bg_record_t *)xmalloc(sizeof(bg_record_t));
	Block::Midplanes midplane_vec;
	hostlist_t hostlist;
	char *node_char = NULL;
	char mp_str[256];
	select_ba_request_t ba_request;

	bg_record->magic = BLOCK_MAGIC;
	bg_record->bg_block_id = xstrdup(block_ptr->getName().c_str());
	bg_record->cnode_cnt = block_ptr->getComputeNodeCount();
	bg_record->cpu_cnt = bg_conf->cpu_ratio * bg_record->cnode_cnt;

	if (block_ptr->isSmall()) {
		char bitstring[BITSIZE];
		int io_cnt, io_start, len;
		Block::NodeBoards nodeboards =
			block_ptr->getNodeBoards();
		int nb_cnt = nodeboards.size();
		std::string nb_name = *(nodeboards.begin());

		if ((io_cnt = nb_cnt * bg_conf->io_ratio))
			io_cnt--;

		/* From the first nodecard id we can figure
		   out where to start from with the alloc of ionodes.
		*/
		len = nb_name.length()-2;
		io_start = atoi((char*)nb_name.c_str()+len) * bg_conf->io_ratio;

		/* sanity check: we have seen (bug 1514) the wrong
		   nodeboard given for a block at times.  The only
		   time we have seen this is when the disk was full on
		   the sn.  It is unclear if this is the root of the
		   problem or not, but at least the seg fault will not
		   happen in this instance.
		*/
		if ((io_start + io_cnt) >= bg_conf->ionodes_per_mp) {
			fatal("_translate_object_to_block: For some reason "
			      "block %s claims to use nodeboard "
			      "%s(starting IOnode %d) using %d extra IOnodes, "
			      "but that would put us over the number "
			      "of IOnodes we have on the midplane %d.  "
			      "Something is most likely wrong with this "
			      "block definition from the API/database.  "
			      "Fixing/removing this block in "
			      "the database is needed to resolve this issue.",
			      bg_record->bg_block_id, nb_name.c_str(),
			      io_start, io_cnt, bg_conf->ionodes_per_mp);
		}

		bg_record->ionode_bitmap = bit_alloc(bg_conf->ionodes_per_mp);

		/* Set the correct ionodes being used in this block */
		bit_nset(bg_record->ionode_bitmap,
			 io_start, io_start+io_cnt);
		bit_fmt(bitstring, BITSIZE, bg_record->ionode_bitmap);
		ba_set_ionode_str(bg_record);
		debug3("%s uses cnodes %s",
		       bg_record->bg_block_id,
		       bg_record->ionode_str);
		bg_record->conn_type[0] = SELECT_SMALL;
	} else {
		for (Dimension dim=Dimension::A; dim<=Dimension::D; dim++) {
			try {
				bg_record->conn_type[dim] =
					block_ptr->isTorus(dim) ?
					SELECT_TORUS : SELECT_MESH;
			} catch (const bgsched::InputException& err) {
				bridge_handle_input_errors(
					"Block::isTorus",
					err.getError().toValue(),
					NULL);
			} catch (...) {
				error("Unknown error from Block::isTorus.");
			}
		}
		/* Set the bitmap blank here if it is a full
		   node we don't want anything set we also
		   don't want the bg_record->ionode_str set.
		*/
		bg_record->ionode_bitmap =
			bit_alloc(bg_conf->ionodes_per_mp);
	}

	hostlist = hostlist_create(NULL);
	midplane_vec = block_ptr->getMidplanes();
	slurm_mutex_lock(&ba_system_mutex);
	BOOST_FOREACH(const std::string midplane, midplane_vec) {
		char temp[256];
		ba_mp_t *curr_mp = loc2ba_mp((char *)midplane.c_str());
		if (!curr_mp) {
			error("Unknown midplane for %s",
			      midplane.c_str());
			continue;
		}
		snprintf(temp, sizeof(temp), "%s%s",
			 bg_conf->slurm_node_prefix,
			 curr_mp->coord_str);

		hostlist_push(hostlist, temp);
	}
	slurm_mutex_unlock(&ba_system_mutex);
	bg_record->mp_str = hostlist_ranged_string_xmalloc(hostlist);
	hostlist_destroy(hostlist);
	debug3("got nodes of %s", bg_record->mp_str);

	process_nodes(bg_record, true);

	/* we are just going to go and destroy this block so
	   just get the name and continue. */
	if (!bg_recover)
		return bg_record;

	reset_ba_system(false);
	if (ba_set_removable_mps(bg_record->mp_bitmap, 1) != SLURM_SUCCESS)
		fatal("It doesn't seem we have a bitmap for %s.  "
		      "YOU MUST COLDSTART",
		      bg_record->bg_block_id);

	if (bg_record->ba_mp_list)
		list_flush(bg_record->ba_mp_list);
	else
		bg_record->ba_mp_list = list_create(destroy_ba_mp);

	memset(&ba_request, 0, sizeof(ba_request));
	memcpy(ba_request.geometry, bg_record->geo, sizeof(bg_record->geo));
	memcpy(ba_request.conn_type, bg_record->conn_type,
	       sizeof(bg_record->conn_type));
	node_char = set_bg_block(bg_record->ba_mp_list, &ba_request);
	memcpy(bg_record->start, ba_request.start, sizeof(bg_record->start));
	ba_reset_all_removed_mps();
	if (!node_char)
		fatal("I was unable to make the requested block %s.  "
		      "YOU MUST COLDSTART",
		      bg_record->bg_block_id);

	snprintf(mp_str, sizeof(mp_str), "%s%s",
		 bg_conf->slurm_node_prefix,
		 node_char);

	xfree(node_char);
	if (strcmp(mp_str, bg_record->mp_str)) {
		fatal("Couldn't make unknown block %s in our wiring.  "
		      "Something is wrong with our algo.  Remove this block "
		      "to continue (found %s, but allocated %s) "
		      "YOU MUST COLDSTART",
		      bg_record->bg_block_id, mp_str, bg_record->mp_str);
	}

	return bg_record;
}
#endif

static int _block_wait_for_jobs(char *bg_block_id, struct job_record *job_ptr)
{
#ifdef HAVE_BG_FILES
	std::vector<Job::ConstPtr> job_vec;
	JobFilter job_filter;
	JobFilter::Statuses job_statuses;
	uint32_t job_id = 0;
#endif

	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_block_id) {
		error("no block name given");
		return SLURM_ERROR;
	}

	/* This code can be used to simulate having a job hang in the
	 * database.
	 */
	// if (job_ptr && (job_ptr->magic == JOB_MAGIC)) {
	// 	uint32_t job_id = job_ptr->job_id;
	// 	while (1) {
	// 		debug("waiting on slurm job %u to "
	// 		      "finish on block %s",
	// 		      job_id, bg_block_id);
	// 		sleep(3);
	// 		if (job_ptr->magic != JOB_MAGIC) {
	// 			info("bad magic");
	// 			break;
	// 		} else if (IS_JOB_COMPLETED(job_ptr)) {
	// 			info("job completed");
	// 			break;
	// 		}
	// 	}
	// }
#ifdef HAVE_BG_FILES

	job_filter.setComputeBlockName(bg_block_id);

	/* I think these are all the states we need. */
	job_statuses.insert(Job::Setup);
	job_statuses.insert(Job::Loading);
	job_statuses.insert(Job::Starting);
	job_statuses.insert(Job::Running);
	job_statuses.insert(Job::Cleanup);
	job_filter.setStatuses(&job_statuses);

	if (job_ptr && (job_ptr->magic == JOB_MAGIC)) {
		char tmp_char[16];
		job_id = job_ptr->job_id;
		snprintf(tmp_char, sizeof(tmp_char), "%u", job_id);
		job_filter.setSchedulerData(tmp_char);
	}

	while (1) {
		try {
			job_vec = getJobs(job_filter);
			if (job_vec.empty())
				return SLURM_SUCCESS;

			BOOST_FOREACH(const Job::ConstPtr& job, job_vec) {
				if (job_id)
					debug("waiting on mmcs job %lu "
					      "in slurm job %u to "
					      "finish on block %s",
					      job->getId(), job_id,
					      bg_block_id);
				else
					debug("waiting on mmcs job %lu to "
					      "finish on block %s",
					      job->getId(), bg_block_id);
			}
		} catch (const bgsched::DatabaseException& err) {
			bridge_handle_database_errors("getJobs",
						      err.getError().toValue());
		} catch (const bgsched::InternalException& err) {
			bridge_handle_internal_errors("getJobs",
						      err.getError().toValue());
		} catch (...) {
			error("Unknown error from getJobs.");
		}
		sleep(POLL_INTERVAL);
	}
#endif
	return SLURM_SUCCESS;
}

static void _remove_jobs_on_block_and_reset(char *block_id,
					    struct job_record *job_ptr)
{
	char *mp_str = NULL;
	bg_record_t *bg_record = NULL;
	int job_remove_failed = 0;
	slurmctld_lock_t job_read_lock =
		{ NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	if (!block_id) {
		error("_remove_jobs_on_block_and_reset: no block name given");
		return;
	}

	if (_block_wait_for_jobs(block_id, job_ptr) != SLURM_SUCCESS)
		job_remove_failed = 1;

	/* remove the block's users */

	/* Lock job read before block to avoid
	 * issues where a step could complete after the job completion
	 * has taken place (since we are on a thread here).
	 */
	if (job_ptr)
		lock_slurmctld(job_read_lock);
	slurm_mutex_lock(&block_state_mutex);
	bg_record = find_bg_record_in_list(bg_lists->main, block_id);
	if (bg_record) {
		if (job_remove_failed) {
			if (bg_record->mp_str)
				mp_str = xstrdup(bg_record->mp_str);
			else
				error("Block %s doesn't have a node list.",
				      block_id);
		}

		bg_reset_block(bg_record, job_ptr);
	} else if (bg_conf->layout_mode == LAYOUT_DYNAMIC) {
		debug2("Hopefully we are destroying this block %s "
		       "since it isn't in the bg_lists->main",
		       block_id);
	}

	slurm_mutex_unlock(&block_state_mutex);
	if (job_ptr) {
		if (job_ptr->magic == JOB_MAGIC) {
			/* This signals the job purger that the job
			   actually finished in the system.
			*/
			select_jobinfo_t *jobinfo = (select_jobinfo_t *)
				job_ptr->select_jobinfo->data;
			jobinfo->bg_record = NULL;
			jobinfo->cleaning = 0;
		}
		unlock_slurmctld(job_read_lock);
	}

	/* avoid locking issues just do this afterwards. */
	if (mp_str) {
		slurm_drain_nodes(mp_str,
				  (char *)"_term_agent: Couldn't remove job",
				  slurm_get_slurm_user_id());
		xfree(mp_str);
	}

}

extern int bridge_init(char *properties_file)
{
	if (initialized)
		return 1;

#ifdef HAVE_BG_FILES
	if (!properties_file)
		properties_file = (char *)"";
	try {
		bgsched::init(properties_file);
	} catch (const bgsched::InitializationException& err) {
		bridge_handle_init_errors("bgsched::init",
					  err.getError().toValue());
		fatal("can't init bridge");
	} catch (...) {
		fatal("Unknown error from bgsched::init, can't continue");
	}
#endif
	initialized = true;

	return 1;
}

extern int bridge_fini()
{
	initialized = false;
	if (bg_recover != NOT_FROM_CONTROLLER)
		bridge_status_fini();

	return SLURM_SUCCESS;
}

extern int bridge_get_size(int *size)
{
	if (!bridge_init(NULL))
		return SLURM_ERROR;
#ifdef HAVE_BG_FILES
	memset(size, 0, sizeof(int) * SYSTEM_DIMENSIONS);

	try {
		Coordinates bgq_size = core::getMachineSize();
		for (int dim=0; dim< SYSTEM_DIMENSIONS; dim++)
			size[dim] = bgq_size[dim];
	} catch (const bgsched::DatabaseException& err) {
		bridge_handle_database_errors("core::getMachineSize",
					      err.getError().toValue());
	} catch (...) {
		error("Unknown error from core::getMachineSize");
	}
#endif

	return SLURM_SUCCESS;
}

extern int bridge_setup_system()
{
	static bool inited = false;

	if (inited)
		return SLURM_SUCCESS;

	if (!bridge_init(NULL))
		return SLURM_ERROR;

	inited = true;

	slurm_mutex_lock(&ba_system_mutex);
	assert(ba_main_grid);

#ifdef HAVE_BG_FILES
	uint16_t coords[SYSTEM_DIMENSIONS];
	_setup_ba_mp(0, coords, bridge_get_compute_hardware());
#endif
	slurm_mutex_unlock(&ba_system_mutex);

	return SLURM_SUCCESS;
}

extern int bridge_block_create(bg_record_t *bg_record)
{
	int rc = SLURM_SUCCESS;

#ifdef HAVE_BG_FILES
	Block::Ptr block_ptr;
	Block::Midplanes midplanes;
	Block::NodeBoards nodecards;
        Block::PassthroughMidplanes pt_midplanes;
        Block::DimensionConnectivity conn_type;
	Midplane::Ptr midplane;
	Dimension dim;
	ba_mp_t *ba_mp = NULL;
#endif

	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_record->ba_mp_list || !list_count(bg_record->ba_mp_list)) {
		error("There are no midplanes in this block?");
		return SLURM_ERROR;
	}

	if (!bg_record->bg_block_id) {
		struct tm my_tm;
		struct timeval my_tv;
		/* set up a common unique name */
		gettimeofday(&my_tv, NULL);
		localtime_r(&my_tv.tv_sec, &my_tm);
		bg_record->bg_block_id = xstrdup_printf(
			"RMP%2.2d%2.2s%2.2d%2.2d%2.2d%3.3ld",
			my_tm.tm_mday, mon_abbr(my_tm.tm_mon),
			my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec,
			my_tv.tv_usec/1000);
#ifndef HAVE_BG_FILES
		/* Since we divide by 1000 here we need to sleep that
		   long to get a unique id. It takes longer than this
		   in a real system so we don't worry about it. */
		usleep(1000);
#endif
	}


#ifdef HAVE_BG_FILES
	if (bg_record->conn_type[0] == SELECT_SMALL) {
		bool use_nc[bg_conf->mp_nodecard_cnt];
		int i, nc_pos = 0, num_ncards = 0;

		num_ncards = bg_record->cnode_cnt/bg_conf->nodecard_cnode_cnt;
		if (num_ncards < 1) {
			error("You have to have at least 1 nodecard to make "
			      "a small block I got %d/%d = %d",
			      bg_record->cnode_cnt, bg_conf->nodecard_cnode_cnt,
			      num_ncards);
			return SLURM_ERROR;
		}
		memset(use_nc, 0, sizeof(use_nc));

		/* find out how many nodecards to get for each ionode */
		for (i = 0; i<bg_conf->ionodes_per_mp; i++) {
			if (bit_test(bg_record->ionode_bitmap, i)) {
				for (int j=0; j<bg_conf->nc_ratio; j++)
					use_nc[nc_pos+j] = 1;
			}
			nc_pos += bg_conf->nc_ratio;
		}
		// char tmp_char[256];
		// format_node_name(bg_record, tmp_char, sizeof(tmp_char));
		// info("creating %s %s", bg_record->bg_block_id, tmp_char);
		ba_mp = (ba_mp_t *)list_peek(bg_record->ba_mp_list);
		/* Since the nodeboard locations aren't set up in the
		   copy of this pointer we need to go out a get the
		   real one from the system and use it.
		*/
		slurm_mutex_lock(&ba_system_mutex);
		ba_mp = coord2ba_mp(ba_mp->coord);
		for (i=0; i<bg_conf->mp_nodecard_cnt; i++) {
			if (use_nc[i] && ba_mp)
				nodecards.push_back(ba_mp->nodecard_loc[i]);
		}
		slurm_mutex_unlock(&ba_system_mutex);

		try {
			block_ptr = Block::create(nodecards);
			rc = SLURM_SUCCESS;
		} catch (const bgsched::InputException& err) {
			rc = bridge_handle_input_errors(
				"Block::createSmallBlock",
				err.getError().toValue(),
				bg_record);
		} catch (const bgsched::RuntimeException& err) {
			rc = bridge_handle_runtime_errors(
				"Block::createSmallBlock",
				err.getError().toValue(),
				bg_record);
		} catch (...) {
			error("Unknown Error from Block::createSmallBlock");
			rc = SLURM_ERROR;
		}

	} else {
		ListIterator itr;
		ba_mp_t *main_mp, *start_mp;

		/* If we are dealing with meshes we always need to
		   have the first midplane added as the start corner.
		   If we don't the API doesn't know what to do.  Since
		   we only need this here we only set it here. It
		   never gets freed since it is just a copy.
		*/
		slurm_mutex_lock(&ba_system_mutex);
		start_mp = coord2ba_mp(bg_record->start);
		assert(start_mp);
		assert(start_mp->loc);
		midplanes.push_back(start_mp->loc);

		itr = list_iterator_create(bg_record->ba_mp_list);
		while ((ba_mp = (ba_mp_t *)list_next(itr))) {
			/* Since the midplane locations aren't set up in the
			   copy of this pointer we need to go out a get the
			   real one from the system and use it.
			*/
			main_mp = coord2ba_mp(ba_mp->coord);
			/* don't add the start_mp again. */
			if (!main_mp || (main_mp == start_mp))
				continue;

			// info("got %s(%s) %d", main_mp->coord_str,
			//      main_mp->loc, ba_mp->used);
			if (ba_mp->used)
				midplanes.push_back(main_mp->loc);
			else
				pt_midplanes.push_back(main_mp->loc);
		}
		list_iterator_destroy(itr);
		slurm_mutex_unlock(&ba_system_mutex);

		for (dim=Dimension::A; dim<=Dimension::D; dim++) {
			switch (bg_record->conn_type[dim]) {
			case SELECT_MESH:
				conn_type[dim] = Block::Connectivity::Mesh;
				break;
			case SELECT_TORUS:
			default:
				conn_type[dim] = Block::Connectivity::Torus;
				break;
			}
		}
		try {
			block_ptr = Block::create(midplanes,
						  pt_midplanes, conn_type);
			rc = SLURM_SUCCESS;
		} catch (const bgsched::InputException& err) {
			rc = bridge_handle_input_errors(
				"Block::create",
				err.getError().toValue(),
				bg_record);
		} catch (...) {
			error("Unknown Error from Block::createSmallBlock");
			rc = SLURM_ERROR;
		}
	}

	if (rc != SLURM_SUCCESS) {
		/* This is needed because sometimes we
		   get a sub midplane system with not
		   all the hardware there.  This way
		   we can try to create blocks on all
		   the hardware and the good ones will
		   work and the bad ones will just be
		   removed after everything is done
		   being created.
		*/
		if (bg_conf->sub_mp_sys)
			rc = SLURM_SUCCESS;
		else if (bg_record->conn_type[0] != SELECT_SMALL)
			assert(0);
		return rc;
	}

	debug("block created correctly");
	try {
		block_ptr->setName(bg_record->bg_block_id);
	} catch (const bgsched::InputException& err) {
		rc = bridge_handle_input_errors("Block::setName",
						err.getError().toValue(),
						bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (...) {
                error("Unknown error from Block::setName().");
		rc = SLURM_ERROR;
	}

	try {
		block_ptr->setMicroLoaderImage(bg_record->mloaderimage);
	} catch (const bgsched::InputException& err) {
		rc = bridge_handle_input_errors("Block::MicroLoaderImage",
						err.getError().toValue(),
						bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (...) {
                error("Unknown error from Block::setMicroLoaderImage().");
		rc = SLURM_ERROR;
	}

	try {
		block_ptr->add("");
	} catch (const bgsched::InputException& err) {
		rc = bridge_handle_input_errors("Block::add",
						err.getError().toValue(),
						bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (const bgsched::RuntimeException& err) {
		rc = bridge_handle_runtime_errors("Block::add",
						  err.getError().toValue(),
						  bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (...) {
                error("Unknown error from Block::Add().");
		rc = SLURM_ERROR;
	}

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

#ifdef HAVE_BG_FILES
	char *function_name;
	/* Lets see if we are connected to the IO. */
	try {
		uint32_t avail, unavail;
		Block::checkIOLinksSummary(bg_record->bg_block_id,
					   &avail, &unavail);
	} catch (const bgsched::DatabaseException& err) {
		rc = bridge_handle_database_errors("Block::checkIOLinksSummary",
						   err.getError().toValue());
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (const bgsched::InputException& err) {
		rc = bridge_handle_input_errors("Block::checkIOLinksSummary",
						err.getError().toValue(),
						bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (const bgsched::InternalException& err) {
		rc = bridge_handle_internal_errors("Block::checkIOLinksSummary",
						   err.getError().toValue());
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (...) {
                error("checkIOLinksSummary request failed ... continuing.");
		rc = SLURM_ERROR;
	}

	try {
		std::vector<std::string> res_vec;
#ifdef HAVE_BG_NEW_IO_CHECK
		std::vector<std::string> unconn_ionode_vec;

		function_name = (char *)"Block::checkIO";
		Block::checkIO(bg_record->bg_block_id,
			       &unconn_ionode_vec,
			       &res_vec);
		if (!res_vec.empty()) {
			error("Block %s is not IOConnected, "
			      "contact your admin. Midplanes not "
			      "connected ...", bg_record->bg_block_id);
			slurm_mutex_lock(&ba_system_mutex);
			BOOST_FOREACH(const std::string& res, res_vec) {
				ba_mp_t *ba_mp = loc2ba_mp(res.c_str());
				if (ba_mp)
					error("%s(%s)",
					      res.c_str(), ba_mp->coord_str);
				else
					error("%s", res.c_str());
			}
			slurm_mutex_unlock(&ba_system_mutex);
			return BG_ERROR_NO_IOBLOCK_CONNECTED;
		}
#else
		function_name = (char *)"Block::isIOConnected";
		if (!Block::isIOConnected(
			    bg_record->bg_block_id, &res_vec)) {
			error("Using old method, "
			      "block %s is not IOConnected, "
			      "contact your admin. Hardware not "
			      "connected ...", bg_record->bg_block_id);
			BOOST_FOREACH(const std::string& res, res_vec) {
				error("%s", res.c_str());
			}
			return BG_ERROR_NO_IOBLOCK_CONNECTED;
		}
#endif
	} catch (const bgsched::DatabaseException& err) {
		rc = bridge_handle_database_errors(function_name,
						   err.getError().toValue());
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (const bgsched::InputException& err) {
		rc = bridge_handle_input_errors(function_name,
						err.getError().toValue(),
						bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (const bgsched::InternalException& err) {
		rc = bridge_handle_internal_errors(function_name,
						err.getError().toValue());
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (...) {
                error("%s request failed ... continuing.", function_name);
		rc = SLURM_ERROR;
	}

	if ((rc = bridge_block_sync_users(bg_record)) != SLURM_SUCCESS) {
		error("bridge_block_remove_all_users: Something "
		      "happened removing users from block %s",
		      bg_record->bg_block_id);
		return SLURM_ERROR;
	}

        try {
		debug("booting block %s", bg_record->bg_block_id);
		Block::initiateBoot(bg_record->bg_block_id);
		/* Set this here just to make sure we know we
		   are suppose to be booting.  Just incase the
		   block goes free before we notice we are
		   configuring.
		*/
		bg_record->boot_state = 1;
	} catch (const bgsched::RuntimeException& err) {
		rc = bridge_handle_runtime_errors("Block::initiateBoot",
						err.getError().toValue(),
						bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (const bgsched::DatabaseException& err) {
		rc = bridge_handle_database_errors("Block::initiateBoot",
						   err.getError().toValue());
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (const bgsched::InputException& err) {
		rc = bridge_handle_input_errors("Block::initiateBoot",
						err.getError().toValue(),
						bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (...) {
                error("Boot block request failed ... continuing.");
		rc = SLURM_ERROR;
	}
#else
	debug("block %s is ready", bg_record->bg_block_id);
	if (!block_ptr_exist_in_list(bg_lists->booted, bg_record))
	 	list_push(bg_lists->booted, bg_record);
	bg_record->state = BG_BLOCK_INITED;
	last_bg_update = time(NULL);
#endif
	return rc;
}

extern int bridge_block_free(bg_record_t *bg_record)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_record || !bg_record->bg_block_id)
		return SLURM_ERROR;

	debug("freeing block %s", bg_record->bg_block_id);

#ifdef HAVE_BG_FILES
	try {
		Block::initiateFree(bg_record->bg_block_id);
	} catch (const bgsched::RuntimeException& err) {
		rc = bridge_handle_runtime_errors("Block::initiateFree",
						  err.getError().toValue(),
						  bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (const bgsched::DatabaseException& err2) {
		rc = bridge_handle_database_errors("Block::initiateFree",
						   err2.getError().toValue());
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (const bgsched::InputException& err3) {
		rc = bridge_handle_input_errors("Block::initiateFree",
						err3.getError().toValue(),
						bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch(...) {
                error("Free block request failed ... continuing.");
		rc = SLURM_ERROR;
	}
#else
	bg_record->state = BG_BLOCK_FREE;
	last_bg_update = time(NULL);
#endif
	return rc;
}

extern int bridge_block_remove(bg_record_t *bg_record)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_record || !bg_record->bg_block_id)
		return SLURM_ERROR;

	debug("removing block %s %p", bg_record->bg_block_id, bg_record);

#ifdef HAVE_BG_FILES
	try {
		Block::remove(bg_record->bg_block_id);
	} catch (const bgsched::RuntimeException& err) {
		rc = bridge_handle_runtime_errors("Block::remove",
						  err.getError().toValue(),
						  bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (const bgsched::DatabaseException& err) {
		rc = bridge_handle_database_errors("Block::remove",
						   err.getError().toValue());
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (const bgsched::InputException& err) {
		rc = bridge_handle_input_errors("Block::remove",
						err.getError().toValue(),
						bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch(...) {
                error("Remove block request failed ... continuing.");
		rc = SLURM_ERROR;
	}
#endif
	return rc;
}

extern int bridge_block_add_user(bg_record_t *bg_record, const char *user_name)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_record || !bg_record->bg_block_id || !user_name)
		return SLURM_ERROR;

#ifdef HAVE_BG_FILES
	try {
		if (Block::isAuthorized(bg_record->bg_block_id, user_name)) {
			debug2("User %s is already able to run "
			       "jobs on block %s",
			       user_name, bg_record->bg_block_id);
			return SLURM_SUCCESS;
		}
	} catch (const bgsched::InputException& err) {
		rc = bridge_handle_input_errors("Block::isAuthorized",
						err.getError().toValue(),
						bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (const bgsched::RuntimeException& err) {
		rc = bridge_handle_runtime_errors("Block::isAuthorized",
						  err.getError().toValue(),
						  bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch(...) {
                error("isAuthorized user request failed ... continuing.");
		rc = SLURM_ERROR;
	}
#endif
	debug("adding user %s to block %s", user_name, bg_record->bg_block_id);
#ifdef HAVE_BG_FILES
        try {
		Block::addUser(bg_record->bg_block_id, user_name);
	} catch (const bgsched::InputException& err) {
		rc = bridge_handle_input_errors("Block::addUser",
						err.getError().toValue(),
						bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (const bgsched::RuntimeException& err) {
		rc = bridge_handle_runtime_errors("Block::addUser",
						  err.getError().toValue(),
						  bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch(...) {
                error("Add block user request failed ... continuing.");
		rc = SLURM_ERROR;
	}
#endif
	return rc;
}

extern int bridge_block_remove_user(bg_record_t *bg_record,
				    const char *user_name)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_record || !bg_record->bg_block_id || !user_name)
		return SLURM_ERROR;

	debug("removing user %s from block %s",
	      user_name, bg_record->bg_block_id);
#ifdef HAVE_BG_FILES
        try {
		Block::removeUser(bg_record->bg_block_id, user_name);
	} catch (const bgsched::InputException& err) {
		rc = bridge_handle_input_errors("Block::removeUser",
						err.getError().toValue(),
						bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch (const bgsched::RuntimeException& err) {
		rc = bridge_handle_runtime_errors("Block::removeUser",
						  err.getError().toValue(),
						  bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	} catch(...) {
                error("Remove block user request failed ... continuing.");
	        	rc = REMOVE_USER_ERR;
	}
#endif
	return rc;
}

extern int bridge_block_sync_users(bg_record_t *bg_record)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_BG_FILES
	std::vector<std::string> vec;
	vector<std::string>::iterator iter;
	bool found = 0;
#endif

	if (!bridge_init(NULL))
		return REMOVE_USER_ERR;

	if (!bg_record || !bg_record->bg_block_id)
		return REMOVE_USER_ERR;

#ifdef HAVE_BG_FILES
	try {
		vec = Block::getUsers(bg_record->bg_block_id);
	} catch (const bgsched::InputException& err) {
		bridge_handle_input_errors(
			"Block::getUsers",
			err.getError().toValue(), bg_record);
		return REMOVE_USER_ERR;
	} catch (const bgsched::RuntimeException& err) {
		bridge_handle_runtime_errors(
			"Block::getUsers",
			err.getError().toValue(), bg_record);
		return REMOVE_USER_ERR;
	}

	if (bg_record->job_ptr && (bg_record->job_ptr->magic == JOB_MAGIC)) {
		select_jobinfo_t *jobinfo = (select_jobinfo_t *)
			bg_record->job_ptr->select_jobinfo->data;
		BOOST_FOREACH(const std::string& user, vec) {
			if (!user.compare(bg_conf->slurm_user_name))
				continue;
			if (!user.compare(jobinfo->user_name)) {
				found = 1;
				continue;
			}
			bridge_block_remove_user(bg_record, user.c_str());
		}
		if (!found)
			bridge_block_add_user(bg_record,
					      jobinfo->user_name);
	} else if (bg_record->job_list && list_count(bg_record->job_list)) {
		ListIterator itr = list_iterator_create(bg_record->job_list);
		struct job_record *job_ptr = NULL;

		/* First add all that need to be added removing the
		 * name from the vector as we go.
		 */
		while ((job_ptr = (struct job_record *)list_next(itr))) {
			select_jobinfo_t *jobinfo;

			if (job_ptr->magic != JOB_MAGIC) {
				error("bridge_block_sync_users: "
				      "bad magic found when "
				      "looking at block %s",
				      bg_record->bg_block_id);
				list_delete_item(itr);
				continue;
			}

			jobinfo = (select_jobinfo_t *)
				job_ptr->select_jobinfo->data;
			iter = std::find(vec.begin(), vec.end(),
					 jobinfo->user_name);
			if (iter == vec.end())
				bridge_block_add_user(bg_record,
						      jobinfo->user_name);
			else
				vec.erase(iter);
		}
		list_iterator_destroy(itr);

		/* Then remove all that is left */
		BOOST_FOREACH(const std::string& user, vec) {
			bridge_block_remove_user(bg_record, user.c_str());
		}
	} else {
		BOOST_FOREACH(const std::string& user, vec) {
			if (!user.compare(bg_conf->slurm_user_name))
				continue;
			bridge_block_remove_user(bg_record, user.c_str());
		}
	}

#endif
	return rc;
}

extern int bridge_blocks_load_curr(List curr_block_list)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_BG_FILES
	Block::Ptrs vec;
	BlockFilter filter;
	bg_record_t *bg_record = NULL;

	info("querying the system for existing blocks");

	/* Get the midplane info */
	filter.setExtendedInfo(true);

	vec = bridge_get_blocks(filter);
	if (vec.empty()) {
		debug("No blocks in the current system");
		return SLURM_SUCCESS;
	}

	slurm_mutex_lock(&block_state_mutex);

	BOOST_FOREACH(const Block::Ptr &block_ptr, vec) {
		const char *bg_block_id = block_ptr->getName().c_str();
		uint16_t state;

		if (strncmp("RMP", bg_block_id, 3))
			continue;

		/* find BG Block record */
		if (!(bg_record = find_bg_record_in_list(
			      curr_block_list, bg_block_id))) {
			info("%s not found in the state file, adding",
			     bg_block_id);

			if ((bg_record = _translate_object_to_block(block_ptr)))
				slurm_list_append(curr_block_list, bg_record);
		}

		/* modifying will be ceared later in the
		   _validate_config_blocks or _delete_old_blocks
		   functions in select_bluegene.c
		*/
		bg_record->modifying = 1;

		/* If we are in error we really just want to get the
		   new state.
		*/
		state = bridge_translate_status(
			block_ptr->getStatus().toValue());
		if (state == BG_BLOCK_BOOTING)
			bg_record->boot_state = 1;

		if (bg_record->state & BG_BLOCK_ERROR_FLAG)
			state |= BG_BLOCK_ERROR_FLAG;
		bg_record->state = state;

		debug3("Block %s is in state %s",
		       bg_record->bg_block_id,
		       bg_block_state_string(bg_record->state));

		bg_record->job_running = NO_JOB_RUNNING;

		/* we are just going to go and destroy this block so
		   just throw get the name and continue. */
		if (!bg_recover)
			continue;

		xfree(bg_record->mloaderimage);
		bg_record->mloaderimage =
			xstrdup(block_ptr->getMicroLoaderImage().c_str());
	}

	slurm_mutex_unlock(&block_state_mutex);

#endif
	return rc;
}

extern void bridge_block_post_job(char *bg_block_id,
				  struct job_record *job_ptr)
{
	_remove_jobs_on_block_and_reset(bg_block_id, job_ptr);
}


extern uint16_t bridge_block_get_action(char *bg_block_id)
{
	uint16_t action = BG_BLOCK_ACTION_NONE;

#if defined HAVE_BG_FILES && defined HAVE_BG_GET_ACTION
	BlockFilter filter;
	Block::Ptrs vec;

	/* This block hasn't been created yet. */
	if (!bg_block_id)
		return action;

	filter.setName(string(bg_block_id));

	vec = bridge_get_blocks(filter);
	if (vec.empty()) {
		error("bridge_block_get_action: "
		      "block %s not found, this should never happen",
		      bg_block_id);
		/* block is gone? */
		return BG_BLOCK_ACTION_NAV;
	}

	const Block::Ptr &block_ptr = *(vec.begin());
	action = bridge_translate_action(block_ptr->getAction().toValue());
#endif
	return action;
}

extern int bridge_check_nodeboards(char *mp_loc)
{
#ifdef HAVE_BG_FILES
	NodeBoard::ConstPtrs vec = bridge_get_nodeboards(mp_loc);

	BOOST_FOREACH(const NodeBoard::ConstPtr &nb_ptr, vec) {
		if (!nb_ptr->isMetaState()
		    && (nb_ptr->getState() != Hardware::Available))
			return 1;
	}
#endif
	return 0;
}

extern int bridge_set_log_params(char *api_file_name, unsigned int level)
{
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_conf->bridge_api_file)
		return SLURM_SUCCESS;

#ifdef HAVE_BG_FILES
	// Scheduler APIs use the loggers under ibm.
	log4cxx::LoggerPtr logger_ptr(log4cxx::Logger::getLogger("ibm"));
	// Set the pattern for output.
	log4cxx::LayoutPtr layout_ptr(
		new log4cxx::PatternLayout(
			"[%d{yyyy-MM-ddTHH:mm:ss}] %p: %c: %m [%t]%n"));
	// Set the log file
	log4cxx::AppenderPtr appender_ptr(
		new log4cxx::FileAppender(layout_ptr,
					  bg_conf->bridge_api_file));
	log4cxx::LevelPtr level_ptr;

	// Get rid of the console appender.
	logger_ptr->removeAllAppenders();

	switch (level) {
	case 0:
		level_ptr = log4cxx::Level::getOff();
		break;
	case 1:
		level_ptr = log4cxx::Level::getFatal();
		break;
	case 2:
		level_ptr = log4cxx::Level::getError();
		break;
	case 3:
		level_ptr = log4cxx::Level::getWarn();
		break;
	case 4:
		level_ptr = log4cxx::Level::getInfo();
		break;
	case 5:
		level_ptr = log4cxx::Level::getDebug();
		break;
	case 6:
		level_ptr = log4cxx::Level::getTrace();
		break;
	case 7:
		level_ptr = log4cxx::Level::getAll();
		break;
	default:
		level_ptr = log4cxx::Level::getDebug();
		break;
	}
	// Now set the level of debug
	logger_ptr->setLevel(level_ptr);
	// Add the appender to the ibm logger.
	logger_ptr->addAppender(appender_ptr);
#endif
	return SLURM_SUCCESS;
}


