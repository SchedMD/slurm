/*****************************************************************************\
 *  bluegene.c - blue gene node configuration processing module. 
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <auble1@llnl.gov> et. al.
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
#include <stdio.h>

#define BUFSIZE 4096
#define BITSIZE 128
#define MMCS_POLL_TIME 120	/* poll MMCS for down switches and nodes 
				 * every 120 secs */
#define BG_POLL_TIME 0	        /* poll bg blocks every 3 secs */

#define _DEBUG 0

char* bg_conf = NULL;

/* Global variables */
rm_BGL_t *bg = NULL;

List bg_list = NULL;			/* total list of bg_record entries */
List bg_curr_block_list = NULL;  	/* current bg blocks in bluegene.conf*/
List bg_found_block_list = NULL;  	/* found bg blocks already on system */
List bg_job_block_list = NULL;  	/* jobs running in these blocks */
List bg_booted_block_list = NULL;  	/* blocks that are booted */
List bg_freeing_list = NULL;  	        /* blocks that being freed */
List bg_request_list = NULL;  	        /* list of request that can't 
					   be made just yet */

List bg_blrtsimage_list = NULL;
List bg_linuximage_list = NULL;
List bg_mloaderimage_list = NULL;
List bg_ramdiskimage_list = NULL;
char *default_blrtsimage = NULL, *default_linuximage = NULL;
char *default_mloaderimage = NULL, *default_ramdiskimage = NULL;
char *bridge_api_file = NULL; 
bg_layout_t bluegene_layout_mode = NO_VAL;
uint16_t bluegene_numpsets = 0;
uint16_t bluegene_bp_node_cnt = 0;
uint16_t bluegene_quarter_node_cnt = 0;
uint16_t bluegene_quarter_ionode_cnt = 0;
uint16_t bluegene_nodecard_node_cnt = 0;
uint16_t bluegene_nodecard_ionode_cnt = 0;
uint16_t bridge_api_verb = 0;
bool agent_fini = false;
time_t last_bg_update;
pthread_mutex_t block_state_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t request_list_mutex = PTHREAD_MUTEX_INITIALIZER;
int num_block_to_free = 0;
int num_block_freed = 0;
int blocks_are_created = 0;
int num_unused_cpus = 0;

pthread_mutex_t freed_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
List bg_free_block_list = NULL;  	/* blocks to be deleted */
List bg_destroy_block_list = NULL;       /* blocks to be destroyed */
int free_cnt = 0;
int destroy_cnt = 0;

#ifndef HAVE_BG_FILES
# if BA_SYSTEM_DIMENSIONS==3
int max_dim[BA_SYSTEM_DIMENSIONS] = { 0, 0, 0 };
# else
int max_dim[BA_SYSTEM_DIMENSIONS] = { 0 };
# endif
#endif

/* some local functions */
#ifdef HAVE_BG
static int  _addto_node_list(bg_record_t *bg_record, int *start, int *end);
static int  _ba_node_cmpf_inc(ba_node_t *node_a, ba_node_t *node_b);
#endif

static void _set_bg_lists();
static int  _validate_config_nodes(void);
static int  _bg_record_cmpf_inc(bg_record_t *rec_a, bg_record_t *rec_b);
static int _delete_old_blocks(void);
static char *_get_bg_conf(void);
static int _split_block(bg_record_t *bg_record, int procs);
static int _breakup_blocks(ba_request_t *request, List my_block_list);
static bg_record_t *_create_small_record(bg_record_t *bg_record, 
					 uint16_t quarter, uint16_t nodecard);
static int  _reopen_bridge_log(void);

/* Initialize all plugin variables */
extern int init_bg(void)
{
#ifdef HAVE_BG_FILES
	int rc;
	rm_size3D_t bp_size;
	
	info("Attempting to contact MMCS");
	if ((rc = bridge_get_bg(&bg)) != STATUS_OK) {
		fatal("init_bg: rm_get_BGL(): %s", bg_err_str(rc));
		return SLURM_ERROR;
	}
	
	if ((rc = bridge_get_data(bg, RM_Msize, &bp_size)) != STATUS_OK) {
		fatal("init_bg: rm_get_data(): %s", bg_err_str(rc));
		return SLURM_ERROR;
	}
	verbose("BlueGene configured with %d x %d x %d base blocks",
		bp_size.X, bp_size.Y, bp_size.Z);
	DIM_SIZE[X]=bp_size.X;
	DIM_SIZE[Y]=bp_size.Y;
	DIM_SIZE[Z]=bp_size.Z;
#endif
	ba_init(NULL);
	info("BlueGene plugin loaded successfully");

	return SLURM_SUCCESS;
}

/* Purge all plugin variables */
extern void fini_bg(void)
{
#ifdef HAVE_BG_FILES
	int rc;
#endif
	_set_bg_lists();
	
	if (bg_list) {
		list_destroy(bg_list);
		bg_list = NULL;
	}	
	if (bg_curr_block_list) {
		list_destroy(bg_curr_block_list);
		bg_curr_block_list = NULL;
	}	
	if (bg_found_block_list) {
		list_destroy(bg_found_block_list);
		bg_found_block_list = NULL;
	}
	if (bg_job_block_list) {
		list_destroy(bg_job_block_list);
		bg_job_block_list = NULL;
		num_unused_cpus = 0;
	}
	if (bg_booted_block_list) {
		list_destroy(bg_booted_block_list);
		bg_booted_block_list = NULL;
	}

	/* wait for the free threads to finish up don't destroy the
	 * bg_free_block_list here */
	while(free_cnt > 0)
		usleep(1000);
	/* wait for the destroy threads to finish up don't destroy the
	 * bg_destroy_block_list here */
	while(destroy_cnt > 0)
		usleep(1000);
	
	slurm_mutex_lock(&request_list_mutex);
	if (bg_request_list) {
		list_destroy(bg_request_list);
		bg_request_list = NULL;
	}
	slurm_mutex_unlock(&request_list_mutex);
		
	if(bg_blrtsimage_list) {
		list_destroy(bg_blrtsimage_list);
		bg_blrtsimage_list = NULL;
	}
	
	if(bg_linuximage_list) {
		list_destroy(bg_linuximage_list);
		bg_linuximage_list = NULL;
	}
	
	if(bg_mloaderimage_list) {
		list_destroy(bg_mloaderimage_list);
		bg_mloaderimage_list = NULL;
	}

	if(bg_ramdiskimage_list) {
		list_destroy(bg_ramdiskimage_list);
		bg_ramdiskimage_list = NULL;
	}

	xfree(default_blrtsimage);
	xfree(default_linuximage);
	xfree(default_mloaderimage);
	xfree(default_ramdiskimage);
	xfree(bridge_api_file);
	xfree(bg_conf);
	
#ifdef HAVE_BG_FILES
	if(bg)
		if ((rc = bridge_free_bg(bg)) != STATUS_OK)
			error("bridge_free_BGL(): %s", bg_err_str(rc));
#endif	
	ba_fini();
}

extern void print_bg_record(bg_record_t* bg_record)
{
	if (!bg_record) {
		error("print_bg_record, record given is null");
		return;
	}
#if _DEBUG
	info(" bg_record: ");
	if (bg_record->bg_block_id)
		info("\tbg_block_id: %s", bg_record->bg_block_id);
	info("\tnodes: %s", bg_record->nodes);
	info("\tsize: %d BPs %u Nodes %d cpus", 
	     bg_record->bp_count,
	     bg_record->node_cnt,
	     bg_record->cpus_per_bp * bg_record->bp_count);
	info("\tgeo: %ux%ux%u", bg_record->geo[X], bg_record->geo[Y], 
	     bg_record->geo[Z]);
	info("\tconn_type: %s", convert_conn_type(bg_record->conn_type));
	info("\tnode_use: %s", convert_node_use(bg_record->node_use));
	if (bg_record->bitmap) {
		char bitstring[BITSIZE];
		bit_fmt(bitstring, BITSIZE, bg_record->bitmap);
		info("\tbitmap: %s", bitstring);
	}
#else
{
	char tmp_char[256];
	format_node_name(bg_record, tmp_char, sizeof(tmp_char));
	info("Record: BlockID:%s Nodes:%s Conn:%s",
	     bg_record->bg_block_id, tmp_char,
	     convert_conn_type(bg_record->conn_type));
}
#endif
}

extern void destroy_bg_record(void *object)
{
	bg_record_t* bg_record = (bg_record_t*) object;

	if (bg_record) {
		xfree(bg_record->bg_block_id);
		xfree(bg_record->nodes);
		xfree(bg_record->ionodes);
		xfree(bg_record->user_name);
		xfree(bg_record->target_name);
		if(bg_record->bg_block_list)
			list_destroy(bg_record->bg_block_list);
		FREE_NULL_BITMAP(bg_record->bitmap);
		FREE_NULL_BITMAP(bg_record->ionode_bitmap);

		xfree(bg_record->blrtsimage);
		xfree(bg_record->linuximage);
		xfree(bg_record->mloaderimage);
		xfree(bg_record->ramdiskimage);

		xfree(bg_record);
	}
}

extern int block_exist_in_list(List my_list, bg_record_t *bg_record)
{
	ListIterator itr = list_iterator_create(my_list);
	bg_record_t *found_record = NULL;
	int rc = 0;

	while ((found_record = (bg_record_t *) list_next(itr)) != NULL) {
		/* check for full node bitmap compare */
		if(bit_equal(bg_record->bitmap, found_record->bitmap)
		   && bit_equal(bg_record->ionode_bitmap,
				found_record->ionode_bitmap)) {
			if(bg_record->ionodes)
				debug3("This block %s[%s] "
				       "is already in the list %s",
				       bg_record->nodes,
				       bg_record->ionodes,
				       found_record->bg_block_id);
			else
				debug3("This block %s "
				       "is already in the list %s",
				       bg_record->nodes,
				       found_record->bg_block_id);
				
			rc = 1;
			break;
		}
	}
	list_iterator_destroy(itr);
	return rc;
}

extern void process_nodes(bg_record_t *bg_record)
{
#ifdef HAVE_BG
	int j=0, number;
	int start[BA_SYSTEM_DIMENSIONS];
	int end[BA_SYSTEM_DIMENSIONS];
	ListIterator itr;
	ba_node_t* ba_node = NULL;
	
	if(!bg_record->bg_block_list 
	   || !list_count(bg_record->bg_block_list)) {
		if(!bg_record->bg_block_list) {
			bg_record->bg_block_list =
				list_create(destroy_ba_node);
		}
		bg_record->bp_count = 0;
		
		while (bg_record->nodes[j] != '\0') {
			if ((bg_record->nodes[j] == '['
			     || bg_record->nodes[j] == ',')
			    && (bg_record->nodes[j+8] == ']' 
				|| bg_record->nodes[j+8] == ',')
			    && (bg_record->nodes[j+4] == 'x'
				|| bg_record->nodes[j+4] == '-')) {
				j++;
				number = atoi(bg_record->nodes + j);
				start[X] = number / 100;
				start[Y] = (number % 100) / 10;
				start[Z] = (number % 10);
				j += 4;
				number = atoi(bg_record->nodes + j);
				end[X] = number / 100;
				end[Y] = (number % 100) / 10;
				end[Z] = (number % 10);
				j += 3;
				if(!bg_record->bp_count) {
					bg_record->start[X] = start[X];
					bg_record->start[Y] = start[Y];
					bg_record->start[Z] = start[Z];
					debug2("start is %d%d%d",
					       bg_record->start[X],
					       bg_record->start[Y],
					       bg_record->start[Z]);
				}
				bg_record->bp_count += _addto_node_list(
					bg_record, 
					start, 
					end);
				if(bg_record->nodes[j] != ',')
					break;
				j--;
			} else if((bg_record->nodes[j] < 58 
				   && bg_record->nodes[j] > 47)) {
				
				number = atoi(bg_record->nodes + j);
				start[X] = number / 100;
				start[Y] = (number % 100) / 10;
				start[Z] = (number % 10);
				j+=3;
				if(!bg_record->bp_count) {
					bg_record->start[X] = start[X];
					bg_record->start[Y] = start[Y];
					bg_record->start[Z] = start[Z];
					debug2("start is %d%d%d",
					       bg_record->start[X],
					       bg_record->start[Y],
					       bg_record->start[Z]);
				}
				bg_record->bp_count += _addto_node_list(
					bg_record, 
					start, 
					start);
				if(bg_record->nodes[j] != ',')
					break;
			}
			j++;
		}
	}
	
	bg_record->geo[X] = 0;
	bg_record->geo[Y] = 0;
	bg_record->geo[Z] = 0;
	end[X] = -1;
	end[Y] = -1;
	end[Z] = -1;

	list_sort(bg_record->bg_block_list, (ListCmpF) _ba_node_cmpf_inc);

	itr = list_iterator_create(bg_record->bg_block_list);
	while ((ba_node = list_next(itr)) != NULL) {
		debug4("%d%d%d is included in this block",
		       ba_node->coord[X],
		       ba_node->coord[Y],
		       ba_node->coord[Z]);
		       
		if(ba_node->coord[X]>end[X]) {
			bg_record->geo[X]++;
			end[X] = ba_node->coord[X];
		}
		if(ba_node->coord[Y]>end[Y]) {
			bg_record->geo[Y]++;
			end[Y] = ba_node->coord[Y];
		}
		if(ba_node->coord[Z]>end[Z]) {
			bg_record->geo[Z]++;
			end[Z] = ba_node->coord[Z];
		}
	}
	list_iterator_destroy(itr);
	debug3("geo = %d%d%d bp count is %d\n",
	       bg_record->geo[X],
	       bg_record->geo[Y],
	       bg_record->geo[Z],
	       bg_record->bp_count);

	if ((bg_record->geo[X] == DIM_SIZE[X])
	    && (bg_record->geo[Y] == DIM_SIZE[Y])
	    && (bg_record->geo[Z] == DIM_SIZE[Z])) {
		bg_record->full_block = 1;	
	}	
	
#ifndef HAVE_BG_FILES
	max_dim[X] = MAX(max_dim[X], end[X]);
	max_dim[Y] = MAX(max_dim[Y], end[Y]);
	max_dim[Z] = MAX(max_dim[Z], end[Z]);
#endif
   
	if (node_name2bitmap(bg_record->nodes, 
			     false, 
			     &bg_record->bitmap)) {
		fatal("1 Unable to convert nodes %s to bitmap", 
		      bg_record->nodes);
	}
#endif
	return;
}

