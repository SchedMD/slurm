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
#include "src/common/parse_time.h"

#define _DEBUG 0
DEF_TIMERS;

enum { 
	SORTID_POS = POS_LOC,
	SORTID_JOBID, 
	SORTID_ALLOC, 
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
	SORTID_REQ_NODELIST,
	SORTID_EXC_NODELIST,
	SORTID_CONTIGUOUS,
	SORTID_SUBMIT,
	SORTID_START,
	SORTID_END,
	SORTID_TIMELIMIT,
	SORTID_SUSPEND,
	SORTID_PRIORITY,
	SORTID_NUM_PROCS,
	SORTID_TASKS,
	SORTID_SHARED,
	SORTID_CPUS_PER_TASK,
	SORTID_REQ_PROCS,
	SORTID_MIN_NODES,
	SORTID_MIN_PROCS,
	SORTID_MIN_MEM,
	SORTID_TMP_DISK,
	SORTID_NICE,
	SORTID_ACCOUNT,
	SORTID_REASON,
	SORTID_FEATURES,
	SORTID_DEPENDENCY,
#ifdef HAVE_BG
	SORTID_GEOMETRY,
	SORTID_ROTATE,
	SORTID_CONNECTION,
#endif
	SORTID_UPDATED,
	SORTID_CNT
};

static display_data_t display_data_job[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, -1, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_INT, SORTID_JOBID, "JobID", TRUE, -1, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_INT, SORTID_ALLOC, NULL, FALSE, -1, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_PARTITION, "Partition", TRUE, 1, refresh_job,
	 create_model_job, admin_edit_job},
#ifdef HAVE_BG
	{G_TYPE_STRING, SORTID_BLOCK, "BG Block", TRUE, -1, refresh_job,
	 create_model_job, admin_edit_job},
#endif
	{G_TYPE_STRING, SORTID_USER, "User", TRUE, -1, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NAME, "Name", TRUE, 1, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_STATE, "State", TRUE, 0, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIME, "Running Time", TRUE, -1, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NODES, "Nodes", TRUE, -1, refresh_job,
	 create_model_job, admin_edit_job},
#ifdef HAVE_BG
	{G_TYPE_STRING, SORTID_NODELIST, "BP List", TRUE, -1, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_REQ_NODELIST, "Requested BP List",
	 FALSE, -1, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_EXC_NODELIST, "Excluded BP List",
	 FALSE, -1, refresh_job, create_model_job, admin_edit_job},
#else
	{G_TYPE_STRING, SORTID_NODELIST, "Nodelist", TRUE, -1, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_REQ_NODELIST, "Requested NodeList", 
	 FALSE, 1, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_EXC_NODELIST, "Excluded NodeList", 
	 FALSE, -1, refresh_job, create_model_job, admin_edit_job},
#endif
	{G_TYPE_STRING, SORTID_CONTIGUOUS, "Contiguous", FALSE, 0, 
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_SUBMIT, "Submit Time", FALSE, -1, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_START, "Start Time", FALSE, 1, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_END, "End Time", FALSE, -1, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIMELIMIT, "Time limit", FALSE, 1, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_SUSPEND, "Suspend Time", 
	 FALSE, -1, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_INT, SORTID_PRIORITY, "Priority", FALSE, 1, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NUM_PROCS, "Num Processors", 
	 FALSE, -1, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TASKS, "Num Tasks", 
	 FALSE, -1, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_INT, SORTID_SHARED, "Shared", FALSE, -1, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_CPUS_PER_TASK, "Cpus per Task", 
	 FALSE, -1, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_REQ_PROCS, "Requested Procs", 
	 FALSE, 1, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_MIN_NODES, "Min Nodes", 
	 FALSE, 1, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_MIN_PROCS, "Min Procs", 
	 FALSE, 1, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_MIN_MEM, "Min Memory", 
	 FALSE, 1, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TMP_DISK, "Tmp Disk", 
	 FALSE, 1, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NICE, "Nice", 
	 FALSE, 1, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_ACCOUNT, "Account Charged", 
	 FALSE, 1, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_REASON, "Wait Reason", 
	 FALSE, 1, refresh_job, create_model_job, admin_edit_job},	
	{G_TYPE_STRING, SORTID_FEATURES, "Features", 
	 FALSE, 1, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_DEPENDENCY, "Dependency", 
	 FALSE, 1, refresh_job, create_model_job, admin_edit_job},
#ifdef HAVE_BG
	{G_TYPE_STRING, SORTID_GEOMETRY, "Geometry", 
	 FALSE, 1, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_ROTATE, "Rotate", 
	 FALSE, 0, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_CONNECTION, "Connection", 
	 FALSE, 0, refresh_job, create_model_job, admin_edit_job},
#endif
	{G_TYPE_INT, SORTID_UPDATED, NULL, FALSE, -1, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_NONE, -1, NULL, FALSE, -1}
};

