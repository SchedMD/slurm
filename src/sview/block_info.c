/*****************************************************************************\
 *  block_info.c - Functions related to Bluegene block display 
 *  mode of sview.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 * 
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

typedef struct {
	char *bg_user_name;
	char *bg_block_name;
	char *slurm_part_name;
	char *nodes;
	enum connection_type bg_conn_type;
#ifdef HAVE_BGL
	enum node_use_type bg_node_use;
#endif
	rm_partition_state_t state;
	int size;
	int node_cnt;	
	int *bp_inx;            /* list index pairs into node_table for *nodes:
				 * start_range_1, end_range_1,
				 * start_range_2, .., -1  */
	bool printed;
	char *color;
#ifdef HAVE_BGL
	char *blrtsimage;       /* BlrtsImage for this block */
#endif
	char *linuximage;       /* LinuxImage for this block */
	char *mloaderimage;     /* mloaderImage for this block */
	char *ramdiskimage;     /* RamDiskImage for this block */
} sview_block_info_t;

enum { 
	SORTID_POS = POS_LOC,
	SORTID_BLOCK,
#ifdef HAVE_BGL
	SORTID_BLRTSIMAGE,
#endif
	SORTID_CONN,
	SORTID_LINUXIMAGE,
	SORTID_MLOADERIMAGE,
	SORTID_NODES, 
	SORTID_NODELIST, 
	SORTID_PARTITION, 
	SORTID_RAMDISKIMAGE,
	SORTID_STATE,
	SORTID_UPDATED, 
#ifdef HAVE_BGL
	SORTID_USE,
#endif
	SORTID_USER,
	SORTID_CNT
};

static display_data_t display_data_block[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_BLOCK, "Bluegene Block", 
	 TRUE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_STATE, "State", TRUE, EDIT_MODEL, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_USER, "User", TRUE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_NODES, "Nodes", TRUE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_CONN, "Connection Type", 
	 TRUE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_NODELIST, "BP List", TRUE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_PARTITION, "Partition", 
	 TRUE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
#ifdef HAVE_BGL
	{G_TYPE_STRING, SORTID_USE, "Node Use", TRUE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_BLRTSIMAGE, "Blrts Image",
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_LINUXIMAGE, "linux Image",
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_RAMDISKIMAGE, "Ramdisk Image",
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
#else
	{G_TYPE_STRING, SORTID_LINUXIMAGE, "Cnload Image",
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_RAMDISKIMAGE, "Ioload Image",
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
#endif
	{G_TYPE_STRING, SORTID_MLOADERIMAGE, "Mloader Image",
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
	{G_TYPE_INT, SORTID_UPDATED, NULL, FALSE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_NONE, -1, NULL, FALSE, EDIT_NONE}
};

static display_data_t options_data_block[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, EDIT_NONE},
	{G_TYPE_STRING, INFO_PAGE, "Full Info", TRUE, BLOCK_PAGE},
	{G_TYPE_STRING, BLOCK_PAGE, "Put block in error state",
	 TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, BLOCK_PAGE, "Put block in free state",
	 TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Jobs", TRUE, BLOCK_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Partition", TRUE, BLOCK_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Base Partitions", TRUE, BLOCK_PAGE},
	{G_TYPE_STRING, SUBMIT_PAGE, "Job Submit", FALSE, BLOCK_PAGE},
	{G_TYPE_STRING, RESV_PAGE, "Reservation", TRUE, BLOCK_PAGE},
	{G_TYPE_NONE, -1, NULL, FALSE, EDIT_NONE}
};

static display_data_t *local_display_data = NULL;

static char* _convert_conn_type(enum connection_type conn_type);
#ifdef HAVE_BGL
static char* _convert_node_use(enum node_use_type node_use);
#endif
static int _in_slurm_partition(int *part_inx, int *block_inx);
static void _append_block_record(sview_block_info_t *block_ptr,
				 GtkTreeStore *treestore, GtkTreeIter *iter, 
				 int line);

static void _block_list_del(void *object)
{
	sview_block_info_t *block_ptr = (sview_block_info_t *)object;

	if (block_ptr) {
		xfree(block_ptr->bg_user_name);
		xfree(block_ptr->bg_block_name);
		xfree(block_ptr->slurm_part_name);
		xfree(block_ptr->nodes);
#ifdef HAVE_BGL
		xfree(block_ptr->blrtsimage);
#endif
		xfree(block_ptr->linuximage);
		xfree(block_ptr->mloaderimage);
		xfree(block_ptr->ramdiskimage);
		/* don't xfree(block_ptr->bp_inx);
		   it isn't copied like the chars and is freed in the api
		 */
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
#ifdef HAVE_BG
	switch (conn_type) {
	case (SELECT_MESH):
		return "MESH";
	case (SELECT_TORUS):
		return "TORUS";
	case (SELECT_SMALL):
		return "SMALL";
	case (SELECT_NAV):
		return "NAV";
#ifndef HAVE_BGL
	case SELECT_HTC_S:
		return "HTC_S";
		break;
	case SELECT_HTC_D:
		return "HTC_D";
		break;
	case SELECT_HTC_V:
		return "HTC_V";
		break;
	case SELECT_HTC_L:
		return "HTC_L";
		break;
#endif
	default:
		return "?";
	}
#endif
	return "?";
}

#ifdef HAVE_BGL
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
#endif

static void _layout_block_record(GtkTreeView *treeview,
				 sview_block_info_t *block_ptr, 
				 int update)
{
	char tmp_cnt[8];
	GtkTreeIter iter;
	GtkTreeStore *treestore = 
		GTK_TREE_STORE(gtk_tree_view_get_model(treeview));
	
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_block,
						 SORTID_BLOCK),
				   block_ptr->bg_block_name);
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_block,
						 SORTID_PARTITION),
				   block_ptr->slurm_part_name);
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_block, 
						 SORTID_STATE),
				   bg_block_state_string(block_ptr->state));
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_block,
						 SORTID_USER),
				   block_ptr->bg_user_name);
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_block,
						 SORTID_CONN),
				   _convert_conn_type(
					   block_ptr->bg_conn_type));
