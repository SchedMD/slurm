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
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

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
	return false;
}

/* Creates a tree model containing the completions */
void _search_entry(sview_search_info_t *sview_search_info)
{
	int id = 0;
	char title[100] = {0};
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	GError *error = NULL;
	char *upper = NULL, *lower = NULL;

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
		snprintf(title, 100, "Node(s) %s info",
			 sview_search_info->gchar_data);
		break;
	case SEARCH_NODE_STATE:
		id = NODE_PAGE;
		upper = node_state_string(sview_search_info->int_data);
		lower = str_tolower(upper);
		snprintf(title, 100, "Node(s) in the %s state", lower);
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
			if (!xstrcmp(popup_win->spec_info->title, title)) {
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

	if (!sview_thread_new((gpointer)popup_thr, popup_win, false, &error)) {
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
			   GTK_WIDGET(window), true, true, 0);

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

static void
_gtk_print_config_plugin_params_list(List l, char *title, bool first,
				     GtkTreeStore *treestore,
				     GtkTreeIter *iter)
{
	ListIterator itr = NULL;
	config_plugin_params_t *p;
	int update = 0;

	if (!l || !list_count(l))
		return;

	if (!first)
		add_display_treestore_line(update, treestore, iter, "", NULL);

	add_display_treestore_line_with_font(update, treestore, iter,
					     title, NULL, "bold");

	itr = list_iterator_create(l);
	while ((p = list_next(itr))){
		add_display_treestore_line_with_font(update, treestore, iter,
					   p->name, NULL, "italic");
		_gtk_print_key_pairs(p->key_pairs, NULL, 1, treestore, iter);
	}
	list_iterator_destroy(itr);
}

static void _layout_conf_ctl(GtkTreeStore *treestore,
			     slurm_ctl_conf_info_msg_t *slurm_ctl_conf_ptr)
{
	char time_str[256], tmp_str[300];
	GtkTreeIter iter;
	List ret_list = NULL;
	char *select_title = "Select Plugin Configuration";
	char *tmp_title = NULL;

	if (cluster_flags & CLUSTER_FLAG_CRAY)
		select_title = "\nCray configuration\n";

	if (!slurm_ctl_conf_ptr)
		return;

	slurm_make_time_str((time_t *)&slurm_ctl_conf_ptr->last_update,
			    time_str, sizeof(time_str));
	snprintf(tmp_str, sizeof(tmp_str), "Configuration data as of %s",
		 time_str);

	ret_list = slurm_ctl_conf_2_key_pairs(slurm_ctl_conf_ptr);
	_gtk_print_key_pairs(ret_list, tmp_str, 1, treestore, &iter);
	FREE_NULL_LIST(ret_list);

	_gtk_print_key_pairs(slurm_ctl_conf_ptr->acct_gather_conf,
			     "Account Gather", 0, treestore, &iter);

	_gtk_print_key_pairs(slurm_ctl_conf_ptr->cgroup_conf,
			     "Cgroup Support", 0, treestore, &iter);

	_gtk_print_key_pairs(slurm_ctl_conf_ptr->ext_sensors_conf,
			     "External Sensors", 0, treestore, &iter);
	_gtk_print_key_pairs(slurm_ctl_conf_ptr->mpi_conf,
			     "MPI Plugins Configuration:", 0, treestore, &iter);

	xstrcat(tmp_title, "Node Features:");
	_gtk_print_config_plugin_params_list(
		slurm_ctl_conf_ptr->node_features_conf,
		tmp_title, 0, treestore, &iter);
	xfree(tmp_title);

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
	char tmp_str[256], *user_name = NULL;
	List dbd_config_list = NULL;

	/* first load accounting parms from slurm.conf */
	uint16_t track_wckey = slurm_get_track_wckey();

	slurm_make_time_str(&now, tmp_str, sizeof(tmp_str));
	add_display_treestore_line_with_font(
		update, treestore, &iter,
		"Slurm Configuration data as of", tmp_str, "bold");

	add_display_treestore_line(update, treestore, &iter,
				   "AccountingStorageBackupHost",
				   slurm_conf.accounting_storage_backup_host);
	add_display_treestore_line(update, treestore, &iter,
				   "AccountingStorageHost",
				   slurm_conf.accounting_storage_host);
	add_display_treestore_line(update, treestore, &iter,
				   "AccountingStoragePass",
				   slurm_conf.accounting_storage_pass);
	sprintf(tmp_str, "%u", slurm_conf.accounting_storage_port);
	add_display_treestore_line(update, treestore, &iter,
				   "AccountingStorageParameters",
				   slurm_conf.accounting_storage_params);
	add_display_treestore_line(update, treestore, &iter,
				   "AccountingStoragePort", tmp_str);
	add_display_treestore_line(update, treestore, &iter,
	                           "AccountingStorageType",
	                           slurm_conf.accounting_storage_type);
	add_display_treestore_line(update, treestore, &iter,
				   "AccountingStorageUser",
				   slurm_conf.accounting_storage_user);
	add_display_treestore_line(update, treestore, &iter, "AuthType",
				   slurm_conf.authtype);
	snprintf(tmp_str, sizeof(tmp_str), "%u sec", slurm_conf.msg_timeout);
	add_display_treestore_line(update, treestore, &iter,
				   "MessageTimeout", tmp_str);
	add_display_treestore_line(update, treestore, &iter, "PluginDir",
                                   slurm_conf.plugindir);
	private_data_string(slurm_conf.private_data, tmp_str, sizeof(tmp_str));
	add_display_treestore_line(update, treestore, &iter,
				   "PrivateData", tmp_str);
	user_name = uid_to_string_cached(slurm_conf.slurm_user_id);
	sprintf(tmp_str, "%s(%u)", user_name, slurm_conf.slurm_user_id);
	add_display_treestore_line(update, treestore, &iter,
				   "SlurmUserId", tmp_str);
	add_display_treestore_line(update, treestore, &iter,
				   "SLURM_CONF", default_slurm_config_file);
	add_display_treestore_line(update, treestore, &iter,
				   "SLURM_VERSION", SLURM_VERSION_STRING);
	sprintf(tmp_str, "%u", track_wckey);
	add_display_treestore_line(update, treestore, &iter,
				   "TrackWCKey", tmp_str);

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
		"Slurm Config Info",
		GTK_WINDOW(user_data),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_CLOSE,
		GTK_RESPONSE_OK,
		NULL);
	GtkTreeStore *treestore =
		_local_create_treestore_2cols(popup, 600, 400);
	static slurm_ctl_conf_info_msg_t  *slurm_ctl_conf_ptr = NULL;

	gtk_window_set_type_hint(GTK_WINDOW(popup),
				 GDK_WINDOW_TYPE_HINT_NORMAL);
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
		"Slurm Database Config Info",
		GTK_WINDOW(user_data),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_CLOSE,
		GTK_RESPONSE_OK,
		NULL);
	GtkTreeStore *treestore =
		_local_create_treestore_2cols(popup, 600, 400);

	gtk_window_set_type_hint(GTK_WINDOW(popup),
				 GDK_WINDOW_TYPE_HINT_NORMAL);
	g_signal_connect(G_OBJECT(popup), "delete_event",
			 G_CALLBACK(_delete_popup), NULL);
	g_signal_connect(G_OBJECT(popup), "response",
			 G_CALLBACK(_delete_popup), NULL);

	_layout_conf_dbd(treestore);

	gtk_widget_show_all(popup);

	FREE_NULL_LIST(dbd_config_list);

	return;
}

