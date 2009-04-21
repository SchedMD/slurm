/*****************************************************************************\
 *  select_bluegene.c - node selection plugin for Blue Gene system.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov> Danny Auble <da@llnl.gov>
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

#include "bluegene.h"

#ifndef HAVE_BG
#include "defined_block.h"
#endif

//#include "src/common/uid.h"
#include "src/slurmctld/trigger_mgr.h"
#include <fcntl.h>

#define HUGE_BUF_SIZE (1024*16)

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
const uint32_t plugin_version	= 100;

/* pthread stuff for updating BG node status */
static pthread_t block_thread = 0;
static pthread_t state_thread = 0;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;

/** initialize the status pthread */
static int _init_status_pthread(void);
static char *_block_state_str(int state);

extern int select_p_alter_node_cnt(enum select_node_cnt type, void *data);

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
#ifdef HAVE_BGL
	if (!getenv("CLASSPATH") || !getenv("DB2INSTANCE")
	    || !getenv("VWSPATH"))
		fatal("db2profile has not been run to setup DB2 environment");
	
	if ((SELECT_COPROCESSOR_MODE  != RM_PARTITION_COPROCESSOR_MODE)
	    || (SELECT_VIRTUAL_NODE_MODE != RM_PARTITION_VIRTUAL_NODE_MODE))
		fatal("enum node_use_type out of sync with rm_api.h");
#endif
	if ((SELECT_MESH  != RM_MESH)
	    || (SELECT_TORUS != RM_TORUS)
	    || (SELECT_NAV   != RM_NAV))
		fatal("enum conn_type out of sync with rm_api.h");
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
	if ( block_thread ) {
		debug2("Bluegene threads already running, not starting "
		       "another");
		pthread_mutex_unlock( &thread_flag_mutex );
		return SLURM_ERROR;
	}

	slurm_attr_init( &attr );
	/* since we do a join on this later we don't make it detached */
	if (pthread_create( &block_thread, &attr, block_agent, NULL)
	    != 0)
		error("Failed to create block_agent thread");
	slurm_attr_init( &attr );
	/* since we do a join on this later we don't make it detached */
	if (pthread_create( &state_thread, &attr, state_agent, NULL)
	    != 0)
		error("Failed to create state_agent thread");
	pthread_mutex_unlock( &thread_flag_mutex );
	slurm_attr_destroy( &attr );

	return SLURM_SUCCESS;
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

	agent_fini = true;
	pthread_mutex_lock( &thread_flag_mutex );
	if ( block_thread ) {
		verbose("Bluegene select plugin shutting down");
		pthread_join(block_thread, NULL);
		block_thread = 0;
	}
	if ( state_thread ) {
		pthread_join(state_thread, NULL);
		state_thread = 0;
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
	xfree(bg_conf->slurm_user_name);
	xfree(bg_conf->slurm_node_prefix);

	slurm_conf_lock();
	xassert(slurmctld_conf.slurm_user_name);
	xassert(slurmctld_conf.node_prefix);
	bg_conf->slurm_user_name = xstrdup(slurmctld_conf.slurm_user_name);
	bg_conf->slurm_node_prefix = xstrdup(slurmctld_conf.node_prefix);
	slurm_conf_unlock();	

	/* select_p_node_init needs to be called before this to set
	   this up correctly
	*/
	bg_conf->proc_ratio = bg_conf->procs_per_bp/bg_conf->bp_node_cnt;
	if(!bg_conf->proc_ratio)
		fatal("We appear to have less than 1 proc on a cnode.  "
		      "You specified %u for BasePartitionNodeCnt "
		      "in the blugene.conf and %u procs "
		      "for each node in the slurm.conf",
		      bg_conf->bp_node_cnt, bg_conf->procs_per_bp);
	num_unused_cpus = 
		DIM_SIZE[X] * DIM_SIZE[Y] * DIM_SIZE[Z] * bg_conf->procs_per_bp;

	if(part_list) {
		struct part_record *part_ptr = NULL;
		ListIterator itr = list_iterator_create(part_list);
		while((part_ptr = list_next(itr))) {
			part_ptr->max_nodes = part_ptr->max_nodes_orig;
			part_ptr->min_nodes = part_ptr->min_nodes_orig;
			select_p_alter_node_cnt(SELECT_SET_BP_CNT, 
						&part_ptr->max_nodes);
			select_p_alter_node_cnt(SELECT_SET_BP_CNT,
						&part_ptr->min_nodes);
		}
		list_iterator_destroy(itr);
	}

	return SLURM_SUCCESS; 
}