extern void copy_bg_record(bg_record_t *fir_record, bg_record_t *sec_record)
{
	int i;
	ListIterator itr = NULL;
	ba_node_t *ba_node = NULL, *new_ba_node = NULL;

	xfree(sec_record->bg_block_id);
	sec_record->bg_block_id = xstrdup(fir_record->bg_block_id);
	xfree(sec_record->nodes);
	sec_record->nodes = xstrdup(fir_record->nodes);
	xfree(sec_record->ionodes);
	sec_record->ionodes = xstrdup(fir_record->ionodes);
	xfree(sec_record->user_name);
	sec_record->user_name = xstrdup(fir_record->user_name);
	xfree(sec_record->target_name);
	sec_record->target_name = xstrdup(fir_record->target_name);

	xfree(sec_record->blrtsimage);
	sec_record->blrtsimage = xstrdup(fir_record->blrtsimage);
	xfree(sec_record->linuximage);
	sec_record->linuximage = xstrdup(fir_record->linuximage);
	xfree(sec_record->mloaderimage);
	sec_record->mloaderimage = xstrdup(fir_record->mloaderimage);
	xfree(sec_record->ramdiskimage);
	sec_record->ramdiskimage = xstrdup(fir_record->ramdiskimage);

	sec_record->user_uid = fir_record->user_uid;
	sec_record->state = fir_record->state;
	sec_record->conn_type = fir_record->conn_type;
	sec_record->node_use = fir_record->node_use;
	sec_record->bp_count = fir_record->bp_count;
	sec_record->switch_count = fir_record->switch_count;
	sec_record->boot_state = fir_record->boot_state;
	sec_record->boot_count = fir_record->boot_count;
	sec_record->full_block = fir_record->full_block;

	for(i=0;i<BA_SYSTEM_DIMENSIONS;i++) {
		sec_record->geo[i] = fir_record->geo[i];
		sec_record->start[i] = fir_record->start[i];
	}

	FREE_NULL_BITMAP(sec_record->bitmap);
	if(fir_record->bitmap 
	   && (sec_record->bitmap = bit_copy(fir_record->bitmap)) == NULL) {
		error("Unable to copy bitmap for %s", fir_record->nodes);
		sec_record->bitmap = NULL;
	}
	FREE_NULL_BITMAP(sec_record->ionode_bitmap);
	if(fir_record->ionode_bitmap 
	   && (sec_record->ionode_bitmap
	       = bit_copy(fir_record->ionode_bitmap)) == NULL) {
		error("Unable to copy ionode_bitmap for %s",
		      fir_record->nodes);
		sec_record->ionode_bitmap = NULL;
	}
	if(sec_record->bg_block_list)
		list_destroy(sec_record->bg_block_list);
	sec_record->bg_block_list = list_create(destroy_ba_node);
	if(fir_record->bg_block_list) {
		itr = list_iterator_create(fir_record->bg_block_list);
		while((ba_node = list_next(itr))) {
			new_ba_node = ba_copy_node(ba_node);
			list_push(sec_record->bg_block_list, new_ba_node);
		}
		list_iterator_destroy(itr);
	}
	sec_record->job_running = fir_record->job_running;
	sec_record->cpus_per_bp = fir_record->cpus_per_bp;
	sec_record->node_cnt = fir_record->node_cnt;
	sec_record->quarter = fir_record->quarter;
	sec_record->nodecard = fir_record->nodecard;
}

extern bg_record_t *find_bg_record_in_list(List my_list, char *bg_block_id)
{
	ListIterator itr;
	bg_record_t *bg_record = NULL;
		
	if(!bg_block_id)
		return NULL;
			
	if(my_list) {
		slurm_mutex_lock(&block_state_mutex);
		itr = list_iterator_create(my_list);
		while ((bg_record = (bg_record_t *) list_next(itr)) != NULL) {
			if(bg_record->bg_block_id)
				if (!strcmp(bg_record->bg_block_id, 
					    bg_block_id))
					break;
		}
		list_iterator_destroy(itr);
		slurm_mutex_unlock(&block_state_mutex);
		if(bg_record)
			return bg_record;
		else
			return NULL;
	} else {
		error("find_bg_record_in_list: no list");
		return NULL;
	}
	
}
/* All changes to the bg_list target_name must 
   be done before this function is called. 
   also slurm_conf_lock() must be called before calling this
   function along with slurm_conf_unlock() afterwards.		
*/
extern int update_block_user(bg_record_t *bg_record, int set) 
{
	struct passwd *pw_ent = NULL;

	if(!bg_record->target_name) {
		error("Must set target_name to run update_block_user.");
		return -1;
	}
	if(!bg_record->user_name) {
		error("No user_name");
		bg_record->user_name = xstrdup(slurmctld_conf.slurm_user_name);
	}
#ifdef HAVE_BG_FILES
	int rc=0;	
	if(set) {
		if((rc = remove_all_users(bg_record->bg_block_id, 
					  bg_record->target_name))
		   == REMOVE_USER_ERR) {
			error("1 Something happened removing "
			      "users from block %s", 
			      bg_record->bg_block_id);
			return -1;
		} else if (rc == REMOVE_USER_NONE) {
			if (strcmp(bg_record->target_name, 
				   slurmctld_conf.slurm_user_name)) {
				info("Adding user %s to Block %s",
				     bg_record->target_name, 
				     bg_record->bg_block_id);
				
				if ((rc = bridge_add_block_user(
					     bg_record->bg_block_id, 
					     bg_record->target_name)) 
				    != STATUS_OK) {
					error("bridge_add_block_user"
					      "(%s,%s): %s", 
					      bg_record->bg_block_id, 
					      bg_record->target_name,
					      bg_err_str(rc));
					return -1;
				} 
			}
		}
	}
#endif
	
	if(strcmp(bg_record->target_name, bg_record->user_name)) {
		xfree(bg_record->user_name);
		bg_record->user_name = xstrdup(bg_record->target_name);
		if((pw_ent = getpwnam(bg_record->user_name)) == NULL) {
			error("getpwnam(%s): %m", bg_record->user_name);
			return -1;
		} else {
			bg_record->user_uid = pw_ent->pw_uid; 
		}		
		return 1;
	}
	
	return 0;
}

/* If any nodes in node_list are drained, draining, or down, 
 *   then just return
 *   else drain all of the nodes
 * This function lets us drain an entire bgblock only if 
 * we have not already identified a specific node as bad. */
extern void drain_as_needed(bg_record_t *bg_record, char *reason)
{
	bool needed = true;
	hostlist_t hl;
	char *host = NULL;
	char bg_down_node[128];

	if(bg_record->job_running > NO_JOB_RUNNING)
		slurm_fail_job(bg_record->job_running);			

	/* small blocks */
	if(bg_record->cpus_per_bp != procs_per_node) {
		debug2("small block");
		goto end_it;
	}
	
	/* at least one base partition */
	hl = hostlist_create(bg_record->nodes);
	if (!hl) {
		slurm_drain_nodes(bg_record->nodes, reason);
		return;
	}
	while ((host = hostlist_shift(hl))) {
		if (node_already_down(bg_down_node)) {
			needed = false;
			free(host);
			break;
		}
		free(host);
	}
	hostlist_destroy(hl);
	
	if (needed) {
		slurm_drain_nodes(bg_record->nodes, reason);
	}
end_it:
	while(bg_record->job_running > NO_JOB_RUNNING) {
		debug2("block %s is still running job %d",
		       bg_record->bg_block_id, bg_record->job_running);
		sleep(1);
	}
	slurm_mutex_lock(&block_state_mutex);
	bg_record->job_running = BLOCK_ERROR_STATE;
	bg_record->state = RM_PARTITION_ERROR;
	slurm_mutex_unlock(&block_state_mutex);
	return;
}

extern int format_node_name(bg_record_t *bg_record, char *buf, int buf_size)
{
	if(bg_record->ionodes) {
		snprintf(buf, buf_size, "%s[%s]",
			bg_record->nodes,
			bg_record->ionodes);
	} else {
		snprintf(buf, buf_size, "%s", bg_record->nodes);
	}
	return SLURM_SUCCESS;
}

extern bool blocks_overlap(bg_record_t *rec_a, bg_record_t *rec_b)
{
/* 	bitstr_t *my_bitmap = NULL; */
	
	if(rec_a->bp_count > 1 && rec_a->bp_count > 1) {
		reset_ba_system();
		check_and_set_node_list(rec_a->bg_block_list);
		if(check_and_set_node_list(rec_b->bg_block_list)
		   == SLURM_ERROR) 
			return true;
	}
	
	if(!bit_overlap(rec_a->bitmap, rec_b->bitmap)) {
		return false;
	}

	if(!bit_overlap(rec_a->ionode_bitmap, rec_b->ionode_bitmap)) {
		return false;
	}
		
	/* my_bitmap = bit_copy(rec_a->bitmap); */
/* 	bit_and(my_bitmap, rec_b->bitmap); */
/* 	if (bit_ffs(my_bitmap) == -1) { */
/* 		FREE_NULL_BITMAP(my_bitmap); */
/* 		return false; */
/* 	} */
/* 	FREE_NULL_BITMAP(my_bitmap); */
		
/* 	if(rec_a->quarter != (uint16_t) NO_VAL) { */
/* 		if(rec_b->quarter == (uint16_t) NO_VAL) */
/* 			return true; */
/* 		else if(rec_a->quarter != rec_b->quarter) */
/* 			return false; */
/* 		if(rec_a->nodecard != (uint16_t) NO_VAL) { */
/* 			if(rec_b->nodecard == (uint16_t) NO_VAL) */
/* 				return true; */
/* 			else if(rec_a->nodecard  */
/* 				!= rec_b->nodecard) */
/* 				return false; */
/* 		}				 */
/* 	} */
	
	return true;
}

