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
	{G_TYPE_NONE, -1, NULL, FALSE, -1}
};
static display_data_t popup_data_node[SORTID_CNT+1];

static display_data_t options_data_node[] = {
	{G_TYPE_STRING, JOB_PAGE, "Jobs", TRUE, -1},
	{G_TYPE_STRING, NODE_PAGE, "Partition", TRUE, -1},
	{G_TYPE_STRING, SUBMIT_PAGE, "Job Submit", TRUE, -1},
	{G_TYPE_STRING, ADMIN_PAGE, "Admin", TRUE, -1},
	{G_TYPE_NONE, -1, NULL, FALSE, -1}
};

static display_data_t *local_display_data = NULL;

static void _set_up_button(GtkTreeView *tree_view, GdkEventButton *event, 
			   gpointer user_data)
{
	local_display_data->user_data = user_data;
	row_clicked(tree_view, event, local_display_data);
}

static void _append_node_record(node_info_t *node_ptr,
				GtkListStore *liststore, GtkTreeIter *iter,
				int line)
{
	char tmp_cnt[7];

	gtk_list_store_append(liststore, iter);
	gtk_list_store_set(liststore, iter, SORTID_POS, line, -1);
	gtk_list_store_set(liststore, iter, 
			   SORTID_NAME, node_ptr->name, -1);
	gtk_list_store_set(liststore, iter,
			   SORTID_STATE,
			   node_state_string(node_ptr->node_state), -1);
	gtk_list_store_set(liststore, iter, 
			   SORTID_CPUS, node_ptr->cpus, -1);

	convert_num_unit((float)node_ptr->real_memory, tmp_cnt, UNIT_MEGA);
	gtk_list_store_set(liststore, iter, 
			   SORTID_MEMORY, tmp_cnt, -1);
	convert_num_unit((float)node_ptr->tmp_disk, tmp_cnt, UNIT_MEGA);
	gtk_list_store_set(liststore, iter, 
			   SORTID_DISK, tmp_cnt, -1);
	gtk_list_store_set(liststore, iter, 
			   SORTID_WEIGHT, node_ptr->weight, -1);
	gtk_list_store_set(liststore, iter, 
			   SORTID_FEATURES, node_ptr->features, -1);
	gtk_list_store_set(liststore, iter, 
			   SORTID_REASON, node_ptr->reason, -1);
	
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
	g_print("hey got here\n");
}

