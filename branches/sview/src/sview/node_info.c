/*****************************************************************************\
 *  node_info.c - Functions related to node display 
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

enum { 
	SORTID_POS = POS_LOC,
	SORTID_NAME, 
	SORTID_STATE,
	SORTID_CPUS, 
	SORTID_MEMORY, 
	SORTID_DISK, 
	SORTID_WEIGHT, 
	SORTID_FEATURES, 
	SORTID_REASON,
	SORTID_UPDATED, 
	SORTID_CNT
};

static display_data_t display_data_node[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, -1, refresh_node},
	{G_TYPE_STRING, SORTID_NAME, "Name", TRUE, -1, refresh_node},
	{G_TYPE_STRING, SORTID_STATE, "State", TRUE, -1, refresh_node},
	{G_TYPE_INT, SORTID_CPUS, "CPU Count", TRUE, -1, refresh_node},
	{G_TYPE_STRING, SORTID_MEMORY, "Real Memory", TRUE, -1, refresh_node},
	{G_TYPE_STRING, SORTID_DISK, "Tmp Disk", TRUE, -1, refresh_node},
	{G_TYPE_INT, SORTID_WEIGHT,"Weight", FALSE, -1, refresh_node},
	{G_TYPE_STRING, SORTID_FEATURES, "Features", FALSE, -1, refresh_node},
	{G_TYPE_STRING, SORTID_REASON, "Reason", FALSE, -1, refresh_node},
	{G_TYPE_INT, SORTID_UPDATED, NULL, FALSE, -1, refresh_node},
	{G_TYPE_NONE, -1, NULL, FALSE, -1}
};

static display_data_t options_data_node[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, -1},
	{G_TYPE_STRING, JOB_PAGE, "Jobs", TRUE, NODE_PAGE},
#ifdef HAVE_BG
	{G_TYPE_STRING, BLOCK_PAGE, "Blocks", TRUE, NODE_PAGE},
#endif
	{G_TYPE_STRING, PART_PAGE, "Partition", TRUE, NODE_PAGE},
	{G_TYPE_STRING, SUBMIT_PAGE, "Job Submit", TRUE, NODE_PAGE},
	{G_TYPE_STRING, ADMIN_PAGE, "Admin", TRUE, NODE_PAGE},
	{G_TYPE_NONE, -1, NULL, FALSE, -1}
};

static display_data_t *local_display_data = NULL;

static void _update_node_record(node_info_t *node_ptr,
				GtkListStore *liststore, GtkTreeIter *iter)
{
	char tmp_cnt[7];

	gtk_list_store_set(liststore, iter, SORTID_NAME, node_ptr->name, -1);
	gtk_list_store_set(liststore, iter, SORTID_STATE, 
			   node_state_string(node_ptr->node_state), -1);
	gtk_list_store_set(liststore, iter, SORTID_CPUS, node_ptr->cpus, -1);

	convert_num_unit((float)node_ptr->real_memory, tmp_cnt, UNIT_MEGA);
	gtk_list_store_set(liststore, iter, SORTID_MEMORY, tmp_cnt, -1);
	convert_num_unit((float)node_ptr->tmp_disk, tmp_cnt, UNIT_MEGA);
	gtk_list_store_set(liststore, iter, SORTID_DISK, tmp_cnt, -1);
	gtk_list_store_set(liststore, iter, SORTID_WEIGHT, 
			   node_ptr->weight, -1);
	gtk_list_store_set(liststore, iter, SORTID_FEATURES, 
			   node_ptr->features, -1);
	gtk_list_store_set(liststore, iter, SORTID_REASON, 
			   node_ptr->reason, -1);
	gtk_list_store_set(liststore, iter, SORTID_UPDATED, 1, -1);	
	
	return;
}

static void _append_node_record(node_info_t *node_ptr,
				GtkListStore *liststore, GtkTreeIter *iter,
				int line)
{
	gtk_list_store_append(liststore, iter);
	gtk_list_store_set(liststore, iter, SORTID_POS, line, -1);
	_update_node_record(node_ptr, liststore, iter);
}

static void _update_info_node(node_info_msg_t *node_info_ptr, 
			      GtkTreeView *tree_view,
			      specific_info_t *spec_info)
{
	GtkTreePath *path = gtk_tree_path_new_first();
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	GtkTreeIter iter;
	node_info_t node;
	int i, line = 0;
	char *name;
	hostlist_t hostlist = NULL;	
	hostlist_iterator_t itr = NULL;
	
	if(spec_info) {
		hostlist = hostlist_create((char *)spec_info->data);
		itr = hostlist_iterator_create(hostlist);
	}

	/* get the iter, or find out the list is empty goto add */
	if (gtk_tree_model_get_iter(model, &iter, path)) {
		/* make sure all the partitions are still here */
		while(1) {
			gtk_list_store_set(GTK_LIST_STORE(model), &iter, 
					   SORTID_UPDATED, 0, -1);	
			if(!gtk_tree_model_iter_next(model, &iter)) {
				break;
			}
		}
	}
	for (i = 0; i < node_info_ptr->record_count; i++) {
		node = node_info_ptr->node_array[i];
		/* get the iter, or find out the list is empty goto add */
		if (!gtk_tree_model_get_iter(model, &iter, path)) {
			goto adding;
		}
		while(1) {
			/* search for the node name and check to see if 
			   it is in the list */
			gtk_tree_model_get(model, &iter, SORTID_NAME, 
					   &name, -1);
			if(!strcmp(name, node.name)) {
				/* update with new info */
				_update_node_record(&node, 
						    GTK_LIST_STORE(model), 
						    &iter);
				goto found;
			}
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
			int found = 0;
			char *host = NULL;
			while((host = hostlist_next(itr))) { 
				if(!strcmp(host, node.name)) {
					free(host);
					found = 1;
					break; 
				}
				free(host);
			}
			hostlist_iterator_reset(itr);
			if(!found)
				continue;
		}
		
		_append_node_record(&node, GTK_LIST_STORE(model), &iter, i);
	found:
		;
	}
       	gtk_tree_path_free(path);
	/* remove all old nodes */
	remove_old(model, SORTID_UPDATED);
	if(spec_info) {
		hostlist_iterator_destroy(itr);
		hostlist_destroy(hostlist);
	}
	
	
}

