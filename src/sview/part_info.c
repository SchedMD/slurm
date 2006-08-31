/*****************************************************************************\
 *  part_info.c - Functions related to partition display 
 *  mode of sview.
 *****************************************************************************
 *  Copyright (C) 2004-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include "src/sview/sview.h"

#define _DEBUG 0
DEF_TIMERS;

typedef struct {
	partition_info_t* part_ptr;
	uint16_t node_state;

	uint32_t node_cnt;
	uint32_t min_cpus;
	uint32_t max_cpus;
	uint32_t min_disk;
	uint32_t max_disk;
	uint32_t min_mem;
	uint32_t max_mem;
	uint32_t min_weight;
	uint32_t max_weight;

	char *features;
	char *reason;

	hostlist_t hl;
	List node_ptr_list;
} sview_part_sub_t;

/* Collection of data for printing reports. Like data is combined here */
typedef struct {
	/* part_info contains partition, avail, max_time, job_size, 
	 * root, share, groups */
	partition_info_t* part_ptr;
	List sub_list;
} sview_part_info_t;


enum { 
	SORTID_POS = POS_LOC,
	SORTID_NAME, 
	SORTID_DEFAULT, 
	SORTID_HIDDEN,
	SORTID_AVAIL, 
	SORTID_TIMELIMIT, 
	SORTID_NODES, 
	SORTID_STATE,
	SORTID_NODELIST, 
	SORTID_JOB_SIZE,
	SORTID_MIN_NODES,
	SORTID_MAX_NODES,
	SORTID_ROOT, 
	SORTID_SHARE, 
	SORTID_GROUPS,
	SORTID_CPUS, 
	SORTID_DISK, 
	SORTID_MEM, 
	SORTID_WEIGHT,
	SORTID_STATE_NUM,
	SORTID_UPDATED, 
	SORTID_CNT
};

static display_data_t display_data_part[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, -1, refresh_part},
	{G_TYPE_STRING, SORTID_NAME, "Partition", TRUE, -1, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_DEFAULT, "Default", TRUE, 0, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_HIDDEN, "Hidden", FALSE, 0, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_AVAIL, "Availablity", TRUE, 0, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_TIMELIMIT, "Time Limit", 
	 TRUE, 1, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_NODES, "Nodes", TRUE, -1, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_STATE, "State", TRUE, 0, refresh_part,
	 create_model_part, admin_edit_part},
#ifdef HAVE_BG
	{G_TYPE_STRING, SORTID_NODELIST, "BP List", TRUE, 1, refresh_part,
	 create_model_part, admin_edit_part},
#else
	{G_TYPE_STRING, SORTID_NODELIST, "NodeList", TRUE, 1, refresh_part,
	 create_model_part, admin_edit_part},
#endif
	{G_TYPE_STRING, SORTID_JOB_SIZE, "Job Size", FALSE, -1, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_MIN_NODES, "Min Nodes", FALSE, 1, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_MAX_NODES, "Max Nodes", FALSE, 1, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_ROOT, "Root", FALSE, 0, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_SHARE, "Share", FALSE, 0, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_GROUPS, "Groups", FALSE, 0, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_CPUS, "CPUs", FALSE, -1, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_DISK, "Temp Disk", FALSE, -1, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_MEM, "MEM", FALSE, -1, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_WEIGHT, "Weight", FALSE, -1, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_INT, SORTID_STATE_NUM, NULL, FALSE, -1, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_INT, SORTID_UPDATED, NULL, FALSE, -1, refresh_part,
	 create_model_part, admin_edit_part},

	{G_TYPE_NONE, -1, NULL, FALSE, -1}
};