/* We rely upon DB2 to save and restore BlueGene state */
extern int select_p_state_save(char *dir_name)
{
	ListIterator itr;
	bg_record_t *bg_record = NULL;
	int error_code = 0, log_fd;
	char *old_file, *new_file, *reg_file;
	uint32_t blocks_packed = 0, tmp_offset, block_offset;
	Buf buffer = init_buf(BUF_SIZE);
	DEF_TIMERS;

	debug("bluegene: select_p_state_save");
	START_TIMER;
	/* write header: time */
	packstr(BLOCK_STATE_VERSION, buffer);
	block_offset = get_buf_offset(buffer);
	pack32(blocks_packed, buffer);
	pack_time(time(NULL), buffer);

	/* write block records to buffer */
	slurm_mutex_lock(&block_state_mutex);
	itr = list_iterator_create(bg_lists->main);
	while((bg_record = list_next(itr))) {
		/* on real bluegene systems we only want to keep track of
		 * the blocks in an error state
		 */
#ifdef HAVE_BG_FILES
		if(bg_record->state != RM_PARTITION_ERROR)
			continue;
#endif
		xassert(bg_record->bg_block_id != NULL);
				
		pack_block(bg_record, buffer);
		blocks_packed++;
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&block_state_mutex);
	tmp_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, block_offset);
	pack32(blocks_packed, buffer);
	set_buf_offset(buffer, tmp_offset);
	/* Maintain config read lock until we copy state_save_location *\
	\* unlock_slurmctld(part_read_lock);          - see below      */

	/* write the buffer to file */
	slurm_conf_lock();
	old_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(old_file, "/block_state.old");
	reg_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(reg_file, "/block_state");
	new_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(new_file, "/block_state.new");
	slurm_conf_unlock();

	log_fd = creat(new_file, 0600);
	if (log_fd == 0) {
		error("Can't save state, error creating file %s, %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount;
		char *data = (char *)get_buf_data(buffer);

		while (nwrite > 0) {
			amount = write(log_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}
		fsync(log_fd);
		close(log_fd);
	}
	if (error_code)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		(void) link(reg_file, old_file);
		(void) unlink(reg_file);
		(void) link(new_file, reg_file);
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);

	free_buf(buffer);
	END_TIMER2("select_p_state_save");
	return SLURM_SUCCESS;
}

extern int select_p_state_restore(char *dir_name)
{
	debug("bluegene: select_p_state_restore");
	
	return validate_current_blocks(dir_name);
}

/* Sync BG blocks to currently active jobs */
extern int select_p_job_init(List job_list)
{
	return sync_jobs(job_list);
}

/* All initialization is performed by init() */
extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
{
	if(node_cnt>0)
		if(node_ptr->cpus >= bg_conf->bp_node_cnt) 
			bg_conf->procs_per_bp = node_ptr->cpus;
		
	return SLURM_SUCCESS;
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
 * RET zero on success, EINVAL otherwise
 * NOTE: bitmap must be a superset of req_nodes at the time that 
 *	select_p_job_test is called
 */
extern int select_p_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			     uint32_t min_nodes, uint32_t max_nodes, 
			     uint32_t req_nodes, int mode)
{
	/* submit_job - is there a block where we have:
	 * 1) geometry requested
	 * 2) min/max nodes (BPs) requested
	 * 3) type: TORUS or MESH or NAV (torus else mesh)
	 * 
	 * note: we don't have to worry about security at this level
	 * as the SLURM block logic will handle access rights.
	 */

	return submit_job(job_ptr, bitmap, min_nodes, max_nodes, 
			  req_nodes, mode);
}

