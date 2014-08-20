/****************************************************************************\
 *  popups.c - put different popup displays here
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2008 Vijay Ramasubramanian
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
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
	//gdk_flush();
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

/* Creates a tree model containing the completions */
void _search_entry(sview_search_info_t *sview_search_info)
{
	int id = 0;
	char title[100];
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	GError *error = NULL;
	char *upper = NULL, *lower = NULL;
	char *type;

	if (cluster_flags & CLUSTER_FLAG_BG)
		type = "Midplane";
	else
		type = "Node";

	if (sview_search_info->int_data == NO_VAL &&
	    (!sview_search_info->gchar_data
	     || !strlen(sview_search_info->gchar_data))) {
		g_print("nothing given to search for.\n");
		return;
	}

	switch(sview_search_info->search_type) {
	case SEARCH_JOB_STATE:
		id = JOB_PAGE;
		upper = job_state_string(sview_search_info->int_data);
		lower = str_tolower(upper);
		snprintf(title, 100, "Job(s) in the %s state", lower);
		xfree(lower);
		break;
	case SEARCH_JOB_ID:
		id = JOB_PAGE;
		snprintf(title, 100, "Job %s info",
			 sview_search_info->gchar_data);
		break;
	case SEARCH_JOB_USER:
		id = JOB_PAGE;
		snprintf(title, 100, "Job(s) info for user %s",
			 sview_search_info->gchar_data);
		break;
	case SEARCH_BLOCK_STATE:
		id = BLOCK_PAGE;
		upper = bg_block_state_string(sview_search_info->int_data);
		lower = str_tolower(upper);
		snprintf(title, 100, "BG Block(s) in the %s state", lower);
		xfree(lower);
		break;
	case SEARCH_BLOCK_NAME:
		id = BLOCK_PAGE;
		snprintf(title, 100, "Block %s info",
			 sview_search_info->gchar_data);
		break;
	case SEARCH_BLOCK_SIZE:
		id = BLOCK_PAGE;
		sview_search_info->int_data =
			revert_num_unit(sview_search_info->gchar_data);
		if (sview_search_info->int_data == -1)
			return;
		snprintf(title, 100, "Block(s) of size %d cnodes",
			 sview_search_info->int_data);
		break;
	case SEARCH_PARTITION_NAME:
		id = PART_PAGE;
		snprintf(title, 100, "Partition %s info",
			 sview_search_info->gchar_data);
		break;
	case SEARCH_PARTITION_STATE:
		id = PART_PAGE;
		if (sview_search_info->int_data)
			snprintf(title, 100, "Partition(s) that are up");
		else
			snprintf(title, 100, "Partition(s) that are down");
		break;
	case SEARCH_NODE_NAME:
		id = NODE_PAGE;
		snprintf(title, 100, "%s(s) %s info",
			 type, sview_search_info->gchar_data);
		break;
	case SEARCH_NODE_STATE:
		id = NODE_PAGE;
		upper = node_state_string(sview_search_info->int_data);
		lower = str_tolower(upper);
		snprintf(title, 100, "%s(s) in the %s state",
			 type, lower);
		xfree(lower);

		break;
	case SEARCH_RESERVATION_NAME:
		id = RESV_PAGE;
		snprintf(title, 100, "Reservation %s info",
			 sview_search_info->gchar_data);
		break;
	default:
		g_print("unknown search type %d.\n",
			sview_search_info->search_type);

		return;
	}

	itr = list_iterator_create(popup_list);
	while ((popup_win = list_next(itr))) {
		if (popup_win->spec_info)
			if (!strcmp(popup_win->spec_info->title, title)) {
				break;
			}
	}
	list_iterator_destroy(itr);

	if (!popup_win) {
		popup_win = create_popup_info(id, id, title);
	} else {
		gtk_window_present(GTK_WINDOW(popup_win->popup));
		return;
	}
	memcpy(popup_win->spec_info->search_info, sview_search_info,
	       sizeof(sview_search_info_t));

	if (!sview_thread_new((gpointer)popup_thr, popup_win, FALSE, &error)) {
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

static void _gtk_print_key_pairs(List config_list, char *title, bool first,
				 GtkTreeStore *treestore, GtkTreeIter *iter)
{
	ListIterator itr = NULL;
	config_key_pair_t *key_pair;
	int update = 0;

	if (!config_list || !list_count(config_list))
		return;

	if (!first)
		add_display_treestore_line(update, treestore, iter, "", NULL);

	add_display_treestore_line_with_font(update, treestore, iter,
					     title, NULL, "bold");

	itr = list_iterator_create(config_list);
	while ((key_pair = list_next(itr)))
		add_display_treestore_line(update, treestore, iter,
					   key_pair->name, key_pair->value);
	list_iterator_destroy(itr);
}

static void _layout_conf_ctl(GtkTreeStore *treestore,
			     slurm_ctl_conf_info_msg_t *slurm_ctl_conf_ptr)
{
	char time_str[32], tmp_str[128];
	GtkTreeIter iter;
	List ret_list = NULL;
	char *select_title = "Select Plugin Configuration";

	if (cluster_flags & CLUSTER_FLAG_BGL)
		select_title = "Bluegene/L configuration";
	else if (cluster_flags & CLUSTER_FLAG_BGP)
		select_title = "Bluegene/P configuration";
	else if (cluster_flags & CLUSTER_FLAG_BGQ)
		select_title = "Bluegene/Q configuration";
	else if (cluster_flags & CLUSTER_FLAG_CRAY)
		select_title = "\nCray configuration\n";

	if (!slurm_ctl_conf_ptr)
		return;

	slurm_make_time_str((time_t *)&slurm_ctl_conf_ptr->last_update,
			    time_str, sizeof(time_str));
	snprintf(tmp_str, sizeof(tmp_str), "Configuration data as of %s",
		 time_str);

	ret_list = slurm_ctl_conf_2_key_pairs(slurm_ctl_conf_ptr);
	_gtk_print_key_pairs(ret_list, tmp_str, 1, treestore, &iter);
	if (ret_list)
		list_destroy(ret_list);

	_gtk_print_key_pairs(slurm_ctl_conf_ptr->acct_gather_conf,
			     "Account Gather", 0, treestore, &iter);

	_gtk_print_key_pairs(slurm_ctl_conf_ptr->ext_sensors_conf,
			     "External Sensors", 0, treestore, &iter);

	_gtk_print_key_pairs(slurm_ctl_conf_ptr->select_conf_key_pairs,
			     select_title, 0, treestore, &iter);
}

static void _layout_conf_dbd(GtkTreeStore *treestore)
{
	ListIterator itr = NULL;
	GtkTreeIter iter;
	config_key_pair_t *key_pair;
	int update = 0;
	time_t now = time(NULL);
	char tmp_str[128], *user_name = NULL;
	List dbd_config_list = NULL;

	/* first load accounting parms from slurm.conf */
	char *acct_storage_backup_host =
		slurm_get_accounting_storage_backup_host();
	char *acct_storage_host = slurm_get_accounting_storage_host();
	char *acct_storage_loc  = slurm_get_accounting_storage_loc();
	char *acct_storage_pass = slurm_get_accounting_storage_pass();
	uint32_t acct_storage_port = slurm_get_accounting_storage_port();
	char *acct_storage_type = slurm_get_accounting_storage_type();
	char *acct_storage_user = slurm_get_accounting_storage_user();
	char *auth_type = slurm_get_auth_type();
	uint16_t msg_timeout = slurm_get_msg_timeout();
	char *plugin_dir = slurm_get_plugin_dir();
	uint16_t private_data = slurm_get_private_data();
	uint32_t slurm_user_id = slurm_get_slurm_user_id();
	uint16_t track_wckey = slurm_get_track_wckey();

	slurm_make_time_str(&now, tmp_str, sizeof(tmp_str));
	add_display_treestore_line_with_font(
		update, treestore, &iter,
		"SLURM Configuration data as of", tmp_str, "bold");

	add_display_treestore_line(update, treestore, &iter,
				   "AccountingStorageBackupHost",
				   acct_storage_backup_host);
	add_display_treestore_line(update, treestore, &iter,
				   "AccountingStorageHost", acct_storage_host);
	add_display_treestore_line(update, treestore, &iter,
				   "AccountingStorageLoc", acct_storage_loc);
	add_display_treestore_line(update, treestore, &iter,
				   "AccountingStoragePass", acct_storage_pass);
	sprintf(tmp_str, "%u", acct_storage_port);
	add_display_treestore_line(update, treestore, &iter,
				   "AccountingStoragePort", tmp_str);
	add_display_treestore_line(update, treestore, &iter,
				   "AccountingStorageType", acct_storage_type);
	add_display_treestore_line(update, treestore, &iter,
				   "AccountingStorageUser", acct_storage_user);
	add_display_treestore_line(update, treestore, &iter,
				   "AuthType", auth_type);
	sprintf(tmp_str, "%u sec", msg_timeout);
	add_display_treestore_line(update, treestore, &iter,
				   "MessageTimeout", tmp_str);
	add_display_treestore_line(update, treestore, &iter,
				   "PluginDir", plugin_dir);
	private_data_string(private_data, tmp_str, sizeof(tmp_str));
	add_display_treestore_line(update, treestore, &iter,
				   "PrivateData", tmp_str);
	user_name = uid_to_string_cached(slurm_user_id);
	sprintf(tmp_str, "%s(%u)", user_name, slurm_user_id);
	add_display_treestore_line(update, treestore, &iter,
				   "SlurmUserId", tmp_str);
	add_display_treestore_line(update, treestore, &iter,
				   "SLURM_CONF", default_slurm_config_file);
	add_display_treestore_line(update, treestore, &iter,
				   "SLURM_VERSION", SLURM_VERSION_STRING);
	sprintf(tmp_str, "%u", track_wckey);
	add_display_treestore_line(update, treestore, &iter,
				   "TrackWCKey", tmp_str);

	xfree(acct_storage_backup_host);
	xfree(acct_storage_host);
	xfree(acct_storage_loc);
	xfree(acct_storage_pass);
	xfree(acct_storage_type);
	xfree(acct_storage_user);
	xfree(auth_type);
	xfree(plugin_dir);

	/* now load accounting parms from slurmdbd.conf */

	/* second load slurmdbd.conf parms */
	if (!(dbd_config_list = slurmdb_config_get(NULL)))
		return;

	add_display_treestore_line_with_font(
		update, treestore, &iter,
		"\nSlurmDBD Configuration:", NULL, "bold");

	itr = list_iterator_create(dbd_config_list);
	while ((key_pair = list_next(itr))) {
		add_display_treestore_line(update, treestore, &iter,
					   key_pair->name,
					   key_pair->value);
	}
	list_iterator_destroy(itr);
}

extern void create_config_popup(GtkAction *action, gpointer user_data)
{
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"SLURM Config Info",
		GTK_WINDOW(user_data),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_CLOSE,
		GTK_RESPONSE_OK,
		NULL);
	GtkTreeStore *treestore =
		_local_create_treestore_2cols(popup, 600, 400);
	static slurm_ctl_conf_info_msg_t  *slurm_ctl_conf_ptr = NULL;

	g_signal_connect(G_OBJECT(popup), "delete_event",
			 G_CALLBACK(_delete_popup), NULL);
	g_signal_connect(G_OBJECT(popup), "response",
			 G_CALLBACK(_delete_popup), NULL);

	(void) get_new_info_config(&slurm_ctl_conf_ptr);
	_layout_conf_ctl(treestore, slurm_ctl_conf_ptr);

	gtk_widget_show_all(popup);

	return;
}

extern void create_dbconfig_popup(GtkAction *action, gpointer user_data)
{
	List dbd_config_list = NULL;
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"SLURM Database Config Info",
		GTK_WINDOW(user_data),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_CLOSE,
		GTK_RESPONSE_OK,
		NULL);
	GtkTreeStore *treestore =
		_local_create_treestore_2cols(popup, 600, 400);

	g_signal_connect(G_OBJECT(popup), "delete_event",
			 G_CALLBACK(_delete_popup), NULL);
	g_signal_connect(G_OBJECT(popup), "response",
			 G_CALLBACK(_delete_popup), NULL);

	_layout_conf_dbd(treestore);

	gtk_widget_show_all(popup);

	if (dbd_config_list)
		list_destroy(dbd_config_list);

	return;
}

