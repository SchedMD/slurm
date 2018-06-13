/****************************************************************************\
 *  sview.c - main for sview
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
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

#define _DEBUG 0

typedef struct {
	GtkTable *table;
	int page_num;
} page_thr_t;

static void _get_info_tabs(GtkTable *table, display_data_t *display_data);

/* globals */
sview_config_t default_sview_config;
sview_config_t working_sview_config;
int adding = 1;
int fini = 0;
int grid_init = 0;
bool toggled = false;
bool force_refresh = false;
bool apply_hidden_change = true;
bool apply_partition_check = false;
List popup_list = NULL;
List signal_params_list = NULL;
int page_running = -1;
bool global_entry_changed = 0;
bool global_send_update_msg = 0;
bool global_edit_error = 0;
int global_error_code = 0;
int global_row_count = 0;
bool global_multi_error = 0;
gint last_event_x = 0;
gint last_event_y = 0;
GdkCursor* in_process_cursor;
gchar *global_edit_error_msg = NULL;
GtkWidget *main_notebook = NULL;
GtkWidget *main_statusbar = NULL;
GtkWidget *main_window = NULL;
GtkWidget *grid_window = NULL;
GtkTable *main_grid_table = NULL;
GMutex *sview_mutex = NULL;
GMutex *grid_mutex = NULL;
GCond *grid_cond = NULL;
int cluster_dims;
uint32_t cluster_flags;
List cluster_list = NULL;
char *orig_cluster_name = NULL;
switch_record_bitmaps_t *g_switch_nodes_maps = NULL;
popup_pos_t popup_pos;
char *federation_name = NULL;

front_end_info_msg_t *g_front_end_info_ptr;
job_info_msg_t *g_job_info_ptr = NULL;
node_info_msg_t *g_node_info_ptr = NULL;
partition_info_msg_t *g_part_info_ptr = NULL;
reserve_info_msg_t *g_resv_info_ptr = NULL;
burst_buffer_info_msg_t *g_bb_info_ptr = NULL;
slurm_ctl_conf_info_msg_t *g_ctl_info_ptr = NULL;
job_step_info_response_msg_t *g_step_info_ptr = NULL;
topo_info_response_msg_t *g_topo_info_msg_ptr = NULL;

static GtkActionGroup *admin_action_group = NULL;
static GtkActionGroup *menu_action_group = NULL;
static bool debug_inited = 0;
static int g_menu_id = 0;
static GtkUIManager *g_ui_manager = NULL;
static GtkToggleActionEntry *debug_actions = NULL;
static int debug_action_entries = 0;
/*
  popup_positioner_t main_popup_positioner[] = {
  {0,"Sview Defaults", 150, 700 },
  {0,"Full info for job", 450, 650 },
  {0,"Title_2", 100, 100 },
  {-1,"FENCE", -1, -1 }
  };
*/

display_data_t main_display_data[] = {
	{G_TYPE_NONE, JOB_PAGE, "Jobs", true, -1,
	 refresh_main, create_model_job, admin_edit_job,
	 get_info_job, specific_info_job,
	 set_menus_job, NULL},
	{G_TYPE_NONE, PART_PAGE, "Partitions", true, -1,
	 refresh_main, create_model_part, admin_edit_part,
	 get_info_part, specific_info_part,
	 set_menus_part, NULL},
	{G_TYPE_NONE, RESV_PAGE, "Reservations", true, -1,
	 refresh_main, create_model_resv, admin_edit_resv,
	 get_info_resv, specific_info_resv,
	 set_menus_resv, NULL},
	{G_TYPE_NONE, BB_PAGE, "Burst Buffers", true, -1,
	 refresh_main, create_model_bb, admin_edit_bb,
	 get_info_bb, specific_info_bb,
	 set_menus_bb, NULL},
	{G_TYPE_NONE, NODE_PAGE, "Nodes", false, -1,
	 refresh_main, NULL, NULL,
	 get_info_node, specific_info_node,
	 set_menus_node, NULL},
	{G_TYPE_NONE, FRONT_END_PAGE, "Front End Nodes", false, -1,
	 refresh_main, create_model_front_end, admin_edit_front_end,
	 get_info_front_end, specific_info_front_end,
	 set_menus_front_end, NULL},
	{G_TYPE_NONE, SUBMIT_PAGE, NULL, false, -1,
	 refresh_main, NULL, NULL, NULL,
	 NULL, NULL, NULL},
	{G_TYPE_NONE, ADMIN_PAGE, NULL, false, -1,
	 refresh_main, NULL, NULL,
	 NULL, NULL,
	 NULL, NULL},
	{G_TYPE_NONE, INFO_PAGE, NULL, false, -1,
	 refresh_main, NULL, NULL,
	 NULL, NULL,
	 NULL, NULL},
	{G_TYPE_NONE, TAB_PAGE, "Visible Tabs", true, -1,
	 refresh_main, NULL, NULL, _get_info_tabs,
	 NULL, NULL, NULL},
	{G_TYPE_NONE, -1, NULL, false, -1}
};

void *_page_thr(void *arg)
{
	page_thr_t *page = (page_thr_t *)arg;
	int num = page->page_num;
	GtkTable *table = page->table;
	display_data_t *display_data = &main_display_data[num];
	static int thread_count = 0;
	bool reset_highlight = true;

	xfree(page);
	if (!grid_init) {
		/* we need to signal any threads that are waiting */
		g_mutex_lock(grid_mutex);
		g_cond_signal(grid_cond);
		g_mutex_unlock(grid_mutex);

		/* wait for the grid to be inited */
		g_mutex_lock(grid_mutex);
		g_cond_wait(grid_cond, grid_mutex);
		g_mutex_unlock(grid_mutex);

		/* if the grid isn't there just return */
		if (!grid_init)
			return NULL;
	}

	g_mutex_lock(sview_mutex);
	thread_count++;
	g_mutex_unlock(sview_mutex);
	while (page_running == num) {
#if _DEBUG
		DEF_TIMERS;
		START_TIMER;
#endif
//		g_mutex_lock(sview_mutex);
		gdk_threads_enter();
		sview_init_grid(reset_highlight);
		reset_highlight=false;
		(display_data->get_info)(table, display_data);
		//gdk_flush();
		gdk_threads_leave();
//		g_mutex_unlock(sview_mutex);
#if _DEBUG
		END_TIMER;
		g_print("got for iteration: %s\n", TIME_STR);
#endif
		sleep(working_sview_config.refresh_delay);
		g_mutex_lock(sview_mutex);
		if (thread_count > 1) {
			g_mutex_unlock(sview_mutex);
			break;
		}
		g_mutex_unlock(sview_mutex);
	}
	g_mutex_lock(sview_mutex);
	//g_print("now here\n");
	thread_count--;
	//g_print("done decrementing\n");
	g_mutex_unlock(sview_mutex);
	//g_print("done\n");
	return NULL;
}

