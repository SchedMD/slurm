/****************************************************************************\
 *  sview.c - main for sview
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#define MAX_RETRIES 3		/* g_thread_create retries */

typedef struct {
	GtkTable *table;
	int page_num;
} page_thr_t;

/* globals */
sview_parameters_t params;
int adding = 1;
int fini = 0;
int grid_init = 0;
bool toggled = FALSE;
bool force_refresh = FALSE;
List popup_list;
int page_running[PAGE_CNT];
int global_sleep_time = 5;
bool admin_mode = FALSE;
GtkWidget *main_notebook = NULL;
GtkWidget *main_statusbar = NULL;
GtkWidget *main_window = NULL;
GtkWidget *grid_window = NULL;
GtkTable *main_grid_table = NULL;
GStaticMutex sview_mutex = G_STATIC_MUTEX_INIT;
GMutex *grid_mutex = NULL;
GCond *grid_cond = NULL;
GtkActionGroup *admin_action_group = NULL;
int grid_speedup = 0;

display_data_t main_display_data[] = {
	{G_TYPE_NONE, JOB_PAGE, "Jobs", TRUE, -1,
	 refresh_main, create_model_job, admin_edit_job,
	 get_info_job, specific_info_job, 
	 set_menus_job, NULL},
	{G_TYPE_NONE, STEP_PAGE, NULL, FALSE, -1,
	 refresh_main, NULL, NULL, NULL,
	 NULL, NULL, NULL},
	{G_TYPE_NONE, PART_PAGE, "Partitions", TRUE, -1, 
	 refresh_main, create_model_part, admin_edit_part,
	 get_info_part, specific_info_part, 
	 set_menus_part, NULL},
	{G_TYPE_NONE, RESV_PAGE, "Reservations", TRUE, -1, 
	 refresh_main, create_model_resv, admin_edit_resv,
	 get_info_resv, specific_info_resv, 
	 set_menus_resv, NULL},
#ifdef HAVE_BG
	{G_TYPE_NONE, BLOCK_PAGE, "BG Blocks", TRUE, -1,
	 refresh_main, NULL, NULL,
	 get_info_block, specific_info_block, 
	 set_menus_block, NULL},
	{G_TYPE_NONE, NODE_PAGE, "Base Partitions", FALSE, -1,
	 refresh_main, NULL, NULL,
	 get_info_node, specific_info_node, 
	 set_menus_node, NULL},
#else
	{G_TYPE_NONE, BLOCK_PAGE, "BG Blocks", FALSE, -1,
	 refresh_main, NULL, NULL,
	 get_info_block, specific_info_block, 
	 set_menus_block, NULL},
	{G_TYPE_NONE, NODE_PAGE, "Nodes", FALSE, -1,
	 refresh_main, NULL, NULL,
	 get_info_node, specific_info_node, 
	 set_menus_node, NULL},
#endif
	{G_TYPE_NONE, SUBMIT_PAGE, "Submit Job", FALSE, -1,
	 refresh_main, NULL, NULL, NULL,
	 NULL, NULL, NULL},
	{G_TYPE_NONE, INFO_PAGE, NULL, FALSE, -1,
	 refresh_main, NULL, NULL,
	 NULL, NULL,
	 NULL, NULL},
	{G_TYPE_NONE, -1, NULL, FALSE, -1}
};