extern void create_daemon_popup(GtkAction *action, gpointer user_data)
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

	gethostname_short(me, MAX_SLURM_NAME);
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
	} else if ((n = slurm_conf_get_aliased_nodename())) {
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

extern void create_create_popup(GtkAction *action, gpointer user_data)
{
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"Create",
		GTK_WINDOW(user_data),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);
	int i, response = 0;
	GtkWidget *label = NULL;
	GtkWidget *entry = NULL;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	const gchar *name = gtk_action_get_name(action);
	sview_search_info_t sview_search_info;
	job_desc_msg_t *job_msg = NULL;
	submit_response_msg_t *slurm_alloc_msg = NULL;
	update_part_msg_t *part_msg = NULL;
	resv_desc_msg_t *resv_msg = NULL;
	char *res_name, *temp;

	sview_search_info.gchar_data = NULL;
	sview_search_info.int_data = NO_VAL;
	sview_search_info.int_data2 = NO_VAL;

	label = gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_OK, GTK_RESPONSE_OK);
	gtk_window_set_default(GTK_WINDOW(popup), label);
	gtk_dialog_add_button(GTK_DIALOG(popup),
			      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	gtk_window_set_default_size(GTK_WINDOW(popup), 400, 600);

	if (!strcmp(name, "batch_job")) {
		sview_search_info.search_type = CREATE_BATCH_JOB;
		label = gtk_label_new(
			"Batch job submission specifications\n\n"
			"Specify size (task and/or node count) plus the\n"
			"script. All other fields are optional.\n\n"
			"More fields will be made available later.");
		job_msg = xmalloc(sizeof(job_desc_msg_t));
		slurm_init_job_desc_msg(job_msg);
		job_msg->group_id = getgid();
		job_msg->user_id  = getuid();
		job_msg->work_dir = xmalloc(1024);
		if (!getcwd(job_msg->work_dir, 1024))
			goto end_it;
		entry = create_job_entry(job_msg, model, &iter);
	} else if (!strcmp(name, "partition")) {
		sview_search_info.search_type = CREATE_PARTITION;
		label = gtk_label_new(
			"Partition creation specifications\n\n"
			"Specify Name. All other fields are optional.");
		part_msg = xmalloc(sizeof(update_part_msg_t));
		slurm_init_part_desc_msg(part_msg);
		entry = create_part_entry(part_msg, model, &iter);
	} else if (!strcmp(name, "reservation")) {
		sview_search_info.search_type = CREATE_RESERVATION;
		label = gtk_label_new(
			"Reservation creation specifications\n\n"
			"Specify Time_Start and either Duration or Time_End.\n"
#ifdef HAVE_BG
			"Specify either Node_Count or Midplane_List.\n"
#else
			"Specify either Node_Count or Node_List.\n"
#endif
			"Specify either Accounts or Users.\n\n"
			"Supported Flags include: Maintenance, Overlap,\n"
			"Ignore_Jobs, Daily and Weekly, License_Only\n"
			"Part_Nodes and Static_Alloc.\n"
			"All other fields are optional.");
		resv_msg = xmalloc(sizeof(resv_desc_msg_t));
		slurm_init_resv_desc_msg(resv_msg);
		entry = create_resv_entry(resv_msg, model, &iter);
	} else {
		sview_search_info.search_type = 0;
		goto end_it;
	}

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   entry, TRUE, TRUE, 0);

	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK) {
		if (!sview_search_info.search_type)
			goto end_it;

		switch(sview_search_info.search_type) {
		case CREATE_BATCH_JOB:
			response = slurm_submit_batch_job(job_msg,
							  &slurm_alloc_msg);
			if (response == SLURM_SUCCESS) {
				temp = g_strdup_printf(
					"Job %u submitted",
					slurm_alloc_msg->job_id);
			} else {
				temp = g_strdup_printf(
					"Problem submitting job: %s",
					slurm_strerror(slurm_get_errno()));
			}
			display_edit_note(temp);
			g_free(temp);
			break;
		case CREATE_PARTITION:
			response = slurm_create_partition(part_msg);
			if (response == SLURM_SUCCESS) {
				temp = g_strdup_printf("Partition %s created",
						       part_msg->name);
			} else {
				temp = g_strdup_printf(
					"Problem creating partition: %s",
					slurm_strerror(slurm_get_errno()));
			}
			display_edit_note(temp);
			g_free(temp);
			break;
		case CREATE_RESERVATION:
			res_name = slurm_create_reservation(resv_msg);
			if (res_name) {
				temp = g_strdup_printf(
					"Reservation %s created",
					res_name);
				free(res_name);
			} else {
				temp = g_strdup_printf(
					"Problem creating reservation: %s",
					slurm_strerror(slurm_get_errno()));
			}
			display_edit_note(temp);
			g_free(temp);
			break;
		default:
			break;
		}
	}

