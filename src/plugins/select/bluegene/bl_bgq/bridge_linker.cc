/*****************************************************************************\
 *  bridge_linker.cc
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
#include "../ba_bgq/block_allocator.h"
#include "../bg_record_functions.h"
#include "src/common/parse_time.h"
#include "src/common/uid.h"
}

#include "bridge_status.h"

/* local vars */
//static pthread_mutex_t api_file_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool initialized = false;


#ifdef HAVE_BG_FILES

static void _setup_ba_mp(ComputeHardware::ConstPtr bgq, ba_mp_t *ba_mp)
{
	// int i;
	Midplane::Coordinates coords = {{ba_mp->coord[A], ba_mp->coord[X],
					 ba_mp->coord[Y], ba_mp->coord[Z]}};
	Midplane::ConstPtr mp_ptr = bgq->getMidplane(coords);

	ba_mp->loc = xstrdup(mp_ptr->getLocation().c_str());

	// info("%s which is %c%c%c%c is setup",
	//      ba_mp->loc,
	//      alpha_num[ba_mp->coord[A]],
	//      alpha_num[ba_mp->coord[X]],
	//      alpha_num[ba_mp->coord[Y]],
	//      alpha_num[ba_mp->coord[Z]]);
	// for (i=0; i < SYSTEM_DIMENSIONS; i++) {
	// 	Switch::ConstPtr bg_switch = mp_ptr->getSwitch(
	// 		(bgsched::Dimension::Value)i);
	// 	switch (bg_switch.getInUse().Value()) {
	// 	case Switch::NotInUse:
	// 		ba_mp->axis_switch[i].usage = BG_SWITCH_NONE;
	// 		break;
	// 	case Switch::IncludedBothPortsInUse:
	// 		ba_mp->axis_switch[i].usage = BG_SWITCH_TORUS;
	// 		break;
	// 	case Switch::IncludedOutputPortInUse:
	// 		ba_mp->axis_switch[i].usage =
	// 			(BG_SWITCH_OUT | BG_SWITCH_OUT_PASS);
	// 		break;
	// 	case Switch::IncludedInputPortInUse:
	// 		ba_mp->axis_switch[i].usage =
	// 			(BG_SWITCH_IN | BG_SWITCH_IN_PASS);
	// 		break;
	// 	case Switch::Wrapped:
	// 		ba_mp->axis_switch[i].usage = BG_SWITCH_WRAPPED;
	// 		break;
	// 	case Switch::Passthrough:
	// 		ba_mp->axis_switch[i].usage = BG_SWITCH_PASS;
	// 		break;
	// 	case Switch::WrappedPassthrough:
	// 		ba_mp->axis_switch[i].usage = BG_SWITCH_WRAPPED_PASS;
	// 		break;
	// 	default:
	// 		error("unknown switch conf");
	// 		break;
	// 	}
	// 	info("mp %s(%d) usage is %s", ba_mp->coord_str,
	// 	     i, ba_switch_usage_str(ba_mp->axis_switch[i].usage);

	// }
}
#endif

static int _block_wait_for_jobs(char *bg_block_id)
{
#ifdef HAVE_BG_FILES
	std::vector<Job::ConstPtr> job_vec;
	JobFilter job_filter;
	JobFilter::Statuses job_statuses;
#endif

	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_block_id) {
		error("no block name given");
		return SLURM_ERROR;
	}

#ifdef HAVE_BG_FILES

	job_filter.setComputeBlockName(bg_block_id);

	/* I think these are all the states we need. */
	job_statuses.insert(Job::Setup);
	job_statuses.insert(Job::Loading);
	job_statuses.insert(Job::Starting);
	job_statuses.insert(Job::Running);
	job_statuses.insert(Job::Cleanup);
	job_filter.setStatuses(&job_statuses);

	while (1) {
		job_vec = getJobs(job_filter);
		if (job_vec.empty())
			return SLURM_SUCCESS;

		BOOST_FOREACH(const Job::ConstPtr& job_ptr, job_vec) {
			debug("waiting on job %lu to finish on block %s",
			      job_ptr->getId(), bg_block_id);
		}
		sleep(POLL_INTERVAL);
	}
#endif
	return SLURM_SUCCESS;
}