void *_page_thr(void *arg)
{
	page_thr_t *page = (page_thr_t *)arg;
	int num = page->page_num;
	GtkTable *table = page->table;
	display_data_t *display_data = &main_display_data[num];
	static int thread_count = 0;
/* 	DEF_TIMERS; */
	xfree(page);
	
	if(!grid_init) {
		/* we need to signal any threads that are waiting */
		g_mutex_lock(grid_mutex);
		g_cond_signal(grid_cond);
		g_mutex_unlock(grid_mutex);
		
		/* wait for the grid to be inited */
		g_mutex_lock(grid_mutex);
		g_cond_wait(grid_cond, grid_mutex);
		g_mutex_unlock(grid_mutex);
		
		/* if the grid isn't there just return */
		if(!grid_init)
			return NULL;
	}
	gdk_threads_enter();
	//sview_reset_grid();
	thread_count++;
	gdk_flush();
	gdk_threads_leave();
	while(page_running[num]) {		
/* 		START_TIMER; */
		g_static_mutex_lock(&sview_mutex);
		gdk_threads_enter();
		sview_init_grid();
		(display_data->get_info)(table, display_data);
		gdk_flush();
		gdk_threads_leave();
		g_static_mutex_unlock(&sview_mutex);
/* 		END_TIMER; */
/* 		g_print("got for initeration: %s\n", TIME_STR); */
		sleep(global_sleep_time);
		
		gdk_threads_enter();
		if(thread_count > 1) {
			gdk_flush();
			gdk_threads_leave();	
			break;
		}
		gdk_flush();
		gdk_threads_leave();
	
	}	
	gdk_threads_enter();
	thread_count--;
	gdk_flush();
	gdk_threads_leave();
					
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
		
	while(!grid_init) {
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
		gdk_flush();
		gdk_threads_leave();
	
		if(rc != SLURM_SUCCESS)
			sleep(global_sleep_time);
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
	if(!window)
		return;
	GtkBin *bin = GTK_BIN(&window->container);
	GtkViewport *view = GTK_VIEWPORT(bin->child);
	GtkBin *bin2 = GTK_BIN(&view->bin);
	GtkTable *table = GTK_TABLE(bin2->child);
	int i = 0;
	static int running=-1;
	page_thr_t *page_thr = NULL;
	GError *error = NULL;
	static int started_grid_init = 0;
	
	/* make sure we aren't adding the page, and really asking for info */
	if(adding)
		return;
	else if(!grid_init && !started_grid_init) {
		/* start the thread to make the grid only once */
		if (!g_thread_create(_grid_init_thr, notebook, FALSE, &error))
		{
			g_printerr ("Failed to create grid init thread: %s\n",
				    error->message);
			return;
		}
		started_grid_init = 1;
	}
	
	if(running != -1) {
		page_running[running] = 0;
	}	
	
	for(i=0; i<PAGE_CNT; i++) {
		if(main_display_data[i].id == -1)
			break;
		else if(!main_display_data[i].show) 
			continue;
		if(main_display_data[i].extra == page_num)
			break;
	}

	if(main_display_data[i].extra != page_num) {
		g_print("page %d not found\n", page_num);
		return;
	} 
	if(main_display_data[i].get_info) {
		running = i;
		page_running[i] = 1;
		if(toggled || force_refresh) {
			(main_display_data[i].get_info)(
				table, &main_display_data[i]);
			return;
		}

		page_thr = xmalloc(sizeof(page_thr_t));		
		page_thr->page_num = i;
		page_thr->table = table;
		
		if (!g_thread_create(_page_thr, page_thr, FALSE, &error))
		{
			g_printerr ("Failed to create page thread: %s\n", 
				    error->message);
			return;
		}
	}
}

static void _set_admin_mode(GtkToggleAction *action)
{
//	GtkAction *admin_action = NULL;

	if(admin_mode) {
		admin_mode = FALSE;
		gtk_statusbar_pop(GTK_STATUSBAR(main_statusbar), 
				  STATUS_ADMIN_MODE);
	} else {
		admin_mode = TRUE;
		gtk_statusbar_push(GTK_STATUSBAR(main_statusbar), 
				   STATUS_ADMIN_MODE,
				   "Admin mode activated! "
				   "Think before you alter anything.");
	}
	gtk_action_group_set_sensitive(admin_action_group, admin_mode); 
}

static void _set_grid(GtkToggleAction *action)
{
	static bool open = TRUE;
	
	if(open) {
		gtk_widget_hide(grid_window);
		open = FALSE;
	} else {
		gtk_widget_show(grid_window);
		open = TRUE;
	}
		
	return;
}

static void _reconfigure(GtkToggleAction *action)
{
	char *temp = NULL;

	if(!slurm_reconfigure())
		temp = g_strdup_printf(
			"Reconfigure sent to slurm successfully");
	else
		temp = g_strdup_printf("Problem with reconfigure request");
	display_edit_note(temp);
	g_free(temp);
}

static void _tab_pos(GtkRadioAction *action,
		     GtkRadioAction *extra,
		     GtkNotebook *notebook)
{
	gtk_notebook_set_tab_pos(notebook, 
				 gtk_radio_action_get_current_value(action));
}

static void _init_pages()
{
	int i;
	for(i=0; i<PAGE_CNT; i++) {
		if(!main_display_data[i].get_info)
			continue;
		(main_display_data[i].get_info)(NULL, &main_display_data[i]);
	}
}

static gboolean _delete(GtkWidget *widget,
                        GtkWidget *event,
                        gpointer data)
{
	fini = 1;
	gtk_main_quit();
	ba_fini();
	if(grid_button_list)
		list_destroy(grid_button_list);
	if(popup_list)
		list_destroy(popup_list);
	return FALSE;
}

/* Returns a menubar widget made from the above menu */
static GtkWidget *_get_menubar_menu(GtkWidget *window, GtkWidget *notebook)
{
	GtkUIManager *ui_manager = NULL;
	GtkAccelGroup *accel_group = NULL;
	GError *error = NULL;
	GtkActionGroup *action_group = NULL;

	/* Our menu*/
	const char *ui_description =
		"<ui>"
		"  <menubar name='main'>"
		"    <menu action='actions'>"
		"      <menu action='search'>"
		"        <menuitem action='jobid'/>"
		"        <menuitem action='user_jobs'/>"
		"        <menuitem action='state_jobs'/>"
#ifdef HAVE_BG
		"      <separator/>"
		"        <menuitem action='bg_block_name'/>"
		"        <menuitem action='bg_block_size'/>"
		"        <menuitem action='bg_block_state'/>"
#endif
		"      <separator/>"
		"        <menuitem action='partition_name'/>"
		"        <menuitem action='partition_state'/>"
		"      <separator/>"
		"        <menuitem action='node_name'/>"
		"        <menuitem action='node_state'/>"
		"      <separator/>"
		"        <menuitem action='reservation_name'/>"
		"      </menu>"
		"      <menuitem action='refresh'/>"
		"      <menuitem action='reconfig'/>"
		"      <separator/>"
		"      <menuitem action='exit'/>"
		"    </menu>"
		"    <menu action='options'>"
		"      <menuitem action='grid'/>"
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
		"    </menu>"
		"    <menu action='displays'>"
		"      <menuitem action='config'/>"
		"    </menu>"
		"    <menu action='help'>"
		"      <menuitem action='about'/>"
		"      <menuitem action='manual'/>"
		"    </menu>"
		"  </menubar>"
		"</ui>";

	GtkActionEntry entries[] = {
		{"actions", NULL, "_Actions", "<alt>a"},
		{"options", NULL, "_Options", "<alt>o"},
		{"displays", NULL, "_Query", "<alt>q"},
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
#ifdef HAVE_BG
		{"bg_block_name", NULL, "BG Block Name", 
		 "", "Search for a specific BG Block", 
		 G_CALLBACK(create_search_popup)},
		{"bg_block_size", NULL, "BG Block Size", 
		 "", 
		 "Search for BG Blocks having given size in cnodes", 
		 G_CALLBACK(create_search_popup)},
		{"bg_block_state", NULL, "BG Block State", 
		 "", 
		 "Search for BG Blocks having given state", 
		 G_CALLBACK(create_search_popup)},
#endif
		{"partition_name", NULL, "Slurm Partition Name", 
		 "", "Search for a specific SLURM partition", 
		 G_CALLBACK(create_search_popup)},
		{"partition_state", NULL, "Slurm Partition State", 
		 "", "Search for SLURM partitions in a given state", 
		 G_CALLBACK(create_search_popup)},
		{"node_name", NULL, 
#ifdef HAVE_BG
		 "Base Partition(s) Name",
		 "", "Search for a specific Base Partition(s)", 
#else
		 "Node(s) Name", 
		 "", "Search for a specific Node(s)", 
#endif
		 G_CALLBACK(create_search_popup)},		
		{"node_state", NULL, 
#ifdef HAVE_BG
		 "Base Partition State",
		 "", "Search for a Base Partition in a given state", 
#else
		 "Node State", 
		 "", "Search for a Node in a given state", 
#endif
		 G_CALLBACK(create_search_popup)},		
		{"reservation_name", NULL, "Reservation Name", 
		 "", "Search for reservation", 
		 G_CALLBACK(create_search_popup)},
		{"tab_pos", NULL, "_Tab Pos"},
		{"interval", GTK_STOCK_REFRESH, "Set Refresh _Interval", 
		 "<control>i", "Change Refresh Interval", 
		 G_CALLBACK(change_refresh_popup)},
		{"refresh", GTK_STOCK_REFRESH, "Refresh", 
		 "F5", "Refreshes page", G_CALLBACK(refresh_main)},
		{"config", GTK_STOCK_INFO, "_Config Info", 
		 "<control>c", "Displays info from slurm.conf file", 
		 G_CALLBACK(create_config_popup)},
		{"exit", GTK_STOCK_QUIT, "E_xit", 
		 "<control>x", "Exits Program", G_CALLBACK(_delete)},
		{"help", NULL, "_Help", "<alt>h"},
		{"about", GTK_STOCK_ABOUT, "A_bout", "<control>b"},
		{"manual", GTK_STOCK_HELP, "_Manual", "<control>m"}
	};

	GtkActionEntry admin_entries[] = {
		{"reconfig", GTK_STOCK_REDO, "SLUR_M Reconfigure", 
		 "<control>m", "Reconfigures System", 
		 G_CALLBACK(_reconfigure)},
	};

	GtkRadioActionEntry radio_entries[] = {
		{"tab_top", GTK_STOCK_GOTO_TOP, "_Top", 
		 "<control>T", "Move tabs to top", 2},
		{"tab_bottom", GTK_STOCK_GOTO_BOTTOM, "_Bottom", 
		 "<control>B", "Move tabs to the bottom", 3},
		{"tab_left", GTK_STOCK_GOTO_FIRST, "_Left", 
		 "<control>L", "Move tabs to the Left", 4},
		{"tab_right", GTK_STOCK_GOTO_LAST, "_Right", 
		 "<control>R", "Move tabs to the Right", 1}
	};

	GtkToggleActionEntry toggle_entries[] = {
		{"grid", GTK_STOCK_SELECT_COLOR, "Show _Grid",
		 "<control>g", "Visual display of cluster", 
		 G_CALLBACK(_set_grid), TRUE},
		{"admin", GTK_STOCK_PREFERENCES,          
		 "_Admin Mode", "<control>a", 
		 "Allows user to change or update information", 
		 G_CALLBACK(_set_admin_mode), 
		 FALSE} 
	};
		
	/* Make an accelerator group (shortcut keys) */
	action_group = gtk_action_group_new ("MenuActions");
	gtk_action_group_add_actions(action_group, entries, 
				     G_N_ELEMENTS(entries), window);
	gtk_action_group_add_radio_actions(action_group, radio_entries, 
					   G_N_ELEMENTS(radio_entries), 
					   0, G_CALLBACK(_tab_pos), notebook);
	gtk_action_group_add_toggle_actions(action_group, toggle_entries, 
					   G_N_ELEMENTS(toggle_entries), 
					   NULL);
	admin_action_group = gtk_action_group_new ("MenuActions");
	gtk_action_group_add_actions(admin_action_group, admin_entries, 
				     G_N_ELEMENTS(admin_entries),
				     window);
	gtk_action_group_set_sensitive(admin_action_group, FALSE); 
	
	ui_manager = gtk_ui_manager_new();
	gtk_ui_manager_insert_action_group(ui_manager, action_group, 0);
	gtk_ui_manager_insert_action_group(ui_manager, admin_action_group, 1);
	
	accel_group = gtk_ui_manager_get_accel_group(ui_manager);
	gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);

	if (!gtk_ui_manager_add_ui_from_string (ui_manager, ui_description, 
						-1, &error))
	{
		g_error("building menus failed: %s", error->message);
		g_error_free (error);
		exit (0);
	}

	/* Finally, return the actual menu bar created by the item factory. */
	return gtk_ui_manager_get_widget (ui_manager, "/main");
}

