/*****************************************************************************\
 *  select_bluegene.c - node selection plugin for Blue Gene system.
 * 
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov> Danny Auble <da@llnl.gov>
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

#include "bluegene.h"

#ifndef HAVE_BG
#include "defined_block.h"
#endif

#include "src/common/uid.h"
#include "src/slurmctld/trigger_mgr.h"
#include <fcntl.h>
 
#define HUGE_BUF_SIZE (1024*16)

/* Change BLOCK_STATE_VERSION value when changing the state save
 * format i.e. pack_block() */
#define BLOCK_STATE_VERSION      "VER001"

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
const uint32_t plugin_version	= 100;

/* pthread stuff for updating BG node status */
static pthread_t bluegene_thread = 0;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;

/** initialize the status pthread */
static int _init_status_pthread(void);
static int _wait_for_thread (pthread_t thread_id);
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
	if (!getenv("CLASSPATH") || !getenv("DB2INSTANCE") 
	||  !getenv("VWSPATH"))
		fatal("db2profile has not been run to setup DB2 environment");

	if ((SELECT_MESH  != RM_MESH)
	||  (SELECT_TORUS != RM_TORUS)
	||  (SELECT_NAV   != RM_NAV))
		fatal("enum conn_type out of sync with rm_api.h");

#ifdef HAVE_BGL
	if ((SELECT_COPROCESSOR_MODE  != RM_PARTITION_COPROCESSOR_MODE)
	||  (SELECT_VIRTUAL_NODE_MODE != RM_PARTITION_VIRTUAL_NODE_MODE))
		fatal("enum node_use_type out of sync with rm_api.h");