end_it:
	gtk_widget_destroy(popup);
	if (slurm_alloc_msg)
		slurm_free_submit_response_response_msg(slurm_alloc_msg);
	if (job_msg) {
		for (i = 0; i < job_msg->argc; i++)
			xfree(job_msg->argv[i]);
		xfree(job_msg->argv);
		xfree(job_msg->name);
		xfree(job_msg->script);
		xfree(job_msg->work_dir);
		xfree(job_msg);
	}
	xfree(part_msg);
	if (resv_msg)
		slurm_free_resv_desc_msg(resv_msg);
	return;
}

extern void create_search_popup(GtkAction *action, gpointer user_data)
{
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"Search",
		GTK_WINDOW(user_data),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);

	int response = 0;
	GtkWidget *label = NULL;
	GtkWidget *entry = NULL;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	const gchar *name = gtk_action_get_name(action);
	sview_search_info_t sview_search_info;

	sview_search_info.gchar_data = NULL;
	sview_search_info.int_data = NO_VAL;
	sview_search_info.int_data2 = NO_VAL;

	label = gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_OK, GTK_RESPONSE_OK);
	gtk_window_set_default(GTK_WINDOW(popup), label);
	gtk_dialog_add_button(GTK_DIALOG(popup),
			      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	if (!strcmp(name, "jobid")) {
		sview_search_info.search_type = SEARCH_JOB_ID;
		entry = create_entry();
		label = gtk_label_new("Which job id?");
	} else if (!strcmp(name, "user_jobs")) {
		sview_search_info.search_type = SEARCH_JOB_USER;
		entry = create_entry();
		label = gtk_label_new("Which user?");
	} else if (!strcmp(name, "state_jobs")) {
		display_data_t pulldown_display_data[] = {
			{G_TYPE_NONE, JOB_PENDING, "Pending", TRUE, -1},
			{G_TYPE_NONE, JOB_CONFIGURING, "Configuring", TRUE, -1},
			{G_TYPE_NONE, JOB_RUNNING, "Running", TRUE, -1},
			{G_TYPE_NONE, JOB_SUSPENDED, "Suspended", TRUE, -1},
			{G_TYPE_NONE, JOB_COMPLETE, "Complete", TRUE, -1},
			{G_TYPE_NONE, JOB_CANCELLED, "Cancelled", TRUE, -1},
			{G_TYPE_NONE, JOB_FAILED, "Failed", TRUE, -1},
			{G_TYPE_NONE, JOB_TIMEOUT, "Timeout", TRUE, -1},
			{G_TYPE_NONE, JOB_NODE_FAIL, "Node Failure", TRUE, -1},
			{G_TYPE_NONE, JOB_PREEMPTED, "Preempted", TRUE, -1},
			{G_TYPE_NONE, JOB_BOOT_FAIL, "Boot Failure", TRUE, -1},
			{G_TYPE_NONE, -1, NULL, FALSE, -1}
		};

		sview_search_info.search_type = SEARCH_JOB_STATE;
		entry = create_pulldown_combo(pulldown_display_data);
		label = gtk_label_new("Which state?");
	} else if (!strcmp(name, "partition_name")) {
		sview_search_info.search_type = SEARCH_PARTITION_NAME;
		entry = create_entry();
		label = gtk_label_new("Which partition");
	} else if (!strcmp(name, "partition_state")) {
		display_data_t pulldown_display_data[] = {
			{G_TYPE_NONE, PARTITION_UP, "Up", TRUE, -1},
			{G_TYPE_NONE, PARTITION_DOWN, "Down", TRUE, -1},
			{G_TYPE_NONE, PARTITION_INACTIVE, "Inactive", TRUE, -1},
			{G_TYPE_NONE, PARTITION_DRAIN, "Drain", TRUE, -1},
			{G_TYPE_NONE, -1, NULL, FALSE, -1}
		};

		sview_search_info.search_type = SEARCH_PARTITION_STATE;
		entry = create_pulldown_combo(pulldown_display_data);
		label = gtk_label_new("Which state?");
	} else if (!strcmp(name, "node_name")) {
		sview_search_info.search_type = SEARCH_NODE_NAME;
		entry = create_entry();
		if (cluster_flags & CLUSTER_FLAG_BG)
			label = gtk_label_new("Which Midplane(s)?\n"
					      "(ranged or comma separated)");
		else
			label = gtk_label_new("Which node(s)?\n"
					      "(ranged or comma separated)");
	} else if (!strcmp(name, "node_state")) {
		display_data_t pulldown_display_data[] = {
			{G_TYPE_NONE, NODE_STATE_ALLOCATED, "Allocated",
			 TRUE, -1},
			{G_TYPE_NONE, NODE_STATE_COMPLETING, "Completing",
			 TRUE, -1},
			{G_TYPE_NONE, NODE_STATE_DOWN, "Down", TRUE, -1},
			{G_TYPE_NONE, NODE_STATE_ALLOCATED | NODE_STATE_DRAIN,
			 "Draining", TRUE, -1},
			{G_TYPE_NONE, NODE_STATE_IDLE | NODE_STATE_DRAIN,
			 "Drained", TRUE, -1},
			{G_TYPE_NONE, NODE_STATE_ERROR, "Error", TRUE, -1},
			{G_TYPE_NONE, NODE_STATE_FAIL, "Fail", TRUE, -1},
			{G_TYPE_NONE, NODE_STATE_FAIL | NODE_STATE_ALLOCATED,
			 "Failing", TRUE, -1},
			{G_TYPE_NONE, NODE_STATE_FUTURE, "Future", TRUE, -1},
			{G_TYPE_NONE, NODE_STATE_IDLE, "Idle", TRUE, -1},
			{G_TYPE_NONE, NODE_STATE_MAINT, "Maint", TRUE, -1},
			{G_TYPE_NONE, NODE_STATE_MIXED, "Mixed", TRUE, -1},
			{G_TYPE_NONE, NODE_STATE_NO_RESPOND,
			 "No Respond", TRUE, -1},
			{G_TYPE_NONE, NODE_STATE_NET | NODE_STATE_IDLE,
			 "PerfCTRs", TRUE, -1},
			{G_TYPE_NONE, NODE_STATE_POWER_SAVE,
			 "Power Down", TRUE, -1},
			{G_TYPE_NONE, NODE_STATE_POWER_UP,
			 "Power Up", TRUE, -1},
			{G_TYPE_NONE, NODE_STATE_RES | NODE_STATE_IDLE,
			 "Reserved", TRUE, -1},
			{G_TYPE_NONE, NODE_STATE_UNKNOWN, "Unknown", TRUE, -1},
			{G_TYPE_NONE, -1, NULL, FALSE, -1}
		};

		sview_search_info.search_type = SEARCH_NODE_STATE;
		entry = create_pulldown_combo(pulldown_display_data);
		label = gtk_label_new("Which state?");
	} else if ((cluster_flags & CLUSTER_FLAG_BG)
		   && !strcmp(name, "bg_block_name")) {
		sview_search_info.search_type = SEARCH_BLOCK_NAME;
		entry = create_entry();
		label = gtk_label_new("Which block?");
	} else if ((cluster_flags & CLUSTER_FLAG_BG)
		   && !strcmp(name, "bg_block_size")) {
		sview_search_info.search_type = SEARCH_BLOCK_SIZE;
		entry = create_entry();
		label = gtk_label_new("Which block size?");
	} else if (!strcmp(name, "bg_block_state")) {
		display_data_t pulldown_display_data[] = {
			{G_TYPE_NONE, BG_BLOCK_NAV, "Nav", TRUE, -1},
			{G_TYPE_NONE, BG_BLOCK_FREE, "Free", TRUE, -1},
			{G_TYPE_NONE, BG_BLOCK_BUSY, NULL, TRUE, -1},
			{G_TYPE_NONE, BG_BLOCK_BOOTING, "Booting", TRUE, -1},
			{G_TYPE_NONE, BG_BLOCK_REBOOTING, NULL, TRUE, -1},
			{G_TYPE_NONE, BG_BLOCK_INITED, "Inited", TRUE, -1},
			{G_TYPE_NONE, BG_BLOCK_ALLOCATED, NULL, TRUE, -1},
			{G_TYPE_NONE, BG_BLOCK_TERM, "Terminating", TRUE, -1},
			{G_TYPE_NONE, BG_BLOCK_ERROR_FLAG, "Error", TRUE, -1},
			{G_TYPE_NONE, -1, NULL, FALSE, -1}
		};
		display_data_t *display_data = pulldown_display_data;
		while (display_data++) {
			if (display_data->id == -1)
				break;
			if (cluster_flags & CLUSTER_FLAG_BGL) {
				switch(display_data->id) {
				case BG_BLOCK_BUSY:
					display_data->name = "Busy";
					break;
				}
			} else if (cluster_flags & CLUSTER_FLAG_BGP){
				switch(display_data->id) {
				case BG_BLOCK_REBOOTING:
					display_data->name = "Rebooting";
					break;
				}
			} else {
				switch(display_data->id) {
				case BG_BLOCK_ALLOCATED:
					display_data->name = "Allocated";
					break;
				}
			}
		}
		sview_search_info.search_type = SEARCH_BLOCK_STATE;
		entry = create_pulldown_combo(pulldown_display_data);
		label = gtk_label_new("Which state?");
	} else if (!strcmp(name, "reservation_name")) {
		sview_search_info.search_type = SEARCH_RESERVATION_NAME;
		entry = create_entry();
		label = gtk_label_new("Which reservation");
	} else {
		sview_search_info.search_type = 0;
		goto end_it;
	}

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   entry, FALSE, FALSE, 0);

	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK) {
		if (!sview_search_info.search_type)
			goto end_it;

		switch(sview_search_info.search_type) {
		case SEARCH_BLOCK_STATE:
		case SEARCH_JOB_STATE:
		case SEARCH_NODE_STATE:
		case SEARCH_PARTITION_STATE:
			if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(entry),
							   &iter)) {
				g_print("nothing selected\n");
				return;
			}
			model = gtk_combo_box_get_model(GTK_COMBO_BOX(entry));
			if (!model) {
				g_print("nothing selected\n");
				return;
			}

			gtk_tree_model_get(model, &iter, 0,
					   &sview_search_info.int_data, -1);
			break;
		case SEARCH_JOB_ID:
		case SEARCH_JOB_USER:
		case SEARCH_BLOCK_NAME:
		case SEARCH_BLOCK_SIZE:
		case SEARCH_PARTITION_NAME:
		case SEARCH_NODE_NAME:
		case SEARCH_RESERVATION_NAME:
			sview_search_info.gchar_data =
				g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
			break;
		default:

			break;
		}

		_search_entry(&sview_search_info);
	}
