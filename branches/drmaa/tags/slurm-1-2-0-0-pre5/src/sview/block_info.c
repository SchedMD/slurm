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
#include "src/common/node_select.h"
#include "src/api/node_select_info.h"

#define _DEBUG 0
DEF_TIMERS;

typedef struct {
	char *bg_user_name;
	char *bg_block_name;
	char *slurm_part_name;
	char *nodes;
	enum connection_type bg_conn_type;
	enum node_use_type bg_node_use;
	rm_partition_state_t state;
	int size;
	uint16_t quarter;	
	uint16_t nodecard;	
	int node_cnt;	
	int *bp_inx;            /* list index pairs into node_table for *nodes:
				 * start_range_1, end_range_1,
				 * start_range_2, .., -1  */
	bool printed;
	char *color;
} sview_block_info_t;

enum { 
	SORTID_POS = POS_LOC,
	SORTID_BLOCK,
	SORTID_NODES, 
	SORTID_NODELIST, 
	SORTID_STATE,
	SORTID_USER,
	SORTID_CONN,
	SORTID_USE,
	SORTID_PARTITION, 
	SORTID_POINTER,
	SORTID_UPDATED, 
	SORTID_CNT
};

static display_data_t display_data_block[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, -1, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_BLOCK, "Bluegene Block", 
	 TRUE, -1, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_NODES, "Nodes", TRUE, -1, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_NODELIST, "BP List", TRUE, -1, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_STATE, "State", TRUE, -1, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_USER, "User", TRUE, -1, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_CONN, "Connection Type", 
	 TRUE, -1, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_USE, "Node Use", TRUE, -1, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_PARTITION, "Partition", 
	 TRUE, -1, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_POINTER, SORTID_POINTER, NULL, FALSE, -1, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_INT, SORTID_UPDATED, NULL, FALSE, -1, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_NONE, -1, NULL, FALSE, -1}
};

static display_data_t options_data_block[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, -1},
	{G_TYPE_STRING, JOB_PAGE, "Jobs", TRUE, BLOCK_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Partition", TRUE, BLOCK_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Base Partitions", TRUE, BLOCK_PAGE},
	{G_TYPE_STRING, SUBMIT_PAGE, "Job Submit", TRUE, BLOCK_PAGE},
	{G_TYPE_NONE, -1, NULL, FALSE, -1}
};

static display_data_t *local_display_data = NULL;

static char* _convert_conn_type(enum connection_type conn_type);
static char* _convert_node_use(enum node_use_type node_use);
static char *_part_state_str(rm_partition_state_t state);
static int _in_slurm_partition(int *part_inx, int *block_inx);
static void _append_block_record(sview_block_info_t *block_ptr,
				 GtkTreeStore *treestore, GtkTreeIter *iter, 
				 int line);



static char *_part_state_str(rm_partition_state_t state)
{
	static char tmp[16];

#ifdef HAVE_BG
	switch (state) {
		case RM_PARTITION_BUSY: 
			return "BUSY";
		case RM_PARTITION_CONFIGURING:
			return "CONFIG";
		case RM_PARTITION_DEALLOCATING:
			return "DEALLOC";
		case RM_PARTITION_ERROR:
			return "ERROR";
		case RM_PARTITION_FREE:
			return "FREE";
		case RM_PARTITION_NAV:
			return "NAV";
		case RM_PARTITION_READY:
			return "READY";
	}
#endif

	snprintf(tmp, sizeof(tmp), "%d", state);
	return tmp;
}

static void _block_list_del(void *object)
{
	sview_block_info_t *block_ptr = (sview_block_info_t *)object;

	if (block_ptr) {
		xfree(block_ptr->bg_user_name);
		xfree(block_ptr->bg_block_name);
		xfree(block_ptr->slurm_part_name);
		xfree(block_ptr->nodes);
		
		xfree(block_ptr);
		
	}
}

static int _in_slurm_partition(int *part_inx, int *bp_inx)
{
	int found = 0;
	int i=0, j=0;
		
	while(bp_inx[i] >= 0) {
		j = 0;
		found = 0;
		while(part_inx[j] >= 0) {
			if((bp_inx[i] >= part_inx[j])
			   && bp_inx[i+1] <= part_inx[j+1]) {
				found = 1;
				break;
			}
			j += 2;
		}
		if(!found)
			return 0;
		i += 2;
	}
	
	return 1;
}