void *_popup_thr_main(void *arg)
{
	popup_thr(arg);		
	return NULL;
}

extern void refresh_main(GtkAction *action, gpointer user_data)
{
	int page = gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook));
	if(page == -1)
		g_error("no pages in notebook for refresh\n");
	force_refresh = 1;
	_page_switched(GTK_NOTEBOOK(main_notebook), NULL, page, NULL);
}

extern void tab_pressed(GtkWidget *widget, GdkEventButton *event, 
			const display_data_t *display_data)
{
	/* single click with the right mouse button? */
	gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook),
				      display_data->extra);
	if(event->button == 3) {
		right_button_pressed(NULL, NULL, event, 
				     display_data, TAB_CLICKED);
	} 
}

int main(int argc, char *argv[])
{
	GtkWidget *menubar = NULL;
	GtkWidget *table = NULL;
/* 	GtkWidget *button = NULL; */
	GtkBin *bin = NULL;
	GtkViewport *view = NULL;
	int i=0;

	if(getenv("SVIEW_GRID_SPEEDUP"))
		grid_speedup = 1;
	
	_init_pages();
	g_thread_init(NULL);
	gdk_threads_init();
	gdk_threads_enter();
	/* Initialize GTK */
	gtk_init (&argc, &argv);
	grid_mutex = g_mutex_new();
	grid_cond = g_cond_new();
	/* make sure the system is up */
	grid_window = GTK_WIDGET(create_scrolled_window());
	bin = GTK_BIN(&GTK_SCROLLED_WINDOW(grid_window)->container);
	view = GTK_VIEWPORT(bin->child);
	bin = GTK_BIN(&view->bin);
	main_grid_table = GTK_TABLE(bin->child);
	gtk_table_set_homogeneous(main_grid_table, TRUE);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(grid_window),
				       GTK_POLICY_NEVER,
				       GTK_POLICY_AUTOMATIC);
	
