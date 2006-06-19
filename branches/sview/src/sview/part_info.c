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

enum { 
	SORTID_POS = POS_LOC,
	SORTID_NAME, 
	SORTID_AVAIL, 
	SORTID_TIMELIMIT, 
	SORTID_NODES, 
	SORTID_NODELIST, 
	SORTID_CNT
};

static display_data_t display_data_part[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, -1, refresh_part},
	{G_TYPE_STRING, SORTID_NAME, "Partition", TRUE, -1, refresh_part},
	{G_TYPE_STRING, SORTID_AVAIL, "Availablity", TRUE, -1, refresh_part},
	{G_TYPE_STRING, SORTID_TIMELIMIT, "Time Limit", TRUE, -1, refresh_part},
	{G_TYPE_STRING, SORTID_NODES, "Nodes", TRUE, -1, refresh_part},
#ifdef HAVE_BG
	{G_TYPE_STRING, SORTID_NODELIST, "BP List", TRUE, -1, refresh_part},
#else
	{G_TYPE_STRING, SORTID_NODELIST, "NodeList", TRUE, -1, refresh_part},
#endif
	{G_TYPE_NONE, -1, NULL, FALSE, -1}
};
static display_data_t popup_data_part[SORTID_CNT+1];

static display_data_t options_data_part[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, -1},
	{G_TYPE_STRING, JOB_PAGE, "Jobs", TRUE, PART_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Nodes", TRUE, PART_PAGE},
#ifdef HAVE_BG
	{G_TYPE_STRING, BLOCK_PAGE, "Blocks", TRUE, PART_PAGE},
#endif
	{G_TYPE_STRING, SUBMIT_PAGE, "Job Submit", TRUE, PART_PAGE},
	{G_TYPE_STRING, ADMIN_PAGE, "Admin", TRUE, PART_PAGE},
	{G_TYPE_NONE, -1, NULL, FALSE, -1}
};

static display_data_t *local_display_data = NULL;

static void _set_up_button(GtkTreeView *tree_view, GdkEventButton *event, 
			    gpointer user_data)
{
	local_display_data->user_data = user_data;
	row_clicked(tree_view, event, local_display_data);
}
/*
 * diff_tv_str - build a string showing the time difference between two times
 * IN tv1 - start of event
 * IN tv2 - end of event
 * OUT tv_str - place to put delta time in format "usec=%ld"
 * IN len_tv_str - size of tv_str in bytes
 */
inline void diff_tv_str(struct timeval *tv1,struct timeval *tv2, 
		char *tv_str, int len_tv_str)
{
	long delta_t;
	delta_t  = (tv2->tv_sec  - tv1->tv_sec) * 1000000;
	delta_t +=  tv2->tv_usec - tv1->tv_usec;
	snprintf(tv_str, len_tv_str, "usec=%ld", delta_t);
	if (delta_t > 1000000)
		info("Warning: Note very large processing time: %s",tv_str); 
}


static void _append_part_record(partition_info_t *part_ptr,
			       GtkListStore *liststore, GtkTreeIter *iter,
			       int line)
{
	char time_buf[20];
	char tmp_cnt[7];
	
//	START_TIMER;
	gtk_list_store_append(liststore, iter);
	gtk_list_store_set(liststore, iter, SORTID_POS, line, -1);
	gtk_list_store_set(liststore, iter, 
			   SORTID_NAME, part_ptr->name, -1);

	if (part_ptr->state_up) 
		gtk_list_store_set(liststore, iter, 
				   SORTID_AVAIL, "up", -1);
	else
		gtk_list_store_set(liststore, iter, 
				   SORTID_AVAIL, "down", -1);
		
	if (part_ptr->max_time == INFINITE)
		snprintf(time_buf, sizeof(time_buf),
			 "infinite");
	else {
		snprint_time(time_buf,
			     sizeof(time_buf),
			     (part_ptr->max_time
			      * 60));
	}
	
	gtk_list_store_set(liststore, iter, 
			   SORTID_TIMELIMIT, time_buf, -1);
		
       	convert_to_kilo(part_ptr->total_nodes, tmp_cnt);
	gtk_list_store_set(liststore, iter, 
			   SORTID_NODES, tmp_cnt, -1);
	gtk_list_store_set(liststore, iter, 
			   SORTID_NODELIST, part_ptr->nodes, -1);
	//END_TIMER;
	//g_print("Took %s\n",TIME_STR);
	
}