end_it:
	gtk_widget_destroy(popup);

	return;
}

extern void change_refresh_popup(GtkAction *action, gpointer user_data)
{
	GtkWidget *table = gtk_table_new(1, 2, FALSE);
	GtkWidget *label = NULL;
	GtkObject *adjustment = gtk_adjustment_new(
		working_sview_config.refresh_delay,
		1, 10000,
		5, 60,
		0);
	GtkWidget *spin_button =
		gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1, 0);
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"Refresh Interval",
		GTK_WINDOW (user_data),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);
	GError *error = NULL;
	int response = 0;
	char *temp = NULL;

	label = gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_OK, GTK_RESPONSE_OK);
	gtk_window_set_default(GTK_WINDOW(popup), label);
	gtk_dialog_add_button(GTK_DIALOG(popup),
			      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	label = gtk_label_new("Interval in Seconds ");

	gtk_container_set_border_width(GTK_CONTAINER(table), 10);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   table, FALSE, FALSE, 0);

	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), spin_button, 1, 2, 0, 1);

	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK) {
		working_sview_config.refresh_delay =
			gtk_spin_button_get_value_as_int(
				GTK_SPIN_BUTTON(spin_button));
		temp = g_strdup_printf("Refresh Interval set to %d seconds.",
				       working_sview_config.refresh_delay);
		gtk_statusbar_pop(GTK_STATUSBAR(main_statusbar),
				  STATUS_REFRESH);
		response = gtk_statusbar_push(GTK_STATUSBAR(main_statusbar),
					      STATUS_REFRESH,
					      temp);
		g_free(temp);
		if (!sview_thread_new(_refresh_thr, GINT_TO_POINTER(response),
				      FALSE, &error)) {
			g_printerr ("Failed to create refresh thread: %s\n",
				    error->message);
		}
	}

	gtk_widget_destroy(popup);

	return;
}