/*
 * select_p_job_list_test - Given a list of select_will_run_t's in
 *	accending priority order we will see if we can start and
 *	finish all the jobs without increasing the start times of the
 *	jobs specified and fill in the est_start of requests with no
 *	est_start.  If you are looking to see if one job will ever run
 *	then use select_p_job_test instead.
 * IN/OUT req_list - list of select_will_run_t's in asscending
 *	             priority order on success of placement fill in
 *	             est_start of request with time.
 * RET zero on success, EINVAL otherwise
 */
extern int select_p_job_list_test(List req_list)
{
	return test_job_list(req_list);
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
	} else if(blocks_are_created) {
		*buffer_ptr = NULL;
		buffer = init_buf(HUGE_BUF_SIZE);
		pack32(blocks_packed, buffer);
		pack_time(last_bg_update, buffer);

		if(bg_lists->main) {
			slurm_mutex_lock(&block_state_mutex);
			itr = list_iterator_create(bg_lists->main);
			while ((bg_record = list_next(itr))) {
				pack_block(bg_record, buffer);
				blocks_packed++;
			}
			list_iterator_destroy(itr);
			slurm_mutex_unlock(&block_state_mutex);
		} else {
			error("select_p_pack_node_info: no bg_lists->main");
			return SLURM_ERROR;
		}
		/*
		 * get all the blocks we are freeing since they have
		 * been moved here
		 */
		if(bg_lists->freeing) {
			slurm_mutex_lock(&block_state_mutex);
			itr = list_iterator_create(bg_lists->freeing);
			while ((bg_record = (bg_record_t *) list_next(itr)) 
			       != NULL) {
				xassert(bg_record->bg_block_id != NULL);
				
				pack_block(bg_record, buffer);
				blocks_packed++;
			}
			list_iterator_destroy(itr);
			slurm_mutex_unlock(&block_state_mutex);
		} 
		tmp_offset = get_buf_offset(buffer);
		set_buf_offset(buffer, 0);
		pack32(blocks_packed, buffer);
		set_buf_offset(buffer, tmp_offset);
		
		*buffer_ptr = buffer;
	} else {
		error("select_p_pack_node_info: bg_lists->main not ready yet");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}


extern int select_p_get_select_nodeinfo (struct node_record *node_ptr, 
                                         enum select_data_info info,
                                         void *data)
{
       return SLURM_SUCCESS;
}

extern int select_p_update_nodeinfo (struct job_record *job_ptr)
{
       return SLURM_SUCCESS;
}

