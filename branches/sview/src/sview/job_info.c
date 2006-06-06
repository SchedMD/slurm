/*****************************************************************************\
 *  job_info.c - Functions related to job display 
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

#include "src/common/uid.h"
#include "src/common/node_select.h"
#include "src/sview/sview.h"

#define _DEBUG 0
DEF_TIMERS;

enum { 
	SORTID_POS = POS_LOC,
	SORTID_JOBID, 
	SORTID_PARTITION, 
#ifdef HAVE_BG
	SORTID_BLOCK, 
#endif
	SORTID_USER, 
	SORTID_NAME,
	SORTID_STATE,
	SORTID_TIME,
	SORTID_NODES,
	SORTID_NODELIST,
	SORTID_CNT
};

static display_data_t display_data_job[] = {
	{SORTID_POS, NULL, FALSE, -1},
	{SORTID_JOBID, "JOBID", TRUE, -1},
	{SORTID_PARTITION, "PARTITION", TRUE, -1},
#ifdef HAVE_BG
	{SORTID_BLOCK, "BG BLOCK", TRUE, -1},
	
#endif
	{SORTID_USER, "USER", TRUE, -1},
	{SORTID_NAME, "NAME", TRUE, -1},
	{SORTID_STATE, "STATE", TRUE, -1},
	{SORTID_TIME, "TIME", TRUE, -1},
	{SORTID_NODES, "NODES", TRUE, -1},
#ifdef HAVE_BG
	{SORTID_NODELIST, "BP_LIST", TRUE, -1},
#else
	{SORTID_NODELIST, "NODELIST", TRUE, -1},
#endif
	{-1, NULL, FALSE, -1}};
static display_data_t *local_display_data = NULL;
time_t now_time;

static void _set_up_button(GtkTreeView *tree_view, GdkEventButton *event, 
			    gpointer user_data)
{
	local_display_data->user_data = user_data;
	button_pressed(tree_view, event, local_display_data);
}

static int _append_job_record(job_info_t *job_ptr,
			      GtkListStore *liststore, GtkTreeIter *iter,
			      int line)
{
	int printed = 0;
	char *nodes = NULL, time_buf[20];
	char tmp_cnt[7];
	char tmp_char[50];
	time_t time;
	uint32_t node_cnt = 0;
	uint16_t quarter = (uint16_t) NO_VAL;
	uint16_t nodecard = (uint16_t) NO_VAL;
	
	gtk_list_store_append(liststore, iter);
	gtk_list_store_set(liststore, iter, SORTID_POS, line, -1);
	
	snprintf(tmp_char, 50, "%d", job_ptr->job_id);
	gtk_list_store_set(liststore, iter, 
			   SORTID_JOBID, tmp_char, -1);
	gtk_list_store_set(liststore, iter, 
			   SORTID_PARTITION, job_ptr->partition, -1);
#ifdef HAVE_BG
	gtk_list_store_set(liststore, iter, 
			   SORTID_BLOCK, 
			   select_g_sprint_jobinfo(
				   job_ptr->select_jobinfo, 
				   time_buf, 
				   sizeof(time_buf), 
				   SELECT_PRINT_BG_ID), -1);
#endif
	gtk_list_store_set(liststore, iter, 
			   SORTID_USER, 
			   uid_to_string((uid_t)job_ptr->user_id), -1);
	gtk_list_store_set(liststore, iter, 
			   SORTID_NAME, job_ptr->name, -1);
	gtk_list_store_set(liststore, iter, 
			   SORTID_STATE, 
			   job_state_string(job_ptr->job_state), -1);

	if(!strcasecmp(job_ptr->nodes,"waiting...")) {
		sprintf(time_buf,"0:00:00");
	} else {
		time = now_time - job_ptr->start_time;
		snprint_time(time_buf, sizeof(time_buf), time);
	}
	gtk_list_store_set(liststore, iter, 
			   SORTID_TIME, time_buf, -1);

	convert_to_kilo(node_cnt, tmp_cnt);
	gtk_list_store_set(liststore, iter, 
			   SORTID_NODES, tmp_cnt, -1);
				
	nodes = job_ptr->nodes;
	if(quarter != (uint16_t) NO_VAL) {
		if(nodecard != (uint16_t) NO_VAL)
			snprintf(tmp_char, 50, "%s.%d.%d", 
				 nodes, quarter, nodecard);
		else
			snprintf(tmp_char, 50, "%s.%d", nodes, quarter);
		gtk_list_store_set(liststore, iter, 
				   SORTID_NODELIST, tmp_char, -1);
	} else
		gtk_list_store_set(liststore, iter, 
				   SORTID_NODELIST, nodes, -1);
	
	return printed;
}

extern void get_info_job(GtkTable *table, display_data_t *display_data)
{
	int error_code = SLURM_SUCCESS, i, j, recs;
	static int printed_jobs = 0;
	static int count = 0;
	static job_info_msg_t *job_info_ptr = NULL, *new_job_ptr = NULL;
	job_info_t job;
	uint16_t show_flags = 0;
	char error_char[50];
	GtkTreeIter iter;
	GtkListStore *liststore = NULL;
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	
	local_display_data = display_data;
	if(new_job_ptr && toggled)
		goto got_toggled;
	now_time = time(NULL);
	show_flags |= SHOW_ALL;
	if (job_info_ptr) {
		error_code = slurm_load_jobs(job_info_ptr->last_update,
				&new_job_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_job_info_msg(job_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_job_ptr = job_info_ptr;
		}
	} else
		error_code = slurm_load_jobs((time_t) NULL, &new_job_ptr, 
					     show_flags);
got_toggled:
	if(display_widget)
		gtk_widget_destroy(display_widget);

	if (error_code != SLURM_SUCCESS) {
		sprintf(error_char, "slurm_load_job: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(GTK_TABLE(table), 
					  label,
					  0, 1, 0, 1); 
		gtk_widget_show(label);	
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
		return;
	}

	if (new_job_ptr)
		recs = new_job_ptr->record_count;
	else
		recs = 0;

	tree_view = GTK_TREE_VIEW(gtk_tree_view_new());
	display_widget = gtk_widget_ref(GTK_WIDGET(tree_view));

	g_signal_connect(G_OBJECT(tree_view), "row-activated",
			 G_CALLBACK(row_clicked_job),
			 new_job_ptr);
	g_signal_connect(G_OBJECT(tree_view), "button-press-event",
			 G_CALLBACK(_set_up_button),
			 new_job_ptr);
	
	gtk_table_attach_defaults(GTK_TABLE(table), 
				  GTK_WIDGET(tree_view),
				  0, 1, 0, 1); 
	gtk_widget_show(GTK_WIDGET(tree_view));
	
	liststore = create_liststore(display_data_job, SORTID_CNT);

	load_header(tree_view, display_data_job);

	printed_jobs = 0;
	count = 0;
	for (i = 0; i < recs; i++) {
		job = new_job_ptr->job_array[i];
		
		if ((job.job_state != JOB_PENDING)
		    &&  (job.job_state != JOB_RUNNING)
		    &&  (job.job_state != JOB_SUSPENDED)
		    &&  ((job.job_state & JOB_COMPLETING) == 0))
			continue;	/* job has completed */

		if (job.node_inx[0] != -1) {
			job.num_nodes = 0;
			j = 0;
			while (job.node_inx[j] >= 0) {
				job.num_nodes +=
				    (job.node_inx[j + 1] + 1) -
				    job.node_inx[j];
				/* set_grid(job.node_inx[j], */
/* 					 job.node_inx[j + 1], count); */
				j += 2;
			}
			_append_job_record(&job, liststore, &iter, count);
		
			count++;			
		}
	}
		
	for (i = 0; i < recs; i++) {
		job = new_job_ptr->job_array[i];
		
		if (job.job_state != JOB_PENDING)
			continue;	/* job has completed */
		job.nodes = "waiting...";
		_append_job_record(&job, liststore, &iter, count);
		count++;			
	}
	
	//ba_system_ptr->ycord++;
	
	gtk_tree_view_set_model(tree_view, GTK_TREE_MODEL(liststore));
	g_object_unref(GTK_TREE_MODEL(liststore));
	job_info_ptr = new_job_ptr;
	return;
}