static display_data_t options_data_part[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, -1},
	{G_TYPE_STRING, INFO_PAGE, "Full Info", TRUE, PART_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Jobs", TRUE, PART_PAGE},
#ifdef HAVE_BG
	{G_TYPE_STRING, BLOCK_PAGE, "Blocks", TRUE, PART_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Base Partitions", TRUE, PART_PAGE},
#else
	{G_TYPE_STRING, NODE_PAGE, "Nodes", TRUE, PART_PAGE},
#endif
	{G_TYPE_STRING, SUBMIT_PAGE, "Job Submit", TRUE, PART_PAGE},
	{G_TYPE_NONE, -1, NULL, FALSE, -1}
};

static display_data_t *local_display_data = NULL;

static void _update_part_sub_record(sview_part_sub_t *sview_part_sub,
				    GtkTreeStore *treestore,
				    GtkTreeIter *iter);
static void _append_part_sub_record(sview_part_sub_t *sview_part_sub,
				    GtkTreeStore *treestore, GtkTreeIter *iter,
				    int line);

static int 
_build_min_max_string(char *buffer, int buf_size, int min, int max, bool range)
{
	char tmp_min[7];
	char tmp_max[7];
	convert_num_unit((float)min, tmp_min, UNIT_NONE);
	convert_num_unit((float)max, tmp_max, UNIT_NONE);
	
	if (max == min)
		return snprintf(buffer, buf_size, "%s", tmp_max);
	else if (range) {
		if (max == INFINITE)
			return snprintf(buffer, buf_size, "%s-infinite", 
					tmp_min);
		else
			return snprintf(buffer, buf_size, "%s-%s", 
					tmp_min, tmp_max);
	} else
		return snprintf(buffer, buf_size, "%s+", tmp_min);
}

static void _subdivide_part(sview_part_info_t *sview_part_info,
			    GtkTreeModel *model, 
			    GtkTreeIter *sub_iter,
			    GtkTreeIter *iter)
{
	GtkTreeIter first_sub_iter;
	ListIterator itr = NULL;
	uint16_t state;
	int i = 0, line = 0;
	sview_part_sub_t *sview_part_sub = NULL;
	int set = 0;

	memset(&first_sub_iter, 0, sizeof(GtkTreeIter));

	/* make sure all the steps are still here */
	if (sub_iter) {
		first_sub_iter = *sub_iter;
		while(1) {
			gtk_tree_store_set(GTK_TREE_STORE(model), sub_iter, 
					   SORTID_UPDATED, 0, -1);	
			if(!gtk_tree_model_iter_next(model, sub_iter)) {
				break;
			}
		}
		memcpy(sub_iter, &first_sub_iter, sizeof(GtkTreeIter));
		set = 1;
	}
	itr = list_iterator_create(sview_part_info->sub_list);
	if(list_count(sview_part_info->sub_list) == 1) {
		sview_part_sub = list_next(itr);
		_update_part_sub_record(sview_part_sub,
					GTK_TREE_STORE(model), 
					iter);
	} else {
		while((sview_part_sub = list_next(itr))) {
			if (!sub_iter) {
				i = NO_VAL;
				goto adding;
			} else {
				memcpy(sub_iter, &first_sub_iter, 
				       sizeof(GtkTreeIter));
			
			}
			
			while(1) {
				/* search for the jobid and check to see if 
				   it is in the list */
				gtk_tree_model_get(model, sub_iter, 
						   SORTID_STATE_NUM, 
						   &state, -1);
				if(state == sview_part_sub->node_state) {
					/* update with new info */
					_update_part_sub_record(
						sview_part_sub,
						GTK_TREE_STORE(model), 
						sub_iter);
					goto found;
				}			
				
				/* see what line we were on to add the
				   next one to the list */
				gtk_tree_model_get(model, sub_iter, 
						   SORTID_POS, 
						   &line, -1);
				if(!gtk_tree_model_iter_next(model, 
							     sub_iter)) {
					line++;
					break;
				}
			}
		adding:
			_append_part_sub_record(sview_part_sub, 
						GTK_TREE_STORE(model), 
						iter, line);
			if(i == NO_VAL)
				line++;
		found:
			;
		}
	}
	list_iterator_destroy(itr);

	if(set) {
		sub_iter = &first_sub_iter;
		/* clear all steps that aren't active */
		while(1) {
			gtk_tree_model_get(model, sub_iter, 
					   SORTID_UPDATED, &i, -1);
			if(!i) {
				if(!gtk_tree_store_remove(
					   GTK_TREE_STORE(model), 
					   sub_iter))
					break;
				else
					continue;
			}
			if(!gtk_tree_model_iter_next(model, sub_iter)) {
				break;
			}
		}
	}
	return;
}

static void _update_part_record(sview_part_info_t *sview_part_info,
				GtkTreeStore *treestore, 
				GtkTreeIter *iter)
{
	char time_buf[20];
	char tmp_cnt[7];
	partition_info_t *part_ptr = sview_part_info->part_ptr;
	GtkTreeIter sub_iter;
	int childern = 0;
	
	gtk_tree_store_set(treestore, iter, SORTID_NAME, part_ptr->name, -1);

	if(part_ptr->default_part)
		gtk_tree_store_set(treestore, iter, SORTID_DEFAULT, "*", -1);
	
	if(part_ptr->hidden)
		gtk_tree_store_set(treestore, iter, SORTID_HIDDEN, "*", -1);
	
	if (part_ptr->state_up) 
		gtk_tree_store_set(treestore, iter, SORTID_AVAIL, "up", -1);
	else
		gtk_tree_store_set(treestore, iter, SORTID_AVAIL, "down", -1);
		
	if (part_ptr->max_time == INFINITE)
		snprintf(time_buf, sizeof(time_buf), "infinite");
	else {
		snprint_time(time_buf, sizeof(time_buf), 
			     (part_ptr->max_time * 60));
	}
	
	gtk_tree_store_set(treestore, iter, SORTID_TIMELIMIT, time_buf, -1);
	
	_build_min_max_string(time_buf, sizeof(time_buf), 
			      part_ptr->min_nodes, 
			      part_ptr->max_nodes, true);
	gtk_tree_store_set(treestore, iter, SORTID_JOB_SIZE, time_buf, -1);
	
	if (part_ptr->min_nodes == INFINITE)
		snprintf(time_buf, sizeof(time_buf), "infinite");
	else {
		convert_num_unit((float)part_ptr->min_nodes, 
				 time_buf, UNIT_NONE);
	}
	gtk_tree_store_set(treestore, iter, SORTID_MIN_NODES, 
			   time_buf, -1);
	if (part_ptr->max_nodes == INFINITE)
		snprintf(time_buf, sizeof(time_buf), "infinite");
	else {
		convert_num_unit((float)part_ptr->max_nodes, 
				 time_buf, UNIT_NONE);
	}
	gtk_tree_store_set(treestore, iter, SORTID_MAX_NODES, 
			   time_buf, -1);

	if(part_ptr->root_only)
		gtk_tree_store_set(treestore, iter, SORTID_ROOT, "yes", -1);
	else
		gtk_tree_store_set(treestore, iter, SORTID_ROOT, "no", -1);
	
	if(part_ptr->shared > 1)
		gtk_tree_store_set(treestore, iter, SORTID_SHARE, "force", -1);
	else if(part_ptr->shared)
		gtk_tree_store_set(treestore, iter, SORTID_SHARE, "yes", -1);
	else
		gtk_tree_store_set(treestore, iter, SORTID_SHARE, "no", -1);
	
	if(part_ptr->allow_groups)
		gtk_tree_store_set(treestore, iter, SORTID_GROUPS,
				   part_ptr->allow_groups, -1);
	else
		gtk_tree_store_set(treestore, iter, SORTID_GROUPS, "all", -1);

	convert_num_unit((float)part_ptr->total_nodes, tmp_cnt, UNIT_NONE);
	gtk_tree_store_set(treestore, iter, SORTID_NODES, tmp_cnt, -1);

	gtk_tree_store_set(treestore, iter, SORTID_NODELIST, 
			   part_ptr->nodes, -1);

	/* clear out info for the main listing */
	gtk_tree_store_set(treestore, iter, SORTID_STATE, "", -1);
	gtk_tree_store_set(treestore, iter, SORTID_STATE_NUM, -1, -1);
	gtk_tree_store_set(treestore, iter, SORTID_CPUS, "", -1);
	gtk_tree_store_set(treestore, iter, SORTID_DISK, "", -1);
	gtk_tree_store_set(treestore, iter, SORTID_MEM, "", -1);
	gtk_tree_store_set(treestore, iter, SORTID_WEIGHT, "", -1);
	gtk_tree_store_set(treestore, iter, SORTID_UPDATED, 1, -1);	
	
	childern = gtk_tree_model_iter_children(GTK_TREE_MODEL(treestore),
						&sub_iter, iter);
	if(gtk_tree_model_iter_children(GTK_TREE_MODEL(treestore),
					&sub_iter, iter))
		_subdivide_part(sview_part_info, 
				GTK_TREE_MODEL(treestore), &sub_iter, iter);
	else
		_subdivide_part(sview_part_info, 
				GTK_TREE_MODEL(treestore), NULL, iter);

	return;
}

static void _update_part_sub_record(sview_part_sub_t *sview_part_sub,
				    GtkTreeStore *treestore, GtkTreeIter *iter)
{
	char time_buf[20];
	char tmp_cnt[7];
	partition_info_t *part_ptr = sview_part_sub->part_ptr;
	char *upper = NULL, *lower = NULL;		     
	char tmp[1024];

	gtk_tree_store_set(treestore, iter, SORTID_NAME, part_ptr->name, -1);

	upper = node_state_string(sview_part_sub->node_state);
	lower = str_tolower(upper);
	gtk_tree_store_set(treestore, iter, SORTID_STATE, 
			   lower, -1);
	xfree(lower);
	gtk_tree_store_set(treestore, iter, SORTID_STATE_NUM,
			   sview_part_sub->node_state, -1);
	
	_build_min_max_string(time_buf, sizeof(time_buf), 
			      sview_part_sub->min_cpus, 
			      sview_part_sub->max_cpus, false);
	gtk_tree_store_set(treestore, iter, SORTID_CPUS, time_buf, -1);
	
	_build_min_max_string(time_buf, sizeof(time_buf), 
			      sview_part_sub->min_disk, 
			      sview_part_sub->max_disk, false);
	gtk_tree_store_set(treestore, iter, SORTID_DISK, time_buf, -1);

	_build_min_max_string(time_buf, sizeof(time_buf), 
			      sview_part_sub->min_mem, 
			      sview_part_sub->max_mem, false);
	gtk_tree_store_set(treestore, iter, SORTID_MEM, time_buf, -1);

	_build_min_max_string(time_buf, sizeof(time_buf), 
			      sview_part_sub->min_weight, 
			      sview_part_sub->max_weight, false);
	gtk_tree_store_set(treestore, iter, SORTID_WEIGHT, time_buf, -1);

	convert_num_unit((float)sview_part_sub->node_cnt, tmp_cnt, UNIT_NONE);
	gtk_tree_store_set(treestore, iter, SORTID_NODES, tmp_cnt, -1);
	
	hostlist_ranged_string(sview_part_sub->hl, sizeof(tmp), tmp);
	gtk_tree_store_set(treestore, iter, SORTID_NODELIST, 
			   tmp, -1);
	gtk_tree_store_set(treestore, iter, SORTID_UPDATED, 1, -1);	
	
		
	return;
}

static void _append_part_record(sview_part_info_t *sview_part_info,
				GtkTreeStore *treestore, GtkTreeIter *iter,
				int line)
{
	gtk_tree_store_append(treestore, iter, NULL);
	gtk_tree_store_set(treestore, iter, SORTID_POS, line, -1);
	_update_part_record(sview_part_info, treestore, iter);
}

static void _append_part_sub_record(sview_part_sub_t *sview_part_sub,
				    GtkTreeStore *treestore, GtkTreeIter *iter,
				    int line)
{
	GtkTreeIter sub_iter;
	
	gtk_tree_store_append(treestore, &sub_iter, iter);
	gtk_tree_store_set(treestore, &sub_iter, SORTID_POS, line, -1);
	_update_part_sub_record(sview_part_sub, treestore, &sub_iter);
}

static void _update_info_part(List info_list, 
			      GtkTreeView *tree_view,
			      specific_info_t *spec_info)
{
	GtkTreePath *path = gtk_tree_path_new_first();
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	GtkTreeIter iter;
	partition_info_t *part_ptr = NULL;
	int line = 0;
	char *host = NULL, *host2 = NULL, *part_name = NULL;
	hostlist_t hostlist = NULL;
	int found = 0;
	ListIterator itr = NULL;
	sview_part_info_t *sview_part_info = NULL;

	/* get the iter, or find out the list is empty goto add */
	if (gtk_tree_model_get_iter(model, &iter, path)) {
		/* make sure all the partitions are still here */
		while(1) {
			gtk_tree_store_set(GTK_TREE_STORE(model), &iter, 
					   SORTID_UPDATED, 0, -1);	
			if(!gtk_tree_model_iter_next(model, &iter)) {
				break;
			}
		}
	}

	itr = list_iterator_create(info_list);
	while ((sview_part_info = (sview_part_info_t*) list_next(itr))) {
		part_ptr = sview_part_info->part_ptr;
		/* get the iter, or find out the list is empty goto add */
		if (!gtk_tree_model_get_iter(model, &iter, path)) {
			goto adding;
		} 
		while(1) {
			/* search for the jobid and check to see if 
			   it is in the list */
			gtk_tree_model_get(model, &iter, SORTID_NAME, 
					   &part_name, -1);
			if(!strcmp(part_name, part_ptr->name)) {
				/* update with new info */
				g_free(part_name);
				_update_part_record(sview_part_info, 
						    GTK_TREE_STORE(model), 
						    &iter);
				goto found;
			}
			g_free(part_name);
				
			/* see what line we were on to add the next one 
			   to the list */
			gtk_tree_model_get(model, &iter, SORTID_POS, 
					   &line, -1);
			if(!gtk_tree_model_iter_next(model, &iter)) {
				line++;
				break;
			}
		}
	adding:
		if(spec_info) {
			switch(spec_info->type) {
			case NODE_PAGE:
				if(!part_ptr->nodes)
					continue;

				hostlist = hostlist_create(
					(char *)spec_info->data);
				host = hostlist_shift(hostlist);
				hostlist_destroy(hostlist);
				if(!host) 
					continue;
			
				hostlist = hostlist_create(part_ptr->nodes);
				found = 0;
				while((host2 = hostlist_shift(hostlist))) { 
					if(!strcmp(host, host2)) {
						free(host2);
						found = 1;
						break; 
					}
					free(host2);
				}
				hostlist_destroy(hostlist);
				if(!found)
					continue;
				break;
			case PART_PAGE:
			case BLOCK_PAGE:
			case JOB_PAGE:
				if(strcmp(part_ptr->name, 
					  (char *)spec_info->data)) 
					continue;
				break;
			default:
				g_print("Unkown type %d\n", spec_info->type);
				continue;
			}
		}
		_append_part_record(sview_part_info, GTK_TREE_STORE(model), 
				    &iter, line);
	found:
		;
	}
	list_iterator_destroy(itr);
	if(host)
		free(host);

	gtk_tree_path_free(path);
	/* remove all old partitions */
	remove_old(model, SORTID_UPDATED);
	return;
}

static void _info_list_del(void *object)
{
	sview_part_info_t *sview_part_info = (sview_part_info_t *)object;

	if (sview_part_info) {
		if(sview_part_info->sub_list)
			list_destroy(sview_part_info->sub_list);
		xfree(sview_part_info);
	}
}

static void _destroy_part_sub(void *object)
{
	sview_part_sub_t *sview_part_sub = (sview_part_sub_t *)object;

	if (sview_part_sub) {
		xfree(sview_part_sub->features);
		xfree(sview_part_sub->reason);
		if(sview_part_sub->hl)
			hostlist_destroy(sview_part_sub->hl);
		if(sview_part_sub->node_ptr_list)
			list_destroy(sview_part_sub->node_ptr_list);
		xfree(sview_part_sub);
	}
}

/* like strcmp, but works with NULL pointers */
static int _strcmp(char *data1, char *data2) 
{
	static char null_str[] = "(null)";

	if (data1 == NULL)
		data1 = null_str;
	if (data2 == NULL)
		data2 = null_str;
	return strcmp(data1, data2);
}

/* 
 * _find_node - find a node by name
 * node_name IN     - name of node to locate
 * node_msg IN      - node information message from API
 */
static node_info_t *_find_node(char *node_name, node_info_msg_t *node_msg)
{
	int i;
	if (node_name == NULL)
		return NULL;

	for (i=0; i<node_msg->record_count; i++) {
		if (_strcmp(node_name, node_msg->node_array[i].name))
			continue;
		return &(node_msg->node_array[i]);
	}

	/* not found */
	return NULL;
}

static void _update_sview_part_sub(sview_part_sub_t *sview_part_sub, 
				   node_info_t *node_ptr, 
				   int node_scaling)
{
	list_append(sview_part_sub->node_ptr_list, node_ptr);

	if(!node_scaling)
		node_scaling = 1;
	
	if (sview_part_sub->node_cnt == 0) {	/* first node added */
		sview_part_sub->node_state = node_ptr->node_state;
		sview_part_sub->features   = xstrdup(node_ptr->features);
		sview_part_sub->reason     = xstrdup(node_ptr->reason);
		sview_part_sub->min_cpus   = node_ptr->cpus;
		sview_part_sub->max_cpus   = node_ptr->cpus;
		sview_part_sub->min_disk   = node_ptr->tmp_disk;
		sview_part_sub->max_disk   = node_ptr->tmp_disk;
		sview_part_sub->min_mem    = node_ptr->real_memory;
		sview_part_sub->max_mem    = node_ptr->real_memory;
		sview_part_sub->min_weight = node_ptr->weight;
		sview_part_sub->max_weight = node_ptr->weight;
	} else if (hostlist_find(sview_part_sub->hl, 
				 node_ptr->name) != -1) {
		/* we already have this node in this record,
		 * just return, don't duplicate */
		return;
	} else {
		if (sview_part_sub->min_cpus > node_ptr->cpus)
			sview_part_sub->min_cpus = node_ptr->cpus;
		if (sview_part_sub->max_cpus < node_ptr->cpus)
			sview_part_sub->max_cpus = node_ptr->cpus;

		if (sview_part_sub->min_disk > node_ptr->tmp_disk)
			sview_part_sub->min_disk = node_ptr->tmp_disk;
		if (sview_part_sub->max_disk < node_ptr->tmp_disk)
			sview_part_sub->max_disk = node_ptr->tmp_disk;

		if (sview_part_sub->min_mem > node_ptr->real_memory)
			sview_part_sub->min_mem = node_ptr->real_memory;
		if (sview_part_sub->max_mem < node_ptr->real_memory)
			sview_part_sub->max_mem = node_ptr->real_memory;

		if (sview_part_sub->min_weight> node_ptr->weight)
			sview_part_sub->min_weight = node_ptr->weight;
		if (sview_part_sub->max_weight < node_ptr->weight)
			sview_part_sub->max_weight = node_ptr->weight;
	}

	sview_part_sub->node_cnt += node_scaling;
	hostlist_push(sview_part_sub->hl, node_ptr->name);
}

/* 
 * _create_sview_part_sub - create an sview_part_sub record for 
 *                          the given partition
 * sview_part_sub OUT     - ptr to an inited sview_part_sub_t
 */
static sview_part_sub_t *_create_sview_part_sub(partition_info_t *part_ptr,
						node_info_t *node_ptr, 
						int node_scaling)
{
	sview_part_sub_t *sview_part_sub_ptr = 
		xmalloc(sizeof(sview_part_sub_t));
	
	if(!node_scaling)
		node_scaling = 1;

	if (!part_ptr) {
		g_print("got no part_ptr!\n");
		xfree(sview_part_sub_ptr);
		return NULL;
	}
	if (!node_ptr) {
		g_print("got no node_ptr!\n");
		xfree(sview_part_sub_ptr);
		return NULL;
	}
	sview_part_sub_ptr->part_ptr = part_ptr;
		
	sview_part_sub_ptr->node_state = node_ptr->node_state;
	sview_part_sub_ptr->node_cnt = node_scaling;
	
	sview_part_sub_ptr->min_cpus = node_ptr->cpus;
	sview_part_sub_ptr->max_cpus = node_ptr->cpus;

	sview_part_sub_ptr->min_disk = node_ptr->tmp_disk;
	sview_part_sub_ptr->max_disk = node_ptr->tmp_disk;

	sview_part_sub_ptr->min_mem = node_ptr->real_memory;
	sview_part_sub_ptr->max_mem = node_ptr->real_memory;

	sview_part_sub_ptr->min_weight = node_ptr->weight;
	sview_part_sub_ptr->max_weight = node_ptr->weight;

	sview_part_sub_ptr->features = xstrdup(node_ptr->features);
	sview_part_sub_ptr->reason   = xstrdup(node_ptr->reason);

	sview_part_sub_ptr->hl = hostlist_create(node_ptr->name);
	
	sview_part_sub_ptr->node_ptr_list = list_create(NULL);

	list_push(sview_part_sub_ptr->node_ptr_list, node_ptr);

	return sview_part_sub_ptr;
}

/* 
 * _create_sview_part_info - create an sview_part_info record for 
 *                           the given partition
 * part_ptr IN             - pointer to partition record to add
 * sview_part_info OUT     - ptr to an inited sview_part_info_t
 */
static sview_part_info_t *_create_sview_part_info(partition_info_t* part_ptr)
{
	sview_part_info_t *sview_part_info = 
		xmalloc(sizeof(sview_part_info_t));
		
	
	sview_part_info->part_ptr = part_ptr;
	sview_part_info->sub_list = list_create(_destroy_part_sub);
	return sview_part_info;
}

static List _create_info_list(partition_info_msg_t *part_info_ptr,
			      node_info_msg_t *node_info_ptr,
			      int changed)
{
	sview_part_info_t *sview_part_info = NULL;
	sview_part_sub_t *sview_part_sub = NULL;
	partition_info_t *part_ptr = NULL;
	node_info_t *node_ptr = NULL;
	static List info_list = NULL;
	char *node_name = NULL;
	int i, found = 0;
	ListIterator itr = NULL;
	hostlist_t hl;

	if(!changed && info_list) {
		return info_list;
	}
	
	if(info_list) {
		list_destroy(info_list);
	}
	info_list = list_create(_info_list_del);
	if (!info_list) {
		g_print("malloc error\n");
		return NULL;
	}
	for (i=0; i<part_info_ptr->record_count; i++) {
		part_ptr = &(part_info_ptr->partition_array[i]);
		sview_part_info = _create_sview_part_info(part_ptr);
		hl = hostlist_create(part_ptr->nodes);
		while((node_name = hostlist_shift(hl))) {
			node_ptr = _find_node(node_name, node_info_ptr);
			free(node_name);
			itr = list_iterator_create(sview_part_info->sub_list);
			while((sview_part_sub = list_next(itr))) {
				if(sview_part_sub->node_state
				   == node_ptr->node_state) {
					_update_sview_part_sub(
						sview_part_sub, 
						node_ptr,
						part_ptr->node_scaling);
					found = 1;
					break;
				}
			}
			list_iterator_destroy(itr);

			if(!found) {
				sview_part_sub = 
					_create_sview_part_sub(
						part_ptr,
						node_ptr,
						part_ptr->node_scaling);
				list_push(sview_part_info->sub_list, 
					  sview_part_sub);
			}

			found = 0;
		}
		hostlist_destroy(hl);
		list_append(info_list, sview_part_info);
	}
	return info_list;
}

void _display_info_part(partition_info_msg_t *part_info_ptr,
			popup_info_t *popup_win)
{
	specific_info_t *spec_info = popup_win->spec_info;
	char *name = (char *)spec_info->data;
	int i, found = 0;
	partition_info_t part;
	char *info = NULL;
	char *not_found = NULL;
	GtkWidget *label = NULL;
	
	if(!spec_info->data) {
		info = xstrdup("No pointer given!");
		goto finished;
	}

	if(spec_info->display_widget) {
		not_found = 
			xstrdup(GTK_LABEL(spec_info->display_widget)->text);
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
	}
	for (i = 0; i < part_info_ptr->record_count; i++) {
		part = part_info_ptr->partition_array[i];
		if (!part.nodes || (part.nodes[0] == '\0'))
			continue;	/* empty partition */
		if(!strcmp(part.name, name)) {
			if(!(info = slurm_sprint_partition_info(&part, 0))) {
				info = xmalloc(100);
				sprintf(info, 
					"Problem getting partition "
					"info for %s", 
					part.name);
			}
			found = 1;
			break;
		}
	}
	if(!found) {
		char *temp = "PARTITION DOESN'T EXSIST\n";
		if(!not_found || strncmp(temp, not_found, strlen(temp))) 
			info = xstrdup(temp);
		xstrcat(info, not_found);
	}
finished:
	label = gtk_label_new(info);
	xfree(info);
	xfree(not_found);
	gtk_table_attach_defaults(popup_win->table, label, 0, 1, 0, 1); 
	gtk_widget_show(label);	
	spec_info->display_widget = gtk_widget_ref(GTK_WIDGET(label));
	
	return;
}

extern void refresh_part(GtkAction *action, gpointer user_data)
{
	popup_info_t *popup_win = (popup_info_t *)user_data;
	xassert(popup_win != NULL);
	xassert(popup_win->spec_info != NULL);
	xassert(popup_win->spec_info->title != NULL);
	popup_win->force_refresh = 1;
	specific_info_part(popup_win);
}

extern int get_new_info_part(partition_info_msg_t **part_ptr, int force)
{
	static partition_info_msg_t *part_info_ptr = NULL;
	static partition_info_msg_t *new_part_ptr = NULL;
	int error_code = SLURM_SUCCESS;
	time_t now = time(NULL);
	static time_t last;
		
	if(!force && ((now - last) < global_sleep_time)) {
		*part_ptr = part_info_ptr;
		return error_code;
	}
	last = now;
	if (part_info_ptr) {
		error_code = slurm_load_partitions(part_info_ptr->last_update, 
						   &new_part_ptr, SHOW_ALL);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg(part_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_part_ptr = part_info_ptr;
		}
	} else {
		error_code = slurm_load_partitions((time_t) NULL, 
						   &new_part_ptr, SHOW_ALL);
	}
	
	part_info_ptr = new_part_ptr;
	*part_ptr = new_part_ptr;
	return error_code;
}

extern GtkListStore *create_model_part(int type)
{
	GtkListStore *model = NULL;
	GtkTreeIter iter;
	char *upper = NULL, *lower = NULL;		     
	int i=0;
	switch(type) {
	case SORTID_DEFAULT:
		model = gtk_list_store_new(1, G_TYPE_STRING,
					   G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "yes",
				   -1);	
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "no",
				   -1);	

		break;
	case SORTID_HIDDEN:
		model = gtk_list_store_new(1, G_TYPE_STRING);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "yes",
				   -1);	
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "no",
				   -1);	

		break;
	case SORTID_TIMELIMIT:
	case SORTID_MIN_NODES:
		break;
	case SORTID_MAX_NODES:
		break;
	case SORTID_ROOT:
		model = gtk_list_store_new(1, G_TYPE_STRING);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "yes",
				   -1);	
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "no",
				   -1);	
		break;
	case SORTID_SHARE:
		model = gtk_list_store_new(1, G_TYPE_STRING);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "yes",
				   -1);	
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "no",
				   -1);	
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "force",
				   -1);	
		break;
	case SORTID_GROUPS:
		break;
	case SORTID_NODELIST:
		break;
	case SORTID_AVAIL:
		model = gtk_list_store_new(1, G_TYPE_STRING);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "up",
				   -1);	
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "down",
				   -1);	
		break;
	case SORTID_STATE:
		model = gtk_list_store_new(1, G_TYPE_STRING,
					   G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "drain",
				   -1);	
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "resume",
				   -1);	
		for(i = 0; i < NODE_STATE_END; i++) {
			upper = node_state_string(i);
			if(!strcmp(upper, "UNKNOWN"))
				continue;
			
			gtk_list_store_append(model, &iter);
			lower = str_tolower(upper);
			gtk_list_store_set(model, &iter,
					   0, lower,
					   -1);
			xfree(lower);
		}
						
		break;

	}
	return model;
}