static char* _convert_conn_type(enum connection_type conn_type)
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
	}
	return "?";
}

static char* _convert_node_use(enum node_use_type node_use)
{
	switch (node_use) {
	case (SELECT_COPROCESSOR_MODE):
		return "COPROCESSOR";
	case (SELECT_VIRTUAL_NODE_MODE):
		return "VIRTUAL";
	case (SELECT_NAV_MODE):
		return "NAV";
	}
	return "?";
}

static void _update_block_record(sview_block_info_t *block_ptr, 
				 GtkTreeStore *treestore, GtkTreeIter *iter)
{
	char *nodes = NULL;
	char tmp_cnt[7];
	char tmp_nodes[30];
	
	gtk_tree_store_set(treestore, iter, SORTID_POINTER, block_ptr, -1);
	gtk_tree_store_set(treestore, iter, SORTID_BLOCK, 
			   block_ptr->bg_block_name, -1);
	gtk_tree_store_set(treestore, iter, SORTID_PARTITION, 
			   block_ptr->slurm_part_name, -1);
	gtk_tree_store_set(treestore, iter, SORTID_STATE, 
			   _part_state_str(block_ptr->state), -1);
	gtk_tree_store_set(treestore, iter, SORTID_USER, 
			   block_ptr->bg_user_name, -1);
	gtk_tree_store_set(treestore, iter, SORTID_CONN, 
			   _convert_conn_type(block_ptr->bg_conn_type), -1);
	gtk_tree_store_set(treestore, iter, SORTID_USE, 
			   _convert_node_use(block_ptr->bg_node_use), -1);
	
	convert_num_unit((float)block_ptr->node_cnt, tmp_cnt, UNIT_NONE);
	gtk_tree_store_set(treestore, iter, SORTID_NODES, tmp_cnt, -1);

	nodes = block_ptr->nodes;			
	if(block_ptr && (block_ptr->quarter != (uint16_t) NO_VAL)) {
		if(block_ptr->nodecard != (uint16_t) NO_VAL)
			sprintf(tmp_nodes, "%s.%d.%d", nodes,
				block_ptr->quarter,
				block_ptr->nodecard);
		else
			sprintf(tmp_nodes, "%s.%d", nodes,
				block_ptr->quarter);
		nodes = tmp_nodes;
	} 
	gtk_tree_store_set(treestore, iter, SORTID_NODELIST, nodes, -1);

	gtk_tree_store_set(treestore, iter, SORTID_UPDATED, 1, -1);
	
	return;
}
	
static void _append_block_record(sview_block_info_t *block_ptr,
				 GtkTreeStore *treestore, GtkTreeIter *iter,
				 int line)
{
	gtk_tree_store_append(treestore, iter, NULL);
	gtk_tree_store_set(treestore, iter, SORTID_POS, line, -1);
	_update_block_record(block_ptr, treestore, iter);
}