static void _remove_jobs_on_block_and_reset(char *block_id)
{
	bg_record_t *bg_record = NULL;
	int job_remove_failed = 0;

	if (!block_id) {
		error("_remove_jobs_on_block_and_reset: no block name given");
		return;
	}

	if (_block_wait_for_jobs(block_id) != SLURM_SUCCESS)
		job_remove_failed = 1;

	/* remove the block's users */
	slurm_mutex_lock(&block_state_mutex);
	bg_record = find_bg_record_in_list(bg_lists->main, block_id);
	if (bg_record) {
		debug("got the record %s user is %s",
		      bg_record->bg_block_id,
		      bg_record->user_name);

		if (job_remove_failed) {
			if (bg_record->nodes)
				slurm_drain_nodes(
					bg_record->nodes,
					(char *)
					"_term_agent: Couldn't remove job",
					slurm_get_slurm_user_id());
			else
				error("Block %s doesn't have a node list.",
				      block_id);
		}

		bg_reset_block(bg_record);
	} else if (bg_conf->layout_mode == LAYOUT_DYNAMIC) {
		debug2("Hopefully we are destroying this block %s "
		       "since it isn't in the bg_lists->main",
		       block_id);
	}

	slurm_mutex_unlock(&block_state_mutex);

}

extern int bridge_init(char *properties_file)
{
	if (initialized)
		return 1;

	if (bg_recover == NOT_FROM_CONTROLLER)
		return 0;

#ifdef HAVE_BG_FILES
	bgsched::init(properties_file);
#endif
	bridge_status_init();
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

extern int bridge_get_size(int *size)
{
#ifdef HAVE_BG_FILES
        Midplane::Coordinates bgq_size;
	Dimension dim;
#else
	int dim;
#endif
	if (!bridge_init(NULL))
		return SLURM_ERROR;
#ifdef HAVE_BG_FILES
	memset(size, 0, sizeof(int) * SYSTEM_DIMENSIONS);

	bgq_size = core::getMachineSize();
	for (dim=Dimension::A; dim<=Dimension::D; dim++)
		size[dim] = bgq_size[dim];
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
#ifdef HAVE_BG_FILES
	ComputeHardware::ConstPtr bgq = getComputeHardware();

	for (int a = 0; a < DIM_SIZE[A]; a++)
		for (int x = 0; x < DIM_SIZE[X]; x++)
			for (int y = 0; y < DIM_SIZE[Y]; y++)
				for (int z = 0; z < DIM_SIZE[Z]; z++)
					_setup_ba_mp(
						bgq, &ba_main_grid[a][x][y][z]);
#endif

	return SLURM_SUCCESS;
}

extern int bridge_block_create(bg_record_t *bg_record)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;

#ifdef HAVE_BG_FILES
	Block::Ptr block_ptr;
	Block::Midplanes midplanes;
        Block::PassthroughMidplanes pt_midplanes;
        Block::DimensionConnectivity conn_type;
	Midplane::Ptr midplane;
	Dimension dim;
	ba_mp_t *ba_mp = NULL;
#endif

	if (!bridge_init(NULL))
		return SLURM_ERROR;

#ifdef HAVE_BG_FILES
	if (bg_record->node_cnt < bg_conf->mp_node_cnt) {
		info("we can't make small blocks yet");
		return SLURM_ERROR;
	}
#endif

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
	itr = list_iterator_create(bg_record->ba_mp_list);
	while ((ba_mp = (ba_mp_t *)list_next(itr))) {
		if (ba_mp->used)
			midplanes.push_back(ba_mp->loc);
		else
			pt_midplanes.push_back(ba_mp->loc);
	}
	list_iterator_destroy(itr);

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
		block_ptr = Block::create(midplanes, pt_midplanes, conn_type);
	} catch (const bgsched::InputException& err) {
		rc = bridge_handle_input_errors("Block::create",
						err.getError().toValue(),
						bg_record);
		if (rc != SLURM_SUCCESS)
			return rc;
	}

	info("block created correctly");
	block_ptr->setName(bg_record->bg_block_id);
	block_ptr->setMicroLoaderImage(bg_record->mloaderimage);

	try {
		block_ptr->add("");
		// block_ptr->addUser(bg_record->bg_block_id,
		// 		   bg_record->user_name);
		//info("got past add");
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
	if (bridge_block_set_owner(
		    bg_record, bg_conf->slurm_user_name) != SLURM_SUCCESS)
		return SLURM_ERROR;

        try {
		Block::initiateBoot(bg_record->bg_block_id);
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
	/* Set this here just to make sure we know we are suppose to
	   be booting.  Just incase the block goes free before we
	   notice we are configuring.
	*/
	bg_record->boot_state = BG_BLOCK_BOOTING;
#else
	info("block %s is ready", bg_record->bg_block_id);
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

	info("freeing block %s", bg_record->bg_block_id);

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

	info("removing block %s", bg_record->bg_block_id);

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

extern int bridge_block_add_user(bg_record_t *bg_record, char *user_name)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_record || !bg_record->bg_block_id || !user_name)
		return SLURM_ERROR;

	info("adding user %s to block %s", user_name, bg_record->bg_block_id);
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
		// FIXME: this should do something, but for now we won't
//                error("Remove block request failed ... continuing.");
//		rc = SLURM_ERROR;
	}