void *_grid_init_thr(void *arg)
{
	guint page = 0;
	GtkScrolledWindow *window = NULL;
	GtkBin *bin = NULL;
	GtkViewport *view = NULL;
	GtkTable *table = NULL;
	int rc = SLURM_SUCCESS;

	while (!grid_init && !fini) {
		gdk_threads_enter();
		page = gtk_notebook_get_current_page(
			GTK_NOTEBOOK(main_notebook));
		window = GTK_SCROLLED_WINDOW(
			gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_notebook),
						  page));
		bin = GTK_BIN(&window->container);
		view = GTK_VIEWPORT(bin->child);
		bin = GTK_BIN(&view->bin);
		table = GTK_TABLE(bin->child);
		/* set up the main grid */
		rc = get_system_stats(table);
		//gdk_flush();
		gdk_threads_leave();

		if (rc != SLURM_SUCCESS)
			sleep(working_sview_config.refresh_delay);
		else
			grid_init = 1;

	}
	g_mutex_lock(grid_mutex);
	g_cond_signal(grid_cond);
	g_mutex_unlock(grid_mutex);

	return NULL;
}

static void _page_switched(GtkNotebook     *notebook,
			   GtkNotebookPage *page,
			   guint            page_num,
			   gpointer         user_data)
{
	GtkScrolledWindow *window = GTK_SCROLLED_WINDOW(
		gtk_notebook_get_nth_page(notebook, page_num));
	if (!window)
		return;
	GtkBin *bin = GTK_BIN(&window->container);
	GtkViewport *view = GTK_VIEWPORT(bin->child);
	GtkBin *bin2 = GTK_BIN(&view->bin);
	GtkTable *table = GTK_TABLE(bin2->child);
	int i = 0;
	page_thr_t *page_thr = NULL;
	GError *error = NULL;
	static int started_grid_init = 0;


	/*set spinning cursor while loading*/
	if (main_window->window && (page_num != TAB_PAGE))
		gdk_window_set_cursor(main_window->window, in_process_cursor);

	/* make sure we aren't adding the page, and really asking for info */
	if (adding)
		return;
	else if (!grid_init && !started_grid_init) {
		/* start the thread to make the grid only once */
		if (!sview_thread_new(
			    _grid_init_thr, notebook, false, &error)) {
			g_printerr ("Failed to create grid init thread: %s\n",
				    error->message);
			return;
		}
		started_grid_init = 1;
	}

	if (page_running != -1)
		page_running = page_num;

	for(i=0; i<PAGE_CNT; i++) {
		if ((main_display_data[i].id == -1)
		    || (main_display_data[i].extra == page_num))
			break;
	}

	if (main_display_data[i].extra != page_num)
		return;
	if (main_display_data[i].get_info) {
		page_running = i;
		/* If we return here we would not clear the grid which
		   may need to be done. */
		/* if (toggled || force_refresh) { */
		/*	(main_display_data[i].get_info)( */
		/*		table, &main_display_data[i]); */
		/*	return; */
		/* } */

		page_thr = xmalloc(sizeof(page_thr_t));
		page_thr->page_num = i;
		page_thr->table = table;

		if (!sview_thread_new(_page_thr, page_thr, false, &error)) {
			g_printerr ("Failed to create page thread: %s\n",
				    error->message);
			return;
		}
	}
}

static void _set_admin_mode(GtkToggleAction *action)
{
//	GtkAction *admin_action = NULL;
	if (action)
		working_sview_config.admin_mode
			= gtk_toggle_action_get_active(action);
	if (!working_sview_config.admin_mode)
		gtk_statusbar_pop(GTK_STATUSBAR(main_statusbar),
				  STATUS_ADMIN_MODE);
	else
		gtk_statusbar_push(GTK_STATUSBAR(main_statusbar),
				   STATUS_ADMIN_MODE,
				   "Admin mode activated! "
				   "Think before you alter anything.");

	gtk_action_group_set_sensitive(admin_action_group,
				       working_sview_config.admin_mode);
}

static void _set_grid(GtkToggleAction *action)
{
	if (action)
		working_sview_config.show_grid
			= gtk_toggle_action_get_active(action);

	if (cluster_flags & CLUSTER_FLAG_FED)
		return;

	if (!working_sview_config.show_grid)
		gtk_widget_hide(grid_window);
	else
		gtk_widget_show(grid_window);

	return;
}

static void _set_hidden(GtkToggleAction *action)
{
	char *tmp;
	if (action)
		working_sview_config.show_hidden
			= gtk_toggle_action_get_active(action);
	if (!working_sview_config.show_hidden)
		tmp = g_strdup_printf(
			"Hidden partitions and their jobs are now hidden");
	else
		tmp = g_strdup_printf(
			"Hidden partitions and their jobs are now visible");
	if (apply_hidden_change) {
		FREE_NULL_LIST(grid_button_list);
		get_system_stats(main_grid_table);
	}
	apply_hidden_change = true;
	refresh_main(NULL, NULL);
	display_edit_note(tmp);
	g_free(tmp);
	return;
}

static void _set_page_opts(GtkToggleAction *action)
{
	char *tmp;
	if (action)
		working_sview_config.save_page_opts
			= gtk_toggle_action_get_active(action);
	if (working_sview_config.save_page_opts)
		tmp = g_strdup_printf("Save Page Options now ON");
	else
		tmp = g_strdup_printf("Save Page Options now OFF");

	refresh_main(NULL, NULL);
	display_edit_note(tmp);
	g_free(tmp);
	return;
}

#ifdef WANT_TOPO_ON_MAIN_OPTIONS
static void _set_topogrid(GtkToggleAction *action)
{
	char *tmp;
	int rc = SLURM_SUCCESS;

	if (action) {
		working_sview_config.grid_topological
			= gtk_toggle_action_get_active(action);
	}
	apply_hidden_change = false;
	if (working_sview_config.grid_topological) {
		if (!g_switch_nodes_maps)
			rc = get_topo_conf();
		if (rc != SLURM_SUCCESS)
			/*denied*/
			tmp = g_strdup_printf("Valid topology not detected");
		else
			tmp = g_strdup_printf("Grid changed to topology order");

	}
	refresh_main(NULL, NULL);
	display_edit_note(tmp);
	g_free(tmp);
	return;
}
#endif

static void _set_ruled(GtkToggleAction *action)
{
	char *tmp;
	if (action)
		working_sview_config.ruled_treeview
			= gtk_toggle_action_get_active(action);
	if (!working_sview_config.ruled_treeview)
		tmp = g_strdup_printf(
			"Tables not ruled");
	else
		tmp = g_strdup_printf(
			"Tables ruled");

	/* get rid of each existing table */
	cluster_change_front_end();
	cluster_change_resv();
	cluster_change_part();
	cluster_change_job();
	cluster_change_node();
	cluster_change_bb();

	refresh_main(NULL, NULL);
	display_edit_note(tmp);
	g_free(tmp);
	return;
}