#ifdef HAVE_BGL
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_block,
						 SORTID_USE),
				   _convert_node_use(block_ptr->bg_node_use));
#endif	
	convert_num_unit((float)block_ptr->node_cnt, tmp_cnt, sizeof(tmp_cnt),
			 UNIT_NONE);
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_block,
						 SORTID_NODES),
				   tmp_cnt);
		
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_block,
						 SORTID_NODELIST),
				   block_ptr->nodes);
#ifdef HAVE_BGL
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_block,
						 SORTID_BLRTSIMAGE),
				   block_ptr->blrtsimage);
#endif
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_block,
						 SORTID_LINUXIMAGE),
				   block_ptr->linuximage);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_block,
						 SORTID_MLOADERIMAGE),
				   block_ptr->mloaderimage);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_block,
						 SORTID_RAMDISKIMAGE),
				   block_ptr->ramdiskimage);
}

static void _update_block_record(sview_block_info_t *block_ptr, 
				 GtkTreeStore *treestore, GtkTreeIter *iter)
{
	char tmp_cnt[8];
	
	gtk_tree_store_set(treestore, iter, SORTID_BLOCK, 
			   block_ptr->bg_block_name, -1);
	gtk_tree_store_set(treestore, iter, SORTID_PARTITION, 
			   block_ptr->slurm_part_name, -1);
	gtk_tree_store_set(treestore, iter, SORTID_STATE, 
			   bg_block_state_string(block_ptr->state), -1);
	gtk_tree_store_set(treestore, iter, SORTID_USER, 
			   block_ptr->bg_user_name, -1);
	gtk_tree_store_set(treestore, iter, SORTID_CONN, 
			   _convert_conn_type(block_ptr->bg_conn_type), -1);
#ifdef HAVE_BGL
	gtk_tree_store_set(treestore, iter, SORTID_USE, 
			   _convert_node_use(block_ptr->bg_node_use), -1);
#endif	
	convert_num_unit((float)block_ptr->node_cnt, tmp_cnt, sizeof(tmp_cnt),
			 UNIT_NONE);
	gtk_tree_store_set(treestore, iter, SORTID_NODES, tmp_cnt, -1);

	gtk_tree_store_set(treestore, iter, SORTID_NODELIST,
			   block_ptr->nodes, -1);

#ifdef HAVE_BGL
	gtk_tree_store_set(treestore, iter, SORTID_BLRTSIMAGE,
			   block_ptr->blrtsimage, -1);
#endif
	gtk_tree_store_set(treestore, iter, SORTID_LINUXIMAGE,
			   block_ptr->linuximage, -1);
	gtk_tree_store_set(treestore, iter, SORTID_MLOADERIMAGE,
			   block_ptr->mloaderimage, -1);
	gtk_tree_store_set(treestore, iter, SORTID_RAMDISKIMAGE,
			   block_ptr->ramdiskimage, -1);

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
			block_ptr->slurm_part_name = xstrdup("no part");
		
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
	char tmp_nodes[50];
	
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
		if(node_select_ptr->bg_info_array[i].ionodes) {
			snprintf(tmp_nodes, sizeof(tmp_nodes),
				 "%s[%s]",
				 block_ptr->nodes,
				 node_select_ptr->bg_info_array[i].ionodes);
			xfree(block_ptr->nodes);
			block_ptr->nodes = xstrdup(tmp_nodes);
		}
				
		block_ptr->bg_user_name 
			= xstrdup(node_select_ptr->
				  bg_info_array[i].owner_name);
#ifdef HAVE_BGL
		block_ptr->blrtsimage = xstrdup(
			node_select_ptr->bg_info_array[i].blrtsimage);
#endif
		block_ptr->linuximage = xstrdup(
			node_select_ptr->bg_info_array[i].linuximage);
		block_ptr->mloaderimage = xstrdup(
			node_select_ptr->bg_info_array[i].mloaderimage);
		block_ptr->ramdiskimage = xstrdup(
			node_select_ptr->bg_info_array[i].ramdiskimage);
		
		block_ptr->state 
			= node_select_ptr->bg_info_array[i].state;
		block_ptr->bg_conn_type 
			= node_select_ptr->bg_info_array[i].conn_type;
#ifdef HAVE_BGL
		block_ptr->bg_node_use 
			= node_select_ptr->bg_info_array[i].node_use;
#endif
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
	char *name = (char *)spec_info->search_info->gchar_data;
	int i = -1, j = 0, found = 0;
	sview_block_info_t *block_ptr = NULL;
	char *info = NULL;
	int update = 0;
	GtkTreeView *treeview = NULL;
	ListIterator itr = NULL;
	
	if(!spec_info->search_info->gchar_data) {
		info = xstrdup("No pointer given!");
		goto finished;
	}

need_refresh:
	if(!spec_info->display_widget) {
		treeview = create_treeview_2cols_attach_to_table(
			popup_win->table);
		spec_info->display_widget = 
			gtk_widget_ref(GTK_WIDGET(treeview));
	} else {
		treeview = GTK_TREE_VIEW(spec_info->display_widget);
		update = 1;
	}
	/* this is here for if later we have more stats on a bluegene
	   block */
	itr = list_iterator_create(block_list);
	while ((block_ptr = (sview_block_info_t*) list_next(itr))) {
		i++;
		if(!strcmp(block_ptr->bg_block_name, name)
		   || !strcmp(block_ptr->nodes, name)) {
			j = 0;
			while(block_ptr->bp_inx[j] >= 0) {
				change_grid_color(
					popup_win->grid_button_list,
					block_ptr->bp_inx[j],
					block_ptr->bp_inx[j+1], i);
				j += 2;
			}
			_layout_block_record(treeview, block_ptr, update);
			found = 1;
			break;
		}
	}
	list_iterator_destroy(itr);
	
	if(!found) {
		if(!popup_win->not_found) { 
			char *temp = "BLOCK DOESN'T EXSIST\n";
			GtkTreeIter iter;
			GtkTreeModel *model = NULL;
	
			/* only time this will be run so no update */
			model = gtk_tree_view_get_model(treeview);
			add_display_treestore_line(0, 
						   GTK_TREE_STORE(model), 
						   &iter,
						   temp, "");
		}
		popup_win->not_found = true;
	} else {
		if(popup_win->not_found) { 
			popup_win->not_found = false;
			gtk_widget_destroy(spec_info->display_widget);
			
			goto need_refresh;
		}
	}
	gtk_widget_show(spec_info->display_widget);
	
finished:

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
	int error_code = SLURM_NO_CHANGE_IN_DATA;
#ifdef HAVE_BG
	static node_select_info_msg_t *bg_info_ptr = NULL;
	static node_select_info_msg_t *new_bg_ptr = NULL;
	time_t now = time(NULL);
	static time_t last;
	static bool changed = 0;
		
	if(!force && ((now - last) < global_sleep_time)) {
		*node_select_ptr = bg_info_ptr;
		if(changed) 
			return SLURM_SUCCESS;
		return error_code;
	}
	last = now;
	if (bg_info_ptr) {
		error_code = slurm_load_node_select(bg_info_ptr->last_update, 
						    &new_bg_ptr);
		if (error_code == SLURM_SUCCESS) {
			select_g_free_node_info(&bg_info_ptr);
			changed = 1;
		} else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_bg_ptr = bg_info_ptr;
			changed = 0;
		}
	} else {
		error_code = slurm_load_node_select((time_t) NULL, 
						    &new_bg_ptr);
		changed = 1;
	}

	bg_info_ptr = new_bg_ptr;
	*node_select_ptr = new_bg_ptr;