#endif
	
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
	agent_fini = true;
	if ( bluegene_thread ) {
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
	xfree(bg_slurm_user_name);
	xfree(bg_slurm_node_prefix);

	slurm_conf_lock();
	xassert(slurmctld_conf.slurm_user_name);
	xassert(slurmctld_conf.node_prefix);
	bg_slurm_user_name = xstrdup(slurmctld_conf.slurm_user_name);
	bg_slurm_node_prefix = xstrdup(slurmctld_conf.node_prefix);
	slurm_conf_unlock();	

#ifdef HAVE_BG
	if(read_bg_conf() == SLURM_ERROR) {
		fatal("Error, could not read the file");
		return SLURM_ERROR;
	}
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
#else
	/*looking for blocks only I created */
	if (create_defined_blocks(bluegene_layout_mode, NULL) 
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
	itr = list_iterator_create(bg_list);
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
	int error_code = SLURM_SUCCESS;
	int state_fd, i, j=0;
	char *state_file = NULL;
	Buf buffer = NULL;
	char *data = NULL;
	int data_size = 0;
	node_select_info_msg_t *node_select_ptr = NULL;
	ListIterator itr;
	bg_record_t *bg_record = NULL;
	bg_info_record_t *bg_info_record = NULL;
	bitstr_t *node_bitmap = NULL, *ionode_bitmap = NULL;
	int geo[BA_SYSTEM_DIMENSIONS];
	char temp[256];
	List results = NULL;
	int data_allocated, data_read = 0;
	char *ver_str = NULL;
	uint32_t ver_str_len;
	int blocks = 0;
	uid_t my_uid;

	debug("bluegene: select_p_state_restore");
#ifdef HAVE_BG_FILES
	debug("This doesn't do anything on a real bluegene system");
	return SLURM_SUCCESS;
#endif
	if(!dir_name) {
		debug2("Starting bluegene with clean slate");
		return SLURM_SUCCESS;
	}
	state_file = xstrdup(dir_name);
	xstrcat(state_file, "/block_state");
	state_fd = open(state_file, O_RDONLY);
	if(state_fd < 0) {
		error("No block state file (%s) to recover", state_file);
		xfree(state_file);
		return SLURM_SUCCESS;
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					 BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m", 
					      state_file);
					break;
				}
			} else if (data_read == 0)	/* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
	}
	xfree(state_file);

	buffer = create_buf(data, data_size);

	/*
	 * Check the data version so that when the format changes, we 
	 * we don't try to unpack data using the wrong format routines
	 */
	if(size_buf(buffer)
	   >= sizeof(uint32_t) + strlen(BLOCK_STATE_VERSION)) {
	        char *ptr = get_buf_data(buffer);
		if (!memcmp(&ptr[sizeof(uint32_t)], BLOCK_STATE_VERSION, 3)) {
		        safe_unpackstr_xmalloc(&ver_str, &ver_str_len, buffer);
		        debug3("Version string in block_state header is %s",
			       ver_str);
		}
	}
	if (ver_str && (strcmp(ver_str, BLOCK_STATE_VERSION) != 0)) {
		error("Can not recover block state, "
		      "data version incompatable");
		xfree(ver_str);
		free_buf(buffer);
		return EFAULT;
	}
	xfree(ver_str);
	if(select_g_unpack_node_info(&node_select_ptr, buffer) == SLURM_ERROR) { 
		error("select_p_state_restore: problem unpacking node_info");
		goto unpack_error;
	}
	slurm_mutex_lock(&block_state_mutex);
	reset_ba_system(false);

	node_bitmap = bit_alloc(node_record_count);	
	ionode_bitmap = bit_alloc(bluegene_numpsets);	
	itr = list_iterator_create(bg_list);
	for (i=0; i<node_select_ptr->record_count; i++) {
		bg_info_record = &(node_select_ptr->bg_info_array[i]);
		
		bit_nclear(node_bitmap, 0, bit_size(node_bitmap) - 1);
		bit_nclear(ionode_bitmap, 0, bit_size(ionode_bitmap) - 1);
		
		j = 0;
		while(bg_info_record->bp_inx[j] >= 0) {
			if (bg_info_record->bp_inx[j+1]
			    >= node_record_count) {
				fatal("Job state recovered incompatable with "
					"bluegene.conf. bp=%u state=%d",
					node_record_count,
					bg_info_record->bp_inx[j+1]);
			}
			bit_nset(node_bitmap,
				 bg_info_record->bp_inx[j],
				 bg_info_record->bp_inx[j+1]);
			j += 2;
		}		

		j = 0;
		while(bg_info_record->ionode_inx[j] >= 0) {
			if (bg_info_record->ionode_inx[j+1]
			    >= bluegene_numpsets) {
				fatal("Job state recovered incompatable with "
					"bluegene.conf. ionodes=%u state=%d",
					bluegene_numpsets,
					bg_info_record->ionode_inx[j+1]);
			}
			bit_nset(ionode_bitmap,
				 bg_info_record->ionode_inx[j],
				 bg_info_record->ionode_inx[j+1]);
			j += 2;
		}		
					
		while((bg_record = list_next(itr))) {
			if(bit_equal(bg_record->bitmap, node_bitmap)
			   && bit_equal(bg_record->ionode_bitmap,
					ionode_bitmap))
				break;			
		}

		list_iterator_reset(itr);
		if(bg_record) {
			if(bg_info_record->state == RM_PARTITION_ERROR)
				bg_record->job_running = BLOCK_ERROR_STATE;
			bg_record->state = bg_info_record->state;
			blocks++;
		} else {
			int ionodes = 0;
			char *name = NULL;
			/* make the record that wasn't there (only for
			 * dynamic systems that are in emulation mode */
#ifdef HAVE_BG_FILES
			error("Previous block %s is gone, not adding.",
			      bg_info_record->bg_block_id);
			continue;
#endif
			if(bluegene_layout_mode != LAYOUT_DYNAMIC) {
				error("Evidently we found a block (%s) which "
				      "we had before but no longer care about. "
				      "We are not adding it since we aren't "
				      "using Dynamic mode",
				      bg_info_record->bg_block_id);
				continue;
			}
	
			bg_record = xmalloc(sizeof(bg_record_t));
			bg_record->bg_block_id =
				xstrdup(bg_info_record->bg_block_id);
			bg_record->nodes =
				xstrdup(bg_info_record->nodes);
			bg_record->ionodes =
				xstrdup(bg_info_record->ionodes);
			bg_record->ionode_bitmap = bit_copy(ionode_bitmap);
			bg_record->state = bg_info_record->state;
#ifdef HAVE_BGL
			bg_record->quarter = bg_info_record->quarter;
			bg_record->nodecard = bg_info_record->nodecard;
#endif
			if(bg_info_record->state == RM_PARTITION_ERROR)
				bg_record->job_running = BLOCK_ERROR_STATE;
			else
				bg_record->job_running = NO_JOB_RUNNING;
			bg_record->bp_count = bit_size(node_bitmap);
			bg_record->node_cnt = bg_info_record->node_cnt;
			if(bluegene_bp_node_cnt > bg_record->node_cnt) {
				ionodes = bluegene_bp_node_cnt 
					/ bg_record->node_cnt;
				bg_record->cpus_per_bp =
					procs_per_node / ionodes;
			} else {
				bg_record->cpus_per_bp = procs_per_node;
			}
#ifdef HAVE_BGL
			bg_record->node_use = bg_info_record->node_use;
#endif
			bg_record->conn_type = bg_info_record->conn_type;
			bg_record->boot_state = 0;

			process_nodes(bg_record, true);

			
			bg_record->target_name = 
				xstrdup(bg_slurm_user_name);
			bg_record->user_name = 
				xstrdup(bg_slurm_user_name);
			
			my_uid = uid_from_string(bg_record->user_name);
			if (my_uid == (uid_t) -1) {
				error("uid_from_strin(%s): %m", 
				      bg_record->user_name);
			} else {
				bg_record->user_uid = my_uid;
			} 
				
#ifdef HAVE_BGL
			bg_record->blrtsimage =
				xstrdup(bg_info_record->blrtsimage);
#endif
			bg_record->linuximage = 
				xstrdup(bg_info_record->linuximage);
			bg_record->mloaderimage =
				xstrdup(bg_info_record->mloaderimage);
			bg_record->ramdiskimage =
				xstrdup(bg_info_record->ramdiskimage);

			for(j=0; j<BA_SYSTEM_DIMENSIONS; j++) 
				geo[j] = bg_record->geo[j];
				
			results = list_create(NULL);
			name = set_bg_block(results,
					    bg_record->start, 
					    geo, 
					    bg_record->conn_type);
			if(!name) {
				error("I was unable to "
				      "make the "
				      "requested block.");
				list_destroy(results);
				destroy_bg_record(bg_record);
				continue;
			}

			
			snprintf(temp, sizeof(temp), "%s%s",
				 bg_slurm_node_prefix,
				 name);
			

			xfree(name);
			if(strcmp(temp, bg_record->nodes)) {
#ifdef HAVE_BG_FILES
				fatal("given list of %s "
				      "but allocated %s, "
				      "your order might be "
				      "wrong in bluegene.conf",
				      bg_record->nodes, temp);
#else
				fatal("bad wiring in preserved state "
				      "(found %s, but allocated %s) "
				      "YOU MUST COLDSTART",
				      bg_record->nodes, temp);
#endif
			}
			if(bg_record->bg_block_list)
				list_destroy(bg_record->bg_block_list);
			bg_record->bg_block_list =
				list_create(destroy_ba_node);
			copy_node_path(results, &bg_record->bg_block_list);
			list_destroy(results);			
			
			configure_block(bg_record);
			blocks++;
			list_push(bg_list, bg_record);		
		}
	}
	FREE_NULL_BITMAP(ionode_bitmap);
	FREE_NULL_BITMAP(node_bitmap);
	list_iterator_destroy(itr);

	sort_bg_record_inc_size(bg_list);
	slurm_mutex_unlock(&block_state_mutex);
		
	info("Recovered %d blocks", blocks);
	select_g_free_node_info(&node_select_ptr);
	free_buf(buffer);
	return error_code;