extern int remove_all_users(char *bg_block_id, char *user_name) 
{
	int returnc = REMOVE_USER_NONE;
#ifdef HAVE_BG_FILES
	char *user;
	rm_partition_t *block_ptr = NULL;
	int rc, i, user_count;

	if ((rc = bridge_get_block(bg_block_id,  &block_ptr)) != STATUS_OK) {
		if(rc == INCONSISTENT_DATA
		   && bluegene_layout_mode == LAYOUT_DYNAMIC)
			return REMOVE_USER_FOUND;
			
		error("bridge_get_block(%s): %s", 
		      bg_block_id, 
		      bg_err_str(rc));
		return REMOVE_USER_ERR;
	}	
	
	if((rc = bridge_get_data(block_ptr, RM_PartitionUsersNum, 
				 &user_count)) 
	   != STATUS_OK) {
		error("bridge_get_data(RM_PartitionUsersNum): %s", 
		      bg_err_str(rc));
		returnc = REMOVE_USER_ERR;
		user_count = 0;
	} else
		debug2("got %d users for %s",user_count, bg_block_id);
	for(i=0; i<user_count; i++) {
		if(i) {
			if ((rc = bridge_get_data(block_ptr, 
						  RM_PartitionNextUser, 
						  &user)) 
			    != STATUS_OK) {
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
			    != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_PartitionFirstUser): %s",
				      bg_err_str(rc));
				returnc = REMOVE_USER_ERR;
				break;
			}
		}
		if(!user) {
			error("No user was returned from database");
			continue;
		}
		if(!strcmp(user, slurmctld_conf.slurm_user_name)) {
			free(user);
			continue;
		}

		if(user_name) {
			if(!strcmp(user, user_name)) {
				returnc = REMOVE_USER_FOUND;
				free(user);
				continue;
			}
		}
		
		info("Removing user %s from Block %s", user, bg_block_id);
		if ((rc = bridge_remove_block_user(bg_block_id, user)) 
		    != STATUS_OK) {
			debug("user %s isn't on block %s",
			      user, 
			      bg_block_id);
		}
		free(user);
	}
	if ((rc = bridge_free_block(block_ptr)) != STATUS_OK) {
		error("bridge_free_block(): %s", bg_err_str(rc));
	}
#endif
	return returnc;
}

/* if SLURM_ERROR you will need to fail the job with
   slurm_fail_job(bg_record->job_running);
*/

extern int set_block_user(bg_record_t *bg_record) 
{
	int rc = 0;
	debug("resetting the boot state flag and "
	      "counter for block %s.",
	      bg_record->bg_block_id);
	bg_record->boot_state = 0;
	bg_record->boot_count = 0;
	slurm_conf_lock();
	if((rc = update_block_user(bg_record, 1)) == 1) {
		last_bg_update = time(NULL);
		rc = SLURM_SUCCESS;
	} else if (rc == -1) {
		error("Unable to add user name to block %s. "
		      "Cancelling job.",
		      bg_record->bg_block_id);
		rc = SLURM_ERROR;
	}	
	xfree(bg_record->target_name);
	bg_record->target_name = 
		xstrdup(slurmctld_conf.slurm_user_name);
	slurm_conf_unlock();	
	return rc;
}

extern char* convert_conn_type(rm_connection_type_t conn_type)
{
	switch (conn_type) {
	case (SELECT_MESH): 
		return "MESH"; 
	case (SELECT_TORUS): 
		return "TORUS"; 
	case (SELECT_SMALL): 
		return "SMALL"; 
	case (SELECT_NAV):
		return "NAV";
	default:
		break;
	}
	return "";
}

extern char* convert_node_use(rm_partition_mode_t pt)
{
	switch (pt) {
	case (SELECT_COPROCESSOR_MODE): 
		return "COPROCESSOR"; 
	case (SELECT_VIRTUAL_NODE_MODE): 
		return "VIRTUAL"; 
	default:
		break;
	}
	return "";
}

/** 
 * sort the partitions by increasing size
 */
extern void sort_bg_record_inc_size(List records){
	if (records == NULL)
		return;
	slurm_mutex_lock(&block_state_mutex);
	list_sort(records, (ListCmpF) _bg_record_cmpf_inc);
	last_bg_update = time(NULL);
	slurm_mutex_unlock(&block_state_mutex);
}

/*
 * bluegene_agent - detached thread periodically updates status of
 * bluegene nodes. 
 * 
 * NOTE: I don't grab any locks here because slurm_drain_nodes grabs
 * the necessary locks.
 */
extern void *bluegene_agent(void *args)
{
	static time_t last_mmcs_test;
	static time_t last_bg_test;
	int rc;

	last_mmcs_test = time(NULL) + MMCS_POLL_TIME;
	last_bg_test = time(NULL) + BG_POLL_TIME;
	while (!agent_fini) {
		time_t now = time(NULL);

		if (difftime(now, last_bg_test) >= BG_POLL_TIME) {
			if (agent_fini)		/* don't bother */
				return NULL;	/* quit now */
			if(blocks_are_created) {
				last_bg_test = now;
				if((rc = update_block_list()) == 1) {
					slurm_mutex_lock(&block_state_mutex);
					last_bg_update = now;
					slurm_mutex_unlock(&block_state_mutex);
				} else if(rc == -1)
					error("Error with update_block_list");
				if(bluegene_layout_mode == LAYOUT_DYNAMIC) {
					if((rc = update_freeing_block_list())
					   == -1)
						error("Error with "
						      "update_block_list 2");
				}
			}
		}

		if (difftime(now, last_mmcs_test) >= MMCS_POLL_TIME) {
			if (agent_fini)		/* don't bother */
				return NULL;	/* quit now */
			last_mmcs_test = now;
			test_mmcs_failures();	/* can run for a while */
		}	
				
		sleep(1);
	}
	return NULL;
}

/*
 * create_defined_blocks - create the static blocks that will be used
 * for scheduling, all partitions must be able to be created and booted
 * at once.  
 * IN - int overlapped, 1 if partitions are to be overlapped, 0 if they are
 * static.
 * RET - success of fitting all configurations
 */
extern int create_defined_blocks(bg_layout_t overlapped)
{
	int rc = SLURM_SUCCESS;

	ListIterator itr;
	bg_record_t *bg_record = NULL;
	ListIterator itr_found;
	int i;
	bg_record_t *found_record = NULL;
	int geo[BA_SYSTEM_DIMENSIONS];
	char temp[256];
	List results = NULL;
	
#ifdef HAVE_BG_FILES
	init_wires();
#endif
	slurm_mutex_lock(&block_state_mutex);
	reset_ba_system();
	if(bg_list) {
		itr = list_iterator_create(bg_list);
		while((bg_record = list_next(itr))) {
			if(bg_found_block_list) {
				itr_found = list_iterator_create(
					bg_found_block_list);
				while ((found_record = (bg_record_t*) 
					list_next(itr_found)) != NULL) {
/* 					info("%s.%d.%d ?= %s.%d.%d\n", */
/* 					     bg_record->nodes, */
/* 					     bg_record->quarter, */
/* 					     bg_record->nodecard, */
/* 					     found_record->nodes, */
/* 					     found_record->quarter, */
/* 					     found_record->nodecard); */
					
					if ((bit_equal(bg_record->bitmap, 
						       found_record->bitmap))
					    && (bg_record->quarter ==
						found_record->quarter)
					    && (bg_record->nodecard ==
						found_record->nodecard)) {
						/* don't reboot this one */
						break;	
					}
				}
				list_iterator_destroy(itr_found);
			} else {
				error("create_defined_blocks: "
				      "no bg_found_block_list 1");
			}
			if(bg_record->bp_count>0 
			   && !bg_record->full_block
			   && bg_record->cpus_per_bp == procs_per_node) {
				char *name = NULL;
				if(overlapped == LAYOUT_OVERLAP)
					reset_ba_system();
				for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
					geo[i] = bg_record->geo[i];
				debug2("adding %s %d%d%d %d%d%d",
				       bg_record->nodes,
				       bg_record->start[X],
				       bg_record->start[Y],
				       bg_record->start[Z],
				       geo[X],
				       geo[Y],
				       geo[Z]);
				if(bg_record->bg_block_list
				   && list_count(bg_record->bg_block_list)) {
					if(check_and_set_node_list(
						   bg_record->bg_block_list)
					   == SLURM_ERROR) {
						debug2("something happened in "
						       "the load of %s"
						       "Did you use smap to "
						       "make the "
						       "bluegene.conf file?",
						       bg_record->bg_block_id);
						list_iterator_destroy(itr);
						slurm_mutex_unlock(
							&block_state_mutex);
						return SLURM_ERROR;
					}
				} else {
					results = list_create(NULL);
					name = set_bg_block(
						results,
						bg_record->start, 
						geo, 
						bg_record->conn_type);
					if(!name) {
						error("I was unable to "
						      "make the "
						      "requested block.");
						list_destroy(results);
						list_iterator_destroy(itr);
						slurm_mutex_unlock(
							&block_state_mutex);
						return SLURM_ERROR;
					}
					slurm_conf_lock();
					snprintf(temp, sizeof(temp), "%s%s",
						 slurmctld_conf.node_prefix,
						 name);
					slurm_conf_unlock();
					xfree(name);
					if(strcmp(temp, bg_record->nodes)) {
						fatal("given list of %s "
						      "but allocated %s, "
						      "your order might be "
						      "wrong in the "
						      "bluegene.conf",
						      bg_record->nodes,
						      temp);
					}
					if(bg_record->bg_block_list)
						list_destroy(bg_record->
							     bg_block_list);
					bg_record->bg_block_list =
						list_create(destroy_ba_node);
					copy_node_path(
						results, 
						bg_record->bg_block_list);
					list_destroy(results);
				}
			}
			if(found_record == NULL) {
				if(bg_record->full_block) {
					/* if this is defined we need
					   to remove it since we are
					   going to try to create it
					   later on overlap systems
					   this doesn't matter, but
					   since we don't clear the
					   table on static mode we
					   can't do it here or it just
					   won't work since other
					   wires will be or are
					   already set
					*/ 
					list_remove(itr);
					continue;
				}
				if((rc = configure_block(bg_record)) 
				   == SLURM_ERROR) {
					list_iterator_destroy(itr);
					slurm_mutex_unlock(&block_state_mutex);
					return rc;
				}
				print_bg_record(bg_record);
			}
		}
		list_iterator_destroy(itr);
	} else {
		error("create_defined_blocks: no bg_list 2");
		slurm_mutex_unlock(&block_state_mutex);
		return SLURM_ERROR;
	}
	slurm_mutex_unlock(&block_state_mutex);
	create_full_system_block();
	sort_bg_record_inc_size(bg_list);
	
#ifdef _PRINT_BLOCKS_AND_EXIT
	if(bg_list) {
		itr = list_iterator_create(bg_list);
		debug("\n\n");
		while ((found_record = (bg_record_t *) list_next(itr)) 
		       != NULL) {
			print_bg_record(found_record);
		}
		list_iterator_destroy(itr);
	} else {
		error("create_defined_blocks: no bg_list 5");
	}
 	exit(0);
#endif	/* _PRINT_BLOCKS_AND_EXIT */
	rc = SLURM_SUCCESS;
	//exit(0);
	return rc;
}



/*
 * create_dynamic_block - create a new block to be used for a new
 * job allocation.  This will be added to the booted and job bg_lists.
 * RET - success of fitting configuration in the running system.
 */