static void _reconfigure(GtkToggleAction *action)
{
	char *temp = NULL;

	if (!slurm_reconfigure())
		temp = g_strdup_printf(
			"Reconfigure sent to slurm successfully");
	else
		temp = g_strdup_printf("Problem with reconfigure request");
	display_edit_note(temp);
	g_free(temp);
}

static void _get_current_debug(GtkRadioAction *action)
{
	static int debug_level = 0;
	static slurm_ctl_conf_info_msg_t  *slurm_ctl_conf_ptr = NULL;
	static GtkAction *debug_action = NULL;
	int err_code = get_new_info_config(&slurm_ctl_conf_ptr);

	if (err_code != SLURM_ERROR)
		debug_level = slurm_ctl_conf_ptr->slurmctld_debug;

	if (!debug_action)
		debug_action = gtk_action_group_get_action(
			menu_action_group, "debug_quiet");
	/* Since this is the inital value we don't signal anything
	   changed so we need to make it happen here */
	if (debug_level == 0)
		debug_inited = 1;
	sview_radio_action_set_current_value(GTK_RADIO_ACTION(debug_action),
					     debug_level);
}

static void _get_current_debug_flags(GtkToggleAction *action)
{
	static uint64_t debug_flags = 0, tmp_flags;
	static slurm_ctl_conf_info_msg_t  *slurm_ctl_conf_ptr = NULL;
	int err_code = get_new_info_config(&slurm_ctl_conf_ptr);
	GtkAction *debug_action = NULL;
	GtkToggleAction *toggle_action;
	gboolean orig_state, new_state;
	int i;

	if (err_code != SLURM_ERROR)
		debug_flags = slurm_ctl_conf_ptr->debug_flags;

	for (i = 0; i < debug_action_entries; i++)  {
		debug_action = gtk_action_group_get_action(
			menu_action_group, debug_actions[i].name);
		toggle_action = GTK_TOGGLE_ACTION(debug_action);
		orig_state = gtk_toggle_action_get_active(toggle_action);
		if (debug_str2flags((char *)debug_actions[i].name, &tmp_flags)
		    != SLURM_SUCCESS) {
			g_error("debug_str2flags no good: %s\n",
				debug_actions[i].name);
			continue;
		}
		new_state = debug_flags & tmp_flags;
		if (orig_state != new_state)
			gtk_toggle_action_set_active(toggle_action, new_state);
	}
}

static void _set_debug(GtkRadioAction *action,
		       GtkRadioAction *extra,
		       GtkNotebook *notebook)
{
	char *temp = NULL;
	int level;
	/* This is here to make sure we got the correct value in the
	   beginning.  This gets called when the value is
	   changed. And since we don't set it at the beginning we
	   need to check it here. */
	if (!debug_inited) {
		debug_inited = 1;
		return;
	}

	level = gtk_radio_action_get_current_value(action);
	if (!slurm_set_debug_level(level)) {
		temp = g_strdup_printf(
			"Slurmctld debug level is now set to %d", level);
	} else
		temp = g_strdup_printf("Problem with set debug level request");
	display_edit_note(temp);
	g_free(temp);
}

static void _set_flags(GtkToggleAction *action)
{
	char *temp = NULL;
	uint64_t debug_flags_plus = 0, debug_flags_minus = 0;
	uint64_t flag = (uint64_t)NO_VAL;
	const char *name;

	if (!action)
		return;

	name = gtk_action_get_name(GTK_ACTION(action));
	if (!name)
		return;

	if (debug_str2flags((char *)name, &flag) != SLURM_SUCCESS)
		return;

	if (action && gtk_toggle_action_get_active(action))
		debug_flags_plus  |= flag;
	else
		debug_flags_minus |= flag;

	if (!slurm_set_debugflags(debug_flags_plus, debug_flags_minus))
		temp = g_strdup_printf("Slurmctld DebugFlags reset");
	else
		temp = g_strdup_printf("Problem with set DebugFlags request");
	display_edit_note(temp);
	g_free(temp);
}

static void _tab_pos(GtkRadioAction *action,
		     GtkRadioAction *extra,
		     GtkNotebook *notebook)
{
	working_sview_config.tab_pos =
		gtk_radio_action_get_current_value(action);
	gtk_notebook_set_tab_pos(notebook, working_sview_config.tab_pos);
}

static void _init_pages(void)
{
	int i;
	for(i=0; i<PAGE_CNT; i++) {
		if (!main_display_data[i].get_info)
			continue;
		(main_display_data[i].get_info)(NULL, &main_display_data[i]);
	}
}

static void _persist_dynamics(void)
{

	gint g_x;
	gint g_y;

	gtk_window_get_size(GTK_WINDOW(main_window), &g_x, &g_y);

	default_sview_config.main_width = g_x;
	default_sview_config.main_height = g_y;

	save_defaults(true);
}

static gboolean _delete(GtkWidget *widget,
			GtkWidget *event,
			gpointer data)
{
	int i;

	_persist_dynamics();
	fini = 1;
	gtk_main_quit();

#ifdef MEMORY_LEAK_DEBUG
	FREE_NULL_LIST(popup_list);
	FREE_NULL_LIST(grid_button_list);
	FREE_NULL_LIST(multi_button_list);
	FREE_NULL_LIST(signal_params_list);
	FREE_NULL_LIST(cluster_list);
	xfree(orig_cluster_name);
	uid_cache_clear();
#endif
	for (i = 0; i<debug_action_entries; i++) {
		xfree(debug_actions[i].name);
	}
	xfree(debug_actions);

	return false;
}

