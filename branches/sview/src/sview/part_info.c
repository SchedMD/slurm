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
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, -1},
	{G_TYPE_STRING, SORTID_NAME, "Partition", TRUE, -1},
	{G_TYPE_STRING, SORTID_AVAIL, "Availablity", TRUE, -1},
	{G_TYPE_STRING, SORTID_TIMELIMIT, "Time Limit", TRUE, -1},
	{G_TYPE_STRING, SORTID_NODES, "Nodes", TRUE, -1},
#ifdef HAVE_BG
	{G_TYPE_STRING, SORTID_NODELIST, "BP List", TRUE, -1},
#else
	{G_TYPE_STRING, SORTID_NODELIST, "NodeList", TRUE, -1},
#endif
	{G_TYPE_NONE, -1, NULL, FALSE, -1}
};

static display_data_t options_data_part[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, -1},
	{G_TYPE_STRING, JOB_PAGE, "Jobs", TRUE, PARTITION_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Nodes", TRUE, PARTITION_PAGE},
#ifdef HAVE_BG
	{G_TYPE_STRING, BLOCK_PAGE, "Blocks", TRUE, PARTITION_PAGE},
#endif
	{G_TYPE_STRING, SUBMIT_PAGE, "Job Submit", TRUE, PARTITION_PAGE},
	{G_TYPE_STRING, ADMIN_PAGE, "Admin", TRUE, PARTITION_PAGE},
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
	if(!table)
		return;
	if(new_part_ptr && toggled)
		goto got_toggled;
	if (part_info_ptr) {
		error_code = slurm_load_partitions(part_info_ptr->last_update, 
						   &new_part_ptr, SHOW_ALL);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg(part_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_part_ptr = part_info_ptr;
			return;
		}
	} else {
		error_code = slurm_load_partitions((time_t) NULL, 
						   &new_part_ptr, SHOW_ALL);
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
	GtkScrolledWindow *window = create_scrolled_window();
	GtkBin *bin = GTK_BIN(&window->container);
	GtkViewport *view = GTK_VIEWPORT(bin->child);
	GtkBin *bin2 = GTK_BIN(&view->bin);
	GtkTable *table = GTK_TABLE(bin2->child);
	GtkWidget *popup = gtk_dialog_new();
	GtkWidget *event_box = gtk_event_box_new();
	
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			   event_box, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			 GTK_WIDGET(window), TRUE, TRUE, 0);
	
	toggled = true;
	
	switch(id) {
	case JOB_PAGE:
		gtk_tree_model_get(model, iter, SORTID_NAME, &name, -1);
		get_info_job(table, NULL);
		break;
	case NODE_PAGE:
		gtk_tree_model_get(model, iter, SORTID_NODELIST, &name, -1);
		specific_info_node(table, event_box, name);
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
		g_print("got %d\n", id);
	}
	
	gtk_window_set_default_size(GTK_WINDOW(popup), 600, 400);
	gtk_window_set_title(GTK_WINDOW(popup), "Sview");
	gtk_widget_show_all(popup);

	toggled = false;	
}