extern int create_dynamic_block(ba_request_t *request, List my_block_list)
{
	int rc = SLURM_SUCCESS;
	
	ListIterator itr;
	bg_record_t *bg_record = NULL;
	List results = NULL;
	List requests = NULL;
	uint16_t num_quarter=0, num_nodecard=0;
	bitstr_t *my_bitmap = NULL;
	int geo[BA_SYSTEM_DIMENSIONS];
	int i;
	blockreq_t blockreq;

	slurm_mutex_lock(&block_state_mutex);
	reset_ba_system();
		
	if(my_block_list) {
		itr = list_iterator_create(my_block_list);
		while ((bg_record = (bg_record_t *) list_next(itr)) != NULL) {
			if(!my_bitmap) {
				my_bitmap = 
					bit_alloc(bit_size(bg_record->bitmap));
			}
				
			if(!bit_super_set(bg_record->bitmap, my_bitmap)) {
				bit_or(my_bitmap, bg_record->bitmap);
				for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
					geo[i] = bg_record->geo[i];
				debug2("adding %s %d%d%d %d%d%d",
				       bg_record->nodes,
				       bg_record->start[X],
				       bg_record->start[Y],
				       bg_record->start[Z],
				       geo[X],
				       geo[Y],
				       geo[Z]);

				if(check_and_set_node_list(
					   bg_record->bg_block_list)
				   == SLURM_ERROR) {
					debug2("something happened in "
					      "the load of %s",
					      bg_record->bg_block_id);
					list_iterator_destroy(itr);
					slurm_mutex_unlock(&block_state_mutex);
					FREE_NULL_BITMAP(my_bitmap);
					return SLURM_ERROR;
				}
				//set_node_list(bg_record->bg_block_list);
/* #endif	 */
			}
		}
		list_iterator_destroy(itr);
		FREE_NULL_BITMAP(my_bitmap);
	} else {
		debug("No list was given");
	}

	if(request->size==1 && request->procs < bluegene_bp_node_cnt) {
		request->conn_type = SELECT_SMALL;
		if(request->procs == (procs_per_node/16)) {
			if(!bluegene_nodecard_ionode_cnt) {
				error("can't create this size %d "
				      "on this system numpsets is %d",
				      request->procs,
				      bluegene_numpsets);
				goto finished;
			}

			num_nodecard=4;
			num_quarter=3;
		} else {
			if(!bluegene_quarter_ionode_cnt) {
				error("can't create this size %d "
				      "on this system numpsets is %d",
				      request->procs,
				      bluegene_numpsets);
				goto finished;
			}
			num_quarter=4;
		}
		
		if(_breakup_blocks(request, my_block_list) != SLURM_SUCCESS) {
			debug2("small block not able to be placed");
			//rc = SLURM_ERROR;
		} else 
			goto finished;
	}
	
	if(request->conn_type == SELECT_NAV)
		request->conn_type = SELECT_TORUS;
	
	if(!new_ba_request(request)) {
		error("Problems with request for size %d geo %dx%dx%d", 
		      request->size,
		      request->geometry[X], 
		      request->geometry[Y], 
		      request->geometry[Z]);
		rc = SLURM_ERROR;
		goto finished;
	} 
	
	if(!list_count(bg_list) || !my_block_list) {
		bg_record = NULL;
		goto no_list;
	}

	/*Try to put block starting in the smallest of the exisiting blocks*/
	if(!request->start_req) {
		itr = list_iterator_create(bg_list);
		while ((bg_record = (bg_record_t *) list_next(itr)) != NULL) {
			request->rotate_count = 0;
			request->elongate_count = 1;
		
			if(bg_record->job_running == NO_JOB_RUNNING 
			   && (bg_record->quarter == (uint16_t) NO_VAL
			       || (bg_record->quarter == 0 
				   && (bg_record->nodecard == (uint16_t) NO_VAL
				       || bg_record->nodecard == 0)))) {
				
				for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
					request->start[i] = 
						bg_record->start[i];
				debug2("allocating %s %d%d%d %d",
				       bg_record->nodes,
				       request->start[X],
				       request->start[Y],
				       request->start[Z],
				       request->size);
				request->start_req = 1;
				rc = SLURM_SUCCESS;
				if(results)
					list_delete_all(
						results,
						&empty_null_destroy_list, "");
				else
					results = list_create(NULL);
				if (!allocate_block(request, results)){
					debug2("allocate failure for size %d "
					       "base partitions", 
					       request->size);
					rc = SLURM_ERROR;
				} else 
					break;
			}
		}
		list_iterator_destroy(itr);
		
		request->start_req = 0;
		for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
			request->start[i] = (uint16_t) NO_VAL;
	}
no_list:
	if(!bg_record) {		
		rc = SLURM_SUCCESS;
		if(results)
			list_delete_all(results, 
					&empty_null_destroy_list, "");
		else
			results = list_create(NULL);
		if (!allocate_block(request, results)) {
			debug("allocate failure for size %d base partitions", 
			      request->size);
			rc = SLURM_ERROR;
		}
	}

	if(rc == SLURM_ERROR || !my_block_list) {
		goto finished;
	}
	/*set up bg_record(s) here */
	requests = list_create(destroy_bg_record);
	
	blockreq.block = request->save_name;
	blockreq.blrtsimage = request->blrtsimage;
	blockreq.linuximage = request->linuximage;
	blockreq.mloaderimage = request->mloaderimage;
	blockreq.ramdiskimage = request->ramdiskimage;
	blockreq.conn_type = request->conn_type;
	blockreq.nodecards = num_nodecard;
	blockreq.quarters = num_quarter;

	add_bg_record(requests, results, &blockreq);

	while((bg_record = (bg_record_t *) list_pop(requests)) != NULL) {
		if(block_exist_in_list(bg_list, bg_record))
			destroy_bg_record(bg_record);
		else {
			if(configure_block(bg_record) == SLURM_ERROR) {
				destroy_bg_record(bg_record);
				error("create_dynamic_block: "
				      "unable to configure block in api");
				goto finished;
			}
			
			list_append(bg_list, bg_record);
			print_bg_record(bg_record);
		}
	}

finished:
	if(my_block_list)
		xfree(request->save_name);
	if(request->elongate_geos)
		list_destroy(request->elongate_geos);
	if(results)
		list_destroy(results);
	if(requests)
		list_destroy(requests);
	
	slurm_mutex_unlock(&block_state_mutex);
	sort_bg_record_inc_size(bg_list);
	
	return rc;
}

extern int create_full_system_block()
{
	int rc = SLURM_SUCCESS;
	ListIterator itr;
	bg_record_t *bg_record = NULL;
	char *name = NULL;
	List records = NULL;
	int geo[BA_SYSTEM_DIMENSIONS];
	int i;
	blockreq_t blockreq;
	List results = NULL;
	
	/* Here we are adding a block that in for the entire machine 
	   just in case it isn't in the bluegene.conf file.
	*/
	slurm_mutex_lock(&block_state_mutex);
	
#ifdef HAVE_BG_FILES
	geo[X] = DIM_SIZE[X] - 1;
	geo[Y] = DIM_SIZE[Y] - 1;
	geo[Z] = DIM_SIZE[Z] - 1;
#else
	geo[X] = max_dim[X];
	geo[Y] = max_dim[Y];
	geo[Z] = max_dim[Z];
#endif
	slurm_conf_lock();
	i = (10+strlen(slurmctld_conf.node_prefix));
	name = xmalloc(i);
	if((geo[X] == 0) && (geo[Y] == 0) && (geo[Z] == 0))
		snprintf(name, i, "%s000",
			 slurmctld_conf.node_prefix);
	else
		snprintf(name, i, "%s[000x%d%d%d]",
			 slurmctld_conf.node_prefix,
			 geo[X], geo[Y], geo[Z]);
	slurm_conf_unlock();
			
	if(bg_found_block_list) {
		itr = list_iterator_create(bg_found_block_list);
		while ((bg_record = (bg_record_t *) list_next(itr)) != NULL) {
			if (!strcmp(name, bg_record->nodes)) {
				xfree(name);
				list_iterator_destroy(itr);
				/* don't create total already there */
				goto no_total;	
			}
		}
		list_iterator_destroy(itr);
	} else {
		error("create_full_system_block: no bg_found_block_list 2");
	}
	
	if(bg_list) {
		itr = list_iterator_create(bg_list);
		while ((bg_record = (bg_record_t *) list_next(itr)) 
		       != NULL) {
			if (!strcmp(name, bg_record->nodes)) {
				xfree(name);
				list_iterator_destroy(itr);
				/* don't create total already there */
				goto no_total;	
			}
		}
		list_iterator_destroy(itr);
	} else {
		xfree(name);
		error("create_overlapped_blocks: no bg_list 3");
		rc = SLURM_ERROR;
		goto no_total;
	}

	records = list_create(destroy_bg_record);
	blockreq.block = name;
	blockreq.blrtsimage = NULL;
	blockreq.linuximage = NULL;
	blockreq.mloaderimage = NULL;
	blockreq.ramdiskimage = NULL;
	blockreq.conn_type = SELECT_TORUS;
	blockreq.nodecards = 0;
	blockreq.quarters = 0;
	add_bg_record(records, NULL, &blockreq);
	xfree(name);
	
	bg_record = (bg_record_t *) list_pop(records);
	if(!bg_record) {
		error("Nothing was returned from full system create");
		rc = SLURM_ERROR;
		goto no_total;
	}
	reset_ba_system();
	for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
		geo[i] = bg_record->geo[i];
	debug2("adding %s %d%d%d %d%d%d",
	       bg_record->nodes,
	       bg_record->start[X],
	       bg_record->start[Y],
	       bg_record->start[Z],
	       geo[X],
	       geo[Y],
	       geo[Z]);
	results = list_create(NULL);
	name = set_bg_block(results,
			    bg_record->start, 
			    geo, 
			    bg_record->conn_type);
	if(!name) {
		error("I was unable to make the "
		      "requested block.");
		list_destroy(results);
		list_iterator_destroy(itr);
		slurm_mutex_unlock(&block_state_mutex);
		return SLURM_ERROR;
	}
	xfree(name);
	if(bg_record->bg_block_list)
		list_destroy(bg_record->bg_block_list);
	bg_record->bg_block_list =
		list_create(destroy_ba_node);
	copy_node_path(results, 
		       bg_record->bg_block_list);
	list_destroy(results);
				
	if((rc = configure_block(bg_record)) == SLURM_ERROR) {
		error("create_full_system_block: "
		      "unable to configure block in api");
		destroy_bg_record(bg_record);
		goto no_total;
	}

	print_bg_record(bg_record);
	list_append(bg_list, bg_record);

no_total:
	if(records)
		list_destroy(records);
	slurm_mutex_unlock(&block_state_mutex);
	return rc;
}

/* must set the protecting mutex if any before this function is called */

extern int remove_from_bg_list(List my_bg_list, bg_record_t *bg_record)
{
	bg_record_t *found_record = NULL;
	ListIterator itr;
	int rc = SLURM_ERROR;

	if(!bg_record)
		return rc;

	//slurm_mutex_lock(&block_state_mutex);	
	itr = list_iterator_create(my_bg_list);
	while ((found_record = (bg_record_t *) list_next(itr)) != NULL) {
		if(found_record)
			if(bg_record == found_record) {
				list_remove(itr);
				rc = SLURM_SUCCESS;
				break;
			}
	}
	list_iterator_destroy(itr);
	//slurm_mutex_unlock(&block_state_mutex);

	return rc;
}

extern int remove_from_request_list()
{
	ba_request_t *try_request = NULL; 
	ListIterator itr;
	int rc = SLURM_ERROR;

	/* 
	   remove all requests out of the list.
	*/
		
	slurm_mutex_lock(&request_list_mutex);
	itr = list_iterator_create(bg_request_list);
	while ((try_request = list_next(itr)) != NULL) {
		debug3("removing size %d", 
		       try_request->procs);
		list_remove(itr);
		delete_ba_request(try_request);
		//list_iterator_reset(itr);
		rc = SLURM_SUCCESS;
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&request_list_mutex);
	return rc;
}

extern int bg_free_block(bg_record_t *bg_record)
{
#ifdef HAVE_BG_FILES
	int rc;
#endif
	if(!bg_record) {
		error("bg_free_block: there was no bg_record");
		return SLURM_ERROR;
	}
	
	while (1) {
		if(!bg_record) {
			error("bg_free_block: there was no bg_record");
			return SLURM_ERROR;
		}
		
		slurm_mutex_lock(&block_state_mutex);			
		if (bg_record->state != NO_VAL
		    && bg_record->state != RM_PARTITION_FREE 
		    && bg_record->state != RM_PARTITION_DEALLOCATING) {
#ifdef HAVE_BG_FILES
			debug2("bridge_destroy %s",bg_record->bg_block_id);
			
			rc = bridge_destroy_block(bg_record->bg_block_id);
			if (rc != STATUS_OK) {
				if(rc == PARTITION_NOT_FOUND) {
					debug("block %s is not found",
					      bg_record->bg_block_id);
					break;
				} else if(rc == INCOMPATIBLE_STATE) {
					debug2("bridge_destroy_partition"
					       "(%s): %s State = %d",
					       bg_record->bg_block_id, 
					       bg_err_str(rc), 
					       bg_record->state);
				} else {
					error("bridge_destroy_partition"
					      "(%s): %s State = %d",
					      bg_record->bg_block_id, 
					      bg_err_str(rc), 
					      bg_record->state);
				}
			}
#else
			bg_record->state = RM_PARTITION_FREE;	
#endif
		}
		
		if ((bg_record->state == RM_PARTITION_FREE)
		    ||  (bg_record->state == RM_PARTITION_ERROR)) {
			break;
		}
		slurm_mutex_unlock(&block_state_mutex);			
		sleep(3);
	}
	remove_from_bg_list(bg_booted_block_list, bg_record);
	slurm_mutex_unlock(&block_state_mutex);			
		
	return SLURM_SUCCESS;
}