static char *_get_ui_description()
{
	/* Our menu*/
	char *ui_description = NULL;
	int i;

	xstrcat(ui_description,
		"<ui>"
		"  <menubar name='main'>"
		"    <menu action='actions'>"
		"      <menu action='create'>"
		"        <menuitem action='batch_job'/>"
		"        <menuitem action='partition'/>"
		"        <menuitem action='reservation'/>"
		"      </menu>"
		"      <menu action='search'>"
		"        <menuitem action='jobid'/>"
		"        <menuitem action='user_jobs'/>"
		"        <menuitem action='state_jobs'/>");
	xstrcat(ui_description,
		"      <separator/>"
		"        <menuitem action='partition_name'/>"
		"        <menuitem action='partition_state'/>"
		"      <separator/>");
	xstrcat(ui_description,
		"        <menuitem action='node_name'/>"
		"        <menuitem action='node_state'/>");
	xstrcat(ui_description,
		"      <separator/>"
		"        <menuitem action='reservation_name'/>"
		"      </menu>"
		"      <menuitem action='refresh'/>"
		"      <menuitem action='reconfig'/>"
		"      <menu action='debuglevel'>"
		"        <menuitem action='debug_quiet'/>"
		"        <menuitem action='debug_fatal'/>"
		"        <menuitem action='debug_error'/>"
		"        <menuitem action='debug_info'/>"
		"        <menuitem action='debug_verbose'/>"
		"        <menuitem action='debug_debug'/>"
		"        <menuitem action='debug_debug2'/>"
		"        <menuitem action='debug_debug3'/>"
		"        <menuitem action='debug_debug4'/>"
		"        <menuitem action='debug_debug5'/>"
		"      </menu>"
		"      <menu action='debugflags'>");
	for (i = 0; i < debug_action_entries; i++)  {
		xstrfmtcat(ui_description,
			   "        <menuitem action='%s'/>",
			   debug_actions[i].name);
	}
	xstrcat(ui_description,
			"      </menu>"
		"      <separator/>"
		"      <menuitem action='exit'/>"
		"    </menu>"
		"    <menu action='options'>"
		"      <menuitem action='grid'/>"
		"      <menuitem action='hidden'/>"
		"      <menuitem action='page_opts'/>"
#ifdef WANT_TOPO_ON_MAIN_OPTIONS
		"      <menuitem action='topoorder'/>"
#endif
		"      <menuitem action='ruled'/>");
	if (cluster_dims == 1)
		xstrcat(ui_description,
			"      <menuitem action='grid_specs'/>");

	xstrcat(ui_description,
		"      <menuitem action='interval'/>"
		"      <separator/>"
		"      <menuitem action='admin'/>"
		"      <separator/>"
		"      <menu action='tab_pos'>"
		"        <menuitem action='tab_top'/>"
		"        <menuitem action='tab_bottom'/>"
		"        <menuitem action='tab_left'/>"
		"        <menuitem action='tab_right'/>"
		"      </menu>"
		"      <separator/>"
		"      <menuitem action='defaults'/>"
		"    </menu>"
		"    <menu action='displays'>"
		"      <menuitem action='config'/>"
		"      <menuitem action='dbconfig'/>"
		"    </menu>"
		"    <menu action='help'>"
		"      <menuitem action='about'/>"
		"      <menuitem action='usage'/>"
		/* "      <menuitem action='manual'/>" */
		"    </menu>"
		"  </menubar>"
		"</ui>");
	return ui_description;
}

