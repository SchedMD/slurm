/*****************************************************************************\
 *  select_bluegene.c - node selection plugin for Blue Gene system.
 * 
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov> Danny Auble <da@llnl.gov>
 *  UCRL-CODE-217948.
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

#include "bluegene.h"

#define HUGE_BUF_SIZE (1024*16)


/* global */
int procs_per_node = 512;

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "select" for SLURM node selection) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load select plugins if the plugin_type string has a 
 * prefix of "select/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the node selection API matures.
 */
const char plugin_name[]       	= "Blue Gene node selection plugin";
const char plugin_type[]       	= "select/bluegene";
const uint32_t plugin_version	= 90;

/* pthread stuff for updating BG node status */
static pthread_t bluegene_thread = 0;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;

/** initialize the status pthread */
static int _init_status_pthread(void);
static int _wait_for_thread (pthread_t thread_id);
static char *_block_state_str(int state);

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
#ifndef HAVE_BG
	fatal("Plugin select/bluegene is illegal on non-BlueGene computers");
#endif
#if (SYSTEM_DIMENSIONS != 3)
	fatal("SYSTEM_DIMENSIONS value (%d) invalid for Blue Gene",
		SYSTEM_DIMENSIONS);
#endif
#ifdef HAVE_BG_FILES
	if (!getenv("CLASSPATH") || !getenv("DB2INSTANCE") 
	||  !getenv("VWSPATH"))
		fatal("db2profile has not been run to setup DB2 environment");

	if ((SELECT_MESH  != RM_MESH)
	||  (SELECT_TORUS != RM_TORUS)
	||  (SELECT_NAV   != RM_NAV))
		fatal("enum conn_type out of sync with rm_api.h");

	if ((SELECT_COPROCESSOR_MODE  != RM_PARTITION_COPROCESSOR_MODE)
	||  (SELECT_VIRTUAL_NODE_MODE != RM_PARTITION_VIRTUAL_NODE_MODE))
		fatal("enum node_use_type out of sync with rm_api.h");
#endif

	verbose("%s loading...", plugin_name);
	if (init_bg() || _init_status_pthread())
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

static int _init_status_pthread(void)
{
	pthread_attr_t attr;

	pthread_mutex_lock( &thread_flag_mutex );
	if ( bluegene_thread ) {
		debug2("Bluegene thread already running, not starting "
			"another");
		pthread_mutex_unlock( &thread_flag_mutex );
		return SLURM_ERROR;
	}

	slurm_attr_init( &attr );
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
	if (pthread_create( &bluegene_thread, &attr, bluegene_agent, NULL)
	    != 0)
		error("Failed to create bluegene_agent thread");
	pthread_mutex_unlock( &thread_flag_mutex );
	slurm_attr_destroy( &attr );

	return SLURM_SUCCESS;
}

static int _wait_for_thread (pthread_t thread_id)
{
	int i;

	for (i=0; i<4; i++) {
		if (pthread_kill(thread_id, 0))
			return SLURM_SUCCESS;
		sleep(1);
	}
	error("Could not kill select script pthread");
	return SLURM_ERROR;
}

static char *_block_state_str(int state)
{
	static char tmp[16];

#ifdef HAVE_BG
	switch (state) {
		case 0: 
			return "ERROR";
		case 1:
			return "FREE";
	}
#endif

	snprintf(tmp, sizeof(tmp), "%d", state);
	return tmp;
}

extern int fini ( void )
{
	int rc = SLURM_SUCCESS;

	pthread_mutex_lock( &thread_flag_mutex );
	if ( bluegene_thread ) {
		agent_fini = true;
		verbose("Bluegene select plugin shutting down");
		rc = _wait_for_thread(bluegene_thread);
		bluegene_thread = 0;
	}
	pthread_mutex_unlock( &thread_flag_mutex );

	fini_bg();

	return rc;
}

/*
 * The remainder of this file implements the standard SLURM 
 * node selection API.
 */