extern int select_p_update_block (update_part_msg_t *part_desc_ptr)
{
	int rc = SLURM_SUCCESS;
	bg_record_t *bg_record = NULL;
	time_t now;
	char reason[128], tmp[64], time_str[32];

	bg_record = find_bg_record_in_list(bg_lists->main, part_desc_ptr->name);
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
	
	/* First fail any job running on this block */
	if(bg_record->job_running > NO_JOB_RUNNING) {
		slurm_fail_job(bg_record->job_running);	
		/* need to set the job_ptr to NULL
		   here or we will get error message
		   about us trying to free this block
		   with a job in it.
		*/
		bg_record->job_ptr = NULL;
	} 
	
	/* Free all overlapping blocks and kill any jobs only
	 * if we are going into an error state */ 
	if (bg_conf->layout_mode != LAYOUT_DYNAMIC
	    && !part_desc_ptr->state_up) {
		bg_record_t *found_record = NULL;
		ListIterator itr;
		List delete_list = list_create(NULL);
		
		slurm_mutex_lock(&block_state_mutex);
		itr = list_iterator_create(bg_lists->main);
		while ((found_record = list_next(itr))) {
			if (bg_record == found_record)
				continue;
			
			if(!blocks_overlap(bg_record, found_record)) {
				debug2("block %s isn't part of errored %s",
				       found_record->bg_block_id, 
				       bg_record->bg_block_id);
				continue;
			}
			if(found_record->job_running > NO_JOB_RUNNING) {
				info("Failing job %u block %s "
				     "failed because overlapping block %s "
				     "is in an error state.", 
				     found_record->job_running, 
				     found_record->bg_block_id,
				     bg_record->bg_block_id);
				/* We need to fail this job first to
				   get the correct result even though
				   we are freeing the block later */
				slurm_fail_job(found_record->job_running);
				/* need to set the job_ptr to NULL
				   here or we will get error message
				   about us trying to free this block
				   with a job in it.
				*/
				found_record->job_ptr = NULL;
			} else {
				debug2("block %s is part of errored %s "
				       "but no running job",
				       found_record->bg_block_id, 
				       bg_record->bg_block_id);	
			}
			list_push(delete_list, found_record);
			num_block_to_free++;
		}		
		list_iterator_destroy(itr);
		free_block_list(delete_list);
		list_destroy(delete_list);
		slurm_mutex_unlock(&block_state_mutex);
	}

	if(!part_desc_ptr->state_up) {
		put_block_in_error_state(bg_record, BLOCK_ERROR_STATE);
	} else if(part_desc_ptr->state_up){
		resume_block(bg_record);
	} else {
		return rc;
	}
				
	info("%s", reason);
	last_bg_update = time(NULL);
	return rc;
}

extern int select_p_update_sub_node (update_part_msg_t *part_desc_ptr)
{
	int rc = SLURM_SUCCESS;
	int i = 0, j = 0;
	char coord[BA_SYSTEM_DIMENSIONS+1], *node_name = NULL;
	char ionodes[128];
	int set = 0;
	double nc_pos = 0, last_pos = -1;
	bitstr_t *ionode_bitmap = NULL;
	
	if(bg_conf->layout_mode != LAYOUT_DYNAMIC) {
		info("You can't use this call unless you are on a Dynamically "
		     "allocated system.  Please use update BlockName instead");
		rc = SLURM_ERROR;
		goto end_it;
	}

	memset(coord, 0, sizeof(coord));
	memset(ionodes, 0, 128);
	if(!part_desc_ptr->name) {
		error("update_sub_node: No name specified");
		rc = SLURM_ERROR;
		goto end_it;
	}

	while (part_desc_ptr->name[j] != '\0') {
		if (part_desc_ptr->name[j] == '[') {
			if(set<1) {
				rc = SLURM_ERROR;
				goto end_it;
			}
			i = j++;
			if((part_desc_ptr->name[j] < '0'
			    || part_desc_ptr->name[j] > 'Z'
			    || (part_desc_ptr->name[j] > '9' 
				&& part_desc_ptr->name[j] < 'A'))) {
				error("update_sub_node: sub part is empty");
				rc = SLURM_ERROR;
				goto end_it;
			}
			while(part_desc_ptr->name[i] != '\0') {
				if(part_desc_ptr->name[i] == ']') 
					break;
				i++;
			}
			if(part_desc_ptr->name[i] != ']') {
				error("update_sub_node: "
				      "No close (']') on sub part");
				rc = SLURM_ERROR;
				goto end_it;
			}
			
			strncpy(ionodes, part_desc_ptr->name+j, i-j); 
			set++;
			break;
		} else if((part_desc_ptr->name[j] >= '0'
			   && part_desc_ptr->name[j] <= '9')
			  || (part_desc_ptr->name[j] >= 'A'
			      && part_desc_ptr->name[j] <= 'Z')) {
			if(set) {
				rc = SLURM_ERROR;
				goto end_it;
			}
			for(i = 0; i < BA_SYSTEM_DIMENSIONS; i++) {
				if((part_desc_ptr->name[i] >= '0'
				    && part_desc_ptr->name[i] <= '9')
				   || (part_desc_ptr->name[i] >= 'A'
				      && part_desc_ptr->name[i] <= 'Z')) {
					error("update_sub_node: "
					      "misformatted name given %s",
					      part_desc_ptr->name);
					rc = SLURM_ERROR;
					goto end_it;
				}
			}
			
			strncpy(coord, part_desc_ptr->name+j,
				BA_SYSTEM_DIMENSIONS); 
			j += BA_SYSTEM_DIMENSIONS-1;
			set++;
		}
		j++;
	}
	
	if(set != 2) {
		error("update_sub_node: "
		      "I didn't get the base partition and the sub part.");
		rc = SLURM_ERROR;
		goto end_it;
	}
	ionode_bitmap = bit_alloc(bg_conf->numpsets);
	bit_unfmt(ionode_bitmap, ionodes);
	if(bit_ffs(ionode_bitmap) == -1) {
		error("update_sub_node: Invalid ionode '%s' given.", ionodes);
		rc = SLURM_ERROR;
		FREE_NULL_BITMAP(ionode_bitmap);
		goto end_it;		
	}
	node_name = xstrdup_printf("%s%s", bg_conf->slurm_node_prefix, coord);
	/* find out how many nodecards to get for each ionode */
	if(!part_desc_ptr->state_up) {
		info("Admin setting %s[%s] in an error state",
		     node_name, ionodes);
		for(i = 0; i<bg_conf->numpsets; i++) {
			if(bit_test(ionode_bitmap, i)) {
				if((int)nc_pos != (int)last_pos) {
					down_nodecard(node_name, i);
					last_pos = nc_pos;
				}
			}
			nc_pos += bg_conf->nc_ratio;
		}
	} else if(part_desc_ptr->state_up){
		info("Admin setting %s[%s] in an free state",
		     node_name, ionodes);
		up_nodecard(node_name, ionode_bitmap);
	} else {
		error("update_sub_node: Unknown state %d", 
		      part_desc_ptr->state_up);
		rc = SLURM_ERROR;
	}
	
	FREE_NULL_BITMAP(ionode_bitmap);
	xfree(node_name);
	
	last_bg_update = time(NULL);
end_it:
	return rc;
}