/* Returns a menubar widget made from the above menu */
static GtkWidget *_get_menubar_menu(GtkWidget *window, GtkWidget *notebook)
{
	GtkAccelGroup *accel_group = NULL;
	GError *error = NULL;
	char *ui_description;

	GtkActionEntry entries[] = {
		{"actions", NULL, "_Actions", "<alt>a"},
		{"options", NULL, "_Options", "<alt>o"},
		{"displays", NULL, "_Query", "<alt>q"},
		{"batch_job", NULL, "Batch Job", "", "Submit batch job",
		 G_CALLBACK(create_create_popup)},
		{"partition", NULL, "Partition", "", "Create partition",
		 G_CALLBACK(create_create_popup)},
		{"reservation", NULL, "Reservation", "", "Create reservation",
		 G_CALLBACK(create_create_popup)},
		{"search", GTK_STOCK_FIND, "Search", ""},
		{"jobid", NULL, "Job ID",
		 "", "Search for jobid",
		 G_CALLBACK(create_search_popup)},
		{"user_jobs", NULL, "Specific User's Job(s)",
		 "", "Search for a specific users job(s)",
		 G_CALLBACK(create_search_popup)},
		{"state_jobs", NULL, "Job(s) in a Specific State",
		 "", "Search for job(s) in a specific state",
		 G_CALLBACK(create_search_popup)},
		{"partition_name", NULL, "Slurm Partition Name",
		 "", "Search for a specific Slurm partition",
		 G_CALLBACK(create_search_popup)},
		{"partition_state", NULL, "Slurm Partition State",
		 "", "Search for Slurm partitions in a given state",
		 G_CALLBACK(create_search_popup)},
		{"reservation_name", NULL, "Reservation Name",
		 "", "Search for reservation",
		 G_CALLBACK(create_search_popup)},
		{"tab_pos", NULL, "_Tab Position"},
		{"create", GTK_STOCK_ADD, "Create"},
		{"interval", GTK_STOCK_REFRESH, "Set Refresh _Interval",
		 "<control>i", "Change Refresh Interval",
		 G_CALLBACK(change_refresh_popup)},
		{"refresh", GTK_STOCK_REFRESH, "Refresh",
		 "F5", "Refreshes page", G_CALLBACK(refresh_main)},
		{"config", GTK_STOCK_INFO, "_Config Info",
		 "<control>c", "Displays info from slurm.conf file",
		 G_CALLBACK(create_config_popup)},
		{"dbconfig", GTK_STOCK_INFO, "_Database Config Info",
		 "<control>d",
		 "Displays info relevant to the "
		 "configuration of the Slurm Database.",
		 G_CALLBACK(create_dbconfig_popup)},
		{"exit", GTK_STOCK_QUIT, "E_xit",
		 "<control>x", "Exits Program", G_CALLBACK(_delete)},
		{"help", NULL, "_Help", "<alt>h"},
		{"about", GTK_STOCK_ABOUT, "Ab_out", "<control>o",
		 "About", G_CALLBACK(about_popup)},
		{"usage", GTK_STOCK_HELP, "Usage", "",
		 "Usage", G_CALLBACK(usage_popup)},
		//{"manual", GTK_STOCK_HELP, "_Manual", "<control>m"},
		{"grid_specs", GTK_STOCK_EDIT, "Set Grid _Properties",
		 "<control>p", "Change Grid Properties",
		 G_CALLBACK(change_grid_popup)},
		{"defaults", GTK_STOCK_EDIT, "_Set Default Settings",
		 "<control>s", "Change Default Settings",
		 G_CALLBACK(configure_defaults)},
	};

	GtkActionEntry node_entries[] = {
		{"node_name", NULL,
		 "Node(s) Name",
		 "", "Search for a specific Node(s)",
		 G_CALLBACK(create_search_popup)},
		{"node_state", NULL,
		 "Node State",
		 "", "Search for a Node in a given state",
		 G_CALLBACK(create_search_popup)},
	};

	GtkActionEntry admin_entries[] = {
		{"reconfig", GTK_STOCK_REDO, "SLUR_M Reconfigure",
		 "<control>m", "Reconfigures System",
		 G_CALLBACK(_reconfigure)},
		{"debugflags", GTK_STOCK_DIALOG_WARNING,
		 "Slurmctld DebugFlags",
		 "", "Set slurmctld DebugFlags",
		 G_CALLBACK(_get_current_debug_flags)},
		{"debuglevel", GTK_STOCK_DIALOG_WARNING,
		 "Slurmctld Debug Level",
		 "", "Set slurmctld debug level",
		 G_CALLBACK(_get_current_debug)},
	};

	GtkRadioActionEntry radio_entries[] = {
		{"tab_top", GTK_STOCK_GOTO_TOP, "_Top",
		 "<control>T", "Move tabs to top", GTK_POS_TOP},
		{"tab_bottom", GTK_STOCK_GOTO_BOTTOM, "_Bottom",
		 "<control>B", "Move tabs to the bottom", GTK_POS_BOTTOM},
		{"tab_left", GTK_STOCK_GOTO_FIRST, "_Left",
		 "<control>L", "Move tabs to the Left", GTK_POS_LEFT},
		{"tab_right", GTK_STOCK_GOTO_LAST, "_Right",
		 "<control>R", "Move tabs to the Right", GTK_POS_RIGHT}
	};

	GtkToggleActionEntry toggle_entries[] = {
		{"grid", GTK_STOCK_SELECT_COLOR, "Show _Grid",
		 "<control>g", "Visual display of cluster",
		 G_CALLBACK(_set_grid), working_sview_config.show_grid},
		{"hidden", GTK_STOCK_SELECT_COLOR, "Show _Hidden",
		 "<control>h", "Display Hidden Partitions/Jobs",
		 G_CALLBACK(_set_hidden), working_sview_config.show_hidden},
		{"page_opts", GTK_STOCK_SELECT_COLOR, "Save Page Options",
		 "<control>w", "Save Page Options",
		 G_CALLBACK(_set_page_opts),
		 working_sview_config.save_page_opts},
#ifdef WANT_TOPO_ON_MAIN_OPTIONS
		{"topoorder", GTK_STOCK_SELECT_COLOR, "Set Topology Grid",
		 "<control>t", "Set Topology Grid",
		 G_CALLBACK(_set_topogrid),
		 working_sview_config.grid_topological},
#endif
		{"ruled", GTK_STOCK_SELECT_COLOR, "R_uled Tables",
		 "<control>u", "Have ruled tables or not",
		 G_CALLBACK(_set_ruled), working_sview_config.ruled_treeview},
		{"admin", GTK_STOCK_PREFERENCES,
		 "_Admin Mode", "<control>a",
		 "Allows user to change or update information",
		 G_CALLBACK(_set_admin_mode),
		 working_sview_config.admin_mode}
	};

	GtkRadioActionEntry debug_entries[] = {
		{"debug_quiet", NULL, "quiet(0)", "", "Quiet level", 0},
		{"debug_fatal", NULL, "fatal(1)", "", "Fatal level", 1},
		{"debug_error", NULL, "error(2)", "", "Error level", 2},
		{"debug_info", NULL, "info(3)", "", "Info level", 3},
		{"debug_verbose", NULL, "verbose(4)", "", "Verbose level", 4},
		{"debug_debug", NULL, "debug(5)", "", "Debug debug level", 5},
		{"debug_debug2", NULL, "debug2(6)", "", "Debug2 level", 6},
		{"debug_debug3", NULL, "debug3(7)", "", "Debug3 level", 7},
		{"debug_debug4", NULL, "debug4(8)", "", "Debug4 level", 8},
		{"debug_debug5", NULL, "debug5(9)", "", "Debug5 level", 9},
	};

	char *all_debug_flags = debug_flags2str(0xFFFFFFFFFFFFFFFF);
	char *last = NULL;
	char *tok = strtok_r(all_debug_flags, ",", &last);

	/* set up the global debug_actions */
	debug_actions = xmalloc(sizeof(GtkToggleActionEntry));

	while (tok) {
		xrealloc(debug_actions,
			 (debug_action_entries + 1)
			 * sizeof(GtkToggleActionEntry));
		debug_actions[debug_action_entries].name =
			debug_actions[debug_action_entries].label =
			debug_actions[debug_action_entries].tooltip =
			xstrdup(tok);
		debug_actions[debug_action_entries].callback =
			G_CALLBACK(_set_flags);
		debug_action_entries++;
		tok = strtok_r(NULL, ",", &last);
	}
	xfree(all_debug_flags);

	/* Make an accelerator group (shortcut keys) */
	menu_action_group = gtk_action_group_new ("MenuActions");
	gtk_action_group_add_actions(menu_action_group, entries,
				     G_N_ELEMENTS(entries), window);

	gtk_action_group_add_actions(menu_action_group, node_entries,
				     G_N_ELEMENTS(node_entries),
				     window);

	gtk_action_group_add_radio_actions(menu_action_group, radio_entries,
					   G_N_ELEMENTS(radio_entries),
					   working_sview_config.tab_pos,
					   G_CALLBACK(_tab_pos), notebook);
	gtk_action_group_add_toggle_actions(menu_action_group, debug_actions,
					    debug_action_entries, NULL);
	gtk_action_group_add_radio_actions(menu_action_group, debug_entries,
					   G_N_ELEMENTS(debug_entries),
					   -1, G_CALLBACK(_set_debug),
					   notebook);
	gtk_action_group_add_toggle_actions(menu_action_group, toggle_entries,
					    G_N_ELEMENTS(toggle_entries),
					    NULL);
	admin_action_group = gtk_action_group_new ("MenuAdminActions");
	gtk_action_group_add_actions(admin_action_group, admin_entries,
				     G_N_ELEMENTS(admin_entries),
				     window);
	gtk_action_group_set_sensitive(admin_action_group,
				       working_sview_config.admin_mode);

	g_ui_manager = gtk_ui_manager_new();
	gtk_ui_manager_insert_action_group(g_ui_manager, menu_action_group, 0);
	gtk_ui_manager_insert_action_group(g_ui_manager, admin_action_group, 1);

	accel_group = gtk_ui_manager_get_accel_group(g_ui_manager);
	gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);
	ui_description = _get_ui_description();
	if (!(g_menu_id = gtk_ui_manager_add_ui_from_string(
		      g_ui_manager, ui_description, -1, &error))) {
		xfree(ui_description);
		g_error("building menus failed: %s", error->message);
		g_error_free (error);
		exit (0);
	}
	xfree(ui_description);
	/* GList *action_list = */
	/*	gtk_action_group_list_actions(menu_action_group); */
	/* GtkAction *action = NULL; */
	/* int i=0; */
	/* while ((action = g_list_nth_data(action_list, i++))) { */
	/*	g_print("got %s and %x\n", gtk_action_get_name(action), */
	/*		action); */
	/* } */

	/* Get the pointers to the correct action so if we ever need
	   to change it we can effect the action directly so
	   everything stays in sync.
	*/
	default_sview_config.action_admin =
		(GtkToggleAction *)gtk_action_group_get_action(
			menu_action_group, "admin");
	default_sview_config.action_grid =
		(GtkToggleAction *)gtk_action_group_get_action(
			menu_action_group, "grid");
	default_sview_config.action_hidden =
		(GtkToggleAction *)gtk_action_group_get_action(
			menu_action_group, "hidden");
	default_sview_config.action_page_opts =
		(GtkToggleAction *)gtk_action_group_get_action(
			menu_action_group, "page_opts");