/* #ifdef HAVE_BG */
/* 	gtk_widget_set_size_request(grid_window, 164, -1); */
/* #endif */
	/* fill in all static info for pages */
	/* Make a window */
	main_window = gtk_dialog_new();
	g_signal_connect(G_OBJECT(main_window), "delete_event",
			 G_CALLBACK(_delete), NULL);
	
	gtk_window_set_title(GTK_WINDOW(main_window), "Sview");
	gtk_window_set_default_size(GTK_WINDOW(main_window), 700, 450);
	gtk_container_set_border_width(
		GTK_CONTAINER(GTK_DIALOG(main_window)->vbox), 1);
	/* Create the main notebook, place the position of the tabs */
	main_notebook = gtk_notebook_new();
	g_signal_connect(G_OBJECT(main_notebook), "switch_page",
			 G_CALLBACK(_page_switched),
			 NULL);
	table = gtk_table_new(1, 2, FALSE);
	gtk_table_set_homogeneous(GTK_TABLE(table), FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 1);	
	/* Create a menu */
	menubar = _get_menubar_menu(main_window, main_notebook);
	gtk_table_attach_defaults(GTK_TABLE(table), menubar, 0, 1, 0, 1);

	gtk_notebook_popup_enable(GTK_NOTEBOOK(main_notebook));
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(main_notebook), TRUE);
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(main_notebook), GTK_POS_TOP);
	
	main_statusbar = gtk_statusbar_new();
	gtk_statusbar_set_has_resize_grip(GTK_STATUSBAR(main_statusbar), 
					  FALSE);
	/* Pack it all together */
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(main_window)->vbox),
			   table, FALSE, FALSE, 0);
	table = gtk_table_new(1, 2, FALSE);

	gtk_table_attach(GTK_TABLE(table), grid_window, 0, 1, 0, 1,
			 GTK_SHRINK, GTK_EXPAND | GTK_FILL,
			 0, 0);
	gtk_table_attach_defaults(GTK_TABLE(table), main_notebook, 1, 2, 0, 1);
	
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(main_window)->vbox), 
			   table, TRUE, TRUE, 0);	
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(main_window)->vbox),
			   main_statusbar, FALSE, FALSE, 0);	
	
	for(i=0; i<PAGE_CNT; i++) {
		if(main_display_data[i].id == -1)
			break;
		else if(!main_display_data[i].show) 
			continue;
		create_page(GTK_NOTEBOOK(main_notebook), 
			    &main_display_data[i]);
	}

	/* tell signal we are done adding */
	adding = 0;
	popup_list = list_create(destroy_popup_info);
	gtk_widget_show_all(main_window);

	/* Finished! */
	gtk_main ();
	gdk_threads_leave();
	return 0;
}