/* Free multiple blocks in parallel */
extern void *mult_free_block(void *args)
{
	bg_record_t *bg_record = NULL;
	
	slurm_mutex_lock(&freed_cnt_mutex);
	if ((bg_freeing_list == NULL) 
	    && ((bg_freeing_list = list_create(destroy_bg_record)) == NULL))
		fatal("malloc failure in bg_freeing_list");
	slurm_mutex_unlock(&freed_cnt_mutex);
	
	/*
	 * Don't just exit when there is no work left. Creating 
	 * pthreads from within a dynamically linked object (plugin)
	 * causes large memory leaks on some systems that seem 
	 * unavoidable even from detached pthreads.
	 */
	while (!agent_fini) {
		slurm_mutex_lock(&freed_cnt_mutex);
		bg_record = list_dequeue(bg_free_block_list);
		slurm_mutex_unlock(&freed_cnt_mutex);
		if (!bg_record) {
			usleep(100000);
			continue;
		}
		debug("freeing the block %s.", bg_record->bg_block_id);
		bg_free_block(bg_record);	
		debug("done\n");
		slurm_mutex_lock(&freed_cnt_mutex);
		num_block_freed++;
		slurm_mutex_unlock(&freed_cnt_mutex);
	}
	slurm_mutex_lock(&freed_cnt_mutex);
	free_cnt--;
	if(bg_freeing_list) {
		list_destroy(bg_freeing_list);
		bg_freeing_list = NULL;
	}
	if(free_cnt == 0) {
		list_destroy(bg_free_block_list);
		bg_free_block_list = NULL;
	}
	slurm_mutex_unlock(&freed_cnt_mutex);
	return NULL;
}

/* destroy multiple blocks in parallel */
extern void *mult_destroy_block(void *args)
{
	bg_record_t *bg_record = NULL;

#ifdef HAVE_BG_FILES
	int rc;
#endif
	slurm_mutex_lock(&freed_cnt_mutex);
	if ((bg_freeing_list == NULL) 
	    && ((bg_freeing_list = list_create(destroy_bg_record)) == NULL))
		fatal("malloc failure in bg_freeing_list");
	slurm_mutex_unlock(&freed_cnt_mutex);
	
	/*
	 * Don't just exit when there is no work left. Creating 
	 * pthreads from within a dynamically linked object (plugin)
	 * causes large memory leaks on some systems that seem 
	 * unavoidable even from detached pthreads.
	 */
	while (!agent_fini) {
		slurm_mutex_lock(&freed_cnt_mutex);
		bg_record = list_dequeue(bg_destroy_block_list);
		slurm_mutex_unlock(&freed_cnt_mutex);
		if (!bg_record) {
			usleep(100000);
			continue;
		}
		slurm_mutex_lock(&block_state_mutex);
		remove_from_bg_list(bg_list, bg_record);
		slurm_mutex_unlock(&block_state_mutex);
			
		slurm_mutex_lock(&block_state_mutex);
		list_push(bg_freeing_list, bg_record);
		slurm_mutex_unlock(&block_state_mutex);
		
		/* 
		 * we only are sorting this so when we send it to a
		 * tool such as smap it will be in a nice order
		 */
		sort_bg_record_inc_size(bg_freeing_list);
		
		remove_from_request_list();
		
		slurm_mutex_lock(&block_state_mutex);
		if(remove_from_bg_list(bg_job_block_list, bg_record) 
		   == SLURM_SUCCESS) {
			num_unused_cpus += 
				bg_record->bp_count*bg_record->cpus_per_bp;
		}
		slurm_mutex_unlock(&block_state_mutex);
		debug3("removing the jobs on block %s\n",
		       bg_record->bg_block_id);
		term_jobs_on_block(bg_record->bg_block_id);
		
		debug2("destroying %s", (char *)bg_record->bg_block_id);
		if(bg_free_block(bg_record) == SLURM_ERROR) {
			debug("there was an error");
			goto already_here;
		}
		debug2("done destroying");
		slurm_mutex_lock(&block_state_mutex);
		remove_from_bg_list(bg_freeing_list, bg_record);
		slurm_mutex_unlock(&block_state_mutex);
								
#ifdef HAVE_BG_FILES
		debug2("removing from database %s", 
		       (char *)bg_record->bg_block_id);
		
		rc = bridge_remove_block(bg_record->bg_block_id);
		if (rc != STATUS_OK) {
			if(rc == PARTITION_NOT_FOUND) {
				debug("block %s is not found",
				      bg_record->bg_block_id);
			} else {
				error("1 rm_remove_partition(%s): %s",
				      bg_record->bg_block_id,
				      bg_err_str(rc));
			}
		} else
			debug2("done %s", 
			       (char *)bg_record->bg_block_id);
#endif
		destroy_bg_record(bg_record);
		debug2("destroyed");
		
	already_here:
		slurm_mutex_lock(&freed_cnt_mutex);
		num_block_freed++;
		slurm_mutex_unlock(&freed_cnt_mutex);
				
	}
	slurm_mutex_lock(&freed_cnt_mutex);
	destroy_cnt--;
	if(bg_freeing_list) {
		list_destroy(bg_freeing_list);
		bg_freeing_list = NULL;
	}
	if(destroy_cnt == 0) {
		list_destroy(bg_destroy_block_list);
		bg_destroy_block_list = NULL;
	}
	slurm_mutex_unlock(&freed_cnt_mutex);

	return NULL;
}

extern int free_block_list(List delete_list)
{
	bg_record_t *found_record = NULL;
	int retries;
	List *block_list = NULL;
	int *count = NULL;
	pthread_attr_t attr_agent;
	pthread_t thread_agent;

	if(!delete_list || !list_count(delete_list))
		return SLURM_SUCCESS;

	/* set up which list to push onto */
	if(bluegene_layout_mode == LAYOUT_DYNAMIC) {
		block_list = &bg_destroy_block_list;
		count = &destroy_cnt;
	} else {
		block_list = &bg_free_block_list;
		count = &free_cnt;
	}
	
	slurm_mutex_lock(&freed_cnt_mutex);
	
	if ((*block_list == NULL) 
	    && ((*block_list = list_create(NULL)) == NULL))
		fatal("malloc failure in free_block_list");
	
	while ((found_record = (bg_record_t*)list_pop(delete_list)) != NULL) {
		/* push job onto queue in a FIFO */
		debug3("adding %s to be freed", found_record->bg_block_id);
		if (list_push(*block_list, found_record) == NULL)
			fatal("malloc failure in _block_op/list_push");
		
		/* already running MAX_AGENTS we don't really need more 
		   since they don't end until we shut down the controller */
		if (*count > MAX_AGENT_COUNT) 
			continue;
		
		(*count)++;
		
		slurm_attr_init(&attr_agent);
		if (pthread_attr_setdetachstate(
			    &attr_agent, 
			    PTHREAD_CREATE_DETACHED))
			error("pthread_attr_setdetachstate error %m");
		retries = 0;
		if(bluegene_layout_mode == LAYOUT_DYNAMIC) {
			while (pthread_create(&thread_agent, 
					      &attr_agent, 
					      mult_destroy_block,
					      NULL)) {
				error("pthread_create "
				      "error %m");
				if (++retries > MAX_PTHREAD_RETRIES)
					fatal("Can't create "
					      "pthread");
				/* sleep and retry */
				usleep(1000);	
			}
		} else {
			while (pthread_create(&thread_agent, 
					      &attr_agent, 
					      mult_free_block, 
					      NULL)) {
				error("pthread_create "
				      "error %m");
				if (++retries > MAX_PTHREAD_RETRIES)
					fatal("Can't create "
					      "pthread");
				/* sleep and retry */
				usleep(1000);	
			}
		}
		slurm_attr_destroy(&attr_agent);
	}
	slurm_mutex_unlock(&freed_cnt_mutex);
	return SLURM_SUCCESS;
}

/*
 * Read and process the bluegene.conf configuration file so to interpret what
 * blocks are static/dynamic, torus/mesh, etc.
 */
extern int read_bg_conf(void)
{
	int i;
	int count = 0;
	s_p_hashtbl_t *tbl = NULL;
	char *layout = NULL;
	blockreq_t **blockreq_array = NULL;
	image_t **image_array = NULL;
	image_t *image = NULL;
	static time_t last_config_update = (time_t) 0;
	struct stat config_stat;
	ListIterator itr = NULL;

	debug("Reading the bluegene.conf file");

	/* check if config file has changed */
	if (!bg_conf)
		bg_conf = _get_bg_conf();
	if (stat(bg_conf, &config_stat) < 0)
		fatal("can't stat bluegene.conf file %s: %m", bg_conf);
	if (last_config_update) {
		_reopen_bridge_log();
		if(last_config_update == config_stat.st_mtime)
			debug("%s unchanged", bg_conf);
		else {
			info("Restart slurmctld for %s changes to take effect", 
			     bg_conf);
		}
		last_config_update = config_stat.st_mtime; 
		return SLURM_SUCCESS;
	}
	last_config_update = config_stat.st_mtime; 

	/* initialization */
	/* bg_conf defined in bg_node_alloc.h */
	tbl = s_p_hashtbl_create(bg_conf_file_options);
	
	if(s_p_parse_file(tbl, bg_conf) == SLURM_ERROR)
		fatal("something wrong with opening/reading bluegene "
		      "conf file");
	
	_set_bg_lists();	
	if (s_p_get_array((void ***)&image_array, 
			  &count, "AltBlrtsImage", tbl)) {
		for (i = 0; i < count; i++) {
			list_append(bg_blrtsimage_list, image_array[i]);
			image_array[i] = NULL;
		}
	}
	if (!s_p_get_string(&default_blrtsimage, "BlrtsImage", tbl)) {
		if(!list_count(bg_blrtsimage_list))
			fatal("BlrtsImage not configured "
			      "in bluegene.conf");
		itr = list_iterator_create(bg_blrtsimage_list);
		image = list_next(itr);
		image->def = true;
		list_iterator_destroy(itr);
		default_blrtsimage = xstrdup(image->name);
		info("Warning: using %s as the default BlrtsImage.  "
		     "If this isn't correct please set BlrtsImage",
		     default_blrtsimage); 
	} else {
		debug3("default BlrtsImage %s", default_blrtsimage);
		image = xmalloc(sizeof(image_t));
		image->name = xstrdup(default_blrtsimage);
		image->def = true;
		image->groups = NULL;
		/* we want it to be first */
		list_push(bg_blrtsimage_list, image);
	}
		
	if (s_p_get_array((void ***)&image_array, 
			  &count, "AltLinuxImage", tbl)) {
		for (i = 0; i < count; i++) {
			list_append(bg_linuximage_list, image_array[i]);
			image_array[i] = NULL;
		}
	}
	if (!s_p_get_string(&default_linuximage, "LinuxImage", tbl)) {
		if(!list_count(bg_linuximage_list))
			fatal("LinuxImage not configured "
			      "in bluegene.conf");
		itr = list_iterator_create(bg_linuximage_list);
		image = list_next(itr);
		image->def = true;
		list_iterator_destroy(itr);
		default_linuximage = xstrdup(image->name);
		info("Warning: using %s as the default LinuxImage.  "
		     "If this isn't correct please set LinuxImage",
		     default_linuximage); 
	} else {
		debug3("default LinuxImage %s", default_linuximage);
		image = xmalloc(sizeof(image_t));
		image->name = xstrdup(default_linuximage);
		image->def = true;
		image->groups = NULL;
		/* we want it to be first */
		list_push(bg_linuximage_list, image);		
	}

	if (s_p_get_array((void ***)&image_array, 
			  &count, "AltMloaderImage", tbl)) {
		for (i = 0; i < count; i++) {
			list_append(bg_mloaderimage_list, image_array[i]);
			image_array[i] = NULL;
		}
	}
	if (!s_p_get_string(&default_mloaderimage,
			    "MloaderImage", tbl)) {
		if(!list_count(bg_mloaderimage_list))
			fatal("MloaderImage not configured "
			      "in bluegene.conf");
		itr = list_iterator_create(bg_mloaderimage_list);
		image = list_next(itr);
		image->def = true;
		list_iterator_destroy(itr);
		default_mloaderimage = xstrdup(image->name);
		info("Warning: using %s as the default MloaderImage.  "
		     "If this isn't correct please set MloaderImage",
		     default_mloaderimage); 
	} else {
		debug3("default MloaderImage %s", default_mloaderimage);
		image = xmalloc(sizeof(image_t));
		image->name = xstrdup(default_mloaderimage);
		image->def = true;
		image->groups = NULL;
		/* we want it to be first */
		list_push(bg_mloaderimage_list, image);		
	}

	if (s_p_get_array((void ***)&image_array, 
			  &count, "AltRamDiskImage", tbl)) {
		for (i = 0; i < count; i++) {
			list_append(bg_ramdiskimage_list, image_array[i]);
			image_array[i] = NULL;
		}
	}
	if (!s_p_get_string(&default_ramdiskimage,
			    "RamDiskImage", tbl)) {
		if(!list_count(bg_ramdiskimage_list))
			fatal("RamDiskImage not configured "
			      "in bluegene.conf");
		itr = list_iterator_create(bg_ramdiskimage_list);
		image = list_next(itr);
		image->def = true;
		list_iterator_destroy(itr);
		default_ramdiskimage = xstrdup(image->name);
		info("Warning: using %s as the default RamDiskImage.  "
		     "If this isn't correct please set RamDiskImage",
		     default_ramdiskimage); 
	} else {
		debug3("default RamDiskImage %s", default_ramdiskimage);
		image = xmalloc(sizeof(image_t));
		image->name = xstrdup(default_ramdiskimage);
		image->def = true;
		image->groups = NULL;
		/* we want it to be first */
		list_push(bg_ramdiskimage_list, image);		
	}

	if (!s_p_get_uint16(&bluegene_numpsets, "Numpsets", tbl))
		fatal("Warning: Numpsets not configured in bluegene.conf");
	if (!s_p_get_uint16(&bridge_api_verb, "BridgeAPIVerbose", tbl))
		info("Warning: BridgeAPIVerbose not configured "
		     "in bluegene.conf");
	if (!s_p_get_string(&bridge_api_file, "BridgeAPILogFile", tbl)) 
		info("BridgeAPILogFile not configured in bluegene.conf");
	else
		_reopen_bridge_log();
	if (!s_p_get_string(&layout, "LayoutMode", tbl)) {
		info("Warning: LayoutMode was not specified in bluegene.conf "
		     "defaulting to STATIC partitioning");
		bluegene_layout_mode = LAYOUT_STATIC;
	} else {
		if(!strcasecmp(layout,"STATIC")) 
			bluegene_layout_mode = LAYOUT_STATIC;
		else if(!strcasecmp(layout,"OVERLAP")) 
			bluegene_layout_mode = LAYOUT_OVERLAP;
		else if(!strcasecmp(layout,"DYNAMIC")) 
			bluegene_layout_mode = LAYOUT_DYNAMIC;
		else {
			fatal("I don't understand this LayoutMode = %s", 
			      layout);
		}
		xfree(layout);
	}
	if (!s_p_get_uint16(
		    &bluegene_bp_node_cnt, "BasePartitionNodeCnt", tbl)) {
		error("BasePartitionNodeCnt not configured in bluegene.conf "
		      "defaulting to 512 as BasePartitionNodeCnt");
		bluegene_bp_node_cnt = 512;
		bluegene_quarter_node_cnt = 128;
	} else {
		if(bluegene_bp_node_cnt<=0)
			fatal("You should have more than 0 nodes "
			      "per base partition");

		bluegene_quarter_node_cnt = bluegene_bp_node_cnt/4;
	}

	if (!s_p_get_uint16(
		    &bluegene_nodecard_node_cnt, "NodeCardNodeCnt", tbl)) {
		error("NodeCardNodeCnt not configured in bluegene.conf "
		      "defaulting to 32 as NodeCardNodeCnt");
		bluegene_nodecard_node_cnt = 32;
	}
	
	if(bluegene_nodecard_node_cnt<=0)
		fatal("You should have more than 0 nodes per nodecard");

	if(bluegene_numpsets) {
		bluegene_quarter_ionode_cnt = bluegene_numpsets/4;
		bluegene_nodecard_ionode_cnt = bluegene_quarter_ionode_cnt/4;
		if((int)bluegene_nodecard_ionode_cnt < 1) {
			bluegene_nodecard_ionode_cnt = 0;
		}
	} else {
		fatal("your numpsets is 0");
	}

	/* add blocks defined in file */
	if(bluegene_layout_mode != LAYOUT_DYNAMIC) {
		if (!s_p_get_array((void ***)&blockreq_array, 
				   &count, "BPs", tbl)) {
			info("WARNING: no blocks defined in bluegene.conf, "
			     "only making full system block");
			i = 0;
			create_full_system_block(&i);
		}
		
		for (i = 0; i < count; i++) {
			add_bg_record(bg_list, NULL, blockreq_array[i]);
		}
	}
//#if 0	
	/* Check to see if the configs we have are correct */
	if (_validate_config_nodes() == SLURM_ERROR) { 
		_delete_old_blocks();
	}
//#endif
	/* looking for blocks only I created */
	if(bluegene_layout_mode == LAYOUT_DYNAMIC) {
		init_wires();
		info("No blocks created until jobs are submitted");
	} else {
		if (create_defined_blocks(bluegene_layout_mode) 
		    == SLURM_ERROR) {
			/* error in creating the static blocks, so
			 * blocks referenced by submitted jobs won't
			 * correspond to actual slurm blocks.
			 */
			fatal("Error, could not create the static blocks");
			return SLURM_ERROR;
		}
	} 
	
	slurm_mutex_lock(&block_state_mutex);
	list_destroy(bg_curr_block_list);
	bg_curr_block_list = NULL;
	list_destroy(bg_found_block_list);
	bg_found_block_list = NULL;
	last_bg_update = time(NULL);
	blocks_are_created = 1;
	slurm_mutex_unlock(&block_state_mutex);
	sort_bg_record_inc_size(bg_list);
	debug("Blocks have finished being created.");
	s_p_hashtbl_destroy(tbl);

	return SLURM_SUCCESS;
}