//	default_sview_config.action_gridtopo =
//		(GtkToggleAction *)gtk_action_group_get_action(
//			menu_action_group, "topoorder");
	default_sview_config.action_ruled =
		(GtkToggleAction *)gtk_action_group_get_action(
			menu_action_group, "ruled");
	/* Pick the first one of the Radio, it is how GTK references
	   the group in the future.
	*/
	default_sview_config.action_tab =
		(GtkRadioAction *)gtk_action_group_get_action(
			menu_action_group, "tab_top");
	/* g_print("action grid is %x\n", default_sview_config.action_grid); */
	/* Finally, return the actual menu bar created by the item factory. */
	return gtk_ui_manager_get_widget (g_ui_manager, "/main");
}

void *_popup_thr_main(void *arg)
{
	popup_thr(arg);
	return NULL;
}

static void _get_info_tabs(GtkTable *table, display_data_t *display_data)
{
	int i;
	static bool init = 0;

	if (!table || init) {
		return;
	}

	init = 1;
	/* This only needs to be ran once */
	for(i=0; i<PAGE_CNT; i++) {
		if (main_display_data[i].id == -1)
			break;

		if (!main_display_data[i].name || (i == TAB_PAGE))
			continue;
		if (!default_sview_config.page_check_widget[i])
			default_sview_config.page_check_widget[i] =
				gtk_check_button_new_with_label(
					main_display_data[i].name);
		gtk_table_attach_defaults(
			table,
			default_sview_config.page_check_widget[i],
			0, 1, i, i+1);
		gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON(
				default_sview_config.page_check_widget[i]),
			working_sview_config.page_visible[i]);
		g_signal_connect(
			G_OBJECT(default_sview_config.page_check_widget[i]),
			"toggled",
			G_CALLBACK(toggle_tab_visiblity),
			main_display_data+i);
	}

	gtk_widget_show_all(GTK_WIDGET(table));
	return;

}

extern void _change_cluster_main(GtkComboBox *combo, gpointer extra)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	slurmdb_cluster_rec_t *cluster_rec = NULL;
	char *tmp, *ui_description, *selected_name;
	GError *error = NULL;
	GtkWidget *node_tab = NULL;
	int rc;
	bool got_grid = 0;

	if (!gtk_combo_box_get_active_iter(combo, &iter)) {
		g_print("nothing selected\n");
		return;
	}
	model = gtk_combo_box_get_model(combo);
	if (!model) {
		g_print("nothing selected\n");
		return;
	}

	gtk_tree_model_get(model, &iter, 1, &cluster_rec, -1);
	if (!cluster_rec) {
		g_print("no cluster_rec pointer here!");
		return;
	}

	/* From testing it doesn't appear you can get here without a
	   legitimate change, so there isn't a need to check if we are
	   going back to the same cluster we were just at.
	*/
	/* if (working_cluster_rec) { */
	/*	if (!xstrcmp(cluster_rec->name, working_cluster_rec->name)) */
	/*		return; */
	/* } */

	/* free old info under last cluster */
	slurm_free_front_end_info_msg(g_front_end_info_ptr);
	g_front_end_info_ptr = NULL;
	slurm_free_burst_buffer_info_msg(g_bb_info_ptr);
	g_bb_info_ptr = NULL;
	slurm_free_job_info_msg(g_job_info_ptr);
	g_job_info_ptr = NULL;
	slurm_free_node_info_msg(g_node_info_ptr);
	g_node_info_ptr = NULL;
	slurm_free_partition_info_msg(g_part_info_ptr);
	g_part_info_ptr = NULL;
	slurm_free_reservation_info_msg(g_resv_info_ptr);
	g_resv_info_ptr = NULL;
	slurm_free_ctl_conf(g_ctl_info_ptr);
	g_ctl_info_ptr = NULL;
	slurm_free_job_step_info_response_msg(g_step_info_ptr);
	g_step_info_ptr = NULL;
	slurm_free_topo_info_msg(g_topo_info_msg_ptr);
	g_topo_info_msg_ptr = NULL;

	/* set up working_cluster_rec */
	if (cluster_dims > 1) {
		/* reset from a multi-dim cluster */
		working_sview_config.grid_x_width =
			default_sview_config.grid_x_width;
		working_sview_config.grid_hori = default_sview_config.grid_hori;
		working_sview_config.grid_vert = default_sview_config.grid_vert;
	}
	gtk_table_set_col_spacings(main_grid_table, 0);
	gtk_table_set_row_spacings(main_grid_table, 0);

	if (!orig_cluster_name)
		orig_cluster_name = slurm_get_cluster_name();
	if (!xstrcmp(cluster_rec->name, orig_cluster_name))
		working_cluster_rec = NULL;
	else
		working_cluster_rec = cluster_rec;
	cluster_dims = slurmdb_setup_cluster_dims();
	cluster_flags = slurmdb_setup_cluster_flags();

	gtk_tree_model_get(model, &iter, 0, &selected_name, -1);
	if (!xstrncmp(selected_name, "FED:", strlen("FED:"))) {
		cluster_flags |= CLUSTER_FLAG_FED;
		federation_name = xstrdup(selected_name + strlen("FED:"));
		gtk_widget_hide(grid_window);
	} else {
		xfree(federation_name);
		if (working_sview_config.show_grid)
			gtk_widget_show(grid_window);
	}

	/* set up menu */
	ui_description = _get_ui_description();
	gtk_ui_manager_remove_ui(g_ui_manager, g_menu_id);
	if (!(g_menu_id = gtk_ui_manager_add_ui_from_string(
		      g_ui_manager, ui_description, -1, &error))) {
		xfree(ui_description);
		g_error("building menus failed: %s", error->message);
		g_error_free (error);
		exit (0);
	}
	xfree(ui_description);

	/* make changes for each object */
	cluster_change_front_end();
	cluster_change_resv();
	cluster_change_part();
	cluster_change_job();
	cluster_change_node();
	cluster_change_bb();

	/* destroy old stuff */
	if (grid_button_list) {
		FREE_NULL_LIST(grid_button_list);
		got_grid = 1;
	}

	/* sorry popups can't survive a cluster change */
	if (popup_list)
		list_flush(popup_list);
	if (signal_params_list)
		list_flush(signal_params_list);
	if (signal_params_list)
		list_flush(signal_params_list);
	if (g_switch_nodes_maps)
		free_switch_nodes_maps(g_switch_nodes_maps);

	/* change the node tab name if needed */
	node_tab = gtk_notebook_get_nth_page(
		GTK_NOTEBOOK(main_notebook), NODE_PAGE);
	node_tab = gtk_notebook_get_tab_label(GTK_NOTEBOOK(main_notebook),
					      node_tab);
