/****************************************************************************\
 *  popups.c - put different popup displays here
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>, et. al.
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
#include "sview.h"
#include "src/common/parse_time.h"

void *_refresh_thr(gpointer arg)
{
	int msg_id = GPOINTER_TO_INT(arg);
	sleep(5);
	gdk_threads_enter();
	gtk_statusbar_remove(GTK_STATUSBAR(main_statusbar), 
			     STATUS_REFRESH, msg_id);
	gdk_flush();
	gdk_threads_leave();
	return NULL;	
}
static gboolean _delete_popup(GtkWidget *widget,
			      GtkWidget *event,
			      gpointer data)
{
	gtk_widget_destroy(widget);
	return FALSE;
}

void *_local_popup_thr(GtkWidget *popup)
{
	int response = 0;
	
	gdk_threads_enter();
	response = gtk_dialog_run(GTK_DIALOG(popup));
	gdk_flush();
	gdk_threads_leave();

	if (response == GTK_RESPONSE_OK)
	{
		
	}

	gtk_widget_destroy(popup);
	return NULL;
}

/* Creates a tree model containing the completions */
void _search_entry(GtkEntry *entry, GtkComboBox *combo)
{
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	int id;
	char *data = xstrdup(gtk_entry_get_text(entry));
	char title[100];
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	GError *error = NULL;
	job_step_num_t *job_step = NULL;

	gtk_entry_set_text(entry, "");

	if(!strlen(data)) {
		g_print("nothing given to search for.\n");
		return;
	}
	if(!gtk_combo_box_get_active_iter(combo, &iter)) {
		g_print("nothing selected\n");
		return;
	}
	model = gtk_combo_box_get_model(combo);
	if(!model) {
		g_print("nothing selected\n");
		return;
	}
	
	gtk_tree_model_get(model, &iter, 0, &id, -1);
	
	switch(id) {
	case JOB_PAGE:
		snprintf(title, 100, "Job %s info", data);
		break;
	case PART_PAGE:
		snprintf(title, 100, "Partition %s info", data);
		break;
	case BLOCK_PAGE:
		snprintf(title, 100, "BG Block %s info", data);
		break;
	case NODE_PAGE:
#ifdef HAVE_BG
		snprintf(title, 100, 
			 "Base partition(s) %s info", data);
#else
		snprintf(title, 100, "Node(s) %s info", data);
#endif
		break;
	default:
		g_print("unknown selection %s\n", data);
		break;
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
		popup_win = create_popup_info(id, id, title);
	}

	switch(id) {
	case JOB_PAGE:
		id = atoi(data);
		xfree(data);
		job_step = g_malloc(sizeof(job_step_num_t));
		job_step->jobid = id;
		job_step->stepid = NO_VAL;
		popup_win->spec_info->data = job_step;
		break;
	case PART_PAGE:
	case BLOCK_PAGE:
	case NODE_PAGE:
		popup_win->spec_info->data = data;
		break;
	default:
		g_print("unknown selection %d\n", id);
		return;
	}
	if (!g_thread_create((gpointer)popup_thr, popup_win, FALSE, &error))
	{
		g_printerr ("Failed to create main popup thread: %s\n", 
			    error->message);
		return;
	}
	return;
}

static GtkTreeStore *_local_create_treestore_2cols(GtkWidget *popup, 
						   int x, int y)
{
	GtkScrolledWindow *window = create_scrolled_window();
	GtkBin *bin = NULL;
	GtkViewport *view = NULL;
	GtkTable *table = NULL;
	GtkTreeView *treeview = NULL;
	GtkTreeStore *treestore = NULL;

	bin = GTK_BIN(&window->container);
	view = GTK_VIEWPORT(bin->child);
	bin = GTK_BIN(&view->bin);
	table = GTK_TABLE(bin->child);
	
	gtk_window_set_default_size(GTK_WINDOW(popup), 
				    x, y);
	
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			   GTK_WIDGET(window), TRUE, TRUE, 0);
	
	treeview = create_treeview_2cols_attach_to_table(table);
	treestore = GTK_TREE_STORE(gtk_tree_view_get_model(treeview));
	return treestore;
}