extern int select_p_get_info_from_plugin (enum select_data_info info, 
					  struct job_record *job_ptr,
					  void *data)
{
	if (info == SELECT_STATIC_PART) {
		uint16_t *tmp16 = (uint16_t *) data;
		if (bg_conf->layout_mode == LAYOUT_STATIC)
			*tmp16 = 1;
		else
			*tmp16 = 0;
	}

	return SLURM_SUCCESS;
}

extern int select_p_update_node_state (int index, uint16_t state)
{
	int x;
#ifdef HAVE_BG
	int y, z;
	
	for (y = DIM_SIZE[Y] - 1; y >= 0; y--) {
		for (z = 0; z < DIM_SIZE[Z]; z++) {
			for (x = 0; x < DIM_SIZE[X]; x++) {
				if (ba_system_ptr->grid[x][y][z].index 
				     == index) {
					ba_update_node_state(
						&ba_system_ptr->grid[x][y][z],
						state);
					return SLURM_SUCCESS;
				}
			}
		}
	}
#else
	for (x = 0; x < DIM_SIZE[X]; x++) {
		if (ba_system_ptr->grid[x].index == index) {
			ba_update_node_state(&ba_system_ptr->grid[x], state);
			return SLURM_SUCCESS;
		}
	}
#endif
	return SLURM_ERROR;
}