extern int set_ionodes(bg_record_t *bg_record)
{
	int i = 0;
	int start_bit = 0;
	int size = 0;
	char bitstring[BITSIZE];
	
	if(!bg_record)
		return SLURM_ERROR;
	/* set the bitmap blank here if it is a full node we don't
	   want anything set we also don't want the bg_record->ionodes set.
	*/
	bg_record->ionode_bitmap = bit_alloc(bluegene_numpsets);
	if(bg_record->quarter == (uint16_t)NO_VAL) {
		return SLURM_SUCCESS;
	}

	start_bit = bluegene_quarter_ionode_cnt*bg_record->quarter;
		
	if(bg_record->nodecard != (uint16_t)NO_VAL) {
		start_bit += bluegene_nodecard_ionode_cnt*bg_record->nodecard;
		size = bluegene_nodecard_ionode_cnt;
	} else
		size = bluegene_quarter_ionode_cnt;
	size += start_bit;

	for(i=start_bit; i<size; i++)
		bit_set(bg_record->ionode_bitmap, i);
	
	bit_fmt(bitstring, BITSIZE, bg_record->ionode_bitmap);
	bg_record->ionodes = xstrdup(bitstring);
	
	return SLURM_SUCCESS;
}

extern int add_bg_record(List records, List used_nodes, blockreq_t *blockreq)
{
	bg_record_t *bg_record = NULL;
	bg_record_t *found_record = NULL;
	ba_node_t *ba_node = NULL;
	ListIterator itr;
	struct passwd *pw_ent = NULL;
	int i, len;
	int small_size = 0;
	int small_count = 0;
	uint16_t quarter = 0;
	uint16_t nodecard = 0;
	int node_cnt = 0;
	
	if(!records) {
		fatal("add_bg_record: no records list given");
	}
	bg_record = (bg_record_t*) xmalloc(sizeof(bg_record_t));
	
	slurm_conf_lock();
	bg_record->user_name = 
		xstrdup(slurmctld_conf.slurm_user_name);
	bg_record->target_name = 
		xstrdup(slurmctld_conf.slurm_user_name);
	slurm_conf_unlock();
	if((pw_ent = getpwnam(bg_record->user_name)) == NULL) {
		error("getpwnam(%s): %m", bg_record->user_name);
	} else {
		bg_record->user_uid = pw_ent->pw_uid;
	}

	bg_record->bg_block_list = list_create(destroy_ba_node);
	if(used_nodes) {
		if(copy_node_path(used_nodes, bg_record->bg_block_list)
		   == SLURM_ERROR)
			error("couldn't copy the path for the allocation");
		bg_record->bp_count = list_count(used_nodes);
	}
	bg_record->quarter = (uint16_t)NO_VAL;
	bg_record->nodecard = (uint16_t)NO_VAL;
	if(set_ionodes(bg_record) == SLURM_ERROR) {
		error("add_bg_record: problem creating ionodes");
	}
	/* bg_record->boot_state = 0; 	Implicit */
	/* bg_record->state = 0;	Implicit */
	debug2("asking for %s %d %d %s", 
	       blockreq->block, blockreq->quarters, blockreq->nodecards,
	       convert_conn_type(blockreq->conn_type));
	len = strlen(blockreq->block);
	i=0;
	while((blockreq->block[i] != '[' 
	       && (blockreq->block[i] > 57 || blockreq->block[i] < 48)) 
	      && (i<len)) 		
		i++;
	
	if(i<len) {
		len -= i;
		slurm_conf_lock();
		len += strlen(slurmctld_conf.node_prefix)+1;
		bg_record->nodes = xmalloc(len);
		snprintf(bg_record->nodes, len, "%s%s", 
			slurmctld_conf.node_prefix, blockreq->block+i);
		slurm_conf_unlock();
			
	} else 
		fatal("BPs=%s is in a weird format", blockreq->block); 
	
	process_nodes(bg_record);
	
	bg_record->node_use = SELECT_COPROCESSOR_MODE;
	bg_record->conn_type = blockreq->conn_type;
	bg_record->cpus_per_bp = procs_per_node;
	bg_record->node_cnt = bluegene_bp_node_cnt * bg_record->bp_count;
	bg_record->job_running = NO_JOB_RUNNING;

	if(blockreq->blrtsimage)
		bg_record->blrtsimage = xstrdup(blockreq->blrtsimage);
	else
		bg_record->blrtsimage = xstrdup(default_blrtsimage);

	if(blockreq->linuximage)
		bg_record->linuximage = xstrdup(blockreq->linuximage);
	else
		bg_record->linuximage = xstrdup(default_linuximage);

	if(blockreq->mloaderimage)
		bg_record->mloaderimage = xstrdup(blockreq->mloaderimage);
	else
		bg_record->mloaderimage = xstrdup(default_mloaderimage);

	if(blockreq->ramdiskimage)
		bg_record->ramdiskimage = xstrdup(blockreq->ramdiskimage);
	else
		bg_record->ramdiskimage = xstrdup(default_ramdiskimage);
		
	if(bg_record->conn_type != SELECT_SMALL) {
		/* this needs to be an append so we keep things in the
		   order we got them, they will be sorted later */
		list_append(records, bg_record);
		/* this isn't a correct list so we need to set it later for
		   now we just used it to be the bp number */
		if(!used_nodes) {
			debug4("we didn't get a request list so we are "
			       "destroying this bp list");
			list_destroy(bg_record->bg_block_list);
			bg_record->bg_block_list = NULL;
		}
	} else {
		debug("adding a small block");
		if(blockreq->nodecards==0 && blockreq->quarters==0) {
			info("No specs given for this small block, "
			     "I am spliting this block into 4 quarters");
			blockreq->quarters=4;
		}
		i = (blockreq->nodecards*bluegene_nodecard_node_cnt) + 
			(blockreq->quarters*bluegene_quarter_node_cnt);
		if(i != bluegene_bp_node_cnt)
			fatal("There is an error in your bluegene.conf file.\n"
			      "I am unable to request %d nodes in one "
			      "base partition with %d nodes.", 
			      i, bluegene_bp_node_cnt);
		small_count = blockreq->nodecards+blockreq->quarters; 
		
		/* Automatically create 4-way split if 
		 * conn_type == SELECT_SMALL in bluegene.conf
		 * Here we go through each node listed and do the same thing
		 * for each node.
		 */
		itr = list_iterator_create(bg_record->bg_block_list);
		while ((ba_node = list_next(itr)) != NULL) {
			/* break base partition up into 16 parts */
			small_size = 16;
			node_cnt = 0;
			quarter = 0;
			nodecard = 0;
			for(i=0; i<small_count; i++) {
				if(i == blockreq->nodecards) {
					/* break base partition 
					   up into 4 parts */
					small_size = 4;
				}
									
				if(small_size == 4)
					nodecard = (uint16_t)NO_VAL;
				else
					nodecard = i%4; 
				found_record = _create_small_record(bg_record,
								    quarter,
								    nodecard);
								 
				/* this needs to be an append so we
				   keep things in the order we got
				   them, they will be sorted later */
				list_append(records, found_record);
				node_cnt += bluegene_bp_node_cnt/small_size;
				if(node_cnt == 128) {
					node_cnt = 0;
					quarter++;
				}
			}
		}
		list_iterator_destroy(itr);
		destroy_bg_record(bg_record);
	} 
	return SLURM_SUCCESS;
}