extern void change_grid_popup(GtkAction *action, gpointer user_data)
{
	GtkWidget *table = gtk_table_new(1, 2, FALSE);
	GtkWidget *label;
	GtkObject *adjustment;
	GtkWidget *width_sb, *hori_sb, *vert_sb;
	int width = working_sview_config.grid_x_width,
		hori = working_sview_config.grid_hori,
		vert = working_sview_config.grid_vert;
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"Grid Properties",
		GTK_WINDOW (user_data),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);
	GError *error = NULL;
	int response = 0;
	char *temp = NULL;

	label = gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_OK, GTK_RESPONSE_OK);
	gtk_window_set_default(GTK_WINDOW(popup), label);
	gtk_dialog_add_button(GTK_DIALOG(popup),
			      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   table, FALSE, FALSE, 0);

	label = gtk_label_new("Nodes in row ");
	adjustment = gtk_adjustment_new(working_sview_config.grid_x_width,
					1, 1000, 1, 60, 0);
	width_sb = gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1, 0);
	gtk_container_set_border_width(GTK_CONTAINER(table), 10);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), width_sb, 1, 2, 0, 1);

	label = gtk_label_new("Nodes before horizontal break ");
	adjustment = gtk_adjustment_new(working_sview_config.grid_hori,
					1, 1000, 1, 60, 0);
	hori_sb = gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1, 0);
	gtk_container_set_border_width(GTK_CONTAINER(table), 10);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 1, 2);
	gtk_table_attach_defaults(GTK_TABLE(table), hori_sb, 1, 2, 1, 2);

	label = gtk_label_new("Nodes before vertical break ");
	adjustment = gtk_adjustment_new(working_sview_config.grid_vert,
					1, 1000, 1, 60, 0);
	vert_sb = gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1, 0);
	gtk_container_set_border_width(GTK_CONTAINER(table), 10);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 2, 3);
	gtk_table_attach_defaults(GTK_TABLE(table), vert_sb, 1, 2, 2, 3);

	/*TODO
	 * do we care about this?

	 label = gtk_label_new("Topology ordered ");
	 adjustment = gtk_adjustment_new(working_sview_config.grid_topological,
	 1, 1000, 1, 60, 0);
	 GtkWidget *gtbtton =  gtk_check_button_new ();
	 gtk_container_set_border_width(GTK_CONTAINER(table), 10);
	 gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 3, 4);
	 gtk_table_attach_defaults(GTK_TABLE(table), gtbtton, 1, 2, 3, 4);

	 gtk_toggle_button_set_active (&gtbtton,
                                       working_sview_config.grid_topological);
	*/

	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK) {
		working_sview_config.grid_x_width =
			gtk_spin_button_get_value_as_int(
				GTK_SPIN_BUTTON(width_sb));
		working_sview_config.grid_hori =
			gtk_spin_button_get_value_as_int(
				GTK_SPIN_BUTTON(hori_sb));
		working_sview_config.grid_vert =
			gtk_spin_button_get_value_as_int(
				GTK_SPIN_BUTTON(vert_sb));
		memcpy(&default_sview_config, &working_sview_config,
		       sizeof(sview_config_t));
		if ((width == working_sview_config.grid_x_width)
		    && (hori == working_sview_config.grid_hori)
		    && (vert == working_sview_config.grid_vert)) {
			temp = g_strdup_printf("Grid: Nothing changed.");
		} else if (working_sview_config.grid_topological) {
			temp = g_strdup_printf("Grid: Invalid mode .."
					       " switch to non-topology "
					       "order first.");
		} else {
			bool refresh = 0;
			temp = g_strdup_printf(
				"Grid set to %d nodes breaks "
				"at %d H and %d V.",
				working_sview_config.grid_x_width,
				working_sview_config.grid_hori,
				working_sview_config.grid_vert);
			/* If the old width was wider than the
			 * current we need to remake the list so the
			 * table gets set up correctly, so destroy it
			 * here and it will be remade in get_system_stats(). */
			if ((width > working_sview_config.grid_x_width)
			    && grid_button_list) {
				list_destroy(grid_button_list);
				grid_button_list = NULL;
				refresh = 1;
			}
			get_system_stats(main_grid_table);
			if (refresh)
				refresh_main(NULL, NULL);
		}
		gtk_statusbar_pop(GTK_STATUSBAR(main_statusbar),
				  STATUS_REFRESH);
		response = gtk_statusbar_push(GTK_STATUSBAR(main_statusbar),
					      STATUS_REFRESH,
					      temp);
		g_free(temp);
		if (!sview_thread_new(_refresh_thr, GINT_TO_POINTER(response),
				      FALSE, &error)) {
			g_printerr ("Failed to create refresh thread: %s\n",
				    error->message);
		}
	}

	gtk_widget_destroy(popup);

	return;
}