extern void create_daemon_popup(GtkAction *action, gpointer user_data)
{
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"Slurm Daemons running",
		GTK_WINDOW(user_data),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_CLOSE,
		GTK_RESPONSE_OK,
		NULL);
	int i, update = 0;
	slurm_ctl_conf_info_msg_t *conf;
	char me[HOST_NAME_MAX], *b, *c, *n;
	char *token, *save_ptr = NULL;
	int actld = 0, ctld = 0, d = 0;
	GtkTreeStore *treestore =
		_local_create_treestore_2cols(popup, 300, 100);
	GtkTreeIter iter;
	gtk_window_set_type_hint(GTK_WINDOW(popup),
				 GDK_WINDOW_TYPE_HINT_NORMAL);
	g_signal_connect(G_OBJECT(popup), "delete_event",
			 G_CALLBACK(_delete_popup), NULL);
	g_signal_connect(G_OBJECT(popup), "response",
			 G_CALLBACK(_delete_popup), NULL);

	slurm_conf_init(NULL);
	conf = slurm_conf_lock();

	gethostname_short(me, HOST_NAME_MAX);
	for (i = 1; i < conf->control_cnt; i++) {
		if ((b = conf->control_machine[i])) {
			if (!xstrcmp(b, me) ||
			    !xstrcasecmp(b, "localhost"))
				ctld = 1;
		}
	}
	if (conf->control_machine[0]) {
		actld = 1;
		c = xstrdup(conf->control_machine[0]);
		token = strtok_r(c, ",", &save_ptr);
		while (token) {
			if ((xstrcmp(token, me) == 0) ||
			    (xstrcasecmp(token, "localhost") == 0)) {
				ctld = 1;
				break;
			}
			token = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(c);
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
	int response = 0;
	GtkWidget *label = NULL;
	GtkWidget *entry = NULL;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	const gchar *name = gtk_action_get_name(action);
	sview_search_info_t sview_search_info;
	update_part_msg_t *part_msg = NULL;
	resv_desc_msg_t *resv_msg = NULL;
	char *res_name, *temp;

	sview_search_info.gchar_data = NULL;
	sview_search_info.int_data = NO_VAL;
	sview_search_info.int_data2 = NO_VAL;

	label = gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_OK, GTK_RESPONSE_OK);
	gtk_window_set_type_hint(GTK_WINDOW(popup),
				 GDK_WINDOW_TYPE_HINT_NORMAL);
	gtk_window_set_default(GTK_WINDOW(popup), label);
	gtk_dialog_add_button(GTK_DIALOG(popup),
			      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	gtk_window_set_default_size(GTK_WINDOW(popup), 400, 600);

	if (!xstrcmp(name, "partition")) {
		sview_search_info.search_type = CREATE_PARTITION;
		label = gtk_label_new(
			"Partition creation specifications\n\n"
			"Specify Name. All other fields are optional.");
		part_msg = xmalloc(sizeof(update_part_msg_t));
		slurm_init_part_desc_msg(part_msg);
		entry = create_part_entry(part_msg, model, &iter);
	} else if (!xstrcmp(name, "reservation")) {
		sview_search_info.search_type = CREATE_RESERVATION;
		label = gtk_label_new(
			"Reservation creation specifications\n\n"
			"Specify Time_Start and either Duration or Time_End.\n"
			"Specify either Node_Count or Node_List.\n"
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
			   label, false, false, 0);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   entry, true, true, 0);

	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK) {
		if (!sview_search_info.search_type)
			goto end_it;

		switch(sview_search_info.search_type) {
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

	sview_search_info.cluster_name = NULL;
	sview_search_info.gchar_data = NULL;
	sview_search_info.int_data = NO_VAL;
	sview_search_info.int_data2 = NO_VAL;

	label = gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_OK, GTK_RESPONSE_OK);
	gtk_window_set_type_hint(GTK_WINDOW(popup),
				 GDK_WINDOW_TYPE_HINT_NORMAL);
	gtk_window_set_default(GTK_WINDOW(popup), label);
	gtk_dialog_add_button(GTK_DIALOG(popup),
			      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	if (!xstrcmp(name, "jobid")) {
		sview_search_info.search_type = SEARCH_JOB_ID;
		entry = create_entry();
		label = gtk_label_new("Which job id?");
	} else if (!xstrcmp(name, "user_jobs")) {
		sview_search_info.search_type = SEARCH_JOB_USER;
		entry = create_entry();
		label = gtk_label_new("Which user?");
	} else if (!xstrcmp(name, "state_jobs")) {
		display_data_t pulldown_display_data[] = {
			{G_TYPE_NONE, JOB_PENDING, "Pending", true, -1},
			{G_TYPE_NONE, JOB_CONFIGURING, "Configuring", true, -1},
			{G_TYPE_NONE, JOB_RUNNING, "Running", true, -1},
			{G_TYPE_NONE, JOB_SUSPENDED, "Suspended", true, -1},
			{G_TYPE_NONE, JOB_COMPLETE, "Complete", true, -1},
			{G_TYPE_NONE, JOB_CANCELLED, "Cancelled", true, -1},
			{G_TYPE_NONE, JOB_FAILED, "Failed", true, -1},
			{G_TYPE_NONE, JOB_TIMEOUT, "Timeout", true, -1},
			{G_TYPE_NONE, JOB_NODE_FAIL, "Node Failure", true, -1},
			{G_TYPE_NONE, JOB_PREEMPTED, "Preempted", true, -1},
			{G_TYPE_NONE, JOB_BOOT_FAIL, "Boot Failure", true, -1},
			{G_TYPE_NONE, JOB_DEADLINE, "Deadline", true, -1},
			{G_TYPE_NONE, JOB_OOM, "Out Of Memory", true, -1},
			{G_TYPE_NONE, -1, NULL, false, -1}
		};

		sview_search_info.search_type = SEARCH_JOB_STATE;
		entry = create_pulldown_combo(pulldown_display_data);
		label = gtk_label_new("Which state?");
	} else if (!xstrcmp(name, "partition_name")) {
		sview_search_info.search_type = SEARCH_PARTITION_NAME;
		entry = create_entry();
		label = gtk_label_new("Which partition");
	} else if (!xstrcmp(name, "partition_state")) {
		display_data_t pulldown_display_data[] = {
			{G_TYPE_NONE, PARTITION_UP, "Up", true, -1},
			{G_TYPE_NONE, PARTITION_DOWN, "Down", true, -1},
			{G_TYPE_NONE, PARTITION_INACTIVE, "Inactive", true, -1},
			{G_TYPE_NONE, PARTITION_DRAIN, "Drain", true, -1},
			{G_TYPE_NONE, -1, NULL, false, -1}
		};

		sview_search_info.search_type = SEARCH_PARTITION_STATE;
		entry = create_pulldown_combo(pulldown_display_data);
		label = gtk_label_new("Which state?");
	} else if (!xstrcmp(name, "node_name")) {
		sview_search_info.search_type = SEARCH_NODE_NAME;
		entry = create_entry();
		label = gtk_label_new("Which node(s)?\n"
				      "(ranged or comma separated)");
	} else if (!xstrcmp(name, "node_state")) {
		display_data_t pulldown_display_data[] = {
			{G_TYPE_NONE, NODE_STATE_ALLOCATED, "Allocated",
			 true, -1},
			{G_TYPE_NONE, NODE_STATE_COMPLETING, "Completing",
			 true, -1},
			{G_TYPE_NONE, NODE_STATE_DOWN, "Down", true, -1},
			{G_TYPE_NONE, NODE_STATE_ALLOCATED | NODE_STATE_DRAIN,
			 "Draining", true, -1},
			{G_TYPE_NONE, NODE_STATE_IDLE | NODE_STATE_DRAIN,
			 "Drained", true, -1},
			{G_TYPE_NONE, NODE_STATE_FAIL, "Fail", true, -1},
			{G_TYPE_NONE, NODE_STATE_FAIL | NODE_STATE_ALLOCATED,
			 "Failing", true, -1},
			{G_TYPE_NONE, NODE_STATE_FUTURE, "Future", true, -1},
			{G_TYPE_NONE, NODE_STATE_IDLE, "Idle", true, -1},
			{G_TYPE_NONE, NODE_STATE_MAINT, "Maint", true, -1},
			{G_TYPE_NONE, NODE_STATE_MIXED, "Mixed", true, -1},
			{G_TYPE_NONE, NODE_STATE_NO_RESPOND,
			 "No Respond", true, -1},
			{G_TYPE_NONE, NODE_STATE_NET | NODE_STATE_IDLE,
			 "PerfCTRs", true, -1},
			{G_TYPE_NONE, NODE_STATE_IDLE | NODE_STATE_PLANNED,
			 "Planned", true, -1},
			{G_TYPE_NONE, NODE_STATE_POWERED_DOWN,
			 "Power Down", true, -1},
			{G_TYPE_NONE, NODE_STATE_POWERING_UP,
			 "Power Up", true, -1},
			{G_TYPE_NONE, NODE_STATE_REBOOT_REQUESTED,
			 "Reboot", true, -1},
			{G_TYPE_NONE, NODE_STATE_REBOOT_ISSUED,
			 "Reboot^", true, -1},
			{G_TYPE_NONE, NODE_STATE_RES | NODE_STATE_IDLE,
			 "Reserved", true, -1},
			{G_TYPE_NONE, NODE_STATE_UNKNOWN, "Unknown", true, -1},
			{G_TYPE_NONE, -1, NULL, false, -1}
		};

		sview_search_info.search_type = SEARCH_NODE_STATE;
		entry = create_pulldown_combo(pulldown_display_data);
		label = gtk_label_new("Which state?");
	} else if (!xstrcmp(name, "reservation_name")) {
		sview_search_info.search_type = SEARCH_RESERVATION_NAME;
		entry = create_entry();
		label = gtk_label_new("Which reservation");
	} else {
		sview_search_info.search_type = 0;
		goto end_it;
	}

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   label, false, false, 0);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   entry, false, false, 0);

	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK) {
		if (!sview_search_info.search_type)
			goto end_it;

		switch(sview_search_info.search_type) {
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
	GtkWidget *table = gtk_table_new(1, 2, false);
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
	gtk_window_set_type_hint(GTK_WINDOW(popup),
				 GDK_WINDOW_TYPE_HINT_NORMAL);
	gtk_window_set_default(GTK_WINDOW(popup), label);
	gtk_dialog_add_button(GTK_DIALOG(popup),
			      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	label = gtk_label_new("Interval in Seconds ");

	gtk_container_set_border_width(GTK_CONTAINER(table), 10);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   table, false, false, 0);

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
				      false, &error)) {
			g_printerr ("Failed to create refresh thread: %s\n",
				    error->message);
		}
	}

	gtk_widget_destroy(popup);

	return;
}