/*
 * Called by slurmctld when a new configuration file is loaded
 * or scontrol is used to change block configuration
 */
 extern int select_p_block_init(List part_list)
{
#ifdef HAVE_BG
	if(read_bg_conf() == SLURM_ERROR) {
		fatal("Error, could not read the file");
		return SLURM_ERROR;
	}
#else
	/*looking for blocks only I created */
	if (create_defined_blocks(bluegene_layout_mode) 
			== SLURM_ERROR) {
		/* error in creating the static blocks, so
		 * blocks referenced by submitted jobs won't
		 * correspond to actual slurm blocks.
		 */
		fatal("Error, could not create the static blocks");
		return SLURM_ERROR;
	}
#endif
	
	return SLURM_SUCCESS; 
}

/* We rely upon DB2 to save and restore BlueGene state */
extern int select_p_state_save(char *dir_name)
{
	return SLURM_SUCCESS;
}

extern int select_p_state_restore(char *dir_name)
{
	return SLURM_SUCCESS;
}

/* Sync BG blocks to currently active jobs */
extern int select_p_job_init(List job_list)
{
	return sync_jobs(job_list);
}

/* All initialization is performed by select_p_block_init() */
extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
{
	if(node_cnt>0)
		if(node_ptr->cpus > 512)
			procs_per_node = node_ptr->cpus;
	return SLURM_SUCCESS;
}

/*
 * select_p_job_test - Given a specification of scheduling requirements, 
 *	identify the nodes which "best" satify the request. The specified 
 *	nodes may be DOWN or BUSY at the time of this test as may be used 
 *	to deterime if a job could ever run.
 * IN job_ptr - pointer to job being scheduled
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to 
 *	satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN max_nodes - maximum count of nodes (0==don't care)
 * IN req_nodes - requested (or desired) count of nodes
 * IN test_only - if true, only test if ever could run, not necessarily now
 * RET zero on success, EINVAL otherwise
 * NOTE: bitmap must be a superset of req_nodes at the time that 
 *	select_p_job_test is called
 */
extern int select_p_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			uint32_t min_nodes, uint32_t max_nodes, 
			uint32_t req_nodes, bool test_only)
{
	/* bg block test - is there a block where we have:
	 * 1) geometry requested
	 * 2) min/max nodes (BPs) requested
	 * 3) type: TORUS or MESH or NAV (torus else mesh)
	 * 4) use: VIRTUAL or COPROCESSOR
	 * 
	 * note: we don't have to worry about security at this level
	 * as the SLURM block logic will handle access rights.
	 */

	return submit_job(job_ptr, bitmap, min_nodes, max_nodes, 
			  req_nodes, test_only);
}

extern int select_p_job_begin(struct job_record *job_ptr)
{
	return start_job(job_ptr);
}

extern int select_p_job_fini(struct job_record *job_ptr)
{
	return term_job(job_ptr);
}

extern int select_p_job_suspend(struct job_record *job_ptr)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int select_p_job_resume(struct job_record *job_ptr)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int select_p_job_ready(struct job_record *job_ptr)
{
#ifdef HAVE_BG_FILES
	return block_ready(job_ptr);
#else
	if (job_ptr->job_state == JOB_RUNNING)
		return 1;
	return 0;
#endif
}

extern int select_p_pack_node_info(time_t last_query_time, Buf *buffer_ptr)
{
	ListIterator itr;
	bg_record_t *bg_record = NULL;
	uint32_t blocks_packed = 0, tmp_offset;
	Buf buffer;

	/* check to see if data has changed */
	if (last_query_time >= last_bg_update) {
		debug2("Node select info hasn't changed since %d", 
			last_bg_update);
		return SLURM_NO_CHANGE_IN_DATA;
	} else {
		*buffer_ptr = NULL;
		buffer = init_buf(HUGE_BUF_SIZE);
		pack32(blocks_packed, buffer);
		pack_time(last_bg_update, buffer);

		if(bg_list) {
			slurm_mutex_lock(&block_state_mutex);
			itr = list_iterator_create(bg_list);
			while ((bg_record = (bg_record_t *) list_next(itr)) 
			       != NULL) {
				xassert(bg_record->bg_block_id != NULL);
				
				pack_block(bg_record, buffer);
				blocks_packed++;
			}
			list_iterator_destroy(itr);
			slurm_mutex_unlock(&block_state_mutex);
		} else {
			error("select_p_pack_node_info: no bg_list");
			return SLURM_ERROR;
		}
		tmp_offset = get_buf_offset(buffer);
		set_buf_offset(buffer, 0);
		pack32(blocks_packed, buffer);
		set_buf_offset(buffer, tmp_offset);

		*buffer_ptr = buffer;
	}

	return SLURM_SUCCESS;
}