void *_popup_thr_node(void *arg)
{
	popup_thr(arg);		
	return NULL;
}

extern int get_new_info_node(node_info_msg_t **info_ptr)
{
	static node_info_msg_t *node_info_ptr = NULL, *new_node_ptr = NULL;
	uint16_t show_flags = 0;
	int error_code = SLURM_SUCCESS;

	show_flags |= SHOW_ALL;
	if (node_info_ptr) {
		error_code = slurm_load_node(node_info_ptr->last_update,
					     &new_node_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_node_info_msg(node_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_node_ptr = node_info_ptr;
		}
	} else
		error_code = slurm_load_node((time_t) NULL, &new_node_ptr, 
					     show_flags);
	node_info_ptr = new_node_ptr;
	*info_ptr = new_node_ptr;
	return error_code;
}

extern void refresh_node(GtkAction *action, gpointer user_data)
{
	popup_info_t *popup_win = (popup_info_t *)user_data;
	xassert(popup_win != NULL);
	xassert(popup_win->spec_info != NULL);
	xassert(popup_win->spec_info->title != NULL);
	specific_info_node(popup_win);
}

extern void get_info_node(GtkTable *table, display_data_t *display_data)
{
	int error_code = SLURM_SUCCESS;
	static int view = -1;
	static node_info_msg_t *node_info_ptr = NULL, *new_node_ptr = NULL;
	char error_char[50];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	
	if(display_data)
		local_display_data = display_data;
	if(!table) {
		display_data_node->set_menu = local_display_data->set_menu;
		return;
	}
	if(new_node_ptr && toggled) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
		goto display_it;
	}

	if((error_code = get_new_info_node(&new_node_ptr))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		if(!display_widget || view == ERROR_VIEW)
			goto display_it;
		_update_info_node(new_node_ptr, GTK_TREE_VIEW(display_widget), 
				  NULL);
		return;
	} 

	if (error_code != SLURM_SUCCESS) {
		view = ERROR_VIEW;
		if(display_widget)
			gtk_widget_destroy(display_widget);
		sprintf(error_char, "slurm_load_node: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(GTK_TABLE(table), 
					  label,
					  0, 1, 0, 1); 
		gtk_widget_show(label);	
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
		return;
	}
display_it:
	if(view == ERROR_VIEW && display_widget) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
	}
	if(!display_widget) {
		tree_view = create_treeview(local_display_data, new_node_ptr);
		
		display_widget = gtk_widget_ref(GTK_WIDGET(tree_view));
		gtk_table_attach_defaults(GTK_TABLE(table),
					  GTK_WIDGET(tree_view),
					  0, 1, 0, 1);		
		gtk_widget_show(GTK_WIDGET(tree_view));
		/* since this function sets the model of the tree_view 
		   to the liststore we don't really care about 
		   the return value */
		create_liststore(tree_view, display_data_node, SORTID_CNT);
	}
	view = INFO_VIEW;
	_update_info_node(new_node_ptr, GTK_TREE_VIEW(display_widget), NULL);
	toggled = FALSE;
		
	node_info_ptr = new_node_ptr;
	return;
	
}