static void _layout_ctl_conf(GtkTreeStore *treestore,
			     slurm_ctl_conf_info_msg_t *slurm_ctl_conf_ptr)
{
	char temp_str[32];
	int update = 0;
	GtkTreeIter iter;
	
	if(!slurm_ctl_conf_ptr)
		return;

	slurm_make_time_str((time_t *)&slurm_ctl_conf_ptr->last_update, 
			    temp_str, sizeof(temp_str));
	add_display_treestore_line(update, treestore, &iter, 
			    "Configuration data as of", temp_str);
	add_display_treestore_line(update, treestore, &iter, 
			    "AuthType",	slurm_ctl_conf_ptr->authtype);
	add_display_treestore_line(update, treestore, &iter, 
			    "BackupAddr", slurm_ctl_conf_ptr->backup_addr);
	add_display_treestore_line(update, treestore, &iter, 
			    "BackupController", 
			    slurm_ctl_conf_ptr->backup_controller);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->cache_groups);
	add_display_treestore_line(update, treestore, &iter, 
			    "CacheGroups", temp_str); 
	add_display_treestore_line(update, treestore, &iter, 
			    "CheckpointType",
			    slurm_ctl_conf_ptr->checkpoint_type);
	add_display_treestore_line(update, treestore, &iter, 
			    "ControlAddr", 
			    slurm_ctl_conf_ptr->control_addr);
	add_display_treestore_line(update, treestore, &iter, 
			    "ControlMachine", 
			    slurm_ctl_conf_ptr->control_machine);
	add_display_treestore_line(update, treestore, &iter, 
			    "Epilog", 
			    slurm_ctl_conf_ptr->epilog);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->fast_schedule);
	add_display_treestore_line(update, treestore, &iter, 
			    "FastSchedule", 
			    temp_str);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->first_job_id);
	add_display_treestore_line(update, treestore, &iter, 
			    "FirstJobId", 
			    temp_str);
#ifdef HAVE_XCPU
	add_display_treestore_line(update, treestore, &iter, 
			    "HAVE_XCPU", "1");
#endif
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->inactive_limit);
	add_display_treestore_line(update, treestore, &iter, 
			    "InactiveLimit", 
			    temp_str);
	add_display_treestore_line(update, treestore, &iter, 
			    "JobAcctLogFile", 
			    slurm_ctl_conf_ptr->job_acct_logfile);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->job_acct_freq);
	add_display_treestore_line(update, treestore, &iter, 
			    "JobAcctFrequency",
			    temp_str);
	add_display_treestore_line(update, treestore, &iter, 
			    "JobAcctType", 
			    slurm_ctl_conf_ptr->job_acct_type);
	add_display_treestore_line(update, treestore, &iter, 
			    "JobCompLoc", 
			    slurm_ctl_conf_ptr->job_comp_loc);
	add_display_treestore_line(update, treestore, &iter, 
			    "JobCompType", 
			    slurm_ctl_conf_ptr->job_comp_type);
	add_display_treestore_line(update, treestore, &iter, 
			    "JobCredentialPrivateKey", 
			    slurm_ctl_conf_ptr->job_credential_private_key);
	add_display_treestore_line(update, treestore, &iter, 
			    "JobCredentialPublicCertificate", 
			    slurm_ctl_conf_ptr->
			    job_credential_public_certificate);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->kill_wait);
	add_display_treestore_line(update, treestore, &iter, 
			    "KillWait", 
			    temp_str);
	add_display_treestore_line(update, treestore, &iter, 
			    "MailProg",
			    slurm_ctl_conf_ptr->mail_prog);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->max_job_cnt);
	add_display_treestore_line(update, treestore, &iter, 
			    "MaxJobCount", 
			    temp_str);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->msg_timeout);
	add_display_treestore_line(update, treestore, &iter, 
			    "MessageTimeout",
			    temp_str);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->min_job_age);
	add_display_treestore_line(update, treestore, &iter, 
			    "MinJobAge", 
			    temp_str);
	add_display_treestore_line(update, treestore, &iter, 
			    "MpiDefault",
			    slurm_ctl_conf_ptr->mpi_default);
#ifdef MULTIPLE_SLURMD
	add_display_treestore_line(update, treestore, &iter, 
			    "MULTIPLE_SLURMD", "1");