extern void change_grid_popup(GtkAction *action, gpointer user_data)
{
	GtkWidget *table = gtk_table_new(1, 2, false);
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
	gtk_window_set_type_hint(GTK_WINDOW(popup),
				 GDK_WINDOW_TYPE_HINT_NORMAL);
	gtk_window_set_default(GTK_WINDOW(popup), label);
	gtk_dialog_add_button(GTK_DIALOG(popup),
			      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   table, false, false, 0);

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
				FREE_NULL_LIST(grid_button_list);
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
				      false, &error)) {
			g_printerr ("Failed to create refresh thread: %s\n",
				    error->message);
		}
	}

	gtk_widget_destroy(popup);

	return;
}

extern void about_popup(GtkAction *action, gpointer user_data)
{
	GtkWidget *table = gtk_table_new(1, 1, false);
	GtkWidget *label = NULL;

	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"About",
		GTK_WINDOW(user_data),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);
	char *version = NULL;

	version = xstrdup_printf("Slurm Version: %s", SLURM_VERSION_STRING);

	label = gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_OK, GTK_RESPONSE_OK);

	gtk_window_set_type_hint(GTK_WINDOW(popup),
				 GDK_WINDOW_TYPE_HINT_NORMAL);

	gtk_window_set_default(GTK_WINDOW(popup), label);

	gtk_window_set_default_size(GTK_WINDOW(popup), 200, 50);

	label = gtk_label_new(version);

	xfree(version);

	gtk_container_set_border_width(GTK_CONTAINER(table), 10);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   table, false, false, 0);

	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);

	gtk_widget_show_all(popup);
	(void) gtk_dialog_run (GTK_DIALOG(popup));

	gtk_widget_destroy(popup);

	return;
}