extern void admin_edit_part(GtkCellRendererText *cell,
			    const char *path_string,
			    const char *new_text,
			    gpointer data)
{
	GtkTreeStore *treestore = GTK_TREE_STORE(data);
	GtkTreePath *path = gtk_tree_path_new_from_string(path_string);
	GtkTreeIter iter;
	update_node_msg_t node_msg;
	update_part_msg_t part_msg;
	
	char *temp = NULL;
	char *type = NULL;
	int column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell), 
						       "column"));
	
	if(!new_text || !strcmp(new_text, ""))
		goto no_input;

	gtk_tree_model_get_iter(GTK_TREE_MODEL(treestore), &iter, path);

	if(column != SORTID_STATE) {
		slurm_init_part_desc_msg(&part_msg);
		gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter, 
				   SORTID_NAME, 
				   &part_msg.name, -1);
	}

	switch(column) {
	case SORTID_DEFAULT:
		if (!strcasecmp(new_text, "yes")) {
			part_msg.default_part = 1;
			temp = "*";
		} else {
			part_msg.default_part = 0;
			temp = "";
		}
		type = "default";
		break;
	case SORTID_HIDDEN:
		if (!strcasecmp(new_text, "yes")) {
			part_msg.hidden = 1;
			temp = "*";
		} else {
			part_msg.hidden = 0;
			temp = "";
		}
		type = "hidden";
		break;
	case SORTID_TIMELIMIT:
		if ((strcasecmp(new_text,"infinite") == 0))
			part_msg.max_time = INFINITE;
		else
			part_msg.max_time = 
				(uint32_t)strtol(new_text, (char **)NULL, 10);
		temp = (char *)new_text;
		type = "timelimit";
		if((int32_t)part_msg.max_time <= 0
		   && part_msg.max_time != INFINITE)
			goto print_error;
		break;
	case SORTID_MIN_NODES:
		part_msg.min_nodes = 
			(uint32_t)strtol(new_text, (char **)NULL, 10);
		
		temp = (char *)new_text;
		type = "min_nodes";
		if((int32_t)part_msg.max_time <= 0)
			goto print_error;
		break;
	case SORTID_MAX_NODES:
		if (!strcasecmp(new_text, "infinite")) {
			part_msg.max_nodes = INFINITE;
		} else {
			part_msg.max_nodes = 
				(uint32_t)strtol(new_text, (char **)NULL, 10);
		}
		temp = (char *)new_text;
		type = "max_nodes";
		if((int32_t)part_msg.max_time <= 0 
		   && part_msg.max_nodes != INFINITE)
			goto print_error;
		break;
	case SORTID_ROOT:
		if (!strcasecmp(new_text, "yes")) {
			part_msg.default_part = 1;
		} else {
			part_msg.default_part = 0;
		}
		temp = (char *)new_text;
		type = "root";
		break;
	case SORTID_SHARE:
		if (!strcasecmp(new_text, "yes")) {
			part_msg.default_part = SHARED_YES;
		} else if (!strcasecmp(new_text, "no")) {
			part_msg.default_part = SHARED_NO;
		} else {
			part_msg.default_part = SHARED_FORCE;
		}
		type = "share";
		break;
	case SORTID_GROUPS:
		type = "groups";
		break;
	case SORTID_NODELIST:
		temp = (char *)new_text;
		part_msg.nodes = temp;
		type = "nodelist";
		break;
	case SORTID_AVAIL:
		slurm_init_part_desc_msg(&part_msg);
		gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter, 
				   SORTID_NAME, 
				   &part_msg.name, -1);
		if (!strcasecmp(new_text, "up"))
			part_msg.state_up = 1;
		else
			part_msg.state_up = 0;
		temp = (char *)new_text;
		type = "availability";
		break;
	case SORTID_STATE:
		gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter, 
				   SORTID_NODELIST, 
				   &node_msg.node_names, -1);
		update_state_node(treestore, &iter, 
				  SORTID_STATE, SORTID_STATE_NUM,
				  new_text, &node_msg);
		g_free(node_msg.node_names);
		break;			
	}

	if(column != SORTID_STATE) {
		if(slurm_update_partition(&part_msg) == SLURM_SUCCESS) {
			gtk_tree_store_set(treestore, &iter, column, temp, -1);
			temp = g_strdup_printf("Partition %s %s changed to %s",
					       part_msg.name,
					       type,
					       new_text);
			display_edit_note(temp);
			g_free(temp);
		} else {
		print_error:
			temp = g_strdup_printf("Partition %s %s can't be "
					       "set to %s",
					       part_msg.name,
					       type,
					       new_text);
			display_edit_note(temp);
			g_free(temp);
		}
		g_free(part_msg.name);
		
	}