#ifdef HAVE_BG
static int _addto_node_list(bg_record_t *bg_record, int *start, int *end)
{
	int node_count=0;
	int x,y,z;
	char node_name_tmp[255];
	ba_node_t *ba_node = NULL;

	if ((start[X] < 0) || (start[Y] < 0) || (start[Z] < 0)) {
		fatal("bluegene.conf starting coordinate is invalid: %d%d%d",
			start[X], start[Y], start[Z]);
	}
	if ((end[X] >= DIM_SIZE[X]) || (end[Y] >= DIM_SIZE[Y])
	||  (end[Z] >= DIM_SIZE[Z])) {
		fatal("bluegene.conf matrix size exceeds space defined in " 
			"slurm.conf %d%d%dx%d%d%d => %d%d%d",
			start[X], start[Y], start[Z], 
			end[X], end[Y], end[Z], 
			DIM_SIZE[X], DIM_SIZE[Y], DIM_SIZE[Z]);
	}
	debug3("bluegene.conf: %d%d%dx%d%d%d",
		start[X], start[Y], start[Z], end[X], end[Y], end[Z]);
	debug3("slurm.conf:    %d%d%d",
		DIM_SIZE[X], DIM_SIZE[Y], DIM_SIZE[Z]); 
	
	for (x = start[X]; x <= end[X]; x++) {
		for (y = start[Y]; y <= end[Y]; y++) {
			for (z = start[Z]; z <= end[Z]; z++) {
				slurm_conf_lock();
				snprintf(node_name_tmp, sizeof(node_name_tmp),
					 "%s%d%d%d", 
					 slurmctld_conf.node_prefix,
					 x, y, z);		
				slurm_conf_unlock();
				ba_node = ba_copy_node(
					&ba_system_ptr->grid[x][y][z]);
				list_append(bg_record->bg_block_list, ba_node);
				node_count++;
			}
		}
	}
	return node_count;
}

static int _ba_node_cmpf_inc(ba_node_t *node_a, ba_node_t *node_b)
{
	if (node_a->coord[X] < node_b->coord[X])
		return -1;
	else if (node_a->coord[X] > node_b->coord[X])
		return 1;
	
	if (node_a->coord[Y] < node_b->coord[Y])
		return -1;
	else if (node_a->coord[Y] > node_b->coord[Y])
		return 1;

	if (node_a->coord[Z] < node_b->coord[Z])
		return -1;
	else if (node_a->coord[Z] > node_b->coord[Z])
		return 1;

	error("You have the node %d%d%d in the list twice",
	      node_a->coord[X],
	      node_a->coord[Y],
	      node_a->coord[Z]); 
	return 0;
}
#endif //HAVE_BG

static void _set_bg_lists()
{
	slurm_mutex_lock(&block_state_mutex);
	if(bg_found_block_list)
		list_destroy(bg_found_block_list);
	bg_found_block_list = list_create(NULL);
	if(bg_booted_block_list) 
		list_destroy(bg_booted_block_list);
	bg_booted_block_list = list_create(NULL);
	if(bg_job_block_list) 
		list_destroy(bg_job_block_list);
	bg_job_block_list = list_create(NULL);	
	num_unused_cpus = 
		DIM_SIZE[X] * DIM_SIZE[Y] * DIM_SIZE[Z] * procs_per_node;
	if(bg_curr_block_list)
		list_destroy(bg_curr_block_list);	
	bg_curr_block_list = list_create(destroy_bg_record);
	
	
	if(bg_list) 
		list_destroy(bg_list);
	bg_list = list_create(destroy_bg_record);

	slurm_mutex_lock(&request_list_mutex);
	if(bg_request_list) 
		list_destroy(bg_request_list);
	bg_request_list = list_create(delete_ba_request);
	slurm_mutex_unlock(&request_list_mutex);
	
	slurm_mutex_unlock(&block_state_mutex);	
	
	if(bg_blrtsimage_list)
		list_destroy(bg_blrtsimage_list);
	bg_blrtsimage_list = list_create(destroy_image);
	if(bg_linuximage_list)
		list_destroy(bg_linuximage_list);
	bg_linuximage_list = list_create(destroy_image);
	if(bg_mloaderimage_list)
		list_destroy(bg_mloaderimage_list);
	bg_mloaderimage_list = list_create(destroy_image);
	if(bg_ramdiskimage_list)
		list_destroy(bg_ramdiskimage_list);
	bg_ramdiskimage_list = list_create(destroy_image);
	
}

/*
 * Match slurm configuration information with current BG block 
 * configuration. Return SLURM_SUCCESS if they match, else an error 
 * code. Writes bg_block_id into bg_list records.
 */

static int _validate_config_nodes(void)
{
	int rc = SLURM_ERROR;
#ifdef HAVE_BG_FILES
	bg_record_t* bg_record = NULL;	
	bg_record_t* init_bg_record = NULL;
	bg_record_t* full_system_bg_record = NULL;	
	int full_created = 0;
	ListIterator itr_conf;
	ListIterator itr_curr;
	rm_partition_mode_t node_use;
	char tmp_char[256];
	/* read current bg block info into bg_curr_block_list */
	if (read_bg_blocks() == SLURM_ERROR)
		return SLURM_ERROR;
	
	if(!bg_recover) 
		return SLURM_ERROR;

	if(!bg_curr_block_list)
		return SLURM_ERROR;

	itr_curr = list_iterator_create(bg_curr_block_list);
	while ((init_bg_record = list_next(itr_curr))) 
		if(init_bg_record->full_block) 
			full_system_bg_record = init_bg_record;	
		
	itr_conf = list_iterator_create(bg_list);
	while ((bg_record = (bg_record_t*) list_next(itr_conf))) {
		/* translate hostlist to ranged 
		   string for consistent format
		   search here 
		*/
		node_use = SELECT_COPROCESSOR_MODE; 
		list_iterator_reset(itr_curr);
		while ((init_bg_record = list_next(itr_curr))) {
			if (strcasecmp(bg_record->nodes, 
				       init_bg_record->nodes))
				continue; /* wrong nodes */
			if (bg_record->conn_type 
			    != init_bg_record->conn_type)
				continue; /* wrong conn_type */
			if(bg_record->quarter !=
			   init_bg_record->quarter)
				continue; /* wrong quart */
			if(bg_record->nodecard !=
			   init_bg_record->nodecard)
				continue; /* wrong nodecard */
			if(bg_record->blrtsimage &&
			   strcasecmp(bg_record->blrtsimage,
				      init_bg_record->blrtsimage)) 
				continue;
			if(bg_record->linuximage &&
			   strcasecmp(bg_record->linuximage,
				      init_bg_record->linuximage))
				continue;
			if(bg_record->mloaderimage &&
			   strcasecmp(bg_record->mloaderimage,
				      init_bg_record->mloaderimage))
				continue;
			if(bg_record->ramdiskimage &&
			   strcasecmp(bg_record->ramdiskimage,
				      init_bg_record->ramdiskimage))
				continue;
		       			
			copy_bg_record(init_bg_record, 
				       bg_record);
			break;
		}
			
		if (!bg_record->bg_block_id) {
			format_node_name(bg_record, tmp_char,
					 sizeof(tmp_char));
			info("Block found in bluegene.conf to be "
			     "created: Nodes:%s", 
			     tmp_char);
			rc = SLURM_ERROR;
		} else {
			if(bg_record->full_block)
				full_created = 1;

			list_push(bg_found_block_list, bg_record);
			format_node_name(bg_record, tmp_char,
					 sizeof(tmp_char));
			info("Existing: BlockID:%s Nodes:%s Conn:%s",
			     bg_record->bg_block_id, 
			     tmp_char,
			     convert_conn_type(bg_record->conn_type));
			if(((bg_record->state == RM_PARTITION_READY)
			    || (bg_record->state == RM_PARTITION_CONFIGURING))
			   && !block_exist_in_list(bg_booted_block_list, 
						   bg_record))
				list_push(bg_booted_block_list, bg_record);
		}
	}		
	list_iterator_destroy(itr_conf);
	list_iterator_destroy(itr_curr);
	if(bluegene_layout_mode == LAYOUT_DYNAMIC)
		goto finished;

	if(!full_created && full_system_bg_record) {
		bg_record = xmalloc(sizeof(bg_record_t));
		copy_bg_record(full_system_bg_record, bg_record);
		list_append(bg_list, bg_record);
		list_push(bg_found_block_list, bg_record);
		format_node_name(bg_record, tmp_char, sizeof(tmp_char));
		info("Existing: BlockID:%s Nodes:%s Conn:%s",
		     bg_record->bg_block_id, 
		     tmp_char,
		     convert_conn_type(bg_record->conn_type));
		if(((bg_record->state == RM_PARTITION_READY)
		    || (bg_record->state == RM_PARTITION_CONFIGURING))
		   && !block_exist_in_list(bg_booted_block_list, 
					   bg_record))
			list_push(bg_booted_block_list, bg_record);
	}
		
finished:
	if(list_count(bg_list) == list_count(bg_curr_block_list))
		rc = SLURM_SUCCESS;
	
#endif

	return rc;
}

/* 
 * Comparator used for sorting blocks smallest to largest
 * 
 * returns: -1: rec_a >rec_b   0: rec_a == rec_b   1: rec_a < rec_b
 * 
 */
static int _bg_record_cmpf_inc(bg_record_t* rec_a, bg_record_t* rec_b)
{
	int size_a = rec_a->node_cnt;
	int size_b = rec_b->node_cnt;
	if (size_a < size_b)
		return -1;
	else if (size_a > size_b)
		return 1;
	if(rec_a->nodes && rec_b->nodes) {
		size_a = strcmp(rec_a->nodes, rec_b->nodes);
		if (size_a < 0)
			return -1;
		else if (size_a > 0)
			return 1;
	}
	if (rec_a->quarter < rec_b->quarter)
		return -1;
	else if (rec_a->quarter > rec_b->quarter)
		return 1;

	if(rec_a->nodecard < rec_b->nodecard)
		return -1;
	else if(rec_a->nodecard > rec_b->nodecard)
		return 1;

	return 0;
}

static int _delete_old_blocks(void)
{
#ifdef HAVE_BG_FILES
	ListIterator itr_curr, itr_found;
	bg_record_t *found_record = NULL, *init_record = NULL;
	pthread_attr_t attr_agent;
	pthread_t thread_agent;
	int retries;
	List bg_destroy_list = list_create(NULL);

	info("removing unspecified blocks");
	if(!bg_recover) {
		if(bg_curr_block_list) {
			itr_curr = list_iterator_create(bg_curr_block_list);
			while ((init_record = 
				(bg_record_t*)list_next(itr_curr))) {
				list_remove(itr_curr);
				list_push(bg_destroy_list, init_record);
			}
			list_iterator_destroy(itr_curr);
		} else {
			error("_delete_old_blocks: "
			      "no bg_curr_block_list 1");
			list_destroy(bg_destroy_list);
			return SLURM_ERROR;
		}
	} else {
		if(bg_curr_block_list) {
			itr_curr = list_iterator_create(bg_curr_block_list);
			while ((init_record = (bg_record_t*) 
				list_next(itr_curr))) {
				if(bg_found_block_list) {
					itr_found = list_iterator_create(
						bg_found_block_list);
					while ((found_record = (bg_record_t*) 
						list_next(itr_found)) 
					       != NULL) {
						if (!strcmp(init_record->
							    bg_block_id, 
							    found_record->
							    bg_block_id)) {
							/* don't delete 
							   this one 
							*/
							break;	
						}
					}
					list_iterator_destroy(itr_found);
				} else {
					error("_delete_old_blocks: "
					      "no bg_found_block_list");
					list_iterator_destroy(itr_curr);
					list_destroy(bg_destroy_list);
					return SLURM_ERROR;
				}
				if(found_record == NULL) {
					list_remove(itr_curr);
					list_push(bg_destroy_list, 
						  init_record);
				}
			}		
			list_iterator_destroy(itr_curr);
		} else {
			error("_delete_old_blocks: "
			      "no bg_curr_block_list 2");
			list_destroy(bg_destroy_list);
			return SLURM_ERROR;
		}
	}

	slurm_mutex_lock(&freed_cnt_mutex);
	if ((bg_destroy_block_list == NULL) 
	    && ((bg_destroy_block_list = list_create(NULL)) == NULL))
		fatal("malloc failure in block_list");
		
	itr_curr = list_iterator_create(bg_destroy_list);
	while ((init_record = (bg_record_t*) list_next(itr_curr))) {
		list_push(bg_destroy_block_list, init_record);
		num_block_to_free++;
		if (destroy_cnt > MAX_AGENT_COUNT) 
			continue;
		
		destroy_cnt++;

		slurm_attr_init(&attr_agent);
		if (pthread_attr_setdetachstate(&attr_agent, 
						PTHREAD_CREATE_DETACHED))
			error("pthread_attr_setdetachstate error %m");
		
		retries = 0;
		while (pthread_create(&thread_agent, 
				      &attr_agent, 
				      mult_destroy_block, 
				      NULL)) {
			error("pthread_create "
			      "error %m");
			if (++retries > MAX_PTHREAD_RETRIES)
				fatal("Can't create "
				      "pthread");
			/* sleep and retry */
			usleep(1000);	
		}
		slurm_attr_destroy(&attr_agent);
	}
	list_iterator_destroy(itr_curr);
	slurm_mutex_unlock(&freed_cnt_mutex);
	list_destroy(bg_destroy_list);
		
	retries=30;
	while(num_block_to_free > num_block_freed) {
		update_freeing_block_list();
		if(retries==30) {
			info("Waiting for old blocks to be "
			     "freed.  Have %d of %d",
			     num_block_freed, 
			     num_block_to_free);
			retries=0;
		}
		retries++;
		sleep(1);
	}
	
	info("I am done deleting");
#endif	
	return SLURM_SUCCESS;
}