#endif
	return error_code;
}

extern int update_state_block(GtkDialog *dialog,
			      const char *blockid, const char *type)
{
	int i = 0;
	int rc = SLURM_SUCCESS;
	char tmp_char[100];
	update_part_msg_t part_msg;
	GtkWidget *label = NULL;
	int no_dialog = 0;

	if(!dialog) {
		dialog = GTK_DIALOG(
			gtk_dialog_new_with_buttons(
				type,
				GTK_WINDOW(main_window),
				GTK_DIALOG_MODAL
				| GTK_DIALOG_DESTROY_WITH_PARENT,
				NULL));	
		no_dialog = 1;
	}
	slurm_init_part_desc_msg(&part_msg);
	/* means this is for bluegene */
	part_msg.hidden = (uint16_t)INFINITE;
	part_msg.name = (char *)blockid;
	
	label = gtk_dialog_add_button(dialog,
				      GTK_STOCK_YES, GTK_RESPONSE_OK);
	gtk_window_set_default(GTK_WINDOW(dialog), label);
	gtk_dialog_add_button(dialog,
			      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	
	if(!strcasecmp("Error", type)
	   || !strcasecmp("Put block in error state", type)) {
		snprintf(tmp_char, sizeof(tmp_char), 
			 "Are you sure you want to put block %s "
			 "in an error state?",
			 blockid);
		part_msg.state_up = 0;
	} else {
		snprintf(tmp_char, sizeof(tmp_char), 
			 "Are you sure you want to put block %s "
			 "in a free state?",
			 blockid);
		part_msg.state_up = 1;
	}
	
	label = gtk_label_new(tmp_char);
			
	gtk_box_pack_start(GTK_BOX(dialog->vbox), label, FALSE, FALSE, 0);

	gtk_widget_show_all(GTK_WIDGET(dialog));
	i = gtk_dialog_run(dialog);
	if (i == GTK_RESPONSE_OK) {
		if(slurm_update_partition(&part_msg) == SLURM_SUCCESS) {
			snprintf(tmp_char, sizeof(tmp_char), 
				 "Block %s updated successfully",
				blockid);
		} else {
			snprintf(tmp_char, sizeof(tmp_char), 
				 "Problem updating block %s.",
				 blockid);
		}
		display_edit_note(tmp_char);
	}

	if(no_dialog)
		gtk_widget_destroy(GTK_WIDGET(dialog));
	return rc;
}

extern GtkListStore *create_model_block(int type)
{
	GtkListStore *model = NULL;
	GtkTreeIter iter;
	switch(type) {
	case SORTID_STATE:
		model = gtk_list_store_new(2, G_TYPE_STRING,
					   G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "Error",
				   1, SORTID_STATE,
				   -1);	
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "Free",
				   1, SORTID_STATE,
				   -1);	
		break;
	default:
		break;
	}
	
	return model;
}