#endif
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->next_job_id);
	add_display_treestore_line(update, treestore, &iter, 
			    "NEXT_JOB_ID",
			    temp_str);
	add_display_treestore_line(update, treestore, &iter, 
			    "PluginDir", 
			    slurm_ctl_conf_ptr->plugindir);
	add_display_treestore_line(update, treestore, &iter, 
			    "PlugStackConfig",
			    slurm_ctl_conf_ptr->plugstack);
	add_display_treestore_line(update, treestore, &iter, 
			    "ProctrackType",
			    slurm_ctl_conf_ptr->proctrack_type);
	add_display_treestore_line(update, treestore, &iter, 
			    "Prolog", 
			    slurm_ctl_conf_ptr->prolog);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->propagate_prio_process);
	add_display_treestore_line(update, treestore, &iter, 
			    "PropagatePrioProcess", temp_str);
        add_display_treestore_line(update, treestore, &iter, 
			    "PropagateResourceLimits",
			    slurm_ctl_conf_ptr->propagate_rlimits);
        add_display_treestore_line(update, treestore, &iter, 
			    "PropagateResourceLimitsExcept", 
			    slurm_ctl_conf_ptr->propagate_rlimits_except);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->ret2service);
	add_display_treestore_line(update, treestore, &iter, 
			    "ReturnToService", temp_str);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->schedport);
	add_display_treestore_line(update, treestore, &iter, 
			    "SchedulerPort", temp_str);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->schedrootfltr);
	add_display_treestore_line(update, treestore, &iter, 
			    "SchedulerRootFilter", temp_str);
	add_display_treestore_line(update, treestore, &iter, 
			    "SchedulerType",
			    slurm_ctl_conf_ptr->schedtype);
	add_display_treestore_line(update, treestore, &iter, 
			    "SelectType",
			    slurm_ctl_conf_ptr->select_type);
	snprintf(temp_str, sizeof(temp_str), "%s(%u)", 
		 slurm_ctl_conf_ptr->slurm_user_name,
		 slurm_ctl_conf_ptr->slurm_user_id);
	add_display_treestore_line(update, treestore, &iter, 
			    "SlurmUser", temp_str);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->slurmctld_debug);
	add_display_treestore_line(update, treestore, &iter, 
			    "SlurmctldDebug", temp_str);
	add_display_treestore_line(update, treestore, &iter, 
			    "SlurmctldLogFile", 
			    slurm_ctl_conf_ptr->slurmctld_logfile);
	add_display_treestore_line(update, treestore, &iter, 
			    "SlurmctldPidFile", 
			    slurm_ctl_conf_ptr->slurmctld_pidfile);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->slurmctld_port);
	add_display_treestore_line(update, treestore, &iter, 
			    "SlurmctldPort", temp_str);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->slurmctld_timeout);
	add_display_treestore_line(update, treestore, &iter, 
			    "SlurmctldTimeout", temp_str);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->slurmd_debug);
	add_display_treestore_line(update, treestore, &iter, 
			    "SlurmdDebug", temp_str);
	add_display_treestore_line(update, treestore, &iter, 
			    "SlurmdLogFile", 
			    slurm_ctl_conf_ptr->slurmd_logfile);
	add_display_treestore_line(update, treestore, &iter, 
			    "SlurmdPidFile", 
			    slurm_ctl_conf_ptr->slurmd_pidfile);
#ifndef MULTIPLE_SLURMD
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->slurmd_port);
	add_display_treestore_line(update, treestore, &iter, 
			    "SlurmdPort" temp_str);