extern void specific_info_node(popup_info_t *popup_win)
{
	int error_code = SLURM_SUCCESS;
	static node_info_msg_t *node_info_ptr = NULL, *new_node_ptr = NULL;
	specific_info_t *spec_info = popup_win->spec_info;
	char error_char[50];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	
	if(!spec_info->display_widget)
		setup_popup_info(popup_win, display_data_node, SORTID_CNT);
	
	if(new_node_ptr && popup_win->toggled) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
		goto display_it;
	}
	
	if((error_code = get_new_info_node(&new_node_ptr))
	   == SLURM_NO_CHANGE_IN_DATA) {
		if(!spec_info->display_widget || spec_info->view == ERROR_VIEW)
			goto display_it;
		_update_info_node(new_node_ptr, 
				  GTK_TREE_VIEW(spec_info->display_widget), 
				  spec_info);
		return;
	}  
			
	if (error_code != SLURM_SUCCESS) {
		spec_info->view = ERROR_VIEW;
		if(spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		sprintf(error_char, "slurm_load_node: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(popup_win->table, 
					  label,
					  0, 1, 0, 1); 
		gtk_widget_show(label);	
		spec_info->display_widget = gtk_widget_ref(GTK_WIDGET(label));
		return;
	}
display_it:	
	
	if(spec_info->view == ERROR_VIEW && spec_info->display_widget) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
	}
	if(!spec_info->display_widget) {
		tree_view = create_treeview(local_display_data, new_node_ptr);
		
		spec_info->display_widget = 
			gtk_widget_ref(GTK_WIDGET(tree_view));
		gtk_table_attach_defaults(GTK_TABLE(popup_win->table),
					  GTK_WIDGET(tree_view),
					  0, 1, 0, 1);
		
		/* since this function sets the model of the tree_view 
		   to the liststore we don't really care about 
		   the return value */
		create_liststore(tree_view, popup_win->display_data, 
				 SORTID_CNT);
	}
	spec_info->view = INFO_VIEW;
	_update_info_node(new_node_ptr, 
			  GTK_TREE_VIEW(spec_info->display_widget), spec_info);
	popup_win->toggled = 0;
	
	node_info_ptr = new_node_ptr;
	return;
	
}

extern void set_menus_node(void *arg, GtkTreePath *path, 
			   GtkMenu *menu, int type)
{
	GtkTreeView *tree_view = (GtkTreeView *)arg;
	popup_info_t *popup_win = (popup_info_t *)arg;
	switch(type) {
	case TAB_CLICKED:
		make_fields_menu(menu, display_data_node);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_node);
		break;
	case POPUP_CLICKED:
		make_popup_fields_menu(popup_win, menu);
		break;
	default:
		g_error("UNKNOWN type %d given to set_fields\n", type);
	}
}

extern void row_clicked_node(GtkTreeView *tree_view,
			     GtkTreePath *path,
			     GtkTreeViewColumn *column,
			     gpointer user_data)
{
	node_info_msg_t *node_info_ptr = (node_info_msg_t *)user_data;
	node_info_t *node_ptr = NULL;
	int line = get_row_number(tree_view, path);
	GtkWidget *popup = NULL;
	GtkWidget *label = NULL;
	char *info = NULL;
	if(line == -1) {
		g_error("problem getting line number");
		return;
	}
	
	node_ptr = &node_info_ptr->node_array[line];
	if(!(info = slurm_sprint_node_table(node_ptr, 0))) {
		info = xmalloc(100);
		sprintf(info, "Problem getting node info for %s",
			node_ptr->name);
	}

	popup = gtk_dialog_new();

	label = gtk_label_new(info);
	gtk_box_pack_end(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			   label, TRUE, TRUE, 0);
	xfree(info);
	gtk_widget_show(label);
	
	gtk_widget_show(popup);	
}

extern void popup_all_node(GtkTreeModel *model, GtkTreeIter *iter, int id)
{
	char *name = NULL;
	char title[100];
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	GError *error = NULL;

#ifdef HAVE_BG
	char *node = "base partition";
#else
	char *node = "node";
#endif
	gtk_tree_model_get(model, iter, SORTID_NAME, &name, -1);
	switch(id) {
	case JOB_PAGE:
		snprintf(title, 100, "Jobs(s) with %s %s", node, name);
		break;
	case PART_PAGE:
		snprintf(title, 100, "Partition(s) with %s %s", node, name);
		break;
	case BLOCK_PAGE: 
		snprintf(title, 100, "Blocks(s) with %s %s", node, name);
		break;
	case ADMIN_PAGE: 
		snprintf(title, 100, "Admin Page for %s %s", node, name);
		break;
	case SUBMIT_PAGE: 
		snprintf(title, 100, "Submit job on %s %s", node, name);
		break;
	default:
		g_print("%s got %d\n", node, id);
	}
	
	itr = list_iterator_create(popup_list);
	while((popup_win = list_next(itr))) {
		if(popup_win->spec_info)
			if(!strcmp(popup_win->spec_info->title, title)) {
				break;
			} 
	}
	list_iterator_destroy(itr);
	
	if(!popup_win) 
		popup_win = create_popup_info(NODE_PAGE, id, title);
	popup_win->spec_info->data = name;
	
	if (!g_thread_create(_popup_thr_node, popup_win, FALSE, &error))
	{
		g_printerr ("Failed to create part popup thread: %s\n", 
			    error->message);
		return;
	}
}