no_input:
	gtk_tree_path_free (path);
	
	g_static_mutex_unlock(&sview_mutex);
}

extern void get_info_part(GtkTable *table, display_data_t *display_data)
{
	int part_error_code = SLURM_SUCCESS;
	int node_error_code = SLURM_SUCCESS;
	static int view = -1;
	static partition_info_msg_t *part_info_ptr = NULL;
	static node_info_msg_t *node_info_ptr = NULL;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	List info_list = NULL;
	int changed = 1;

	if(display_data)
		local_display_data = display_data;
	if(!table) {
		display_data_part->set_menu = local_display_data->set_menu;
		return;
	}
	if(display_widget && toggled) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
		goto display_it;
	}
	
	if((part_error_code = get_new_info_part(&part_info_ptr, force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		goto get_node;
	}
	
	if (part_error_code != SLURM_SUCCESS) {
		if(view == ERROR_VIEW)
			goto end_it;
		if(display_widget)
			gtk_widget_destroy(display_widget);
		view = ERROR_VIEW;
		snprintf(error_char, 100, "slurm_load_partitions: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
		gtk_table_attach_defaults(table, label, 0, 1, 0, 1);
		gtk_widget_show(label);
		goto end_it;
	}

get_node:
	if((node_error_code = get_new_info_node(&node_info_ptr, force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		if((!display_widget || view == ERROR_VIEW)
		   || (part_error_code != SLURM_NO_CHANGE_IN_DATA))
			goto display_it;
		changed = 0;
		goto display_it;
	}

	if (node_error_code != SLURM_SUCCESS) {
		if(view == ERROR_VIEW)
			goto end_it;
		if(display_widget)
			gtk_widget_destroy(display_widget);
		view = ERROR_VIEW;
		snprintf(error_char, 100, "slurm_load_node: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
		gtk_table_attach_defaults(table, label, 0, 1, 0, 1);
		gtk_widget_show(label);
		goto end_it;
	}

display_it:

	info_list = _create_info_list(part_info_ptr, node_info_ptr, changed);
	if(!info_list)
		return;

	if(view == ERROR_VIEW && display_widget) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
	}
	if(!display_widget) {
		tree_view = create_treeview(local_display_data);

		display_widget = gtk_widget_ref(GTK_WIDGET(tree_view));
		gtk_table_attach_defaults(table,
					  GTK_WIDGET(tree_view),
					  0, 1, 0, 1);
		/* since this function sets the model of the tree_view 
		   to the treestore we don't really care about 
		   the return value */
		create_treestore(tree_view, display_data_part, SORTID_CNT);
	}
	view = INFO_VIEW;
	_update_info_part(info_list, GTK_TREE_VIEW(display_widget), NULL);
end_it:
	toggled = FALSE;
	force_refresh = 0;
	
	return;
}

extern void specific_info_part(popup_info_t *popup_win)
{
	int part_error_code = SLURM_SUCCESS;
	int node_error_code = SLURM_SUCCESS;
	static partition_info_msg_t *part_info_ptr = NULL;
	static node_info_msg_t *node_info_ptr = NULL;
	specific_info_t *spec_info = popup_win->spec_info;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	List info_list = NULL;
	int changed = 1;
	
	if(!spec_info->display_widget)
		setup_popup_info(popup_win, display_data_part, SORTID_CNT);
	
	if(spec_info->display_widget && popup_win->toggled) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
		goto display_it;
	}

	if((part_error_code = get_new_info_part(&part_info_ptr, 
						popup_win->force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA)  {
		goto get_node;
	}
		
	if (part_error_code != SLURM_SUCCESS) {
		if(spec_info->view == ERROR_VIEW)
			goto end_it;
		if(spec_info->display_widget) {
			gtk_widget_destroy(spec_info->display_widget);
			spec_info->display_widget = NULL;
		}
		spec_info->view = ERROR_VIEW;
		snprintf(error_char, 100, "slurm_load_partitions: %s",
			 slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		spec_info->display_widget = gtk_widget_ref(GTK_WIDGET(label));
		gtk_table_attach_defaults(popup_win->table, label, 0, 1, 0, 1);
		gtk_widget_show(label);			
		goto end_it;
	}
get_node:
	if((node_error_code = get_new_info_node(&node_info_ptr, 
						popup_win->force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		if((!spec_info->display_widget 
		    || spec_info->view == ERROR_VIEW)
		   || (part_error_code != SLURM_NO_CHANGE_IN_DATA))
			goto display_it;
		changed = 0;
		goto display_it;
	}

	if (node_error_code != SLURM_SUCCESS) {
		if(spec_info->view == ERROR_VIEW)
			goto end_it;
		if(spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		spec_info->view = ERROR_VIEW;
		snprintf(error_char, 100, "slurm_load_node: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		spec_info->display_widget = gtk_widget_ref(GTK_WIDGET(label));
		gtk_table_attach_defaults(popup_win->table, label, 0, 1, 0, 1);
		gtk_widget_show(label);
		goto end_it;
	}

display_it:	
	
	info_list = _create_info_list(part_info_ptr, node_info_ptr, changed);
	if(!info_list)
		return;		
	
	if(spec_info->view == ERROR_VIEW && spec_info->display_widget) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
	}
	
	if(spec_info->type != INFO_PAGE && !spec_info->display_widget) {
		tree_view = create_treeview(local_display_data);
		
		spec_info->display_widget = 
			gtk_widget_ref(GTK_WIDGET(tree_view));
		gtk_table_attach_defaults(popup_win->table,
					  GTK_WIDGET(tree_view),
					  0, 1, 0, 1);
		
		/* since this function sets the model of the tree_view 
		   to the treestore we don't really care about 
		   the return value */
		create_treestore(tree_view, popup_win->display_data, 
				 SORTID_CNT);
	}

	spec_info->view = INFO_VIEW;
	if(spec_info->type == INFO_PAGE) {
		_display_info_part(part_info_ptr, popup_win);
	} else {
		_update_info_part(info_list, 
				  GTK_TREE_VIEW(spec_info->display_widget), 
				  spec_info);
	}
	
end_it:
	popup_win->toggled = 0;
	popup_win->force_refresh = 0;
		
	return;
}

extern void set_menus_part(void *arg, GtkTreePath *path, 
			   GtkMenu *menu, int type)
{
	GtkTreeView *tree_view = (GtkTreeView *)arg;
	popup_info_t *popup_win = (popup_info_t *)arg;
	switch(type) {
	case TAB_CLICKED:
		make_fields_menu(menu, display_data_part);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_part);
		break;
	case POPUP_CLICKED:
		make_popup_fields_menu(popup_win, menu);
		break;
	default:
		g_error("UNKNOWN type %d given to set_fields\n", type);
	}
}

extern void popup_all_part(GtkTreeModel *model, GtkTreeIter *iter, int id)
{
	char *name = NULL;
	char title[100];
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	GError *error = NULL;
					
	gtk_tree_model_get(model, iter, SORTID_NAME, &name, -1);
	
	switch(id) {
	case JOB_PAGE:
		snprintf(title, 100, "Job(s) in partition %s", name);
		break;
	case NODE_PAGE:
#ifdef HAVE_BG
		snprintf(title, 100, 
			 "Base partition(s) in partition %s", name);
#else
		snprintf(title, 100, "Node(s) in partition %s", name);
#endif
		break;
	case BLOCK_PAGE: 
		snprintf(title, 100, "Block(s) in partition %s", name);
		break;
	case SUBMIT_PAGE: 
		snprintf(title, 100, "Submit job in partition %s", name);
		break;
	case INFO_PAGE: 
		snprintf(title, 100, "Full info for partition %s", name);
		break;
	default:
		g_print("part got %d\n", id);
	}
	
	itr = list_iterator_create(popup_list);
	while((popup_win = list_next(itr))) {
		if(popup_win->spec_info)
			if(!strcmp(popup_win->spec_info->title, title)) {
				break;
			} 
	}
	list_iterator_destroy(itr);

	if(!popup_win) {
		if(id == INFO_PAGE)
			popup_win = create_popup_info(id, PART_PAGE, title);
		else
			popup_win = create_popup_info(PART_PAGE, id, title);
	}
	switch(id) {
	case JOB_PAGE:
		popup_win->spec_info->data = name;
		//specific_info_job(popup_win);
		break;
	case NODE_PAGE:
		g_free(name);
		gtk_tree_model_get(model, iter, SORTID_NODELIST, &name, -1);
		popup_win->spec_info->data = name;
		//specific_info_node(popup_win);
		break;
	case BLOCK_PAGE: 
		popup_win->spec_info->data = name;
		break;
	case SUBMIT_PAGE: 
		break;
	case INFO_PAGE:
		popup_win->spec_info->data = name;
		break;
	default:
		g_print("part got %d\n", id);
	}
	if (!g_thread_create((gpointer)popup_thr, popup_win, FALSE, &error))
	{
		g_printerr ("Failed to create part popup thread: %s\n", 
			    error->message);
		return;
	}		
}