extern int get_new_info_part(partition_info_msg_t **part_ptr)
{
	static partition_info_msg_t *part_info_ptr = NULL;
	static partition_info_msg_t *new_part_ptr = NULL;
	int error_code = SLURM_SUCCESS;

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

extern void refresh_part(GtkAction *action, gpointer user_data)
{
	g_print("hey got here part\n");
}

extern void get_info_part(GtkTable *table, display_data_t *display_data)
{
	int error_code = SLURM_SUCCESS;
	int i, j, recs, count = 0;
	static partition_info_msg_t *part_info_ptr = NULL;
	static partition_info_msg_t *new_part_ptr = NULL;
	partition_info_t part;
	char error_char[50];
	GtkTreeIter iter;
	GtkListStore *liststore = NULL;
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	
	if(display_data)
		local_display_data = display_data;
	
	if(!table) {
		for(i=0; i<SORTID_CNT+1; i++) {
			memcpy(&popup_data_part[i], &display_data_part[i], 
			       sizeof(display_data_t));
		}
		return;
	}
	if(new_part_ptr && toggled)
		goto got_toggled;
	
	if((error_code = get_new_info_part(&new_part_ptr))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		if(!display_widget)
			goto display_it;
		return;
	}
	
got_toggled:
	if(display_widget)
		gtk_widget_destroy(display_widget);

	if (error_code != SLURM_SUCCESS) {
		sprintf(error_char, "slurm_load_partitions: %s",
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
	if (new_part_ptr)
		recs = new_part_ptr->record_count;
	else
		recs = 0;
	
	tree_view = GTK_TREE_VIEW(gtk_tree_view_new());
	display_widget = gtk_widget_ref(GTK_WIDGET(tree_view));

	g_signal_connect(G_OBJECT(tree_view), "row-activated",
			 G_CALLBACK(row_clicked_part),
			 new_part_ptr);
	g_signal_connect(G_OBJECT(tree_view), "button-press-event",
			 G_CALLBACK(_set_up_button),
			 new_part_ptr);
	
	gtk_table_attach_defaults(GTK_TABLE(table), 
				  GTK_WIDGET(tree_view),
				  0, 1, 0, 1); 
	gtk_widget_show(GTK_WIDGET(tree_view));
	
	liststore = create_liststore(display_data_part, SORTID_CNT);

	load_header(tree_view, display_data_part);
	for (i = 0; i < recs; i++) {
		j = 0;
		part = new_part_ptr->partition_array[i];
		
		if (!part.nodes || (part.nodes[0] == '\0'))
			continue;	/* empty partition */
		
				
		while (part.node_inx[j] >= 0) {
			/* set_grid(part.node_inx[j], */
/* 				 part.node_inx[j + 1], count); */
			j += 2;
		}
		_append_part_record(&part, liststore, &iter, i);
		count++;
			
	}
	gtk_tree_view_set_model(tree_view, GTK_TREE_MODEL(liststore));
	g_object_unref(GTK_TREE_MODEL(liststore));
	part_info_ptr = new_part_ptr;
	return;
}

extern void specific_info_part(popup_info_t *popup_win)
{
	int error_code = SLURM_SUCCESS;
	int i, j, recs;
	static partition_info_msg_t *part_info_ptr = NULL;
	static partition_info_msg_t *new_part_ptr = NULL;
	specific_info_t *spec_info = popup_win->spec_info;
	partition_info_t part;
	char error_char[50];
	GtkTreeIter iter;
	GtkListStore *liststore = NULL;
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	hostlist_t hostlist = NULL;
	hostlist_iterator_t itr = NULL;
	char *host = NULL;
	char *name = NULL;
	int found = 0;
	
	if(spec_info->display_widget)
		gtk_widget_destroy(spec_info->display_widget);
	else {
		gtk_event_box_set_above_child(
			GTK_EVENT_BOX(popup_win->event_box), 
			FALSE);
		g_signal_connect(G_OBJECT(popup_win->event_box), 
				 "button-press-event",
				 G_CALLBACK(redo_popup),
				 local_display_data);
		
		label = gtk_label_new(spec_info->title);
		gtk_container_add(GTK_CONTAINER(popup_win->event_box), label);
		gtk_widget_show(label);
	}

	if(new_part_ptr && toggled)
		goto display_it;
	
	if((error_code = get_new_info_part(&new_part_ptr))
	   == SLURM_NO_CHANGE_IN_DATA) 
		goto display_it;

	if (error_code != SLURM_SUCCESS) {
		sprintf(error_char, "slurm_load_partitions: %s",
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
			
	if (new_part_ptr)
		recs = new_part_ptr->record_count;
	else
		recs = 0;
	
	tree_view = GTK_TREE_VIEW(gtk_tree_view_new());
	spec_info->display_widget = gtk_widget_ref(GTK_WIDGET(tree_view));

	g_signal_connect(G_OBJECT(tree_view), "row-activated",
			 G_CALLBACK(row_clicked_part),
			 new_part_ptr);
	g_signal_connect(G_OBJECT(tree_view), "button-press-event",
			 G_CALLBACK(_set_up_button),
			 new_part_ptr);
	
	gtk_table_attach_defaults(popup_win->table, 
				  GTK_WIDGET(tree_view),
				  0, 1, 0, 1); 
	gtk_widget_show(GTK_WIDGET(tree_view));
	
	liststore = create_liststore(display_data_part, SORTID_CNT);

	load_header(tree_view, display_data_part);

	switch(spec_info->type) {
	case NODE_PAGE:
		hostlist = hostlist_create((char *)spec_info->data);	
		itr = hostlist_iterator_create(hostlist);
		name = hostlist_next(itr);
		hostlist_iterator_destroy(itr);
		hostlist_destroy(hostlist);
		if(name == NULL) {
			g_print("nodelist was empty");
			return;
		}		
		break;
	case JOB_PAGE:
		name = strdup((char *)spec_info->data);
		break;
	default:
		g_print("Unkown type");
		break;
	} 

	for (i = 0; i < recs; i++) {
		j = 0;
		part = new_part_ptr->partition_array[i];
		
		if (!part.nodes || (part.nodes[0] == '\0'))
			continue;	/* empty partition */
		switch(spec_info->type) {
		case NODE_PAGE:
			hostlist = hostlist_create(part.nodes);	
			itr = hostlist_iterator_create(hostlist);
			found = 0;
			while((host = hostlist_next(itr)) != NULL) { 
				if(!strcmp(host, name)) {
					free(host);
					found = 1;
					break; 
				}
				free(host);
			}
			hostlist_iterator_destroy(itr);
			hostlist_destroy(hostlist);
			break;
		case JOB_PAGE:
			if(!strcmp(part.name, name)) 
				found = 1;
			break;
		default:
			g_print("Unkown type");
			break;
		}
		
		if(found)
			_append_part_record(&part, liststore, &iter, i);
	}
	free(name);
		
	gtk_tree_view_set_model(tree_view, GTK_TREE_MODEL(liststore));
	g_object_unref(GTK_TREE_MODEL(liststore));
	part_info_ptr = new_part_ptr;
	return;
}

extern void set_menus_part(GtkTreeView *tree_view, GtkTreePath *path, 
			   GtkMenu *menu, int type)
{
	switch(type) {
	case TAB_CLICKED:
		make_fields_menu(menu, display_data_part);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_part);
		break;
	case POPUP_CLICKED:
		make_popup_fields_menu(menu, popup_data_part);
		break;
	default:
		g_error("UNKNOWN type %d given to set_fields\n", type);
	}
}

extern void row_clicked_part(GtkTreeView *tree_view,
			     GtkTreePath *path,
			     GtkTreeViewColumn *column,
			     gpointer user_data)
{
	partition_info_msg_t *new_part_ptr = (partition_info_msg_t *)user_data;
	partition_info_t *part_ptr = NULL;
	int line = get_row_number(tree_view, path);
	GtkWidget *popup = NULL;
	GtkWidget *label = NULL;
	char *info = NULL;
	if(line == -1) {
		g_error("problem getting line number");
		return;
	}
	
	part_ptr = &new_part_ptr->partition_array[line];
	if(!(info = slurm_sprint_partition_info(part_ptr, 0))) {
		info = xmalloc(100);
		sprintf(info, "Problem getting partition info for %s", 
			part_ptr->name);
	} 

	popup = gtk_dialog_new();

	label = gtk_label_new(info);
	gtk_box_pack_end(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			   label, TRUE, TRUE, 0);
	xfree(info);
	gtk_widget_show(label);
	
	gtk_widget_show(popup);
	
}

extern void popup_all_part(GtkTreeModel *model, GtkTreeIter *iter, int id)
{
	char *name = NULL;
	char *part = NULL;
	char title[100];
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	GtkScrolledWindow *window = NULL;
	GtkBin *bin = NULL;
	GtkViewport *view = NULL;
	GtkBin *bin2 = NULL;
	GtkTable *table = NULL;
	GtkWidget *popup = NULL;
			
	gtk_tree_model_get(model, iter, SORTID_NAME, &part, -1);
	switch(id) {
	case JOB_PAGE:
		snprintf(title, 100, "Job(s) in partition %s", part);
		break;
	case NODE_PAGE:
		snprintf(title, 100, "Node(s) in partition %s", part);
		break;
	case BLOCK_PAGE: 
		snprintf(title, 100, "Block(s) in partition %s", part);
		break;
	case ADMIN_PAGE: 
		snprintf(title, 100, "Admin page for partition %s", part);
		break;
	case SUBMIT_PAGE: 
		snprintf(title, 100, "Submit job in partition %s", part);
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
		popup_win = xmalloc(sizeof(popup_info_t));
		list_push(popup_list, popup_win);
	
		popup_win->spec_info = xmalloc(sizeof(specific_info_t));
		popup_win->popup = gtk_dialog_new();
		gtk_window_set_default_size(GTK_WINDOW(popup_win->popup), 
					    600, 400);
		gtk_window_set_title(GTK_WINDOW(popup_win->popup), "Sview");
	
		popup = popup_win->popup;

		popup_win->event_box = gtk_event_box_new();
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), 
				   popup_win->event_box, FALSE, FALSE, 0);
		
		
		window = create_scrolled_window();
		bin = GTK_BIN(&window->container);
		view = GTK_VIEWPORT(bin->child);
		bin2 = GTK_BIN(&view->bin);
		
		popup_win->table = GTK_TABLE(bin2->child);
	
		gtk_box_pack_end(GTK_BOX(GTK_DIALOG(popup)->vbox), 
				 GTK_WIDGET(window), TRUE, TRUE, 0);
		
		popup_win->spec_info->type = NODE_PAGE;
		popup_win->spec_info->title = xstrdup(title);
		popup_win->spec_info->display_widget = NULL;

		g_signal_connect(G_OBJECT(popup_win->popup), "delete_event",
			 G_CALLBACK(delete_popup), 
			 popup_win->spec_info->title);
		gtk_widget_show_all(popup_win->popup);
	}
	
	toggled = true;
			
	switch(id) {
	case JOB_PAGE:
		gtk_tree_model_get(model, iter, SORTID_NAME, &name, -1);
		get_info_job(table, NULL);
		break;
	case NODE_PAGE:
		gtk_tree_model_get(model, iter, SORTID_NODELIST, &name, -1);
		popup_win->spec_info->data = name;
		specific_info_node(popup_win);
		break;
	case BLOCK_PAGE: 
		gtk_tree_model_get(model, iter, SORTID_NAME, &name, -1);
		get_info_block(table, NULL);
		break;
	case ADMIN_PAGE: 
		gtk_tree_model_get(model, iter, SORTID_NAME, &name, -1);
		break;
	case SUBMIT_PAGE: 
		gtk_tree_model_get(model, iter, SORTID_NAME, &name, -1);
		break;
	default:
		g_print("part got %d\n", id);
	}
	
	toggled = false;	
}