extern int select_p_get_select_nodeinfo (struct node_record *node_ptr, 
                                         enum select_data_info info,
                                         void *data)
{
       return SLURM_SUCCESS;
}

extern int select_p_update_nodeinfo (struct job_record *job_ptr,
                                            enum select_data_info info)
{
       return SLURM_SUCCESS;
}

extern int select_p_update_block (update_part_msg_t *part_desc_ptr)
{
	int rc = SLURM_SUCCESS;
	bg_record_t *bg_record = NULL;
	time_t now;
	char reason[128], tmp[64], time_str[32];

	bg_record = find_bg_record_in_list(bg_list, part_desc_ptr->name);
	if(!bg_record)
		return SLURM_ERROR;
	now = time(NULL);
	slurm_make_time_str(&now, time_str, sizeof(time_str));
	snprintf(tmp, sizeof(tmp), "[SLURM@%s]", time_str);
	snprintf(reason, sizeof(reason),
		 "update_block: "
		 "Admin set block %s state to %s %s",
		 bg_record->bg_block_id, 
		 _block_state_str(part_desc_ptr->state_up), tmp); 
	if(bg_record->job_running > NO_JOB_RUNNING) {
		slurm_fail_job(bg_record->job_running);	
		while(bg_record->job_running > NO_JOB_RUNNING) 
			sleep(1);
	}
	if(!part_desc_ptr->state_up) {
		slurm_mutex_lock(&block_state_mutex);
		bg_record->job_running = BLOCK_ERROR_STATE;
		bg_record->state = RM_PARTITION_ERROR;
		slurm_mutex_unlock(&block_state_mutex);
	} else if(part_desc_ptr->state_up){
		slurm_mutex_lock(&block_state_mutex);
		bg_record->job_running = NO_JOB_RUNNING;
		bg_record->state = RM_PARTITION_FREE;
		slurm_mutex_unlock(&block_state_mutex);
	} else {
		return rc;
	}
	info("%s",reason);
	last_bg_update = time(NULL);
	return rc;
}

extern int select_p_get_extra_jobinfo (struct node_record *node_ptr, 
				       struct job_record *job_ptr, 
                                       enum select_data_info info,
                                       void *data)
{
       return SLURM_SUCCESS;
}

extern int select_p_get_info_from_plugin (enum select_data_info info, 
					  void *data)
{
	return SLURM_SUCCESS;
}