unpack_error:
	error("Incomplete block data checkpoint file");
	free_buf(buffer);
	return SLURM_FAILURE;
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

extern int select_p_get_job_cores(uint32_t job_id, int alloc_index, int s)
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
		/*
		 * get all the blocks we are freeing since they have
		 * been moved here
		 */
		if(bg_freeing_list) {
			slurm_mutex_lock(&block_state_mutex);
			itr = list_iterator_create(bg_freeing_list);
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
		trigger_block_error();
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

extern int select_p_update_sub_node (update_part_msg_t *part_desc_ptr)
{
	int rc = SLURM_SUCCESS;
	bg_record_t *bg_record = NULL, *found_record = NULL;
	time_t now;
	char reason[128], tmp[64], time_str[32];
	blockreq_t blockreq; 
	int i = 0, j = 0;
	char coord[BA_SYSTEM_DIMENSIONS];
	char ionodes[128];
	int set = 0;
	int set_error = 0;
	bitstr_t *ionode_bitmap = NULL;
	List requests = NULL;
	List delete_list = NULL;
	ListIterator itr;
	
	if(bluegene_layout_mode != LAYOUT_DYNAMIC) {
		info("You can't use this call unless you are on a Dynamically "
		     "allocated system.  Please use update BlockName instead");
		rc = SLURM_ERROR;
		goto end_it;
	}

	memset(coord, -1, BA_SYSTEM_DIMENSIONS);
	memset(ionodes, 0, 128);
	if(!part_desc_ptr->name) {
		error("update_sub_node: No name specified");
		rc = SLURM_ERROR;
		goto end_it;
				
	}

	now = time(NULL);
	slurm_make_time_str(&now, time_str, sizeof(time_str));
	snprintf(tmp, sizeof(tmp), "[SLURM@%s]", time_str);
			
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
	ionode_bitmap = bit_alloc(bluegene_numpsets);
	bit_unfmt(ionode_bitmap, ionodes);		

	requests = list_create(destroy_bg_record);
	memset(&blockreq, 0, sizeof(blockreq_t));

	blockreq.block = coord;
	blockreq.conn_type = SELECT_SMALL;
	blockreq.small32 = bluegene_bp_nodecard_cnt;

	add_bg_record(requests, NULL, &blockreq);
	
	delete_list = list_create(NULL);
	while((bg_record = list_pop(requests))) {
		set_error = 0;
		if(bit_overlap(bg_record->ionode_bitmap, ionode_bitmap))
			set_error = 1;
		
		slurm_mutex_lock(&block_state_mutex);
		itr = list_iterator_create(bg_list);
		while((found_record = list_next(itr))) {
			if(!found_record || (bg_record == found_record))
				continue;
			if(bit_equal(bg_record->bitmap, found_record->bitmap)
			   && bit_equal(bg_record->ionode_bitmap, 
					found_record->ionode_bitmap)) {
				debug2("block %s[%s] already there",
				       found_record->nodes, 
				       found_record->ionodes);
				/* we don't need to set this error, it
				   doesn't overlap
				*/
				if(!set_error)
					break;
				
				snprintf(reason, sizeof(reason),
					 "update_sub_node: "
					 "Admin set block %s state to %s %s",
					 found_record->bg_block_id, 
					 _block_state_str(
						 part_desc_ptr->state_up),
					 tmp); 
				info("%s",reason);
				if(found_record->job_running 
				   > NO_JOB_RUNNING) {
					slurm_fail_job(
						found_record->job_running);
				}
			
				if(!part_desc_ptr->state_up) {
					found_record->job_running =
						BLOCK_ERROR_STATE;
					found_record->state =
						RM_PARTITION_ERROR;
					trigger_block_error();
				} else if(part_desc_ptr->state_up){
					found_record->job_running =
						NO_JOB_RUNNING;
					found_record->state =
						RM_PARTITION_FREE;
				} else {
					error("update_sub_node: "
					      "Unknown state %d given",
					      part_desc_ptr->state_up);
					rc = SLURM_ERROR;
					break;
				}	
				break;
			} else if(!set_error
				  && bit_equal(bg_record->bitmap,
					       found_record->bitmap)
				  && bit_overlap(
					  bg_record->ionode_bitmap, 
					  found_record->ionode_bitmap)) {
				break;
			}
			
		}
		list_iterator_destroy(itr);
		slurm_mutex_unlock(&block_state_mutex);
		/* we already found an existing record */
		if(found_record) {
			destroy_bg_record(bg_record);
			continue;
		}
		/* we need to add this record since it doesn't exist */
		if(configure_block(bg_record) == SLURM_ERROR) {
			destroy_bg_record(bg_record);
			error("update_sub_node: "
			      "unable to configure block in api");
		}
		debug2("adding block %s to fill in small blocks "
		       "around bad blocks",
		       bg_record->bg_block_id);
		print_bg_record(bg_record);
		slurm_mutex_lock(&block_state_mutex);
		list_append(bg_list, bg_record);
		slurm_mutex_unlock(&block_state_mutex);
		
		/* We are just adding the block not deleting any or
		   setting this one to an error state.
		*/
		if(!set_error)
			continue;
				
		if(!part_desc_ptr->state_up) {
			bg_record->job_running = BLOCK_ERROR_STATE;
			bg_record->state = RM_PARTITION_ERROR;
			trigger_block_error();
		} else if(part_desc_ptr->state_up){
			bg_record->job_running = NO_JOB_RUNNING;
			bg_record->state = RM_PARTITION_FREE;
		} else {
			error("update_sub_node: Unknown state %d given",
			      part_desc_ptr->state_up);
			rc = SLURM_ERROR;
			continue;
		}
		snprintf(reason, sizeof(reason),
			 "update_sub_node: "
			 "Admin set block %s state to %s %s",
			 bg_record->bg_block_id, 
			 _block_state_str(part_desc_ptr->state_up),
			 tmp); 
		info("%s",reason);
				
		/* remove overlapping blocks */
		slurm_mutex_lock(&block_state_mutex);
		itr = list_iterator_create(bg_list);
		while((found_record = list_next(itr))) {
			if ((!found_record) || (bg_record == found_record))
				continue;
			if(!blocks_overlap(bg_record, found_record)) {
				debug2("block %s isn't part of %s",
				       found_record->bg_block_id, 
				       bg_record->bg_block_id);
				continue;
			}
			debug2("removing block %s because there is something "
			       "wrong with part of the base partition",
			       found_record->bg_block_id);
			if(found_record->job_running > NO_JOB_RUNNING) {
				slurm_fail_job(found_record->job_running);
			}
			list_push(delete_list, found_record);
			list_remove(itr);
			num_block_to_free++;
		}		
		list_iterator_destroy(itr);
		free_block_list(delete_list);
		slurm_mutex_unlock(&block_state_mutex);		
	}
	list_destroy(delete_list);
	FREE_NULL_BITMAP(ionode_bitmap);
		
	/* This only works for the error state, not free */
	
	last_bg_update = time(NULL);
	
end_it:	
	return rc;
}

extern int select_p_get_extra_jobinfo (struct node_record *node_ptr, 
				       struct job_record *job_ptr, 
                                       enum select_data_info info,
                                       void *data)
{
	if (info == SELECT_AVAIL_CPUS) {
		/* Needed to track CPUs allocated to jobs on whole nodes
		 * for sched/wiki2 (Moab scheduler). Small block allocations
		 * handled through use of job_ptr->num_procs in slurmctld */
		uint16_t *cpus_per_bp = (uint16_t *) data;
		*cpus_per_bp = procs_per_node;
	}
	return SLURM_SUCCESS;
}

extern int select_p_get_info_from_plugin (enum select_data_info info, 
					  void *data)
{
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
	int i, block_size=0;
	uint16_t req_geometry[BA_SYSTEM_DIMENSIONS];
	
	if(!bluegene_bp_node_cnt) {
		fatal("select_g_alter_node_cnt: This can't be called "
		      "before select_g_block_init");
	}

	switch (type) {
	case SELECT_GET_NODE_SCALING:
		if((*nodes) != INFINITE)
			(*nodes) = bluegene_bp_node_cnt;
		break;
	case SELECT_SET_BP_CNT:
		if(((*nodes) == INFINITE) || ((*nodes) == NO_VAL))
			tmp = (*nodes);
		else if((*nodes) > bluegene_bp_node_cnt) {
			tmp = (*nodes);
			tmp /= bluegene_bp_node_cnt;
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
		(*nodes) *= bluegene_bp_node_cnt;
		break;
	case SELECT_APPLY_NODE_MAX_OFFSET:
		if((*nodes) != INFINITE)
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
			 * if it is make sure it is a factor of 
			 * bluegene_bp_node_cnt, if it isn't make it 
			 * that way 
			 */
			tmp = job_desc->min_nodes % bluegene_bp_node_cnt;
			if(tmp > 0)
				job_desc->min_nodes += 
					(bluegene_bp_node_cnt-tmp);
		}				
		tmp = job_desc->min_nodes / bluegene_bp_node_cnt;
		
		/* this means it is greater or equal to one bp */
		if(tmp > 0) {
			select_g_set_jobinfo(job_desc->select_jobinfo,
					     SELECT_DATA_NODE_CNT,
					     &job_desc->min_nodes);
			job_desc->min_nodes = tmp;
			job_desc->num_procs = procs_per_node * tmp;
		} else { 
#ifdef HAVE_BGL
			if(job_desc->min_nodes <= bluegene_nodecard_node_cnt
			   && bluegene_nodecard_ionode_cnt)
				job_desc->min_nodes = 
					bluegene_nodecard_node_cnt;
			else if(job_desc->min_nodes 
				<= bluegene_quarter_node_cnt)
				job_desc->min_nodes = 
					bluegene_quarter_node_cnt;
			else 
				job_desc->min_nodes = 
					bluegene_bp_node_cnt;
			
			select_g_set_jobinfo(job_desc->select_jobinfo,
					     SELECT_DATA_NODE_CNT,
					     &job_desc->min_nodes);

			tmp = bluegene_bp_node_cnt/job_desc->min_nodes;
			
			job_desc->num_procs = procs_per_node/tmp;
			job_desc->min_nodes = 1;
#else
			block_size = bluegene_smallest_block;
			while(block_size <= bluegene_bp_node_cnt) {
				if(job_desc->min_nodes <= block_size) {
					job_desc->min_nodes = block_size;
					break;
				}
				block_size *= 2;
			}
			
			select_g_set_jobinfo(job_desc->select_jobinfo,
					     SELECT_DATA_NODE_CNT,
					     &job_desc->min_nodes);

			job_desc->num_procs = job_desc->min_nodes 
				* bluegene_proc_ratio;
			job_desc->min_nodes = 1;
#endif
		}
		
		if(job_desc->max_nodes == (uint32_t) NO_VAL) 
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
#ifdef HAVE_BGL
			if(job_desc->max_nodes <= bluegene_nodecard_node_cnt
			   && bluegene_nodecard_ionode_cnt)
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
#else
			block_size = bluegene_smallest_block;
			while(block_size <= bluegene_bp_node_cnt) {
				if(job_desc->max_nodes <= block_size) {
					job_desc->max_nodes = block_size;
					break;
				}
				block_size *= 2;
			}
			
			tmp = job_desc->max_nodes * bluegene_proc_ratio;
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
	return SLURM_SUCCESS;
}

extern int select_p_step_begin(struct step_record *step_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_step_fini(struct step_record *step_ptr)
{
	return SLURM_SUCCESS;
}