#endif
	return rc;
}

extern int bridge_block_remove_user(bg_record_t *bg_record, char *user_name)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_record || !bg_record->bg_block_id || !user_name)
		return SLURM_ERROR;

	info("removing user %s from block %s", user_name, bg_record->bg_block_id);
#ifdef HAVE_BG_FILES
        try {
		Block::removeUser(bg_record->bg_block_id, user_name);
	} catch(...) {
 		// FIXME: this should do something, but for now we won't
               // error("Remove block request failed ... continuing.");
	       // 	rc = REMOVE_USER_ERR;
	}
#endif
	return rc;
}

extern int bridge_block_remove_all_users(bg_record_t *bg_record,
					 char *user_name)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_BG_FILES
	std::vector<std::string> vec;
	vector<std::string>::iterator iter;
#endif

	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_record || !bg_record->bg_block_id)
		return SLURM_ERROR;

#ifdef HAVE_BG_FILES
	vec = Block::getUsers(bg_record->bg_block_id);
	if (vec.empty())
		return REMOVE_USER_NONE;

	BOOST_FOREACH(const std::string& user, vec) {
		if (user_name && (user == user_name))
			continue;
		if ((rc = bridge_block_remove_user(bg_record, user_name)
		     != SLURM_SUCCESS))
			break;
	}

#endif
	return rc;
}

extern int bridge_block_set_owner(bg_record_t *bg_record, char *user_name)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!bg_record || !bg_record->bg_block_id || !user_name)
		return SLURM_ERROR;

	if ((rc = bridge_block_remove_all_users(
		     bg_record, user_name)) == REMOVE_USER_ERR) {
		error("bridge_block_set_owner: Something happened removing "
		      "users from block %s",
		      bg_record->bg_block_id);
		return SLURM_ERROR;
	} else if (rc == REMOVE_USER_NONE && user_name)
		rc = bridge_block_add_user(bg_record, user_name);

	return rc;
}

extern int bridge_block_get_and_set_mps(bg_record_t *bg_record)
{

	return SLURM_ERROR;
}