extern void set_fields_job(GtkMenu *menu)
{
	make_fields_menu(menu, display_data_job);
}

extern void row_clicked_job(GtkTreeView *tree_view,
			    GtkTreePath *path,
			    GtkTreeViewColumn *column,
			    gpointer user_data)
{
	job_info_msg_t *job_info_ptr = (job_info_msg_t *)user_data;
	job_info_t *job_ptr = NULL;
	char *jobid_char = NULL;
	int job_id = 0, i;
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	GtkWidget *popup = NULL;
	GtkWidget *label = NULL;
	GtkTreeIter iter;
	char *info = NULL;
	bool found = false;

	if(!model) {
		g_error("error getting the model from the tree_view");
		return;
	}
	
	if (!gtk_tree_model_get_iter(model, &iter, path)) {
		g_error("error getting iter from model");
		return;
	}	
	gtk_tree_model_get(model, &iter, SORTID_JOBID, &jobid_char, -1);
	job_id = atoi(jobid_char);

	for (i = 0; i < job_info_ptr->record_count; i++) {
		if(job_info_ptr->job_array[i].job_id == job_id) {
			job_ptr = &job_info_ptr->job_array[i];
			if(!(info = slurm_sprint_job_info(job_ptr, 0))) {
				info = xmalloc(100);
				sprintf(info, 
					"Problem getting job info for %d",
					job_ptr->job_id);
			}
			found = true;
			break;
		}
	}

	if(!found) {
		info = xmalloc(100);
		sprintf(info, "Job %d was not found!", job_ptr->job_id);
	}

	popup = gtk_dialog_new();

	label = gtk_label_new(info);
	gtk_box_pack_end(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			 label, TRUE, TRUE, 0);
	xfree(info);
	gtk_widget_show(label);
	
	gtk_widget_show(popup);
	
}