extern int select_p_alter_node_cnt(enum select_node_cnt type, void *data)
{
	job_desc_msg_t *job_desc = (job_desc_msg_t *)data;
	uint32_t *nodes = (uint32_t *)data, tmp;
	int i;
	uint16_t req_geometry[BA_SYSTEM_DIMENSIONS];

	if(!bg_conf->bp_node_cnt) {
		fatal("select_g_alter_node_cnt: This can't be called "
		      "before init");
	}

	switch (type) {
	case SELECT_GET_NODE_SCALING:
		if((*nodes) != INFINITE)
			(*nodes) = bg_conf->bp_node_cnt;
		break;
	case SELECT_SET_BP_CNT:
		if(((*nodes) == INFINITE) || ((*nodes) == NO_VAL))
			tmp = (*nodes);
		else if((*nodes) > bg_conf->bp_node_cnt) {
			tmp = (*nodes);
			tmp /= bg_conf->bp_node_cnt;
			if(tmp < 1) 
				tmp = 1;
		} else 
			tmp = 1;
		(*nodes) = tmp;
		break;
	case SELECT_APPLY_NODE_MIN_OFFSET:
		if((*nodes) == 1) {
			/* Job will actually get more than one c-node, 
			 * but we can't be sure exactly how much so we 
			 * don't scale up this value. */
			break;
		}
		(*nodes) *= bg_conf->bp_node_cnt;
		break;
	case SELECT_APPLY_NODE_MAX_OFFSET:
		if((*nodes) != INFINITE)
			(*nodes) *= bg_conf->bp_node_cnt;
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
	
		if(job_desc->min_nodes == (uint32_t) NO_VAL)
			return SLURM_SUCCESS;
		select_g_get_jobinfo(job_desc->select_jobinfo,
				     SELECT_DATA_GEOMETRY, &req_geometry);

		if(req_geometry[0] != 0 
		   && req_geometry[0] != (uint16_t)NO_VAL) {
			job_desc->min_nodes = 1;
			for (i=0; i<BA_SYSTEM_DIMENSIONS; i++)
				job_desc->min_nodes *= 
					(uint16_t)req_geometry[i];
			job_desc->min_nodes *= bg_conf->bp_node_cnt;
			job_desc->max_nodes = job_desc->min_nodes;
		}

		if(job_desc->num_procs != NO_VAL) {
			if(job_desc->min_nodes < job_desc->num_procs)
				job_desc->min_nodes = job_desc->num_procs;
			if(job_desc->max_nodes < job_desc->num_procs)
				job_desc->max_nodes = job_desc->num_procs;
		}
		/* See if min_nodes is greater than one base partition */
		if(job_desc->min_nodes > bg_conf->bp_node_cnt) {
			/*
			 * if it is make sure it is a factor of 
			 * bg_conf->bp_node_cnt, if it isn't make it 
			 * that way 
			 */
			tmp = job_desc->min_nodes % bg_conf->bp_node_cnt;
			if(tmp > 0)
				job_desc->min_nodes += 
					(bg_conf->bp_node_cnt-tmp);
		}				
		tmp = job_desc->min_nodes / bg_conf->bp_node_cnt;
		
		/* this means it is greater or equal to one bp */
		if(tmp > 0) {
			select_g_set_jobinfo(job_desc->select_jobinfo,
					     SELECT_DATA_NODE_CNT,
					     &job_desc->min_nodes);
			job_desc->min_nodes = tmp;
			job_desc->num_procs = bg_conf->procs_per_bp * tmp;
		} else { 
#ifdef HAVE_BGL
			if(job_desc->min_nodes <= bg_conf->nodecard_node_cnt
			   && bg_conf->nodecard_ionode_cnt)
				job_desc->min_nodes = 
					bg_conf->nodecard_node_cnt;
			else if(job_desc->min_nodes 
				<= bg_conf->quarter_node_cnt)
				job_desc->min_nodes = 
					bg_conf->quarter_node_cnt;
			else 
				job_desc->min_nodes = 
					bg_conf->bp_node_cnt;
			
			select_g_set_jobinfo(job_desc->select_jobinfo,
					     SELECT_DATA_NODE_CNT,
					     &job_desc->min_nodes);

			tmp = bg_conf->bp_node_cnt/job_desc->min_nodes;
			
			job_desc->num_procs = bg_conf->procs_per_bp/tmp;
			job_desc->min_nodes = 1;
#else
			i = bg_conf->smallest_block;
			while(i <= bg_conf->bp_node_cnt) {
				if(job_desc->min_nodes <= i) {
					job_desc->min_nodes = i;
					break;
				}
				i *= 2;
			}
			
			select_g_set_jobinfo(job_desc->select_jobinfo,
					     SELECT_DATA_NODE_CNT,
					     &job_desc->min_nodes);

			job_desc->num_procs = job_desc->min_nodes 
				* bg_conf->proc_ratio;
			job_desc->min_nodes = 1;
#endif
		}
		
		if(job_desc->max_nodes == (uint32_t) NO_VAL) 
			return SLURM_SUCCESS;
		
		if(job_desc->max_nodes > bg_conf->bp_node_cnt) {
			tmp = job_desc->max_nodes % bg_conf->bp_node_cnt;
			if(tmp > 0)
				job_desc->max_nodes += 
					(bg_conf->bp_node_cnt-tmp);
		}
		tmp = job_desc->max_nodes / bg_conf->bp_node_cnt;
		if(tmp > 0) {
			job_desc->max_nodes = tmp;
			tmp = NO_VAL;
		} else {
#ifdef HAVE_BGL
			if(job_desc->max_nodes <= bg_conf->nodecard_node_cnt
			   && bg_conf->nodecard_ionode_cnt)
				job_desc->max_nodes = 
					bg_conf->nodecard_node_cnt;
			else if(job_desc->max_nodes 
				<= bg_conf->quarter_node_cnt)
				job_desc->max_nodes = 
					bg_conf->quarter_node_cnt;
			else 
				job_desc->max_nodes = 
					bg_conf->bp_node_cnt;
		
			tmp = bg_conf->bp_node_cnt/job_desc->max_nodes;
			tmp = bg_conf->procs_per_bp/tmp;
			
			select_g_set_jobinfo(job_desc->select_jobinfo,
					     SELECT_DATA_MAX_PROCS, 
					     &tmp);
			job_desc->max_nodes = 1;
#else
			i = bg_conf->smallest_block;
			while(i <= bg_conf->bp_node_cnt) {
				if(job_desc->max_nodes <= i) {
					job_desc->max_nodes = i;
					break;
				}
				i *= 2;
			}
			
			tmp = job_desc->max_nodes * bg_conf->proc_ratio;
			select_g_set_jobinfo(job_desc->select_jobinfo,
					     SELECT_DATA_MAX_PROCS,
					     &tmp);

			job_desc->max_nodes = 1;
#endif
		}
		tmp = NO_VAL;
			
		break;
	default:
		error("unknown option %d for alter_node_cnt",type);
	}
	
	return SLURM_SUCCESS;
}