extern int select_p_alter_node_cnt(enum select_node_cnt type, void *data)
{
	job_desc_msg_t *job_desc = (job_desc_msg_t *)data;
	uint32_t *nodes = (uint32_t *)data;
	int tmp, i;
	uint16_t req_geometry[BA_SYSTEM_DIMENSIONS];
	
	switch (type) {
	case SELECT_GET_NODE_SCALING:
		(*nodes) = bluegene_bp_node_cnt;
		break;
	case SELECT_APPLY_NODE_MIN_OFFSET:
		if((*nodes) == 1) {
			/* Job will actually get more than one c-node, 
			 * but we can't be sure exactly how much so we 
			 * don't scale up this value. */
			break;
		}
		(*nodes) *= bluegene_bp_node_cnt;
		break;
	case SELECT_APPLY_NODE_MAX_OFFSET:
		(*nodes) *= bluegene_bp_node_cnt;
		break;
	case SELECT_SET_NODE_CNT:
		select_g_get_jobinfo(job_desc->select_jobinfo,
				     SELECT_DATA_ALTERED, &tmp);
		if(tmp == 1) {
			return SLURM_SUCCESS;
		}
		tmp = 1;
		select_g_set_jobinfo(job_desc->select_jobinfo,
				     SELECT_DATA_ALTERED, &tmp);
		tmp = NO_VAL;
		select_g_set_jobinfo(job_desc->select_jobinfo,
				     SELECT_DATA_MAX_PROCS, 
				     &tmp);
	
		if(job_desc->min_nodes == NO_VAL)
			return SLURM_SUCCESS;
		select_g_get_jobinfo(job_desc->select_jobinfo,
				     SELECT_DATA_GEOMETRY, &req_geometry);

		if(req_geometry[0] != 0 
		   && req_geometry[0] != (uint16_t)NO_VAL) {
			job_desc->min_nodes = 1;
			for (i=0; i<BA_SYSTEM_DIMENSIONS; i++)
				job_desc->min_nodes *= 
					(uint16_t)req_geometry[i];
			job_desc->min_nodes *= bluegene_bp_node_cnt;
			job_desc->max_nodes = job_desc->min_nodes;
		}

		if(job_desc->num_procs != NO_VAL) {
			if(job_desc->min_nodes < job_desc->num_procs)
				job_desc->min_nodes = job_desc->num_procs;
			if(job_desc->max_nodes < job_desc->num_procs)
				job_desc->max_nodes = job_desc->num_procs;
		}
		/* See if min_nodes is greater than one base partition */
		if(job_desc->min_nodes > bluegene_bp_node_cnt) {
			/*
			  if it is make sure it is a factor of 
			  bluegene_bp_node_cnt, if it isn't make it 
			  that way 
			*/
			tmp = job_desc->min_nodes % bluegene_bp_node_cnt;
			if(tmp > 0)
				job_desc->min_nodes += 
					(bluegene_bp_node_cnt-tmp);
		}
		tmp = job_desc->min_nodes / bluegene_bp_node_cnt;
		
		/* this means it is greater or equal to one bp */
		if(tmp > 0) {
			job_desc->min_nodes = tmp;
			job_desc->num_procs = procs_per_node * tmp;
		} else { 
			if(job_desc->min_nodes <= bluegene_nodecard_node_cnt)
				job_desc->min_nodes = 
					bluegene_nodecard_node_cnt;
			else if(job_desc->min_nodes 
				<= bluegene_quarter_node_cnt)
				job_desc->min_nodes = 
					bluegene_quarter_node_cnt;
			else 
				job_desc->min_nodes = 
					bluegene_bp_node_cnt;
			
			tmp = bluegene_bp_node_cnt/job_desc->min_nodes;
			
			job_desc->num_procs = procs_per_node/tmp;
			job_desc->min_nodes = 1;
		}
		
		if(job_desc->max_nodes == NO_VAL) 
			return SLURM_SUCCESS;
		
		if(job_desc->max_nodes > bluegene_bp_node_cnt) {
			tmp = job_desc->max_nodes % bluegene_bp_node_cnt;
			if(tmp > 0)
				job_desc->max_nodes += 
					(bluegene_bp_node_cnt-tmp);
		}
		tmp = job_desc->max_nodes / bluegene_bp_node_cnt;
		if(tmp > 0) {
			job_desc->max_nodes = tmp;
			tmp = NO_VAL;
		} else {
			if(job_desc->max_nodes <= bluegene_nodecard_node_cnt)
				job_desc->max_nodes = 
					bluegene_nodecard_node_cnt;
			else if(job_desc->max_nodes 
				<= bluegene_quarter_node_cnt)
				job_desc->max_nodes = 
					bluegene_quarter_node_cnt;
			else 
				job_desc->max_nodes = 
					bluegene_bp_node_cnt;
		
			tmp = bluegene_bp_node_cnt/job_desc->max_nodes;
			tmp = procs_per_node/tmp;
			
			select_g_set_jobinfo(job_desc->select_jobinfo,
					     SELECT_DATA_MAX_PROCS, 
					     &tmp);
			job_desc->max_nodes = 1;
		}
		tmp = NO_VAL;
			
		break;
	default:
		error("unknown option %d for alter_node_cnt",type);
	}
	
	return SLURM_SUCCESS;
}