#endif
	add_display_treestore_line(update, treestore, &iter, 
			    "SlurmdSpoolDir", 
			    slurm_ctl_conf_ptr->slurmd_spooldir);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->slurmd_timeout);
	add_display_treestore_line(update, treestore, &iter, 
			    "SlurmdTimeout", temp_str);
	add_display_treestore_line(update, treestore, &iter, 
			    "SLURM_CONFIG_FILE", 
			    slurm_ctl_conf_ptr->slurm_conf);
	add_display_treestore_line(update, treestore, &iter, 
			    "SLURM_VERSION", SLURM_VERSION);
	add_display_treestore_line(update, treestore, &iter, 
			    "SrunProlog",
			    slurm_ctl_conf_ptr->srun_prolog);
	add_display_treestore_line(update, treestore, &iter, 
			    "SrunEpilog",
			    slurm_ctl_conf_ptr->srun_epilog);
	add_display_treestore_line(update, treestore, &iter, 
			    "StateSaveLocation", 
			    slurm_ctl_conf_ptr->state_save_location);
	add_display_treestore_line(update, treestore, &iter, 
			    "SwitchType",
			    slurm_ctl_conf_ptr->switch_type);
	add_display_treestore_line(update, treestore, &iter, 
			    "TaskEpilog",
			    slurm_ctl_conf_ptr->task_epilog);
	add_display_treestore_line(update, treestore, &iter, 
			    "TaskPlugin",
			    slurm_ctl_conf_ptr->task_plugin);
	add_display_treestore_line(update, treestore, &iter, 
			    "TaskProlog",
			    slurm_ctl_conf_ptr->task_prolog);
	add_display_treestore_line(update, treestore, &iter, 
			    "TmpFS", 
			    slurm_ctl_conf_ptr->tmp_fs);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->tree_width);
	add_display_treestore_line(update, treestore, &iter, 
			    "TreeWidth", temp_str);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->use_pam);
	add_display_treestore_line(update, treestore, &iter, 
			    "UsePam", temp_str);
	snprintf(temp_str, sizeof(temp_str), "%u", 
		 slurm_ctl_conf_ptr->wait_time);
	add_display_treestore_line(update, treestore, &iter, 
			    "WaitTime", temp_str);
}

extern void create_config_popup(GtkToggleAction *action, gpointer user_data)
{
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"SLURM Config Info",
		GTK_WINDOW(user_data),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_CLOSE,
		GTK_RESPONSE_OK,
		NULL);
	int error_code;
	GtkTreeStore *treestore = 
		_local_create_treestore_2cols(popup, 600, 400);
	static slurm_ctl_conf_info_msg_t *old_slurm_ctl_conf_ptr = NULL;
	slurm_ctl_conf_info_msg_t  *slurm_ctl_conf_ptr = NULL;

	g_signal_connect(G_OBJECT(popup), "delete_event",
			 G_CALLBACK(_delete_popup), NULL);
	g_signal_connect(G_OBJECT(popup), "response",
			 G_CALLBACK(_delete_popup), NULL);
	
	
	if (old_slurm_ctl_conf_ptr) {
		error_code = slurm_load_ctl_conf(
			old_slurm_ctl_conf_ptr->last_update,
			&slurm_ctl_conf_ptr);
		if (error_code == SLURM_SUCCESS)
			slurm_free_ctl_conf(old_slurm_ctl_conf_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			slurm_ctl_conf_ptr = old_slurm_ctl_conf_ptr;
			error_code = SLURM_SUCCESS;
		}
	}
	else
		error_code = slurm_load_ctl_conf((time_t) NULL, 
						 &slurm_ctl_conf_ptr);

	_layout_ctl_conf(treestore, slurm_ctl_conf_ptr);
		
	
	gtk_widget_show_all(popup);

	return;
}

extern void create_daemon_popup(GtkToggleAction *action, gpointer user_data)
{
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"SLURM Daemons running",
		GTK_WINDOW(user_data),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_CLOSE,
		GTK_RESPONSE_OK,
		NULL);
	
	int update = 0;
	slurm_ctl_conf_info_msg_t *conf;
	char me[MAX_SLURM_NAME], *b, *c, *n;
	int actld = 0, ctld = 0, d = 0;
	GtkTreeStore *treestore = 
		_local_create_treestore_2cols(popup, 300, 100);
	GtkTreeIter iter;
	g_signal_connect(G_OBJECT(popup), "delete_event",
			 G_CALLBACK(_delete_popup), NULL);
	g_signal_connect(G_OBJECT(popup), "response",
			 G_CALLBACK(_delete_popup), NULL);
	
	slurm_conf_init(NULL);
	conf = slurm_conf_lock();

	getnodename(me, MAX_SLURM_NAME);
	if ((b = conf->backup_controller)) {
		if ((strcmp(b, me) == 0) ||
		    (strcasecmp(b, "localhost") == 0))
			ctld = 1;
	}
	if ((c = conf->control_machine)) {
		actld = 1;
		if ((strcmp(c, me) == 0) ||
		    (strcasecmp(c, "localhost") == 0))
			ctld = 1;
	}
	slurm_conf_unlock();

	if ((n = slurm_conf_get_nodename(me))) {
		d = 1;
		xfree(n);
	} else if ((n = slurm_conf_get_nodename("localhost"))) {
		d = 1;
		xfree(n);
	}
	if (actld && ctld)
		add_display_treestore_line(update, treestore, &iter, 
					   "Slurmctld", "1");
	if (actld && d)
		add_display_treestore_line(update, treestore, &iter,
					   "Slurmd", "1");
	

	gtk_widget_show_all(popup);
		
	return;
}