extern int select_p_reconfigure(void)
{
	if(read_bg_conf() == SLURM_ERROR) {
		fatal("Error, could not read the file");
		return SLURM_ERROR;
	}
	
	return SLURM_SUCCESS;
}

extern List select_p_get_config(void)
{
//	config_key_pair_t *key_pair;
	List my_list = list_create(destroy_config_key_pair);

/* 	if (!my_list) */
/* 		fatal("malloc failure on list_create"); */

/* 	key_pair = xmalloc(sizeof(config_key_pair_t)); */
/* 	key_pair->name = xstrdup("ArchiveDir"); */
/* 	key_pair->value = xstrdup(bg_conf->archive_dir); */
/* 	list_append(my_list, key_pair); */

/* 	key_pair = xmalloc(sizeof(config_key_pair_t)); */
/* 	key_pair->name = xstrdup("ArchiveScript"); */
/* 	key_pair->value = xstrdup(bg_conf->archive_script); */
/* 	list_append(my_list, key_pair); */

/* 	key_pair = xmalloc(sizeof(config_key_pair_t)); */
/* 	key_pair->name = xstrdup("AuthInfo"); */
/* 	key_pair->value = xstrdup(bg_conf->auth_info); */
/* 	list_append(my_list, key_pair); */

/* 	key_pair = xmalloc(sizeof(config_key_pair_t)); */
/* 	key_pair->name = xstrdup("AuthType"); */
/* 	key_pair->value = xstrdup(bg_conf->auth_type); */
/* 	list_append(my_list, key_pair); */

	return my_list;
}