static void _update_info_block(List block_list, 
			       GtkTreeView *tree_view)
{
	ListIterator itr;
	sview_block_info_t *block_ptr = NULL;
	GtkTreePath *path = gtk_tree_path_new_first();
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	GtkTreeIter iter;
	char *name = NULL;
	char *host = NULL;
	int line = 0;

	if (!block_list) {
		g_print("No block_list given");
		return;
	}

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


 	/* Report the BG Blocks */
	
	itr = list_iterator_create(block_list);
	while ((block_ptr = (sview_block_info_t*) list_next(itr))) {
		if(block_ptr->node_cnt == 0)
			block_ptr->node_cnt = block_ptr->size;
		if(!block_ptr->slurm_part_name)
			block_ptr->slurm_part_name = "no part";
		
		/* get the iter, or find out the list is empty goto add */
		if (!gtk_tree_model_get_iter(model, &iter, path)) {
			goto adding;
		}
		while(1) {
			/* search for the jobid and check to see if 
			   it is in the list */
			gtk_tree_model_get(model, &iter, SORTID_BLOCK, 
					   &name, -1);
			if(!strcmp(name, block_ptr->bg_block_name)) {
				/* update with new info */
				g_free(name);
				_update_block_record(block_ptr, 
						     GTK_TREE_STORE(model), 
						     &iter);
				goto found;
			}
			g_free(name);
			
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
		_append_block_record(block_ptr, GTK_TREE_STORE(model), 
				     &iter, line);
	found:
		;
	}
		
	list_iterator_destroy(itr);
	if(host)
		free(host);

	gtk_tree_path_free(path);
	/* remove all old blocks */
	remove_old(model, SORTID_UPDATED);
}

static List _create_block_list(partition_info_msg_t *part_info_ptr,
			       node_select_info_msg_t *node_select_ptr,
			       int changed)
{
	int i, j;
	static List block_list = NULL;
	partition_info_t part;
	sview_block_info_t *block_ptr = NULL;
	
	if(!changed && block_list) {
		return block_list;
	}
	
	if(block_list) {
		list_destroy(block_list);
	}
	block_list = list_create(_block_list_del);
	if (!block_list) {
		g_print("malloc error\n");
		return NULL;
	}
	for (i=0; i<node_select_ptr->record_count; i++) {
		block_ptr = xmalloc(sizeof(sview_block_info_t));
			
		block_ptr->bg_block_name 
			= xstrdup(node_select_ptr->
				  bg_info_array[i].bg_block_id);
		block_ptr->nodes 
			= xstrdup(node_select_ptr->bg_info_array[i].nodes);
		
		block_ptr->bg_user_name 
			= xstrdup(node_select_ptr->
				  bg_info_array[i].owner_name);
		block_ptr->state 
			= node_select_ptr->bg_info_array[i].state;
		block_ptr->bg_conn_type 
			= node_select_ptr->bg_info_array[i].conn_type;
		block_ptr->bg_node_use 
			= node_select_ptr->bg_info_array[i].node_use;
		block_ptr->quarter 
			= node_select_ptr->bg_info_array[i].quarter;
		block_ptr->nodecard 
			= node_select_ptr->bg_info_array[i].nodecard;
		block_ptr->node_cnt 
			= node_select_ptr->bg_info_array[i].node_cnt;
		block_ptr->bp_inx 
			= node_select_ptr->bg_info_array[i].bp_inx;
		for(j = 0; j < part_info_ptr->record_count; j++) {
			part = part_info_ptr->partition_array[j];
			if(_in_slurm_partition(part.node_inx,
					       block_ptr->bp_inx)) {
				block_ptr->slurm_part_name 
					= xstrdup(part.name);
				break;
			}
		}
		if(block_ptr->bg_conn_type == SELECT_SMALL)
			block_ptr->size = 0;

		list_append(block_list, block_ptr);
	}
	
	return block_list;
}
void _display_info_block(List block_list,
			 popup_info_t *popup_win)
{
	specific_info_t *spec_info = popup_win->spec_info;
	/* char *name = (char *)spec_info->data; */
/* 	int i, found = 0; */
/* 	sview_block_info_t *block_ptr = NULL; */
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
	/* this is here for if later we have more stats on a bluegene
	   block */
	/* itr = list_iterator_create(block_list); */
/* 	while ((block_ptr = (sview_block_info_t*) list_next(itr))) { */
/* 		if(!strcmp(block_ptr->bg_block_name, name)) { */
/* 			if(!(info = slurm_sprint_block_info(&block_ptr, 0))) { */
/* 				info = xmalloc(100); */
/* 				sprintf(info,  */
/* 					"Problem getting block " */
/* 					"info for %s",  */
/* 					block_ptr->bg_block_name); */
/* 			} */
/* 			found = 1; */
/* 			break; */
/* 		} */
/* 	} */
/* 	if(!found) { */
/* 		char *temp = "BLOCK DOESN'T EXSIST\n"; */
/* 		if(!not_found || strncmp(temp, not_found, strlen(temp)))  */
/* 			info = xstrdup(temp); */
/* 		xstrcat(info, not_found); */
/* 	} */
	info = xstrdup("No extra info avaliable.");
finished:
	label = gtk_label_new(info);
	xfree(info);
	xfree(not_found);
	gtk_table_attach_defaults(popup_win->table, label, 0, 1, 0, 1); 
	gtk_widget_show(label);	
	spec_info->display_widget = gtk_widget_ref(GTK_WIDGET(label));
	
	return;
}
	
extern void refresh_block(GtkAction *action, gpointer user_data)
{
	popup_info_t *popup_win = (popup_info_t *)user_data;
	xassert(popup_win != NULL);
	xassert(popup_win->spec_info != NULL);
	xassert(popup_win->spec_info->title != NULL);
	popup_win->force_refresh = 1;
	specific_info_block(popup_win);
}

extern int get_new_info_node_select(node_select_info_msg_t **node_select_ptr,
				    int force)
{
	static node_select_info_msg_t *bg_info_ptr = NULL;
	static node_select_info_msg_t *new_bg_ptr = NULL;
	int error_code = SLURM_SUCCESS;
	time_t now = time(NULL);
	static time_t last;
		
	if(!force && ((now - last) < global_sleep_time)) {
		*node_select_ptr = bg_info_ptr;
		return error_code;
	}
	last = now;
	if (bg_info_ptr) {
		error_code = slurm_load_node_select(bg_info_ptr->last_update, 
						    &new_bg_ptr);
		if (error_code == SLURM_SUCCESS)
			select_g_free_node_info(&bg_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_bg_ptr = bg_info_ptr;
		}
	} else {
		error_code = slurm_load_node_select((time_t) NULL, 
						    &new_bg_ptr);
	}

	bg_info_ptr = new_bg_ptr;
	*node_select_ptr = new_bg_ptr;
	return error_code;
}

extern GtkListStore *create_model_block(int type)
{
	GtkListStore *model = NULL;
	
	return model;
}

extern void admin_edit_block(GtkCellRendererText *cell,
			     const char *path_string,
			     const char *new_text,
			     gpointer data)
{
	g_print("Something block related altered\n");
	g_static_mutex_unlock(&sview_mutex);
}

extern void get_info_block(GtkTable *table, display_data_t *display_data)
{
	int part_error_code = SLURM_SUCCESS;
	int block_error_code = SLURM_SUCCESS;
	static int view = -1;
	static partition_info_msg_t *part_info_ptr = NULL;
	static node_select_info_msg_t *node_select_ptr = NULL;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	List block_list = NULL;
	int changed = 1;
	int i=0, j=0;
	ListIterator itr = NULL;
	sview_block_info_t *sview_block_info_ptr = NULL;

	if(display_data)
		local_display_data = display_data;
	if(!table) {
		display_data_block->set_menu = local_display_data->set_menu;
		return;
	}
	if(display_widget && toggled) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
		goto display_it;
	}
	
	if((part_error_code = get_new_info_part(&part_info_ptr, force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		goto get_node_select;
	}
		
	if (part_error_code != SLURM_SUCCESS) {
		if(view == ERROR_VIEW)
			goto end_it;
		view = ERROR_VIEW;
		if(display_widget)
			gtk_widget_destroy(display_widget);
		sprintf(error_char, "slurm_load_partitions: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(GTK_TABLE(table), 
					  label,
					  0, 1, 0, 1); 
		gtk_widget_show(label);	
		display_widget = gtk_widget_ref(label);
		goto end_it;
	}

get_node_select:
	if((block_error_code = get_new_info_node_select(&node_select_ptr, 
							force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		if((!display_widget || view == ERROR_VIEW) 
		   || (part_error_code != SLURM_NO_CHANGE_IN_DATA))
			goto display_it;
		changed = 0;
		goto display_it;
	}

	if (block_error_code != SLURM_SUCCESS) {
		if(view == ERROR_VIEW)
			goto end_it;
		view = ERROR_VIEW;
		if(display_widget)
			gtk_widget_destroy(display_widget);
		sprintf(error_char, "slurm_load_node_select: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(table, 
					  label,
					  0, 1, 0, 1); 
		gtk_widget_show(label);	
		display_widget = gtk_widget_ref(label);
		goto end_it;
	}
		
display_it:
	
	block_list = _create_block_list(part_info_ptr, node_select_ptr,
					changed);
	if(!block_list)
		return;
	i=0;
	/* set up the grid */
	itr = list_iterator_create(block_list);
	while ((sview_block_info_ptr = list_next(itr))) {
		j=0;
		while(sview_block_info_ptr->bp_inx[j] >= 0) {
			sview_block_info_ptr->color =
				change_grid_color(grid_button_list,
						  sview_block_info_ptr->
						  bp_inx[j],
						  sview_block_info_ptr->
						  bp_inx[j+1],
						  i);
			j += 2;
		}
		i++;
	}
	list_iterator_destroy(itr);

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
		create_treestore(tree_view, display_data_block, SORTID_CNT);
	}
	view = INFO_VIEW;
	_update_info_block(block_list, GTK_TREE_VIEW(display_widget));
end_it:
	toggled = FALSE;
	force_refresh = FALSE;
	
	return;
}

extern void specific_info_block(popup_info_t *popup_win)
{
	int part_error_code = SLURM_SUCCESS;
	int block_error_code = SLURM_SUCCESS;
	static partition_info_msg_t *part_info_ptr = NULL;
	static node_select_info_msg_t *node_select_ptr = NULL;
	specific_info_t *spec_info = popup_win->spec_info;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	List block_list = NULL;
	List send_block_list = NULL;
	int changed = 1;
	sview_block_info_t *block_ptr = NULL;
	int j=0, i=-1;
	char *host = NULL, *host2 = NULL;
	hostlist_t hostlist = NULL;
	int found = 0;
	ListIterator itr = NULL;
	
	if(!spec_info->display_widget) {
		setup_popup_info(popup_win, display_data_block, SORTID_CNT);
	}

	if(spec_info->display_widget && popup_win->toggled) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
		goto display_it;
	}
	
	if((part_error_code = get_new_info_part(&part_info_ptr, 
						popup_win->force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		goto get_node_select;
	}
	
	if (part_error_code != SLURM_SUCCESS) {
		if(spec_info->view == ERROR_VIEW)
			goto end_it;
		spec_info->view = ERROR_VIEW;
		if(spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		sprintf(error_char, "slurm_load_partitions: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(popup_win->table, 
					  label,
					  0, 1, 0, 1); 
		gtk_widget_show(label);	
		spec_info->display_widget = gtk_widget_ref(label);
		goto end_it;
	}

get_node_select:
	if((block_error_code = 
	    get_new_info_node_select(&node_select_ptr,
				     popup_win->force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		if((!spec_info->display_widget 
		    || spec_info->view == ERROR_VIEW) 
		   || (part_error_code != SLURM_NO_CHANGE_IN_DATA)) {
			goto display_it;
		}
		changed = 0;
		goto display_it;
	}
	
	if (block_error_code != SLURM_SUCCESS) {
		if(spec_info->view == ERROR_VIEW)
			goto end_it;
		spec_info->view = ERROR_VIEW;
		if(spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		sprintf(error_char, "slurm_load_node_select: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(popup_win->table, 
					  label,
					  0, 1, 0, 1); 
		gtk_widget_show(label);	
		spec_info->display_widget = gtk_widget_ref(label);
		goto end_it;
	}
	
display_it:
	
	block_list = _create_block_list(part_info_ptr, node_select_ptr,
					changed);
	if(!block_list)
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
		create_treestore(tree_view, 
				 popup_win->display_data, SORTID_CNT);
	}

	if(!popup_win->grid_button_list) {
		popup_win->grid_button_list = copy_main_button_list();
		put_buttons_in_table(popup_win->grid_table,
				     popup_win->grid_button_list);
	}
	spec_info->view = INFO_VIEW;
	if(spec_info->type == INFO_PAGE) {
		_display_info_block(block_list, popup_win);
		goto end_it;
	}

	/* just linking to another list, don't free the inside, just
	   the list */
	send_block_list = list_create(NULL);	
	itr = list_iterator_create(block_list);
	i = -1;
	while ((block_ptr = list_next(itr))) {
		i++;
		switch(spec_info->type) {
		case PART_PAGE:
			if(strcmp(block_ptr->slurm_part_name, 
				  (char *)spec_info->data)) 
				continue;
			break;
		case NODE_PAGE:
			if(!block_ptr->nodes)
				continue;
			
			hostlist = hostlist_create(
				(char *)spec_info->data);
			host = hostlist_shift(hostlist);
			hostlist_destroy(hostlist);
			if(!host) 
				continue;

			hostlist = hostlist_create(block_ptr->nodes);
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
		case BLOCK_PAGE:
		case JOB_PAGE:
			if(strcmp(block_ptr->bg_block_name, 
				  (char *)spec_info->data)) 
				continue;
			break;
		default:
			g_print("Unkown type %d\n", spec_info->type);
			continue;
		}
		list_push(send_block_list, block_ptr);
		j=0;
		while(block_ptr->bp_inx[j] >= 0) {
			change_grid_color(
				popup_win->grid_button_list,
				block_ptr->bp_inx[j],
				block_ptr->bp_inx[j+1], i);
			j += 2;
		}
	}
	list_iterator_destroy(itr);
	 
	_update_info_block(send_block_list, 
			   GTK_TREE_VIEW(spec_info->display_widget));
	list_destroy(send_block_list);
end_it:
	popup_win->toggled = 0;
	popup_win->force_refresh = 0;
	
	return;
}

extern void set_menus_block(void *arg, GtkTreePath *path, 
			    GtkMenu *menu, int type)
{
	GtkTreeView *tree_view = (GtkTreeView *)arg;
	popup_info_t *popup_win = (popup_info_t *)arg;
	switch(type) {
	case TAB_CLICKED:
		make_fields_menu(menu, display_data_block);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_block);
		break;
	case POPUP_CLICKED:
		make_popup_fields_menu(popup_win, menu);
		break;
	default:
		g_error("UNKNOWN type %d given to set_fields\n", type);
	}
}

extern void popup_all_block(GtkTreeModel *model, GtkTreeIter *iter, int id)
{
	char *name = NULL;
	char title[100];
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	GError *error = NULL;
	int i=0;

	gtk_tree_model_get(model, iter, SORTID_BLOCK, &name, -1);
	switch(id) {
	case JOB_PAGE:
		snprintf(title, 100, "Jobs(s) in block %s", name);
		break;
	case PART_PAGE:
		snprintf(title, 100, "Partition(s) containing block %s", name);
		break;
	case NODE_PAGE:
		snprintf(title, 100, "Base Partition(s) in block %s", name);
		break;
	case SUBMIT_PAGE: 
		snprintf(title, 100, "Submit job on %s", name);
		break;
	case INFO_PAGE: 
		snprintf(title, 100, "Full info for block %s", name);
		break;
	default:
		g_print("Block got %d\n", id);
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
			popup_win = create_popup_info(id, BLOCK_PAGE, title);
		else
			popup_win = create_popup_info(BLOCK_PAGE, id, title);
	} else {
		g_free(name);
		gtk_window_present(GTK_WINDOW(popup_win->popup));
		return;
	}
	switch(id) {
	case JOB_PAGE:
		popup_win->spec_info->data = name;
		break;
	case PART_PAGE:
		g_free(name);
		gtk_tree_model_get(model, iter, SORTID_PARTITION, &name, -1);
		popup_win->spec_info->data = name;
		break;
	case NODE_PAGE: 
		g_free(name);
		gtk_tree_model_get(model, iter, SORTID_NODELIST, &name, -1);
		/* strip off the quarter and nodecard part */
		while(name[i]) {
			if(name[i] == '.') {
				name[i] = '\0';
				break;
			}
			i++;
		}
		popup_win->spec_info->data = name;
		break;
	case INFO_PAGE:
		popup_win->spec_info->data = name;
		break;
	default:
		g_print("block got %d\n", id);
	}

	
	if (!g_thread_create((gpointer)popup_thr, popup_win, FALSE, &error))
	{
		g_printerr ("Failed to create part popup thread: %s\n", 
			    error->message);
		return;
	}
}