extern void get_info_node(GtkTable *table, display_data_t *display_data)
{
	int error_code = SLURM_SUCCESS, i, recs;
	static node_info_msg_t *node_info_ptr = NULL, *new_node_ptr = NULL;
	node_info_t node;
	char error_char[50];
	GtkTreeIter iter;
	GtkListStore *liststore = NULL;
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	
	local_display_data = display_data;
	if(!table) {
		for(i=0; i<SORTID_CNT+1; i++) {
			memcpy(&popup_data_node[i], &display_data_node[i], 
			       sizeof(display_data_t));
		}
		return;
	}
	if(new_node_ptr && toggled)
		goto got_toggled;

	if((error_code = get_new_info_node(&new_node_ptr))
	   == SLURM_NO_CHANGE_IN_DATA) {
		if(!display_widget)
			goto display_it;
		return;
	}

got_toggled:
	if(display_widget)
		gtk_widget_destroy(display_widget);

	if (error_code != SLURM_SUCCESS) {
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
	if (new_node_ptr)
		recs = new_node_ptr->record_count;
	else
		recs = 0;

	tree_view = GTK_TREE_VIEW(gtk_tree_view_new());
	display_widget = gtk_widget_ref(GTK_WIDGET(tree_view));

	g_signal_connect(G_OBJECT(tree_view), "row-activated",
			 G_CALLBACK(row_clicked_node),
			 new_node_ptr);
	g_signal_connect(G_OBJECT(tree_view), "button-press-event",
			 G_CALLBACK(_set_up_button),
			 new_node_ptr);
	
	gtk_table_attach_defaults(GTK_TABLE(table), 
				  GTK_WIDGET(tree_view),
				  0, 1, 0, 1); 
	gtk_widget_show(GTK_WIDGET(tree_view));
	
	liststore = create_liststore(display_data_node, SORTID_CNT);

	load_header(tree_view, display_data_node);

	for (i = 0; i < recs; i++) {
		node = new_node_ptr->node_array[i];
		_append_node_record(&node, liststore, &iter, i);
	}
		
	//ba_system_ptr->ycord++;
	
	gtk_tree_view_set_model(tree_view, GTK_TREE_MODEL(liststore));
	g_object_unref(GTK_TREE_MODEL(liststore));
	node_info_ptr = new_node_ptr;
	return;
	
}

extern void specific_info_node(GtkTable *table, GtkWidget *event_box, 
			       char *node_list)
{
	int error_code = SLURM_SUCCESS, i, recs;
	static node_info_msg_t *node_info_ptr = NULL, *new_node_ptr = NULL;
	node_info_t node;
	char error_char[50];
	GtkTreeIter iter;
	GtkListStore *liststore = NULL;
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	hostlist_t hostlist = hostlist_create(node_list);	
	hostlist_iterator_t itr = hostlist_iterator_create(hostlist);
	char *host = NULL;
	int found = 0;
	
	if(new_node_ptr && toggled)
		goto got_toggled;
	
	if((error_code = get_new_info_node(&new_node_ptr))
	   == SLURM_NO_CHANGE_IN_DATA) 
		goto display_it;

got_toggled:
	if(display_widget)
		gtk_widget_destroy(display_widget);

	if (error_code != SLURM_SUCCESS) {
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
	gtk_event_box_set_above_child(GTK_EVENT_BOX(event_box), FALSE);
	g_signal_connect(G_OBJECT(event_box), "button-press-event",
			 G_CALLBACK(redo_popup),
			 local_display_data);
	
	label = gtk_label_new(local_display_data->name);
	gtk_container_add(GTK_CONTAINER(event_box), label);
	gtk_widget_show(label);
	
	if (new_node_ptr)
		recs = new_node_ptr->record_count;
	else
		recs = 0;

	tree_view = GTK_TREE_VIEW(gtk_tree_view_new());
	display_widget = gtk_widget_ref(GTK_WIDGET(tree_view));

	g_signal_connect(G_OBJECT(tree_view), "row-activated",
			 G_CALLBACK(row_clicked_node),
			 new_node_ptr);
	g_signal_connect(G_OBJECT(tree_view), "button-press-event",
			 G_CALLBACK(_set_up_button),
			 new_node_ptr);
	
	gtk_table_attach_defaults(GTK_TABLE(table), 
				  GTK_WIDGET(tree_view),
				  0, 1, 1, 2); 
	gtk_widget_show(GTK_WIDGET(tree_view));
	
	liststore = create_liststore(display_data_node, SORTID_CNT);

	load_header(tree_view, display_data_node);

	for (i = 0; i < recs; i++) {
		node = new_node_ptr->node_array[i];
		found = 0;
		while((host = hostlist_next(itr)) != NULL) { 
			if(!strcmp(host, node.name)) {
  				free(host);
				found = 1;
				break; 
			}
			free(host);
  		}
		hostlist_iterator_reset(itr);
		if(found)
			_append_node_record(&node, liststore, &iter, i);
	}
	hostlist_iterator_destroy(itr);
	hostlist_destroy(hostlist);
	
	//ba_system_ptr->ycord++;
	
	gtk_tree_view_set_model(tree_view, GTK_TREE_MODEL(liststore));
	g_object_unref(GTK_TREE_MODEL(liststore));
	node_info_ptr = new_node_ptr;
	return;
	
}

extern void set_menus_node(GtkTreeView *tree_view, GtkTreePath *path, 
			   GtkMenu *menu, int type)
{
	switch(type) {
	case TAB_CLICKED:
		make_fields_menu(menu, display_data_node);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_node);
		break;
	case POPUP_CLICKED:
		make_popup_fields_menu(menu, popup_data_node);
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

	gtk_tree_model_get(model, iter, SORTID_NAME, &name, -1);
}
