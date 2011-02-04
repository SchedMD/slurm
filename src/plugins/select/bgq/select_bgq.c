/*****************************************************************************\
 *  select_bgq.cc - node selection plugin for Blue Gene/Q system.
 *****************************************************************************
 *  Copyright (C) 2010-2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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


#include "src/common/slurm_xlator.h"
#include "bluegene.h"

#define HUGE_BUF_SIZE (1024*16)
#define NOT_FROM_CONTROLLER -2

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
slurm_ctl_conf_t slurmctld_conf __attribute__((weak_import));
struct node_record *node_record_table_ptr  __attribute__((weak_import)) = NULL;
int bg_recover __attribute__((weak_import)) = NOT_FROM_CONTROLLER;
List part_list  __attribute__((weak_import)) = NULL;
int node_record_count __attribute__((weak_import));
time_t last_node_update __attribute__((weak_import));
time_t last_job_update __attribute__((weak_import));
char *alpha_num  __attribute__((weak_import)) =
	"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
void *acct_db_conn  __attribute__((weak_import)) = NULL;
char *slurmctld_cluster_name  __attribute__((weak_import)) = NULL;
slurmdb_cluster_rec_t *working_cluster_rec  __attribute__((weak_import)) = NULL;
#else
slurm_ctl_conf_t slurmctld_conf;
struct node_record *node_record_table_ptr = NULL;
int bg_recover = NOT_FROM_CONTROLLER;
List part_list = NULL;
int node_record_count;
time_t last_node_update;
time_t last_job_update;
char *alpha_num = (char *)"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
void *acct_db_conn = NULL;
char *slurmctld_cluster_name = NULL;
slurmdb_cluster_rec_t *working_cluster_rec = NULL;
#endif

const char plugin_name[]       	= "BG/Q node selection plugin";
const char plugin_type[]       	= "select/bgq";
const uint32_t plugin_id     	= 103;
const uint32_t plugin_version	= 200;

extern int select_p_alter_node_cnt(enum select_node_cnt type, void *data);

#ifdef HAVE_BGQ
static int _internal_update_node_state(int level, int *coords,
				       int index, uint16_t state)
{
	ba_mp_t *curr_mp;

	if (level > cluster_dims)
		return SLURM_ERROR;

	if (level < cluster_dims) {
		for (coords[level] = 0;
		     coords[level] < DIM_SIZE[level];
		     coords[level]++) {
			/* handle the outter dims here */
			if (_internal_update_node_state(
				    level+1, coords, index, state)
			    == SLURM_SUCCESS)
				return SLURM_SUCCESS;
		}
		return SLURM_ERROR;
	}

	curr_mp = &ba_system_ptr->grid
		[coords[A]][coords[X]][coords[Y]][coords[Z]];

	if (curr_mp->index == index) {
		ba_update_mp_state(curr_mp, state);
		return SLURM_SUCCESS;
	}
	return SLURM_ERROR;
}
#endif

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{

#ifdef HAVE_BGQ
	if(bg_recover != NOT_FROM_CONTROLLER) {
#if (SYSTEM_DIMENSIONS != 4)
		fatal("SYSTEM_DIMENSIONS value (%d) invalid for BGQ",
		      SYSTEM_DIMENSIONS);
#endif

		verbose("%s loading...", plugin_name);
		/* if this is coming from something other than the controller
		   we don't want to read the config or anything like that. */
		if (init_bg())
			return SLURM_ERROR;
	}
	verbose("%s loaded", plugin_name);
#else
	if (bg_recover != NOT_FROM_CONTROLLER)
		fatal("select/bgq is incompatible with a "
		      "non BlueGene/Q system");
#endif
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	int rc = SLURM_SUCCESS;

#ifdef HAVE_BGQ
	fini_bg();
#endif
	return rc;
}

/*
 * The remainder of this file implements the standard SLURM
 * node selection API.
 */