extern int bridge_blocks_load_curr(List curr_block_list)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_BG_FILES
	Block::Ptrs vec;
	BlockFilter filter;
	Dimension dim;
	char temp[256];
	uid_t my_uid;
	bg_record_t *bg_record = NULL;

	info("querying the system for existing blocks");

	/* Get the midplane info */
	filter.setExtendedInfo(true);

	vec = getBlocks(filter, BlockSort::AnyOrder);
	if (vec.empty()) {
		debug("No blocks in the current system");
		return SLURM_SUCCESS;
	}

	slurm_mutex_lock(&block_state_mutex);

	BOOST_FOREACH(const Block::Ptr &block_ptr, vec) {
		const char *bg_block_id = block_ptr->getName().c_str();
		Block::Midplanes midplane_vec;
		hostlist_t hostlist;

		if (strncmp("RMP", bg_block_id, 3))
			continue;

		/* New BG Block record */

		bg_record = (bg_record_t *)xmalloc(sizeof(bg_record_t));
		bg_record->magic = BLOCK_MAGIC;

		slurm_list_append(curr_block_list, bg_record);

		bg_record->bg_block_id = xstrdup(bg_block_id);

		bg_record->state = bridge_translate_status(
			block_ptr->getStatus().toValue());

		if (bg_record->state == BG_BLOCK_BOOTING)
			bg_record->boot_state = 1;

		debug3("Block %s is in state %d",
		       bg_record->bg_block_id,
		       bg_record->state);

		bg_record->node_cnt = block_ptr->getComputeNodeCount();
		bg_record->cpu_cnt = bg_conf->cpu_ratio * bg_record->node_cnt;
		bg_record->job_running = NO_JOB_RUNNING;
		if (block_ptr->isSmall()) {
			char bitstring[BITSIZE];
			int io_cnt, io_start;
			Block::NodeBoards nodeboards =
				block_ptr->getNodeBoards();
			int nb_cnt = nodeboards.size();
			std::string nb_name = *(nodeboards.begin());

			if ((io_cnt = nb_cnt * bg_conf->io_ratio))
				io_cnt--;

			/* From the first nodecard id we can figure
			   out where to start from with the alloc of ionodes.
			*/
			io_start = atoi((char*)nb_name.c_str()+1)
				* bg_conf->io_ratio;
			info("got nb name of %s %d %d",
			     nb_name.c_str(), io_start, nb_cnt);

			bg_record->ionode_bitmap =
				bit_alloc(bg_conf->ionodes_per_mp);
			/* Set the correct ionodes being used in this block */
			bit_nset(bg_record->ionode_bitmap,
				 io_start, io_start+io_cnt);
			bit_fmt(bitstring, BITSIZE, bg_record->ionode_bitmap);
			bg_record->ionodes = xstrdup(bitstring);
			debug3("%s uses ionodes %s",
			       bg_record->bg_block_id,
			       bg_record->ionodes);
			bg_record->conn_type[0] = SELECT_SMALL;
		} else {
			for (dim=Dimension::A; dim<=Dimension::D; dim++) {
				bg_record->conn_type[dim] =
					block_ptr->isTorus(dim) ?
					SELECT_TORUS : SELECT_MESH;
			}
			/* Set the bitmap blank here if it is a full
			   node we don't want anything set we also
			   don't want the bg_record->ionodes set.
			*/
			bg_record->ionode_bitmap =
				bit_alloc(bg_conf->ionodes_per_mp);
		}
		bg_record->mloaderimage =
			xstrdup(block_ptr->getMicroLoaderImage().c_str());

		hostlist = hostlist_create(NULL);
		midplane_vec = block_ptr->getMidplanes();
		BOOST_FOREACH(const std::string midplane, midplane_vec) {
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
		bg_record->nodes = hostlist_ranged_string_xmalloc(hostlist);
		hostlist_destroy(hostlist);
		debug3("got nodes of %s", bg_record->nodes);

		process_nodes(bg_record, true);

		/* We can stop processing information now since we
		   don't need to rest of the information to decide if
		   this is the correct block. */
		if (bg_conf->layout_mode == LAYOUT_DYNAMIC) {
			bg_record_t *tmp_record =
				(bg_record_t *)xmalloc(sizeof(bg_record_t));
			copy_bg_record(bg_record, tmp_record);
			list_push(bg_lists->main, tmp_record);
		}

		reset_ba_system(true);
		if (ba_set_removable_mps(bg_record->bitmap, 1) != SLURM_SUCCESS)
			fatal("It doesn't seem we have a bitmap for %s",
			      bg_record->bg_block_id);

		if (bg_record->ba_mp_list)
			list_flush(bg_record->ba_mp_list);
		else
			bg_record->ba_mp_list = list_create(destroy_ba_mp);

		bg_block_id = set_bg_block(bg_record->ba_mp_list,
					   bg_record->start,
					   bg_record->geo,
					   bg_record->conn_type);
		ba_reset_all_removed_mps();
		if (!bg_block_id)
			fatal("I was unable to make the requested block.");

		snprintf(temp, sizeof(temp), "%s%s",
			 bg_conf->slurm_node_prefix,
			 bg_block_id);

		xfree(bg_block_id);
		if (strcmp(temp, bg_record->nodes)) {
			fatal("bad wiring in preserved state "
			      "(found %s, but allocated %s) "
			      "YOU MUST COLDSTART",
			      bg_record->nodes, temp);
		}

		/* If a user is on the block this will be filled in */
		if (block_ptr->getUser() != "")
			bg_record->user_name =
				xstrdup(block_ptr->getUser().c_str());

		if (!bg_record->user_name)
			bg_record->user_name =
				xstrdup(bg_conf->slurm_user_name);

		if (!bg_record->boot_state)
			bg_record->target_name =
				xstrdup(bg_conf->slurm_user_name);
		else
			bg_record->target_name = xstrdup(bg_record->user_name);

		if (uid_from_string(bg_record->user_name, &my_uid) < 0)
			error("uid_from_string(%s): %m", bg_record->user_name);
		else
			bg_record->user_uid = my_uid;
	}

	slurm_mutex_unlock(&block_state_mutex);

#endif
	return rc;
}

extern void bridge_reset_block_list(List block_list)
{
	ListIterator itr = NULL;
	bg_record_t *bg_record = NULL;

	if (!block_list)
		return;

	itr = list_iterator_create(block_list);
	while ((bg_record = (bg_record_t *)list_next(itr))) {
		info("Queue clearing of users of BG block %s",
		     bg_record->bg_block_id);
		_remove_jobs_on_block_and_reset(bg_record->bg_block_id);
	}
	list_iterator_destroy(itr);
}

extern void bridge_block_post_job(char *bg_block_id)
{
	_remove_jobs_on_block_and_reset(bg_block_id);
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