extern void admin_edit_block(GtkCellRendererText *cell,
			     const char *path_string,
			     const char *new_text,
			     gpointer data)
{
	GtkTreeStore *treestore = GTK_TREE_STORE(data);
	GtkTreePath *path = gtk_tree_path_new_from_string(path_string);
	GtkTreeIter iter;
	int column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell), 
						       "column"));
	
	char *blockid = NULL;
	char *old_text = NULL;
	if(!new_text || !strcmp(new_text, ""))
		goto no_input;

	gtk_tree_model_get_iter(GTK_TREE_MODEL(treestore), &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter, 
			   SORTID_BLOCK, &blockid, 
			   column, &old_text,
			   -1);
	switch(column) {
	case SORTID_STATE:
		update_state_block(NULL, blockid, new_text);
		break;
	default:
		break;
	}

	g_free(blockid);
	g_free(old_text);
no_input:
	gtk_tree_path_free(path);
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
		
	} else if (part_error_code != SLURM_SUCCESS) {
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

	if((block_error_code = get_new_info_node_select(&node_select_ptr, 
							force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		if((!display_widget || view == ERROR_VIEW) 
		   || (part_error_code != SLURM_NO_CHANGE_IN_DATA))
			goto display_it;
		changed = 0;
	} else if (block_error_code != SLURM_SUCCESS) {
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
	sview_search_info_t *search_info = spec_info->search_info;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	List block_list = NULL;
	List send_block_list = NULL;
	int changed = 1;
	sview_block_info_t *block_ptr = NULL;
	int j=0, i=-1;
	hostset_t hostset = NULL;
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
		
	} else if (part_error_code != SLURM_SUCCESS) {
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
		
	} else if (block_error_code != SLURM_SUCCESS) {
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
				  search_info->gchar_data)) 
				continue;
			break;
		case RESV_PAGE:
		case NODE_PAGE:
			if(!block_ptr->nodes)
				continue;
			
			if(!(hostset = hostset_create(search_info->gchar_data)))
				continue;
			if(!hostset_intersects(hostset, block_ptr->nodes)) {
				hostset_destroy(hostset);
				continue;
			}
			hostset_destroy(hostset);				
			break;
		case BLOCK_PAGE:
			switch(search_info->search_type) {
			case SEARCH_BLOCK_NAME:
				if(!search_info->gchar_data)
					continue;
				
				if(strcmp(block_ptr->bg_block_name, 
					  search_info->gchar_data)) 
					continue;
				break;
			case SEARCH_BLOCK_SIZE:
				if(search_info->int_data == NO_VAL)
					continue;
				if(block_ptr->node_cnt
				   != search_info->int_data)
					continue;
				break;
			case SEARCH_BLOCK_STATE:
				if(search_info->int_data == NO_VAL)
					continue;
				if(block_ptr->state != search_info->int_data)
					continue;
				
				break;
			default:
				continue;
				break;
			}
			break;
		case JOB_PAGE:
			if(strcmp(block_ptr->bg_block_name, 
				  search_info->gchar_data)) 
				continue;
			break;
		default:
			g_print("Unknown type %d\n", spec_info->type);
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
		make_fields_menu(menu, display_data_block, SORTID_CNT);
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
	case RESV_PAGE:
		snprintf(title, 100, "Reservations(s) containing block %s",
			 name);
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
		popup_win->spec_info->search_info->gchar_data = name;
		break;
	case PART_PAGE:
		g_free(name);
		gtk_tree_model_get(model, iter, SORTID_PARTITION, &name, -1);
		popup_win->spec_info->search_info->gchar_data = name;
		break;
	case RESV_PAGE: 
	case NODE_PAGE: 
		g_free(name);
		gtk_tree_model_get(model, iter, SORTID_NODELIST, &name, -1);
		/* strip off the ionodes part */
		while(name[i]) {
			if(name[i] == '[') {
				name[i] = '\0';
				break;
			}
			i++;
		}
		popup_win->spec_info->search_info->gchar_data = name;
		break;
	case INFO_PAGE:
		popup_win->spec_info->search_info->gchar_data = name;
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

extern void admin_block(GtkTreeModel *model, GtkTreeIter *iter, char *type)
{
	char *blockid = NULL;
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		type,
		GTK_WINDOW(main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);
	gtk_window_set_transient_for(GTK_WINDOW(popup), NULL);

	gtk_tree_model_get(model, iter, SORTID_BLOCK, &blockid, -1);
	
	update_state_block(GTK_DIALOG(popup), blockid, type);
	
	g_free(blockid);
	gtk_widget_destroy(popup);
	
	return;
}