#ifdef GTK2_USE_GET_FOCUS

	/* ok, now we have a table which we have set up to contain an
	 * event_box which contains the label we are interested.  We
	 * setup this label to be the focus child of the table, so all
	 * we have to do is grab that and we are set. */
	node_tab = gtk_container_get_focus_child(GTK_CONTAINER(node_tab));
#else
	/* See above comment.  Since gtk_container_get_focus_child
	 * doesn't exist yet we will just traverse the children until
	 * we find the label widget and then break.
	 */
	{
		int i = 0;
		GList *children = gtk_container_get_children(
			GTK_CONTAINER(node_tab));
		while ((node_tab = g_list_nth_data(children, i++))) {
			int j = 0;
			GList *children2 = gtk_container_get_children(
				GTK_CONTAINER(node_tab));
			while ((node_tab = g_list_nth_data(children2, j++))) {
				if (GTK_IS_LABEL(node_tab))
					break;
			}
			g_list_free(children2);
			if (node_tab)
				break;
		}
		g_list_free(children);
	}
#endif
	if (node_tab)
		gtk_label_set_text(GTK_LABEL(node_tab),
				   main_display_data[NODE_PAGE].name);

	/* The name in the visible tabs is easier since it is really
	   just a button with a label on it.
	*/
	if (default_sview_config.page_check_widget[NODE_PAGE]) {
		gtk_button_set_label(GTK_BUTTON(default_sview_config.
						page_check_widget[NODE_PAGE]),
				     main_display_data[NODE_PAGE].name);
	}

	/* reinit */
	rc = get_system_stats(main_grid_table);

	if (rc == SLURM_SUCCESS) {
		/* It turns out if we didn't have the grid (cluster
		   not responding) before the
		   new grid doesn't get set up correctly.  Redoing the
		   system_stats fixes it.  There is probably a better
		   way of doing this, but it doesn't happen very often
		   and isn't that bad to handle every once in a while.
		*/
		if (!got_grid) {
			/* I know we just did this before, but it
			   needs to be done again here.
			*/
			FREE_NULL_LIST(grid_button_list);
			get_system_stats(main_grid_table);
		}

		refresh_main(NULL, NULL);
	}

	tmp = g_strdup_printf("Cluster changed to %s",
			      cluster_flags & CLUSTER_FLAG_FED ?
			      selected_name : cluster_rec->name);
	display_edit_note(tmp);
	g_free(tmp);
	g_free(selected_name);
}

static GtkWidget *_create_cluster_combo(void)
{
	GtkListStore *model = NULL;
	GtkWidget *combo = NULL;
	GtkTreeIter iter;
	ListIterator itr;
	slurmdb_cluster_rec_t *cluster_rec;
	GtkCellRenderer *renderer = NULL;
	bool got_db = slurm_get_is_association_based_accounting();
	int count = 0, spot = 0;
	List fed_list = NULL;

	if (!got_db)
		return NULL;

	cluster_list = slurmdb_get_info_cluster(NULL);
	if (!cluster_list || !list_count(cluster_list)) {
		FREE_NULL_LIST(cluster_list);
		return NULL;
	}

	if (!orig_cluster_name)
		orig_cluster_name = slurm_get_cluster_name();

	if (list_count(cluster_list) > 1)
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_POINTER);

	/* Set up the working_cluster_rec just in case we are on a node
	   that doesn't technically belong to a cluster (like
	   the node running the slurmdbd).
	*/
	working_cluster_rec = list_peek(cluster_list);
	itr = list_iterator_create(cluster_list);

	/* Build federated list */
	while ((cluster_rec = list_next(itr))) {
		char *fed_name;

		if (!model)
			continue;
		if (!cluster_rec->fed.name || !*cluster_rec->fed.name)
			continue;

		if (!fed_list)
			fed_list = list_create(NULL);

		if (list_find_first(fed_list, slurm_find_char_in_list,
				    cluster_rec->fed.name))
			continue;

		fed_name = xstrdup_printf("FED:%s", cluster_rec->fed.name);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter, 0, fed_name, 1, cluster_rec,
				   -1);
		list_append(fed_list, cluster_rec->fed.name);
		count++;
	}
	FREE_NULL_LIST(fed_list);

	/* Build cluster list */
	list_iterator_reset(itr);
	while ((cluster_rec = list_next(itr))) {
		if (!model)
			continue;

		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter, 0, cluster_rec->name, 1,
				   cluster_rec, -1);

		if (!xstrcmp(cluster_rec->name, orig_cluster_name)) {
			/* clear it since we found the current cluster */
			working_cluster_rec = NULL;
			spot = count;
		}
		count++;
	}
	list_iterator_destroy(itr);

	if (model) {
		combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(model));
		g_object_unref(model);

		renderer = gtk_cell_renderer_text_new();
		gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo),
					   renderer, true);
		gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(combo),
					      renderer, "text", 0);
		gtk_combo_box_set_active(GTK_COMBO_BOX(combo), spot);
		g_signal_connect(combo, "changed",
				 G_CALLBACK(_change_cluster_main),
				 NULL);
	}
	return combo;
}

extern void refresh_main(GtkAction *action, gpointer user_data)
{
	int page = gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook));
	if (page == -1)
		g_error("no pages in notebook for refresh\n");
	force_refresh = 1;
	_page_switched(GTK_NOTEBOOK(main_notebook), NULL, page, NULL);
}

extern void toggle_tab_visiblity(GtkToggleButton *toggle_button,
				 display_data_t *display_data)
{
	static bool already_here = false;
	int page_num;
	GtkWidget *visible_tab;

	/* When calling the set active below it signals this again, so
	   to avoid an infinite loop we will just fall out.
	*/
	if (already_here)
		return;

	already_here = true;
	page_num = display_data->extra;
	visible_tab = gtk_notebook_get_nth_page(
		GTK_NOTEBOOK(main_notebook), page_num);
	if (toggle_button) {
		working_sview_config.page_visible[page_num] =
			gtk_toggle_button_get_active(toggle_button);
	}

	if (working_sview_config.page_visible[page_num])
		gtk_widget_show(visible_tab);
	else
		gtk_widget_hide(visible_tab);

	if (default_sview_config.page_check_widget[page_num])
		gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON(default_sview_config.
					  page_check_widget[page_num]),
			working_sview_config.page_visible[page_num]);

	already_here = false;

	return;
}