static char *_get_bg_conf(void)
{
	char *val = getenv("SLURM_CONF");
	char *rc;
	int i;

	if (!val)
		return xstrdup(BLUEGENE_CONFIG_FILE);

	/* Replace file name on end of path */
	i = strlen(val) - strlen("slurm.conf") + strlen("bluegene.conf") + 1;
	rc = xmalloc(i);
	strcpy(rc, val);
	val = strrchr(rc, (int)'/');
	if (val)	/* absolute path */
		val++;
	else		/* not absolute path */
		val = rc;
	strcpy(val, "bluegene.conf");
	return rc;
}

static int _split_block(bg_record_t *bg_record, int procs) 
{
	bg_record_t *found_record = NULL;
	bool full_bp = false; 
	int small_count = 0;
	int small_size = 0;
	uint16_t num_nodecard = 0, num_quarter = 0;
	int i;
	int node_cnt = 0;
	uint16_t quarter = 0;
	uint16_t nodecard = 0;

	if(bg_record->quarter == (uint16_t) NO_VAL)
		full_bp = true;
	
	if(procs == (procs_per_node/16) && bluegene_nodecard_ionode_cnt) {
		num_nodecard=4;
		if(full_bp)
			num_quarter=3;
	} else if(full_bp) {
		num_quarter = 4;
	} else {
		error("you asked for something that was already this size");
		return SLURM_ERROR;
	}
	debug2("asking for %d 32s from a %d block",
	       num_nodecard, bg_record->node_cnt);
	small_count = num_nodecard+num_quarter; 

	/* break base partition up into 16 parts */
	small_size = bluegene_bp_node_cnt/bluegene_nodecard_node_cnt;
	node_cnt = 0;
	if(!full_bp)
		quarter = bg_record->quarter;
	else
		quarter = 0;
	nodecard = 0;
	for(i=0; i<small_count; i++) {
		if(i == num_nodecard) {
			/* break base partition up into 4 parts */
			small_size = 4;
		}
		
		if(small_size == 4)
			nodecard = (uint16_t)NO_VAL;
		else
			nodecard = i%4; 
		found_record = _create_small_record(bg_record,
						    quarter,
						    nodecard);
		if(block_exist_in_list(bg_list, found_record)) {
			destroy_bg_record(found_record);
		} else {
			if(configure_block(found_record) == SLURM_ERROR) {
				destroy_bg_record(found_record);
				error("_split_block: "
				      "unable to configure block in api");
				return SLURM_ERROR;
			}
			list_append(bg_list, found_record);
			print_bg_record(found_record);
		}
		node_cnt += bluegene_bp_node_cnt/small_size;
		if(node_cnt == 128) {
			node_cnt = 0;
			quarter++;
		}
	}
		
	return SLURM_SUCCESS;
}

static int _breakup_blocks(ba_request_t *request, List my_block_list)
{
	int rc = SLURM_ERROR;
	bg_record_t *bg_record = NULL;
	ListIterator itr;
	int proc_cnt=0;
	int total_proc_cnt=0;
	uint16_t last_quarter = (uint16_t) NO_VAL;
	char tmp_char[256];
	
	debug2("proc count = %d size = %d",
	       request->procs, request->size);
	
	itr = list_iterator_create(bg_list);			
	while ((bg_record = (bg_record_t *) list_next(itr)) != NULL) {
		if(bg_record->job_running != NO_JOB_RUNNING)
			continue;
		if(bg_record->state != RM_PARTITION_FREE)
			continue;
		if(request->start_req) {
			if ((request->start[X] != bg_record->start[X])
			    || (request->start[Y] != bg_record->start[Y])
			    || (request->start[Z] != bg_record->start[Z])) {
				debug4("small got %d%d%d looking for %d%d%d",
				       bg_record->start[X],
				       bg_record->start[Y],
				       bg_record->start[Z],
				       request->start[X],
				       request->start[Y],
				       request->start[Z]);
				continue;
			}
			debug3("small found %d%d%d looking for %d%d%d",
			       bg_record->start[X],
			       bg_record->start[Y],
			       bg_record->start[Z],
			       request->start[X],
			       request->start[Y],
			       request->start[Z]);
		}
		proc_cnt = bg_record->bp_count * 
			bg_record->cpus_per_bp;
		if(proc_cnt == request->procs) {
			debug2("found it here %s, %s",
			       bg_record->bg_block_id,
			       bg_record->nodes);
			request->save_name = xmalloc(4);
			snprintf(request->save_name,
				 4,
				 "%d%d%d",
				 bg_record->start[X],
				 bg_record->start[Y],
				 bg_record->start[Z]);
			rc = SLURM_SUCCESS;
			goto finished;
		}
		if(bg_record->node_cnt > bluegene_bp_node_cnt)
			continue;
		if(proc_cnt < request->procs) {
			if(last_quarter != bg_record->quarter){
				last_quarter = bg_record->quarter;
				total_proc_cnt = proc_cnt;
			} else {
				total_proc_cnt += proc_cnt;
			}
			debug2("1 got %d on quarter %d",
			       total_proc_cnt, last_quarter);
			if(total_proc_cnt == request->procs) {
				request->save_name = xmalloc(4);
				snprintf(request->save_name, 
					 4,
					 "%d%d%d",
					 bg_record->start[X],
					 bg_record->start[Y],
					 bg_record->start[Z]);
				if(!my_block_list) {
					rc = SLURM_SUCCESS;
					goto finished;	
				}
						
				bg_record = _create_small_record(
					bg_record,
					last_quarter,
					(uint16_t) NO_VAL);
				if(block_exist_in_list(bg_list, bg_record))
					destroy_bg_record(bg_record);
				else {
					if(configure_block(bg_record)
					   == SLURM_ERROR) {
						destroy_bg_record(bg_record);
						error("_breakup_blocks: "
						      "unable to configure "
						      "block in api");
						return SLURM_ERROR;
					}
					list_append(bg_list, bg_record);
					print_bg_record(bg_record);
				}
				rc = SLURM_SUCCESS;
				goto finished;	
			}
			continue;
		}
		break;
	}
	if(bg_record) {
		debug2("got one on the first pass");
		goto found_one;
	}
	list_iterator_reset(itr);
	last_quarter = (uint16_t) NO_VAL;
	while ((bg_record = (bg_record_t *) list_next(itr)) 
	       != NULL) {
		if(bg_record->job_running != NO_JOB_RUNNING)
			continue;
		if(request->start_req) {
			if ((request->start[X] != bg_record->start[X])
			    || (request->start[Y] != bg_record->start[Y])
			    || (request->start[Z] != bg_record->start[Z])) {
				debug4("small 2 got %d%d%d looking for %d%d%d",
				       bg_record->start[X],
				       bg_record->start[Y],
				       bg_record->start[Z],
				       request->start[X],
				       request->start[Y],
				       request->start[Z]);
				continue;
			}
			debug3("small 2 found %d%d%d looking for %d%d%d",
			       bg_record->start[X],
			       bg_record->start[Y],
			       bg_record->start[Z],
			       request->start[X],
			       request->start[Y],
			       request->start[Z]);
		}
				
		proc_cnt = bg_record->bp_count * bg_record->cpus_per_bp;
		if(proc_cnt == request->procs) {
			debug2("found it here %s, %s",
			       bg_record->bg_block_id,
			       bg_record->nodes);
			request->save_name = xmalloc(4);
			snprintf(request->save_name,
				 4,
				 "%d%d%d",
				 bg_record->start[X],
				 bg_record->start[Y],
				 bg_record->start[Z]);
			rc = SLURM_SUCCESS;
			goto finished;
		} 

		if(bg_record->node_cnt > bluegene_bp_node_cnt)
			continue;
		if(proc_cnt < request->procs) {
			if(last_quarter != bg_record->quarter){
				last_quarter = bg_record->quarter;
				total_proc_cnt = proc_cnt;
			} else {
				total_proc_cnt += proc_cnt;
			}
			debug2("got %d on quarter %d",
			       total_proc_cnt, last_quarter);
			if(total_proc_cnt == request->procs) {
				request->save_name = xmalloc(4);
				snprintf(request->save_name,
					 4,
					 "%d%d%d",
					 bg_record->start[X],
					 bg_record->start[Y],
					 bg_record->start[Z]);
				if(!my_block_list) {
					rc = SLURM_SUCCESS;
					goto finished;	
				}
				bg_record = _create_small_record(
					bg_record,
					last_quarter,
					(uint16_t) NO_VAL);
				if(block_exist_in_list(bg_list, bg_record))
					destroy_bg_record(bg_record);
				else {
					if(configure_block(bg_record)
					   == SLURM_ERROR) {
						destroy_bg_record(bg_record);
						error("_breakup_blocks: "
						      "unable to configure "
						      "block in api 2");
						return SLURM_ERROR;
					}
					list_append(bg_list, bg_record);
					print_bg_record(bg_record);
				}
				rc = SLURM_SUCCESS;
				goto finished;	
			}
			continue;
		}				
		break;
	}
found_one:
	if(bg_record) {
		format_node_name(bg_record, tmp_char, sizeof(tmp_char));
			
		debug2("going to split %s, %s",
		       bg_record->bg_block_id,
		       tmp_char);
		request->save_name = xmalloc(4);
		snprintf(request->save_name, 
			 4,
			 "%d%d%d",
			 bg_record->start[X],
			 bg_record->start[Y],
			 bg_record->start[Z]);
		if(!my_block_list) {
			rc = SLURM_SUCCESS;
			goto finished;	
		}
		_split_block(bg_record, request->procs);
		rc = SLURM_SUCCESS;
		goto finished;
	}
	
finished:
	list_iterator_destroy(itr);
		
	return rc;
}

static bg_record_t *_create_small_record(bg_record_t *bg_record, 
					 uint16_t quarter, uint16_t nodecard)
{
	bg_record_t *found_record = NULL;
	int small_size = 4;
	
	found_record = (bg_record_t*) xmalloc(sizeof(bg_record_t));
				
	found_record->job_running = NO_JOB_RUNNING;
	found_record->user_name = xstrdup(bg_record->user_name);
	found_record->user_uid = bg_record->user_uid;
	found_record->bg_block_list = list_create(destroy_ba_node);
	found_record->nodes = xstrdup(bg_record->nodes);
	found_record->blrtsimage = xstrdup(bg_record->blrtsimage);
	found_record->linuximage = xstrdup(bg_record->linuximage);
	found_record->mloaderimage = xstrdup(bg_record->mloaderimage);
	found_record->ramdiskimage = xstrdup(bg_record->ramdiskimage);

	process_nodes(found_record);
				
	found_record->conn_type = SELECT_SMALL;
				
	found_record->node_use = SELECT_COPROCESSOR_MODE;
	if(nodecard != (uint16_t) NO_VAL)
		small_size = 16;
	found_record->cpus_per_bp = procs_per_node/small_size;
	found_record->node_cnt = bluegene_bp_node_cnt/small_size;
	found_record->quarter = quarter; 
	found_record->nodecard = nodecard;
	
	if(set_ionodes(found_record) == SLURM_ERROR) 
		error("couldn't create ionode_bitmap for %d.%d",
		      found_record->quarter, found_record->nodecard);
	return found_record;
}

static int _reopen_bridge_log(void)
{
	int rc = SLURM_SUCCESS;

	if (bridge_api_file == NULL)
		return rc;
	
#ifdef HAVE_BG_FILES
	rc = bridge_set_log_params(bridge_api_file, bridge_api_verb);
#endif
	debug3("Bridge api file set to %s, verbose level %d\n", 
	       bridge_api_file, bridge_api_verb);
	
	return rc;
}