/* We rely upon DB2 to save and restore BlueGene state */
extern int select_p_state_save(char *dir_name)
{
#ifdef HAVE_BGQ
/* 	ListIterator itr; */
/* 	bg_record_t *bg_record = NULL; */
/* 	int error_code = 0, log_fd; */
/* 	char *old_file, *new_file, *reg_file; */
/* 	uint32_t blocks_packed = 0, tmp_offset, block_offset; */
/* 	Buf buffer = init_buf(BUF_SIZE); */
/* 	DEF_TIMERS; */

/* 	debug("bluegene: select_p_state_save"); */
/* 	START_TIMER; */
/* 	/\* write header: time *\/ */
/* 	packstr(BLOCK_STATE_VERSION, buffer); */
/* 	block_offset = get_buf_offset(buffer); */
/* 	pack32(blocks_packed, buffer); */
/* 	pack_time(time(NULL), buffer); */

/* 	/\* write block records to buffer *\/ */
/* 	slurm_mutex_lock(&block_state_mutex); */
/* 	itr = list_iterator_create(bg_lists->main); */
/* 	while ((bg_record = list_next(itr))) { */
/* 		if (bg_record->magic != BLOCK_MAGIC) */
/* 			continue; */
/* 		/\* on real bluegene systems we only want to keep track of */
/* 		 * the blocks in an error state */
/* 		 *\/ */
/* #ifdef HAVE_BGQ_FILES */
/* 		if (bg_record->state != RM_PARTITION_ERROR) */
/* 			continue; */
/* #endif */
/* 		xassert(bg_record->bg_block_id != NULL); */

/* 		pack_block(bg_record, buffer, SLURM_PROTOCOL_VERSION); */
/* 		blocks_packed++; */
/* 	} */
/* 	list_iterator_destroy(itr); */
/* 	slurm_mutex_unlock(&block_state_mutex); */
/* 	tmp_offset = get_buf_offset(buffer); */
/* 	set_buf_offset(buffer, block_offset); */
/* 	pack32(blocks_packed, buffer); */
/* 	set_buf_offset(buffer, tmp_offset); */
/* 	/\* Maintain config read lock until we copy state_save_location *\ */
/* 	   \* unlock_slurmctld(part_read_lock);          - see below      *\/ */

/* 	/\* write the buffer to file *\/ */
/* 	slurm_conf_lock(); */
/* 	old_file = xstrdup(slurmctld_conf.state_save_location); */
/* 	xstrcat(old_file, "/block_state.old"); */
/* 	reg_file = xstrdup(slurmctld_conf.state_save_location); */
/* 	xstrcat(reg_file, "/block_state"); */
/* 	new_file = xstrdup(slurmctld_conf.state_save_location); */
/* 	xstrcat(new_file, "/block_state.new"); */
/* 	slurm_conf_unlock(); */

/* 	log_fd = creat(new_file, 0600); */
/* 	if (log_fd < 0) { */
/* 		error("Can't save state, error creating file %s, %m", */
/* 		      new_file); */
/* 		error_code = errno; */
/* 	} else { */
/* 		int pos = 0, nwrite = get_buf_offset(buffer), amount; */
/* 		char *data = (char *)get_buf_data(buffer); */

/* 		while (nwrite > 0) { */
/* 			amount = write(log_fd, &data[pos], nwrite); */
/* 			if ((amount < 0) && (errno != EINTR)) { */
/* 				error("Error writing file %s, %m", new_file); */
/* 				error_code = errno; */
/* 				break; */
/* 			} */
/* 			nwrite -= amount; */
/* 			pos    += amount; */
/* 		} */
/* 		fsync(log_fd); */
/* 		close(log_fd); */
/* 	} */
/* 	if (error_code) */
/* 		(void) unlink(new_file); */
/* 	else {			/\* file shuffle *\/ */
/* 		(void) unlink(old_file); */
/* 		if (link(reg_file, old_file)) */
/* 			debug4("unable to create link for %s -> %s: %m", */
/* 			       reg_file, old_file); */
/* 		(void) unlink(reg_file); */
/* 		if (link(new_file, reg_file)) */
/* 			debug4("unable to create link for %s -> %s: %m", */
/* 			       new_file, reg_file); */
/* 		(void) unlink(new_file); */
/* 	} */
/* 	xfree(old_file); */
/* 	xfree(reg_file); */
/* 	xfree(new_file); */

/* 	free_buf(buffer); */
/* 	END_TIMER2("select_p_state_save"); */
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_state_restore(char *dir_name)
{
#ifdef HAVE_BGQ
	debug("bgq: select_p_state_restore");

	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

/* Sync BG blocks to currently active jobs */
extern int select_p_job_init(List job_list)
{
#ifdef HAVE_BGQ
	int rc = SLURM_SUCCESS;
	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern bool select_p_node_ranking(struct node_record *node_ptr, int node_cnt)
{
	return false;
}

/* All initialization is performed by init() */
extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
{
#ifdef HAVE_BGQ
	if (node_cnt>0 && bg_conf)
		if (node_ptr->cpus >= bg_conf->mp_node_cnt)
			bg_conf->cpus_per_mp = node_ptr->cpus;

	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

/*
 * Called by slurmctld when a new configuration file is loaded
 * or scontrol is used to change block configuration
 */
 extern int select_p_block_init(List part_list)
{
#ifdef HAVE_BGQ
	/* select_p_node_init needs to be called before this to set
	   this up correctly
	*/
	if (read_bg_conf() == SLURM_ERROR) {
		fatal("Error, could not read the file");
		return SLURM_ERROR;
	}

	if (part_list) {
		struct part_record *part_ptr = NULL;
		ListIterator itr = list_iterator_create(part_list);
		while ((part_ptr = list_next(itr))) {
			part_ptr->max_nodes = part_ptr->max_nodes_orig;
			part_ptr->min_nodes = part_ptr->min_nodes_orig;
			select_p_alter_node_cnt(SELECT_SET_MP_CNT,
						&part_ptr->max_nodes);
			select_p_alter_node_cnt(SELECT_SET_MP_CNT,
						&part_ptr->min_nodes);
		}
		list_iterator_destroy(itr);
	}
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}


/*
 * select_p_job_test - Given a specification of scheduling requirements,
 *	identify the nodes which "best" satify the request. The specified
 *	nodes may be DOWN or BUSY at the time of this test as may be used
 *	to deterime if a job could ever run.
 * IN/OUT job_ptr - pointer to job being scheduled start_time is set
 *	when we can possibly start job.
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to
 *	satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN max_nodes - maximum count of nodes (0==don't care)
 * IN req_nodes - requested (or desired) count of nodes
 * IN mode - SELECT_MODE_RUN_NOW: try to schedule job now
 *           SELECT_MODE_TEST_ONLY: test if job can ever run
 *           SELECT_MODE_WILL_RUN: determine when and where job can run
 * IN preemptee_candidates - List of pointers to jobs which can be preempted.
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode=SELECT_MODE_TEST_ONLY or input pointer is NULL.
 * RET zero on success, EINVAL otherwise
 * NOTE: bitmap must be a superset of req_nodes at the time that
 *	select_p_job_test is called
 */
extern int select_p_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     List preemptee_candidates,
			     List *preemptee_job_list)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_job_begin(struct job_record *job_ptr)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_job_ready(struct job_record *job_ptr)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_job_resized(struct job_record *job_ptr,
				struct node_record *node_ptr)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int select_p_job_fini(struct job_record *job_ptr)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_job_suspend(struct job_record *job_ptr)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int select_p_job_resume(struct job_record *job_ptr)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int select_p_pack_select_info(time_t last_query_time,
				     uint16_t show_flags, Buf *buffer_ptr,
				     uint16_t protocol_version)
{
#ifdef HAVE_BGQ
	return ESLURM_NOT_SUPPORTED;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_select_nodeinfo_pack(select_nodeinfo_t *nodeinfo,
					 Buf buffer,
					 uint16_t protocol_version)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int select_p_select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo,
					   Buf buffer,
					   uint16_t protocol_version)
{
	return ESLURM_NOT_SUPPORTED;
}

extern select_nodeinfo_t *select_p_select_nodeinfo_alloc(uint32_t size)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_free(select_nodeinfo_t *nodeinfo)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set_all(time_t last_query_time)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_get(select_nodeinfo_t *nodeinfo,
					enum select_nodedata_type dinfo,
					enum node_states state,
					void *data)
{
	return SLURM_SUCCESS;
}

select_jobinfo_t *select_p_select_jobinfo_alloc(void)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_jobinfo_set(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_jobinfo_get (select_jobinfo_t *jobinfo,
				 enum select_jobdata_type data_type, void *data)
{
	return SLURM_SUCCESS;
}

extern select_jobinfo_t *select_p_select_jobinfo_copy(select_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_jobinfo_free  (select_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

extern int  select_p_select_jobinfo_pack(select_jobinfo_t *jobinfo, Buf buffer,
					 uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

extern int  select_p_select_jobinfo_unpack(select_jobinfo_t **jobinfo,
					   Buf buffer,
					   uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

extern char *select_p_select_jobinfo_sprint(select_jobinfo_t *jobinfo,
				     char *buf, size_t size, int mode)
{
	return SLURM_SUCCESS;
}

extern char *select_p_select_jobinfo_xstrdup(select_jobinfo_t *jobinfo,
					     int mode)
{
	return SLURM_SUCCESS;
}

extern int select_p_update_block (update_block_msg_t *block_desc_ptr)
{
#ifdef HAVE_BGQ
	int rc = SLURM_SUCCESS;
	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_update_sub_node (update_block_msg_t *block_desc_ptr)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_get_info_from_plugin (enum select_plugindata_info dinfo,
					  struct job_record *job_ptr,
					  void *data)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_update_node_config (int index)
{
	return SLURM_SUCCESS;
}

extern int select_p_update_node_state (int index, uint16_t state)
{
#ifdef HAVE_BGQ
	int coords[cluster_dims];
	return _internal_update_node_state(A, coords, index, state);
#endif
	return SLURM_ERROR;
}

extern int select_p_alter_node_cnt(enum select_node_cnt type, void *data)
{
#ifdef HAVE_BGQ
	job_desc_msg_t *job_desc = (job_desc_msg_t *)data;
	uint16_t *cpus = (uint16_t *)data;
	uint32_t *nodes = (uint32_t *)data, tmp = 0;
	int i;
	uint16_t req_geometry[SYSTEM_DIMENSIONS];

	if (!bg_conf->mp_node_cnt) {
		fatal("select_p_alter_node_cnt: This can't be called "
		      "before init");
	}

	switch (type) {
	case SELECT_GET_NODE_SCALING:
		if ((*nodes) != INFINITE)
			(*nodes) = bg_conf->mp_node_cnt;
		break;
	case SELECT_GET_NODE_CPU_CNT:
		if ((*cpus) != (uint16_t)INFINITE)
			(*cpus) = bg_conf->cpu_ratio;
		break;
	case SELECT_GET_MP_CPU_CNT:
		if ((*nodes) != INFINITE)
			(*nodes) = bg_conf->cpus_per_mp;
		break;
	case SELECT_SET_MP_CNT:
		if (((*nodes) == INFINITE) || ((*nodes) == NO_VAL))
			tmp = (*nodes);
		else if ((*nodes) > bg_conf->mp_node_cnt) {
			tmp = (*nodes);
			tmp /= bg_conf->mp_node_cnt;
			if (tmp < 1)
				tmp = 1;
		} else
			tmp = 1;
		(*nodes) = tmp;
		break;
	case SELECT_APPLY_NODE_MIN_OFFSET:
		if ((*nodes) == 1) {
			/* Job will actually get more than one c-node,
			 * but we can't be sure exactly how much so we
			 * don't scale up this value. */
			break;
		}
		(*nodes) *= bg_conf->mp_node_cnt;
		break;
	case SELECT_APPLY_NODE_MAX_OFFSET:
		if ((*nodes) != INFINITE)
			(*nodes) *= bg_conf->mp_node_cnt;
		break;
	case SELECT_SET_NODE_CNT:
		get_select_jobinfo(job_desc->select_jobinfo->data,
				   SELECT_JOBDATA_ALTERED, &tmp);
		if (tmp == 1) {
			return SLURM_SUCCESS;
		}
		tmp = 1;
		set_select_jobinfo(job_desc->select_jobinfo->data,
				   SELECT_JOBDATA_ALTERED, &tmp);

		if (job_desc->min_nodes == (uint32_t) NO_VAL)
			return SLURM_SUCCESS;

		get_select_jobinfo(job_desc->select_jobinfo->data,
				   SELECT_JOBDATA_GEOMETRY, &req_geometry);

		if (req_geometry[0] != 0
		    && req_geometry[0] != (uint16_t)NO_VAL) {
			job_desc->min_nodes = 1;
			for (i=0; i<SYSTEM_DIMENSIONS; i++)
				job_desc->min_nodes *=
					(uint16_t)req_geometry[i];
			job_desc->min_nodes *= bg_conf->mp_node_cnt;
			job_desc->max_nodes = job_desc->min_nodes;
		}

		/* make sure if the user only specified min_cpus to
		   set min_nodes correctly
		*/
		if ((job_desc->min_cpus != NO_VAL)
		    && (job_desc->min_cpus > job_desc->min_nodes))
			job_desc->min_nodes =
				job_desc->min_cpus / bg_conf->cpu_ratio;

		/* initialize min_cpus to the min_nodes */
		job_desc->min_cpus = job_desc->min_nodes * bg_conf->cpu_ratio;

		if ((job_desc->max_nodes == (uint32_t) NO_VAL)
		    || (job_desc->max_nodes < job_desc->min_nodes))
			job_desc->max_nodes = job_desc->min_nodes;

		/* See if min_nodes is greater than one base partition */
		if (job_desc->min_nodes > bg_conf->mp_node_cnt) {
			/*
			 * if it is make sure it is a factor of
			 * bg_conf->mp_node_cnt, if it isn't make it
			 * that way
			 */
			tmp = job_desc->min_nodes % bg_conf->mp_node_cnt;
			if (tmp > 0)
				job_desc->min_nodes +=
					(bg_conf->mp_node_cnt-tmp);
		}
		tmp = job_desc->min_nodes / bg_conf->mp_node_cnt;

		/* this means it is greater or equal to one mp */
		if (tmp > 0) {
			set_select_jobinfo(job_desc->select_jobinfo->data,
					   SELECT_JOBDATA_NODE_CNT,
					   &job_desc->min_nodes);
			job_desc->min_nodes = tmp;
			job_desc->min_cpus = bg_conf->cpus_per_mp * tmp;
		} else {
#ifdef HAVE_BGL
			if (job_desc->min_nodes <= bg_conf->nodecard_node_cnt
			    && bg_conf->nodecard_ionode_cnt)
				job_desc->min_nodes =
					bg_conf->nodecard_node_cnt;
			else if (job_desc->min_nodes
				 <= bg_conf->quarter_node_cnt)
				job_desc->min_nodes =
					bg_conf->quarter_node_cnt;
			else
				job_desc->min_nodes =
					bg_conf->mp_node_cnt;

			set_select_jobinfo(job_desc->select_jobinfo->data,
					   SELECT_JOBDATA_NODE_CNT,
					   &job_desc->min_nodes);

			tmp = bg_conf->mp_node_cnt/job_desc->min_nodes;

			job_desc->min_cpus = bg_conf->cpus_per_mp/tmp;
			job_desc->min_nodes = 1;
#else
			i = bg_conf->smallest_block;
			while (i <= bg_conf->mp_node_cnt) {
				if (job_desc->min_nodes <= i) {
					job_desc->min_nodes = i;
					break;
				}
				i *= 2;
			}

			set_select_jobinfo(job_desc->select_jobinfo->data,
					   SELECT_JOBDATA_NODE_CNT,
					   &job_desc->min_nodes);

			job_desc->min_cpus = job_desc->min_nodes
				* bg_conf->cpu_ratio;
			job_desc->min_nodes = 1;
#endif
		}

		if (job_desc->max_nodes > bg_conf->mp_node_cnt) {
			tmp = job_desc->max_nodes % bg_conf->mp_node_cnt;
			if (tmp > 0)
				job_desc->max_nodes +=
					(bg_conf->mp_node_cnt-tmp);
		}
		tmp = job_desc->max_nodes / bg_conf->mp_node_cnt;

		if (tmp > 0) {
			job_desc->max_nodes = tmp;
			job_desc->max_cpus =
				job_desc->max_nodes * bg_conf->cpus_per_mp;
			tmp = NO_VAL;
		} else {
#ifdef HAVE_BGL
			if (job_desc->max_nodes <= bg_conf->nodecard_node_cnt
			    && bg_conf->nodecard_ionode_cnt)
				job_desc->max_nodes =
					bg_conf->nodecard_node_cnt;
			else if (job_desc->max_nodes
				 <= bg_conf->quarter_node_cnt)
				job_desc->max_nodes =
					bg_conf->quarter_node_cnt;
			else
				job_desc->max_nodes =
					bg_conf->mp_node_cnt;

			tmp = bg_conf->mp_node_cnt/job_desc->max_nodes;
			job_desc->max_cpus = bg_conf->cpus_per_mp/tmp;
			job_desc->max_nodes = 1;
#else
			i = bg_conf->smallest_block;
			while (i <= bg_conf->mp_node_cnt) {
				if (job_desc->max_nodes <= i) {
					job_desc->max_nodes = i;
					break;
				}
				i *= 2;
			}
			job_desc->max_cpus =
				job_desc->max_nodes * bg_conf->cpu_ratio;

			job_desc->max_nodes = 1;
#endif
		}
		tmp = NO_VAL;

		break;
	default:
		error("unknown option %d for alter_node_cnt", type);
	}

	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_reconfigure(void)
{
#ifdef HAVE_BGQ
	slurm_conf_lock();
	if (!slurmctld_conf.slurm_user_name
	    || strcmp(bg_conf->slurm_user_name, slurmctld_conf.slurm_user_name))
		error("The slurm user has changed from '%s' to '%s'.  "
		      "If this is really what you "
		      "want you will need to restart slurm for this "
		      "change to be enforced in the bluegene plugin.",
		      bg_conf->slurm_user_name, slurmctld_conf.slurm_user_name);
	if (!slurmctld_conf.node_prefix
	    || strcmp(bg_conf->slurm_node_prefix, slurmctld_conf.node_prefix))
		error("Node Prefix has changed from '%s' to '%s'.  "
		      "If this is really what you "
		      "want you will need to restart slurm for this "
		      "change to be enforced in the bluegene plugin.",
		      bg_conf->slurm_node_prefix, slurmctld_conf.node_prefix);
	bg_conf->slurm_debug_flags = slurmctld_conf.debug_flags;
	set_ba_debug_flags(bg_conf->slurm_debug_flags);
	slurm_conf_unlock();

	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern bitstr_t *select_p_resv_test(bitstr_t *avail_bitmap, uint32_t node_cnt)
{
	return NULL;
}

extern void select_p_ba_init(node_info_msg_t *node_info_ptr, bool sanity_check)
{
	ba_init(node_info_ptr, sanity_check);
}

extern void select_p_ba_fini(void)
{
	ba_fini();
}

extern int *select_p_ba_get_dims(void)
{
	return DIM_SIZE;
}

extern void select_p_ba_reset(bool track_down_nodes)
{
	reset_ba_system(track_down_nodes);
}

extern int select_p_ba_request_apply(select_ba_request_t *ba_request)
{
	return new_ba_request(ba_request);
}

extern int select_p_ba_remove_block(List mps, int new_count, bool is_small)
{
	return remove_block(mps, new_count, is_small);
}