static display_data_t options_data_job[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, -1},
	{G_TYPE_STRING, INFO_PAGE, "Full Info", TRUE, JOB_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Partition", TRUE, JOB_PAGE},
#ifdef HAVE_BG
	{G_TYPE_STRING, BLOCK_PAGE, "Blocks", TRUE, JOB_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Base Partitions", TRUE, JOB_PAGE},
#else
	{G_TYPE_STRING, NODE_PAGE, "Nodes", TRUE, JOB_PAGE},
#endif
	{G_TYPE_NONE, -1, NULL, FALSE, -1}
};

static display_data_t *local_display_data = NULL;

static void _update_info_step(job_step_info_response_msg_t *step_info_ptr,
			      int jobid,
			      GtkTreeModel *model, 
			      GtkTreeIter *step_iter,
			      GtkTreeIter *iter);


static int _nodes_in_list(char *node_list)
{
	hostset_t host_set = hostset_create(node_list);
	int count = hostset_count(host_set);
	hostset_destroy(host_set);
	return count;
}

static int _get_node_cnt(job_info_t * job)
{
	int node_cnt = 0;
	bool completing = job->job_state & JOB_COMPLETING;
	uint16_t base_job_state = job->job_state & (~JOB_COMPLETING);

	if (base_job_state == JOB_PENDING || completing) {
		node_cnt = _nodes_in_list(job->req_nodes);
		node_cnt = MAX(node_cnt, job->num_nodes);
	} else
		node_cnt = _nodes_in_list(job->nodes);
	return node_cnt;
}

static void _update_job_record(job_info_t *job_ptr, 
			       job_step_info_response_msg_t *step_info_ptr, 
			       GtkTreeStore *treestore,
			       GtkTreeIter *iter)
{
	char *nodes = NULL, time_buf[20];
	char tmp_cnt[7];
	char tmp_char[50];
	time_t now_time = time(NULL);
	uint16_t quarter = (uint16_t) NO_VAL;
	uint16_t nodecard = (uint16_t) NO_VAL;
	GtkTreeIter step_iter;
	int childern = 0;
	uint32_t node_cnt = 0;
	
	gtk_tree_store_set(treestore, iter, SORTID_UPDATED, 1, -1);
	if(!job_ptr->nodes || !strcasecmp(job_ptr->nodes,"waiting...")) {
		sprintf(time_buf,"0:00:00");
		nodes = "waiting...";
	} else {
		now_time -= job_ptr->start_time;
		snprint_time(time_buf, sizeof(time_buf), now_time);
		nodes = job_ptr->nodes;	
	}
	gtk_tree_store_set(treestore, iter, 
			   SORTID_TIME, time_buf, -1);
		
	gtk_tree_store_set(treestore, iter, SORTID_ALLOC, 1, -1);
	gtk_tree_store_set(treestore, iter, 
			   SORTID_JOBID, job_ptr->job_id, -1);
	gtk_tree_store_set(treestore, iter, 
			   SORTID_PARTITION, job_ptr->partition, -1);
#ifdef HAVE_BG
	gtk_tree_store_set(treestore, iter, 
			   SORTID_BLOCK, 
			   select_g_sprint_jobinfo(
				   job_ptr->select_jobinfo, 
				   time_buf, 
				   sizeof(time_buf), 
				   SELECT_PRINT_BG_ID), -1);
#endif
	gtk_tree_store_set(treestore, iter, 
			   SORTID_USER, 
			   uid_to_string((uid_t)job_ptr->user_id), -1);
	gtk_tree_store_set(treestore, iter, 
			   SORTID_NAME, job_ptr->name, -1);
	gtk_tree_store_set(treestore, iter, 
			   SORTID_STATE, 
			   job_state_string(job_ptr->job_state), -1);
	
#ifdef HAVE_BG
	select_g_get_jobinfo(job_ptr->select_jobinfo, 
			     SELECT_DATA_NODE_CNT, 
			     &node_cnt);
#endif
	if(!node_cnt)
		node_cnt = _get_node_cnt(job_ptr);

	convert_num_unit((float)node_cnt, tmp_cnt, UNIT_NONE);
	gtk_tree_store_set(treestore, iter, 
			   SORTID_NODES, tmp_cnt, -1);

	convert_num_unit((float)job_ptr->num_procs, tmp_cnt, UNIT_NONE);
	gtk_tree_store_set(treestore, iter, 
			   SORTID_NUM_PROCS, tmp_cnt, -1);
	
	if(quarter != (uint16_t) NO_VAL) {
		if(nodecard != (uint16_t) NO_VAL)
			snprintf(tmp_char, 50, "%s.%d.%d", 
				 nodes, quarter, nodecard);
		else
			snprintf(tmp_char, 50, "%s.%d", nodes, quarter);
		gtk_tree_store_set(treestore, iter, 
				   SORTID_NODELIST, tmp_char, -1);
	} else
		gtk_tree_store_set(treestore, iter, 
				   SORTID_NODELIST, nodes, -1);

	childern = gtk_tree_model_iter_children(GTK_TREE_MODEL(treestore),
						&step_iter, iter);
	if(gtk_tree_model_iter_children(GTK_TREE_MODEL(treestore),
					&step_iter, iter))
		_update_info_step(step_info_ptr, job_ptr->job_id, 
				  GTK_TREE_MODEL(treestore), &step_iter, iter);
	else
		_update_info_step(step_info_ptr, job_ptr->job_id, 
				  GTK_TREE_MODEL(treestore), NULL, iter);
		
	return;
}

static void _update_step_record(job_step_info_t *step_ptr, 
				GtkTreeStore *treestore,
				GtkTreeIter *iter)
{
	char *nodes = NULL, time_buf[20];
	char tmp_cnt[7];
	char tmp_char[50];
	time_t now_time = time(NULL);
	uint16_t quarter = (uint16_t) NO_VAL;
	uint16_t nodecard = (uint16_t) NO_VAL;
	enum job_states state;

	gtk_tree_store_set(treestore, iter, SORTID_UPDATED, 1, -1);
	if(!step_ptr->nodes 
	   || !strcasecmp(step_ptr->nodes,"waiting...")) {
		sprintf(time_buf,"0:00:00");
		nodes = "waiting...";
		state = JOB_PENDING;
	} else {
		now_time -= step_ptr->start_time;
		snprint_time(time_buf, sizeof(time_buf), now_time);
		nodes = step_ptr->nodes;
		convert_num_unit((float)_nodes_in_list(nodes), 
				 tmp_cnt, UNIT_NONE);
		gtk_tree_store_set(treestore, iter, 
				   SORTID_NODES, tmp_cnt, -1);
		state = JOB_RUNNING;
	}

	gtk_tree_store_set(treestore, iter,
			   SORTID_STATE,
			   job_state_string(state), -1);
	
	gtk_tree_store_set(treestore, iter, 
			   SORTID_TIME, time_buf, -1);
	
	gtk_tree_store_set(treestore, iter, SORTID_ALLOC, 0, -1);
	gtk_tree_store_set(treestore, iter, 
			   SORTID_JOBID, step_ptr->step_id, -1);
	gtk_tree_store_set(treestore, iter, 
			   SORTID_PARTITION, step_ptr->partition, -1);
/* #ifdef HAVE_BG */
/* 	gtk_tree_store_set(treestore, iter,  */
/* 			   SORTID_BLOCK,  */
/* 			   select_g_sprint_jobinfo( */
/* 				   step_ptr->select_jobinfo,  */
/* 				   time_buf,  */
/* 				   sizeof(time_buf),  */
/* 				   SELECT_PRINT_BG_ID), -1); */
/* #endif */
	gtk_tree_store_set(treestore, iter, 
			   SORTID_USER, 
			   uid_to_string((uid_t)step_ptr->user_id), -1);
	gtk_tree_store_set(treestore, iter, 
			   SORTID_NAME, step_ptr->name, -1);
		
	convert_num_unit((float)step_ptr->num_tasks, tmp_cnt, UNIT_NONE);
	gtk_tree_store_set(treestore, iter, 
			   SORTID_TASKS, tmp_cnt, -1);

	gtk_tree_store_set(treestore, iter, 
			   SORTID_NUM_PROCS, tmp_cnt, -1);

	if(quarter != (uint16_t) NO_VAL) {
		if(nodecard != (uint16_t) NO_VAL)
			snprintf(tmp_char, 50, "%s.%d.%d", 
				 nodes, quarter, nodecard);
		else
			snprintf(tmp_char, 50, "%s.%d", nodes, quarter);
		gtk_tree_store_set(treestore, iter, 
				   SORTID_NODELIST, tmp_char, -1);
	} else
		gtk_tree_store_set(treestore, iter, 
				   SORTID_NODELIST, nodes, -1);
		
	return;
}

static void _append_job_record(job_info_t *job_ptr,
			       job_step_info_response_msg_t *step_info_ptr, 
			       GtkTreeStore *treestore, GtkTreeIter *iter,
			       int line)
{
	gtk_tree_store_append(treestore, iter, NULL);
	gtk_tree_store_set(treestore, iter, SORTID_POS, line, -1);
	_update_job_record(job_ptr, step_info_ptr, treestore, iter);	
}

static void _append_step_record(job_step_info_t *step_ptr,
				GtkTreeStore *treestore, GtkTreeIter *iter,
				int jobid)
{
	GtkTreeIter step_iter;

	gtk_tree_store_append(treestore, &step_iter, iter);
	gtk_tree_store_set(treestore, &step_iter, SORTID_POS, jobid, -1);
	_update_step_record(step_ptr, treestore, &step_iter);
}

static void _update_info_step(job_step_info_response_msg_t *step_info_ptr,
			      int jobid,
			      GtkTreeModel *model, 
			      GtkTreeIter *step_iter,
			      GtkTreeIter *iter)
{
	job_step_info_t step;
	int stepid = 0;
	int i;
	GtkTreeIter first_step_iter;
	int set = 0;

	memset(&first_step_iter, 0, sizeof(GtkTreeIter));

	/* make sure all the steps are still here */
	if (step_iter) {
		first_step_iter = *step_iter;
		while(1) {
			gtk_tree_store_set(GTK_TREE_STORE(model), step_iter, 
					   SORTID_UPDATED, 0, -1);	
			if(!gtk_tree_model_iter_next(model, step_iter)) {
				break;
			}
		}
		memcpy(step_iter, &first_step_iter, sizeof(GtkTreeIter));
	}
	for (i = 0; i < step_info_ptr->job_step_count; i++) {
		step = step_info_ptr->job_steps[i];
		if(step.job_id != jobid)
			continue;
		/* get the iter, or find out the list is 
		   empty goto add */
		if (!step_iter) {
			goto adding;
		} else {
			memcpy(step_iter, &first_step_iter, 
			       sizeof(GtkTreeIter));
		}
		while(1) {
			/* search for the jobid and check to see if 
			   it is in the list */
			gtk_tree_model_get(model, step_iter, SORTID_JOBID, 
					   &stepid, -1);
			if(stepid == (int)step.step_id) {
				/* update with new info */
				_update_step_record(&step,
						    GTK_TREE_STORE(model), 
						    step_iter);
				goto found;
			}			
			
			if(!gtk_tree_model_iter_next(model, step_iter)) {
				step_iter = NULL;
				break;
			}
		}
	adding:
		_append_step_record(&step, GTK_TREE_STORE(model), 
				    iter, jobid);
	found:
		;
	}
	if(set) {
		step_iter = &first_step_iter;
		/* clear all steps that aren't active */
		while(1) {
			gtk_tree_model_get(model, step_iter, 
					   SORTID_UPDATED, &i, -1);
			if(!i) {
				if(!gtk_tree_store_remove(
					   GTK_TREE_STORE(model), 
					   step_iter))
					break;
				else
					continue;
			}
			if(!gtk_tree_model_iter_next(model, step_iter)) {
				break;
			}
		}
	}
	return;
}			       

static void _update_info_job(job_info_msg_t *job_info_ptr, 
			     job_step_info_response_msg_t *step_info_ptr, 
			     GtkTreeView *tree_view,
			     specific_info_t *spec_info)
{
	GtkTreePath *path = gtk_tree_path_new_first();
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	GtkTreeIter iter;
	int jobid = 0;
	int i;
	job_info_t job;
	int line = 0;
	char name[30];
	char *host = NULL, *host2 = NULL;
	hostlist_t hostlist = NULL;
	int found = 0;
	job_step_num_t *job_step = NULL;

	/* make sure all the jobs are still here */
	if (gtk_tree_model_get_iter(model, &iter, path)) {
		while(1) {
			gtk_tree_store_set(GTK_TREE_STORE(model), &iter, 
					   SORTID_UPDATED, 0, -1);	
			if(!gtk_tree_model_iter_next(model, &iter)) {
				break;
			}
		}
	}
	
	for (i = 0; i < job_info_ptr->record_count; i++) {
		job = job_info_ptr->job_array[i];
		/* get the iter, or find out the list is empty goto add */
		if (!gtk_tree_model_get_iter(model, &iter, path)) {
			goto adding;
		}

		while(1) {
			/* search for the jobid and check to see if 
			   it is in the list */
			gtk_tree_model_get(model, &iter, SORTID_JOBID, 
					   &jobid, -1);
			if(jobid == job.job_id) {
				/* We don't really want to display the 
				   completed jobs so well remove it 
				   from the list and continue to 
				   the next job */
				if ((job.job_state != JOB_PENDING)
				    && (job.job_state != JOB_RUNNING)
				    && (job.job_state != JOB_SUSPENDED)
				    && (!(job.job_state & JOB_COMPLETING))) {
					gtk_tree_store_remove(
						GTK_TREE_STORE(model), 
						&iter);
					goto found; /* job has completed */
				}
				/* update with new info */
				_update_job_record(&job, step_info_ptr,
						   GTK_TREE_STORE(model), 
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
		if ((job.job_state != JOB_PENDING)
		    && (job.job_state != JOB_RUNNING)
		    && (job.job_state != JOB_SUSPENDED)
		    && (!(job.job_state & JOB_COMPLETING))) 
			continue;	/* job has completed */

		if(spec_info) {
			switch(spec_info->type) {
			case JOB_PAGE:
				job_step = (job_step_num_t *)spec_info->data;
				if(job.job_id != job_step->jobid) {
					continue;
				}
				break;	
			case PART_PAGE:
				if(strcmp((char *)spec_info->data,
					  job.partition))
					continue;
				break;
			case BLOCK_PAGE:
				select_g_sprint_jobinfo(
					job.select_jobinfo, 
					name, 
					sizeof(name), 
					SELECT_PRINT_BG_ID);
				if(strcmp((char *)spec_info->data, name))
					continue;
				break;
			case NODE_PAGE:
				if(!job.nodes)
					continue;

				hostlist = hostlist_create(
					(char *)spec_info->data);	
				host = hostlist_shift(hostlist);
				hostlist_destroy(hostlist);
				if(!host)
					continue;

				hostlist = hostlist_create(job.nodes);	
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
			default:
				continue;
			}
		}	
		_append_job_record(&job, step_info_ptr, GTK_TREE_STORE(model), 
				   &iter, line);
	found:
		;
	}
	if(host)
		free(host);
	gtk_tree_path_free(path);
	/* remove all old jobs */
	remove_old(model, SORTID_UPDATED);
	return;	
}

void _display_info_job(job_info_msg_t *job_info_ptr,
		       job_step_info_response_msg_t *step_info_ptr,
		       popup_info_t *popup_win)
{
	int i;
	job_info_t job;
	job_step_info_t step;
	specific_info_t *spec_info = popup_win->spec_info;
	job_step_num_t *job_step = (job_step_num_t *)spec_info->data;
	char *info = NULL;
	int found = 0;
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
	
	if(job_step->stepid == NO_VAL) {
		for (i = 0; i < job_info_ptr->record_count; i++) {
			job = job_info_ptr->job_array[i];
			if(job.job_id == job_step->jobid) {
				if(!(info = slurm_sprint_job_info(&job, 0))) {
					info = xmalloc(100);
					snprintf(info, 100, 
						 "Problem getting job "
						 "info for %d",
						 job.job_id);
				}
				found = 1;
				break;
			}
		}
	} else {
		for (i = 0; i < step_info_ptr->job_step_count; i++) {
			if((step_info_ptr->job_steps[i].job_id 
			    == job_step->jobid)
			   && step_info_ptr->job_steps[i].step_id 
			   == job_step->stepid) {
				step = step_info_ptr->job_steps[i];
				if(!(info = slurm_sprint_job_step_info(
					     &step, 0))) {
					info = xmalloc(100);
					snprintf(info, 100,
						 "Problem getting job "
						 "info for %d.%d",
						 step.job_id, 
						 step.step_id);
				}
			}
			found = 1;
			break;
		}
	}
	
	if(!found) {
		char *temp = "JOB ALREADY FINISHED OR NOT FOUND\n";
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

extern void refresh_job(GtkAction *action, gpointer user_data)
{
	popup_info_t *popup_win = (popup_info_t *)user_data;
	xassert(popup_win != NULL);
	xassert(popup_win->spec_info != NULL);
	xassert(popup_win->spec_info->title != NULL);
	popup_win->force_refresh = 1;
	specific_info_job(popup_win);
}

extern int get_new_info_job(job_info_msg_t **info_ptr, 
			    int force)
{
	static job_info_msg_t *job_info_ptr = NULL, *new_job_ptr = NULL;
	uint16_t show_flags = 0;
	int error_code = SLURM_SUCCESS;
	time_t now = time(NULL);
	static time_t last;
		
	if(!force && ((now - last) < global_sleep_time)) {
		error_code = SLURM_NO_CHANGE_IN_DATA;
		*info_ptr = job_info_ptr;
		return error_code;
	}
	last = now;
	show_flags |= SHOW_ALL;
	if (job_info_ptr) {
		error_code = slurm_load_jobs(job_info_ptr->last_update,
					     &new_job_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_job_info_msg(job_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_job_ptr = job_info_ptr;
		}
	} else
		error_code = slurm_load_jobs((time_t) NULL, &new_job_ptr, 
					     show_flags);
	job_info_ptr = new_job_ptr;
	*info_ptr = new_job_ptr;
	return error_code;
}

extern int get_new_info_job_step(job_step_info_response_msg_t **info_ptr, 
				 int force)
{
	static job_step_info_response_msg_t *old_step_ptr = NULL;
	static job_step_info_response_msg_t *new_step_ptr = NULL;
	uint16_t show_flags = 0;
	int error_code = SLURM_SUCCESS;
	time_t now = time(NULL);
	static time_t last;
		
	if(!force && ((now - last) < global_sleep_time)) {
		error_code = SLURM_NO_CHANGE_IN_DATA;
		*info_ptr = old_step_ptr;
		return error_code;
	}
	last = now;
	show_flags |= SHOW_ALL;
	if (old_step_ptr) {
		error_code = slurm_get_job_steps(old_step_ptr->last_update, 
						 0, 0, &new_step_ptr, 
						 show_flags);
		if (error_code ==  SLURM_SUCCESS)
			slurm_free_job_step_info_response_msg(old_step_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_step_ptr = old_step_ptr;
		}
	} else
		error_code = slurm_get_job_steps((time_t) NULL, 0, 0, 
						 &new_step_ptr, show_flags);
	old_step_ptr = new_step_ptr;
	*info_ptr = new_step_ptr;
	return error_code;
}

extern GtkListStore *create_model_job(int type)
{
	GtkListStore *model = NULL;
	GtkTreeIter iter;
	
	switch(type) {
	case SORTID_TIMELIMIT:
		break;
	case SORTID_PRIORITY:
		break;
	case SORTID_NICE:
		break;
	case SORTID_NUM_PROCS:
		break;
	case SORTID_MIN_NODES:
		break;
	case SORTID_MIN_PROCS:
		break;
	case SORTID_MIN_MEM:
		break;
	case SORTID_TMP_DISK:
		break;
	case SORTID_PARTITION:
		break;
	case SORTID_NAME:
		break;
	case SORTID_SHARED:
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
	case SORTID_CONTIGUOUS:
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
	case SORTID_REQ_NODELIST:
		break;
	case SORTID_FEATURES:
		break;
	case SORTID_ACCOUNT:
		break;
	case SORTID_DEPENDENCY:
		break;
#ifdef HAVE_BG
	case SORTID_GEOMETRY:
		break;
	case SORTID_ROTATE:
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
	case SORTID_CONNECTION:
		model = gtk_list_store_new(1, G_TYPE_STRING);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "torus",
				   -1);	
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "mesh",
				   -1);	
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "nav",
				   -1);	
		break;
#endif
	case SORTID_START:
		break;
	default:
		break;
	}

	return model;
}

extern void admin_edit_job(GtkCellRendererText *cell,
			   const char *path_string,
			   const char *new_text,
			   gpointer data)
{
	GtkTreeStore *treestore = GTK_TREE_STORE(data);
	GtkTreePath *path = gtk_tree_path_new_from_string(path_string);
	GtkTreeIter iter;
	job_desc_msg_t job_msg;
	
	char *temp = NULL;
	char *type = NULL;
	int stepid = NO_VAL;
	int column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell), 
						       "column"));
	
	if(!new_text || !strcmp(new_text, ""))
		goto no_input;

	gtk_tree_model_get_iter(GTK_TREE_MODEL(treestore), &iter, path);

	slurm_init_job_desc_msg (&job_msg);	
	gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter, 
			   SORTID_JOBID, &job_msg.job_id, -1);
	gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter, 
			   SORTID_ALLOC, &stepid, -1);
	if(stepid)
		stepid = NO_VAL;
	else {
		stepid = job_msg.job_id;
		gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter, 
				   SORTID_POS, &job_msg.job_id, -1);
	}
	
	switch(column) {
	case SORTID_TIMELIMIT:
		if ((strcasecmp(new_text,"infinite") == 0))
			job_msg.time_limit = INFINITE;
		else
			job_msg.time_limit = 
				(uint32_t)strtol(new_text, (char **)NULL, 10);
		temp = (char *)new_text;
		type = "timelimit";
		if((int32_t)job_msg.time_limit <= 0
		   && job_msg.time_limit != INFINITE)
			goto print_error;
		break;
	case SORTID_PRIORITY:
		job_msg.priority = 
				(uint32_t)strtol(new_text, (char **)NULL, 10);
		temp = (char *)new_text;
		type = "priority";
		if((int32_t)job_msg.priority < 0)
			goto print_error;
		break;
	case SORTID_NICE:
		job_msg.nice = (uint32_t)strtol(new_text, (char **)NULL, 10);
		if (abs(job_msg.nice) > NICE_OFFSET) {
			//error("Invalid nice value, must be between "
			//      "-%d and %d", NICE_OFFSET, NICE_OFFSET);
			goto print_error;
		}
		job_msg.nice += NICE_OFFSET;
		temp = (char *)new_text;
		type = "nice";
		break;
	case SORTID_REQ_PROCS:
		job_msg.num_procs = 
			(uint32_t)strtol(new_text, (char **)NULL, 10);
		temp = (char *)new_text;
		type = "requested procs";
		if((int32_t)job_msg.num_procs <= 0)
			goto print_error;
		break;
	case SORTID_MIN_NODES:
		job_msg.min_nodes = 
			(uint32_t)strtol(new_text, (char **)NULL, 10);
		temp = (char *)new_text;
		type = "min nodes";
		if((int32_t)job_msg.min_nodes <= 0)
			goto print_error;
		break;
	case SORTID_MIN_PROCS:
		job_msg.min_procs = 
			(uint32_t)strtol(new_text, (char **)NULL, 10);
		temp = (char *)new_text;
		type = "min procs";
		if((int32_t)job_msg.min_procs <= 0)
			goto print_error;
		break;
	case SORTID_MIN_MEM:
		job_msg.min_memory = 
			(uint32_t)strtol(new_text, (char **)NULL, 10);
		temp = (char *)new_text;
		type = "min memory";
		if((int32_t)job_msg.min_memory <= 0)
			goto print_error;
		break;
	case SORTID_TMP_DISK:
		job_msg.min_tmp_disk = 
			(uint32_t)strtol(new_text, (char **)NULL, 10);
		temp = (char *)new_text;
		type = "min tmp disk";
		if((int32_t)job_msg.min_tmp_disk <= 0)
			goto print_error;
		break;
	case SORTID_PARTITION:
		temp = (char *)new_text;
		job_msg.partition = temp;
		type = "partition";
		break;
	case SORTID_NAME:
		temp = (char *)new_text;
		job_msg.name = temp;
		type = "name";
		break;
	case SORTID_SHARED:
		if (!strcasecmp(new_text, "yes")) {
			job_msg.shared = 1;
			temp = "*";
		} else {
			job_msg.shared = 0;
			temp = "";
		}
		type = "shared";
		break;
	case SORTID_CONTIGUOUS:
		if (!strcasecmp(new_text, "yes")) {
			job_msg.contiguous = 1;
			temp = "*";
		} else {
			job_msg.contiguous = 0;
			temp = "";
		}
		type = "contiguous";	
		break;
	case SORTID_REQ_NODELIST:
		temp = (char *)new_text;
		job_msg.req_nodes = temp;
		type = "requested nodelist";
		break;
	case SORTID_FEATURES:
		temp = (char *)new_text;
		job_msg.features = temp;
		type = "features";
		break;
	case SORTID_ACCOUNT:
		temp = (char *)new_text;
		job_msg.account = temp;
		type = "account";
		break;
	case SORTID_DEPENDENCY:
		job_msg.dependency = 
			(uint32_t)strtol(new_text, (char **)NULL, 10);
		temp = (char *)new_text;
		type = "dependency";
		if((int32_t)job_msg.dependency <= 0)
			goto print_error;
		break;
#ifdef HAVE_BG
	case SORTID_GEOMETRY:
		{
			char* token, *delimiter = ",x", *next_ptr;
			int j;
			uint16_t geo[SYSTEM_DIMENSIONS];
			char* geometry_tmp = xstrdup(new_text);
			char* original_ptr = geometry_tmp;
		}
		token = strtok_r(geometry_tmp, delimiter, &next_ptr);
		for (j=0; j<SYSTEM_DIMENSIONS; j++) {
			if (token == NULL) {
				//error("insufficient dimensions in "
				//      "Geometry");
				goto print_error;
			}
			geo[j] = (uint16_t) atoi(token);
			if (geo[j] <= 0) {
				//error("invalid --geometry argument");
				xfree(original_ptr);
				goto print_error;
				break;
			}
			geometry_tmp = next_ptr;
			token = strtok_r(geometry_tmp, delimiter, 
					 &next_ptr);
		}
		if (token != NULL) {
			//error("too many dimensions in Geometry");
			xfree(original_ptr);
			goto print_error;
		}
		
		if (rc != 0) {
			for (j=0; j<SYSTEM_DIMENSIONS; j++)
				geo[j] = (uint16_t) NO_VAL;
			exit_code = 1;
		} else
			update_cnt++;
		select_g_set_jobinfo(job_msg.select_jobinfo,
				     SELECT_DATA_GEOMETRY,
				     (void *) &geo);
		temp = (char *)new_text;
		type = "geometry";
		break;
	case SORTID_ROTATE:
		{
			uint16_t rotate;
		}
		if (!strcasecmp(new_text, "yes")) {
			rotate = 1;
			temp = "*";
		} else {
			rotate = 0;
			temp = "";
		}
		select_g_set_jobinfo(job_msg.select_jobinfo,
				     SELECT_DATA_ROTATE,
				     (void *) &rotate);
		temp = (char *)new_text;
		type = "rotate";	
		break;
	case SORTID_CONNECTION:
		{
			uint16_t conn_type;
		}
		if (!strcasecmp(new_text, "torus")) {
			conn_type = SELECT_TORUS;
		} else if (!strcasecmp(new_text, "mesh")) {
			conn_type = SELECT_MESH;
		} else {
			conn_type = SELECT_NAV;
		}
		select_g_set_jobinfo(job_msg.select_jobinfo,
				     SELECT_DATA_CONN_TYPE,
				     (void *) &conn_type);
		temp = (char *)new_text;
		type = "connection";
		break;
#endif
	case SORTID_START:
		temp = (char *)new_text;
		job_msg.begin_time = parse_time(temp);
		type = "start time";
		break;
	default:
		break;
	}
	if(slurm_update_job(&job_msg) == SLURM_SUCCESS) {
		gtk_tree_store_set(treestore, &iter, column, temp, -1);
		temp = g_strdup_printf("Job %d %s changed to %s",
				       job_msg.job_id,
				       type,
				       new_text);
		display_edit_note(temp);
		g_free(temp);
	} else {
	print_error:
		temp = g_strdup_printf("Job %d %s can't be "
				       "set to %s",
				       job_msg.job_id,
				       type,
				       new_text);
		display_edit_note(temp);
		g_free(temp);
	}