extern void usage_popup(GtkAction *action, gpointer user_data)
{
	GtkWidget *table = gtk_table_new(1, 1, false);
	GtkWidget *label = NULL;

	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"Usage",
		GTK_WINDOW(user_data),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);
	char *help_msg =
		"sview can be used to view and modify many of Slurm's\n"
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

	gtk_window_set_type_hint(GTK_WINDOW(popup),
				 GDK_WINDOW_TYPE_HINT_NORMAL);

	gtk_window_set_default(GTK_WINDOW(popup), label);

	gtk_window_set_default_size(GTK_WINDOW(popup), 200, 50);

	label = gtk_label_new(help_msg);

	gtk_container_set_border_width(GTK_CONTAINER(table), 10);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   table, false, false, 0);

	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);

	gtk_widget_show_all(popup);
	(void) gtk_dialog_run (GTK_DIALOG(popup));

	gtk_widget_destroy(popup);

	return;
}

extern void display_fed_disabled_popup(const char *title)
{
	char tmp_char[100];
	GtkWidget *label = NULL;
	GtkDialog *dialog = GTK_DIALOG(gtk_dialog_new_with_buttons(
						title, GTK_WINDOW(main_window),
						GTK_DIALOG_MODAL |
						GTK_DIALOG_DESTROY_WITH_PARENT,
						NULL));
	label = gtk_dialog_add_button(dialog, GTK_STOCK_OK, GTK_RESPONSE_OK);
	gtk_window_set_default(GTK_WINDOW(dialog), label);
	snprintf(tmp_char, sizeof(tmp_char),
		 "Disabled in a federated view.\n"
		 "Go to the individual cluster and perform the action.");
	label = gtk_label_new(tmp_char);
	gtk_box_pack_start(GTK_BOX(dialog->vbox), label, false, false, 0);
	gtk_widget_show_all(GTK_WIDGET(dialog));
	(void) gtk_dialog_run(dialog);
	gtk_widget_destroy(GTK_WIDGET(dialog));
}