extern void create_search_popup(GtkToggleAction *action, gpointer user_data)
{
	GtkWidget *table = gtk_table_new(1, 2, FALSE);
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"Search",
		GTK_WINDOW(user_data),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_OK,
		GTK_RESPONSE_OK,
		GTK_STOCK_CANCEL,
		GTK_RESPONSE_CANCEL,
		NULL);
	int response = 0;
	display_data_t pulldown_display_data[] = {
		{G_TYPE_NONE, JOB_PAGE, "Job", TRUE, -1},
		{G_TYPE_NONE, PART_PAGE, "Partition", TRUE, -1},
#ifdef HAVE_BG
		{G_TYPE_NONE, BLOCK_PAGE, "BG Block", TRUE, -1},
		{G_TYPE_NONE, NODE_PAGE, "Base Partitions", TRUE, -1},	
#else
		{G_TYPE_NONE, NODE_PAGE, "Node", TRUE, -1},
#endif
		{G_TYPE_NONE, -1, NULL, FALSE, -1}
	};
	GtkWidget *combo = 
		create_pulldown_combo(pulldown_display_data, PAGE_CNT);
	GtkWidget *entry = gtk_entry_new();
	
	gtk_container_set_border_width(GTK_CONTAINER(table), 10);
	
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			   table, FALSE, FALSE, 0);
	
	gtk_table_attach_defaults(GTK_TABLE(table), combo, 0, 1, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, 0, 1);

	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK)
		_search_entry(GTK_ENTRY(entry), GTK_COMBO_BOX(combo));
	

	gtk_widget_destroy(popup);
	
	return;
}

extern void change_refresh_popup(GtkToggleAction *action, gpointer user_data)
{
	GtkWidget *table = gtk_table_new(1, 2, FALSE);
	GtkWidget *label = gtk_label_new("Interval in Seconds ");
	GtkObject *adjustment = gtk_adjustment_new(global_sleep_time,
						   1, 10000,
						   5, 60,
						   1);
	GtkWidget *spin_button = 
		gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1, 0);
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"Refresh Interval",
		GTK_WINDOW (user_data),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_OK,
		GTK_RESPONSE_OK,
		GTK_STOCK_CANCEL,
		GTK_RESPONSE_CANCEL,
		NULL);
	GError *error = NULL;
	int response = 0;
	char *temp = NULL;

	gtk_container_set_border_width(GTK_CONTAINER(table), 10);
	
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			   table, FALSE, FALSE, 0);
	
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);	
	gtk_table_attach_defaults(GTK_TABLE(table), spin_button, 1, 2, 0, 1);
	
	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK)
	{
		global_sleep_time = 
			gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_button));
		temp = g_strdup_printf("Refresh Interval set to %d seconds.",
				       global_sleep_time);
		gtk_statusbar_pop(GTK_STATUSBAR(main_statusbar), 
				  STATUS_REFRESH);
		response = gtk_statusbar_push(GTK_STATUSBAR(main_statusbar), 
					      STATUS_REFRESH,
					      temp);
		g_free(temp);
		if (!g_thread_create(_refresh_thr, GINT_TO_POINTER(response),
				     FALSE, &error))
		{
			g_printerr ("Failed to create refresh thread: %s\n", 
				    error->message);
		}
	}

	gtk_widget_destroy(popup);
	
	return;
}