extern void about_popup(GtkAction *action, gpointer user_data)
{
	GtkWidget *table = gtk_table_new(1, 1, FALSE);
	GtkWidget *label = NULL;

	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"About",
		GTK_WINDOW(user_data),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);
	char *version = NULL;

	version = xstrdup_printf("SLURM Version: %s", SLURM_VERSION_STRING);

	label = gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_OK, GTK_RESPONSE_OK);

	gtk_window_set_default(GTK_WINDOW(popup), label);

	gtk_window_set_default_size(GTK_WINDOW(popup), 200, 50);

	label = gtk_label_new(version);

	xfree(version);

	gtk_container_set_border_width(GTK_CONTAINER(table), 10);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   table, FALSE, FALSE, 0);

	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);

	gtk_widget_show_all(popup);
	(void) gtk_dialog_run (GTK_DIALOG(popup));

	gtk_widget_destroy(popup);

	return;
}

extern void usage_popup(GtkAction *action, gpointer user_data)
{
	GtkWidget *table = gtk_table_new(1, 1, FALSE);
	GtkWidget *label = NULL;

	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"Usage",
		GTK_WINDOW(user_data),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);
	char *help_msg =
		"sview can be used to view and modify many of SLURM's\n"
		"records.\n\n"
		"Tabs are used to select the data type to work with.\n"
		"Right click on the tab to select it. Left click on\n"
		"the tab to control the fields of the table to be\n"
		"displayed. Those fields can then be re-ordered or used\n"
		"for sorting the records.\n\n"
		"Left click on a record to see the compute nodes\n"
		"associated with it. Right click on a record to modify\n"
		"it. The colored boxes represent compute nodes associated\n"
		"with each job, partition, etc. and may also selected\n"
		"with right and left buttons.\n\n"
		"Select 'Option' then 'Admin mode' to enable editing\n"
		"of the records.\n";

	label = gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_OK, GTK_RESPONSE_OK);

	gtk_window_set_default(GTK_WINDOW(popup), label);

	gtk_window_set_default_size(GTK_WINDOW(popup), 200, 50);

	label = gtk_label_new(help_msg);

	gtk_container_set_border_width(GTK_CONTAINER(table), 10);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   table, FALSE, FALSE, 0);

	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);

	gtk_widget_show_all(popup);
	(void) gtk_dialog_run (GTK_DIALOG(popup));

	gtk_widget_destroy(popup);

	return;
}