extern gboolean tab_pressed(GtkWidget *widget, GdkEventButton *event,
			display_data_t *display_data)
{
	signal_params_t signal_params;
	signal_params.display_data = display_data;
	signal_params.button_list = &grid_button_list;

	/* single click with the right mouse button? */
	gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook),
				      display_data->extra);
	if ((display_data->extra != TAB_PAGE) && (event->button == 3))
		right_button_pressed(NULL, NULL, event,
				     &signal_params, TAB_CLICKED);
	return true;
}

extern void close_tab(GtkWidget *widget, GdkEventButton *event,
		      display_data_t *display_data)
{
	if (event->button == 3)
		/* don't do anything with a right click */
		return;
	working_sview_config.page_visible[display_data->extra] = false;
	toggle_tab_visiblity(NULL, display_data);
	//g_print("hid %d\n", display_data->extra);
}

int main(int argc, char **argv)
{
	GtkWidget *menubar = NULL;
	GtkWidget *table = NULL;
	GtkWidget *combo = NULL;
	GtkBin *bin = NULL;
	GtkViewport *view = NULL;
	int i=0;
	log_options_t lopts = LOG_OPTS_STDERR_ONLY;

	if (!getenv("SLURM_BITSTR_LEN"))
		setenv("SLURM_BITSTR_LEN", "128", 1);	/* More array info */
	slurm_conf_init(NULL);
	log_init(argv[0], lopts, SYSLOG_FACILITY_USER, NULL);
	load_defaults();
	cluster_flags = slurmdb_setup_cluster_flags();
	cluster_dims = slurmdb_setup_cluster_dims();

	_init_pages();
	sview_thread_init(NULL);
	gdk_threads_init();
	gdk_threads_enter();
	/* Initialize GTK */
	gtk_init (&argc, &argv);
	sview_mutex_new(&sview_mutex);
	sview_mutex_new(&grid_mutex);
	sview_cond_new(&grid_cond);
	/* make sure the system is up */
	grid_window = GTK_WIDGET(create_scrolled_window());
	bin = GTK_BIN(&GTK_SCROLLED_WINDOW(grid_window)->container);
	view = GTK_VIEWPORT(bin->child);
	bin = GTK_BIN(&view->bin);
	main_grid_table = GTK_TABLE(bin->child);
	gtk_table_set_homogeneous(main_grid_table, true);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(grid_window),
				       GTK_POLICY_NEVER,
				       GTK_POLICY_AUTOMATIC);

	/* fill in all static info for pages */
	/* Make a window */
	main_window = gtk_dialog_new();
	gtk_window_set_type_hint(GTK_WINDOW(main_window),
				 GDK_WINDOW_TYPE_HINT_NORMAL);
	g_signal_connect(G_OBJECT(main_window), "delete_event",
			 G_CALLBACK(_delete), NULL);

	gtk_window_set_title(GTK_WINDOW(main_window), "Sview");
	gtk_window_set_default_size(GTK_WINDOW(main_window),
				    working_sview_config.main_width,
				    working_sview_config.main_height);
	gtk_container_set_border_width(
		GTK_CONTAINER(GTK_DIALOG(main_window)->vbox), 1);
	/* Create the main notebook, place the position of the tabs */
	main_notebook = gtk_notebook_new();
	g_signal_connect(G_OBJECT(main_notebook), "switch_page",
			 G_CALLBACK(_page_switched),
			 NULL);
	table = gtk_table_new(1, 3, false);
	gtk_table_set_homogeneous(GTK_TABLE(table), false);
	gtk_container_set_border_width(GTK_CONTAINER(table), 1);
	/* Create a menu */
	menubar = _get_menubar_menu(main_window, main_notebook);
	gtk_table_attach_defaults(GTK_TABLE(table), menubar, 0, 1, 0, 1);

	if ((combo = _create_cluster_combo())) {
		GtkWidget *label = gtk_label_new("Cluster ");
		gtk_table_attach(GTK_TABLE(table), label, 1, 2, 0, 1,
				 GTK_FILL, GTK_SHRINK, 0, 0);
		gtk_table_attach(GTK_TABLE(table), combo, 2, 3, 0, 1,
				 GTK_FILL, GTK_SHRINK, 0, 0);
	}
	gtk_notebook_popup_enable(GTK_NOTEBOOK(main_notebook));
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(main_notebook), true);
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(main_notebook),
				 working_sview_config.tab_pos);

	main_statusbar = gtk_statusbar_new();
	gtk_statusbar_set_has_resize_grip(GTK_STATUSBAR(main_statusbar),
					  false);
	/* Pack it all together */
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(main_window)->vbox),
			   table, false, false, 0);
	table = gtk_table_new(1, 2, false);

	gtk_table_attach(GTK_TABLE(table), grid_window, 0, 1, 0, 1,
			 GTK_SHRINK, GTK_EXPAND | GTK_FILL,
			 0, 0);
	gtk_table_attach_defaults(GTK_TABLE(table), main_notebook, 1, 2, 0, 1);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(main_window)->vbox),
			   table, true, true, 0);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(main_window)->vbox),
			   main_statusbar, false, false, 0);

	in_process_cursor = gdk_cursor_new(GDK_WATCH);

	for(i=0; i<PAGE_CNT; i++) {
		if (main_display_data[i].id == -1)
			break;

		create_page(GTK_NOTEBOOK(main_notebook),
			    &main_display_data[i]);
	}

	/* tell signal we are done adding */

	popup_list = list_create(destroy_popup_info);
	signal_params_list = list_create(destroy_signal_params);

	gtk_widget_show_all(main_window);

	adding = 0;
	/* apply default settings */
	if (!working_sview_config.show_grid)
		gtk_widget_hide(grid_window);

	for(i=0; i<PAGE_CNT; i++) {
		GtkWidget *visible_tab = NULL;

		if (main_display_data[i].id == -1)
			break;

		visible_tab = gtk_notebook_get_nth_page(
			GTK_NOTEBOOK(main_notebook), i);
		if (working_sview_config.page_visible[i]
		    || (i == working_sview_config.default_page)
		    || (i == TAB_PAGE))
			gtk_widget_show(visible_tab);
		else
			gtk_widget_hide(visible_tab);
	}
	/* Set the default page.  This has to be done after the
	 * gtk_widget_show_all since it, for some reason always sets
	 * 0 to be the default page and will just overwrite this. */
	/* Also if we already are set at the current page we need to
	   start up the page thread, so just call the _page_switched
	   function.  If we aren't already there, then set the current
	   page which will inturn call the _page_switched.  If the
	   pages is already this the signal doesn't happen so handle
	   it here.
	*/
	if (gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook))
	    == working_sview_config.default_page)
		_page_switched(GTK_NOTEBOOK(main_notebook), NULL,
			       working_sview_config.default_page, NULL);
	else
		gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook),
					      working_sview_config.
					      default_page);

	/* Finished! */
	gtk_main ();
	gdk_threads_leave();
	return 0;
}