no_input:
	gtk_tree_path_free (path);
		
	g_static_mutex_unlock(&sview_mutex);
}

extern void get_info_job(GtkTable *table, display_data_t *display_data)
{
	int job_error_code = SLURM_SUCCESS;
	int step_error_code = SLURM_SUCCESS;
	static int view = -1;
	static job_info_msg_t *job_info_ptr = NULL;
	static job_step_info_response_msg_t *step_info_ptr = NULL;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	
	if(display_data)
		local_display_data = display_data;
	if(!table) {
		display_data_job->set_menu = local_display_data->set_menu;
		return;
	}
	if(display_widget && toggled) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
		goto display_it;
	}

	if((job_error_code = get_new_info_job(&job_info_ptr, force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA){
		goto get_steps;
	}

	if (job_error_code != SLURM_SUCCESS) {
		if(view == ERROR_VIEW)
			goto end_it;
		if(display_widget)
			gtk_widget_destroy(display_widget);
		view = ERROR_VIEW;
		sprintf(error_char, "slurm_load_job: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(table, label, 0, 1, 0, 1); 
		gtk_widget_show(label);	
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
		goto end_it;
	}
get_steps:
	if((step_error_code = get_new_info_job_step(&step_info_ptr, 
						    force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA){
		if((!display_widget || view == ERROR_VIEW)
		   || (job_error_code != SLURM_NO_CHANGE_IN_DATA))
			goto display_it;
		
		goto update_it;
	}

	if (step_error_code != SLURM_SUCCESS) {
		if(view == ERROR_VIEW)
			goto end_it;
		if(display_widget)
			gtk_widget_destroy(display_widget);
		view = ERROR_VIEW;
		sprintf(error_char, "slurm_load_job_step: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(table, label, 0, 1, 0, 1); 
		gtk_widget_show(label);	
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
		goto end_it;
	}
display_it:
	if(view == ERROR_VIEW && display_widget) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
	}
	if(!display_widget) {
		tree_view = create_treeview(local_display_data);
				
		display_widget = gtk_widget_ref(GTK_WIDGET(tree_view));
		gtk_table_attach_defaults(GTK_TABLE(table), 
					  GTK_WIDGET(tree_view),
					  0, 1, 0, 1); 
		gtk_widget_show(GTK_WIDGET(tree_view));
		/* since this function sets the model of the tree_view 
		   to the treestore we don't really care about 
		   the return value */
		create_treestore(tree_view, display_data_job, SORTID_CNT);
	}

update_it:
	view = INFO_VIEW;
	_update_info_job(job_info_ptr, step_info_ptr,
			 GTK_TREE_VIEW(display_widget), NULL);
end_it:
	toggled = FALSE;
	force_refresh = FALSE;
	
	return;
}

extern void specific_info_job(popup_info_t *popup_win)
{
	int job_error_code = SLURM_SUCCESS;
	int step_error_code = SLURM_SUCCESS;
	static job_info_msg_t *job_info_ptr = NULL;
	static job_step_info_response_msg_t *step_info_ptr = NULL;
	specific_info_t *spec_info = popup_win->spec_info;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	
	if(!spec_info->display_widget)
		setup_popup_info(popup_win, display_data_job, SORTID_CNT);

	if(spec_info->display_widget && popup_win->toggled) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
		goto display_it;
	}

	if((job_error_code =
	    get_new_info_job(&job_info_ptr, popup_win->force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) {
		goto get_steps;
	}
	
	if (job_error_code != SLURM_SUCCESS) {
		if(spec_info->view == ERROR_VIEW)
			goto end_it;
		spec_info->view = ERROR_VIEW;
		if(spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		
		sprintf(error_char, "slurm_load_job: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(GTK_TABLE(popup_win->table), 
					  label,
					  0, 1, 0, 1); 
		gtk_widget_show(label);	
		spec_info->display_widget = gtk_widget_ref(GTK_WIDGET(label));
		goto end_it;
	}
get_steps:
	if((step_error_code = 
	    get_new_info_job_step(&step_info_ptr, popup_win->force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) {
		if((!spec_info->display_widget 
		    || spec_info->view == ERROR_VIEW)
		   || (job_error_code != SLURM_NO_CHANGE_IN_DATA)) 
			goto display_it;
		
		goto update_it;
	}
			
	if (step_error_code != SLURM_SUCCESS) {
		if(spec_info->view == ERROR_VIEW)
			goto end_it;
		if(spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		spec_info->view = ERROR_VIEW;
		sprintf(error_char, "slurm_load_job_step: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(popup_win->table, label, 
					  0, 1, 0, 1); 
		gtk_widget_show(label);	
		spec_info->display_widget = gtk_widget_ref(GTK_WIDGET(label));
		goto end_it;
	}
display_it:
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
update_it:
	spec_info->view = INFO_VIEW;
	if(spec_info->type == INFO_PAGE) {
		_display_info_job(job_info_ptr, step_info_ptr, popup_win);
	} else {
		_update_info_job(job_info_ptr, step_info_ptr,
				 GTK_TREE_VIEW(spec_info->display_widget), 
				 spec_info);
	}		
			 
end_it:
	popup_win->toggled = 0;
	popup_win->force_refresh = 0;
	return;
}

extern void set_menus_job(void *arg, GtkTreePath *path, 
			  GtkMenu *menu, int type)
{
	GtkTreeView *tree_view = (GtkTreeView *)arg;
	popup_info_t *popup_win = (popup_info_t *)arg;
	switch(type) {
	case TAB_CLICKED:
		make_fields_menu(menu, display_data_job);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_job);
		break;
	case POPUP_CLICKED:
		make_popup_fields_menu(popup_win, menu);
		break;
	default:
		g_error("UNKNOWN type %d given to set_fields\n", type);
	}
}

extern void popup_all_job(GtkTreeModel *model, GtkTreeIter *iter, int id)
{
	char *name = NULL;
	char title[100];
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	int jobid = NO_VAL;
	int stepid = NO_VAL;
	GError *error = NULL;
	job_step_num_t *job_step = NULL;

	gtk_tree_model_get(model, iter, SORTID_JOBID, &jobid, -1);
	gtk_tree_model_get(model, iter, SORTID_ALLOC, &stepid, -1);
	if(stepid)
		stepid = NO_VAL;
	else {
		stepid = jobid;
		gtk_tree_model_get(model, iter, SORTID_POS, &jobid, -1);
	}

	switch(id) {
	case PART_PAGE:
		if(stepid == NO_VAL)
			snprintf(title, 100, "Partition with job %d", jobid);
		else
			snprintf(title, 100, "Partition with job %d.%d",
				 jobid, stepid);			
		break;
	case NODE_PAGE:
		if(stepid == NO_VAL) {
#ifdef HAVE_BG
			snprintf(title, 100, 
				 "Base partition(s) running job %d", jobid);
#else
			snprintf(title, 100, "Node(s) running job %d", jobid);
#endif
		} else {
#ifdef HAVE_BG
			snprintf(title, 100, 
				 "Base partition(s) running job %d.%d",
				 jobid, stepid);
#else
			snprintf(title, 100, "Node(s) running job %d.%d",
				 jobid, stepid);
#endif
		}
		break;
	case BLOCK_PAGE: 
		if(stepid == NO_VAL)
			snprintf(title, 100, "Block with job %d", jobid);
		else
			snprintf(title, 100, "Block with job %d.%d",
				 jobid, stepid);
		break;
	case SUBMIT_PAGE: 
		if(stepid == NO_VAL)
			snprintf(title, 100, "Submit job on job %d", jobid);
		else
			snprintf(title, 100, "Submit job on job %d.%d",
				 jobid, stepid);
			
		break;
	case INFO_PAGE: 
		if(stepid == NO_VAL)
			snprintf(title, 100, "Full info for job %d", jobid);
		else
			snprintf(title, 100, "Full info for job %d.%d",
				 jobid, stepid);			
		break;
	default:
		g_print("jobs got id %d\n", id);
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
			popup_win = create_popup_info(id, JOB_PAGE, title);
		else
			popup_win = create_popup_info(JOB_PAGE, id, title);
	}
	
	switch(id) {
	case NODE_PAGE:
		gtk_tree_model_get(model, iter, SORTID_NODELIST, &name, -1);
		popup_win->spec_info->data = name;
		break;
	case PART_PAGE:
		gtk_tree_model_get(model, iter, SORTID_PARTITION, &name, -1);
		popup_win->spec_info->data = name;
		break;
#ifdef HAVE_BG
	case BLOCK_PAGE: 
		gtk_tree_model_get(model, iter, SORTID_BLOCK, &name, -1);
		popup_win->spec_info->data = name;
		break;
#endif
	case SUBMIT_PAGE: 
		break;
	case INFO_PAGE:
		job_step = g_malloc(sizeof(job_step_num_t));
		job_step->jobid = jobid;
		job_step->stepid = stepid;
		popup_win->spec_info->data = job_step;
		break;
	
	default:
		g_print("jobs got %d\n", id);
	}
	if (!g_thread_create((gpointer)popup_thr, popup_win, FALSE, &error))
	{
		g_printerr ("Failed to create part popup thread: %s\n", 
			    error->message);
		return;
	}
}
