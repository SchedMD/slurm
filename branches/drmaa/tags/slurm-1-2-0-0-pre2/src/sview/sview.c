/****************************************************************************\
 *  sview.c - main for sview
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
\****************************************************************************/

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
bool toggled = FALSE;
bool force_refresh = FALSE;
List popup_list;
int page_running[PAGE_CNT];
int global_sleep_time = 5;
bool admin_mode = FALSE;
GtkWidget *main_notebook = NULL;
GtkWidget *main_statusbar = NULL;
GtkWidget *main_window = NULL;
GStaticMutex sview_mutex = G_STATIC_MUTEX_INIT;

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
	xfree(page);

	while(page_running[num]) {
		g_static_mutex_lock(&sview_mutex);
		gdk_threads_enter();
		(display_data->get_info)(table, display_data);
		gdk_flush();
		gdk_threads_leave();
		g_static_mutex_unlock(&sview_mutex);
		sleep(global_sleep_time);
	}	
		
	return NULL;
}

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
	int i;
	static int running=-1;
	page_thr_t *page_thr = NULL;
	GError *error = NULL;
		
	/* make sure we aren't adding the page, and really asking for info */
	if(adding)
		return;
	
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
}

static void _change_refresh(GtkToggleAction *action, gpointer user_data)
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
	gtk_main_quit ();
	list_destroy(popup_list);
	fini = 1;
	return FALSE;
}

/* Returns a menubar widget made from the above menu */
static GtkWidget *_get_menubar_menu(GtkWidget *window, GtkWidget *notebook)
{
	GtkActionGroup *action_group = NULL;
	GtkUIManager *ui_manager = NULL;
	GtkAccelGroup *accel_group = NULL;
	GError *error = NULL;

	/* Our menu*/
	const char *ui_description =
		"<ui>"
		"  <menubar name='MainMenu'>"
		"    <menu action='Options'>"
		"      <menuitem action='Set Refresh Interval'/>"
		"      <menuitem action='Refresh'/>"
		"      <separator/>"
		"      <menuitem action='Admin Mode'/>"
		"      <separator/>"
		"      <menu action='Tab Pos'>"
		"        <menuitem action='Top'/>"
		"        <menuitem action='Bottom'/>"
		"        <menuitem action='Left'/>"
		"        <menuitem action='Right'/>"
		"      </menu>"
		"      <separator/>"
		"      <menuitem action='Exit'/>"
		"    </menu>"
		"    <menu action='Help'>"
		"      <menuitem action='About'/>"
		"    </menu>"
		"  </menubar>"
		"</ui>";

	GtkActionEntry entries[] = {
		{"Options", NULL, "_Options"},
		{"Tab Pos", NULL, "_Tab Pos"},
		{"Set Refresh Interval", NULL, "Set _Refresh Interval", 
		 "<control>r", "Change Refresh Interval", 
		 G_CALLBACK(_change_refresh)},
		{"Refresh", NULL, "Refresh", 
		 "F5", "Refreshes page", G_CALLBACK(refresh_main)},
		{"Exit", NULL, "E_xit", 
		 "<control>x", "Exits Program", G_CALLBACK(_delete)},
		{"Help", NULL, "_Help"},
		{"About", NULL, "_About"}
	};

	GtkRadioActionEntry radio_entries[] = {
		{"Top", NULL, "_Top", 
		 "<control>T", "Move tabs to top", 2},
		{"Bottom", NULL, "_Bottom", 
		 "<control>B", "Move tabs to the bottom", 3},
		{"Left", NULL, "_Left", 
		 "<control>L", "Move tabs to the Left", 4},
		{"Right", NULL, "_Right", 
		 "<control>R", "Move tabs to the Right", 1}
	};

	GtkToggleActionEntry toggle_entries[] = {
		{"Admin Mode", NULL,          
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
	ui_manager = gtk_ui_manager_new ();
	gtk_ui_manager_insert_action_group (ui_manager, action_group, 0);

	accel_group = gtk_ui_manager_get_accel_group (ui_manager);
	gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);

	if (!gtk_ui_manager_add_ui_from_string (ui_manager, ui_description, 
						-1, &error))
	{
		g_error("building menus failed: %s", error->message);
		g_error_free (error);
		exit (0);
	}

	/* Finally, return the actual menu bar created by the item factory. */
	return gtk_ui_manager_get_widget (ui_manager, "/MainMenu");
}
void *_popup_thr_main(void *arg)
{
	popup_thr(arg);		
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


int main(int argc, char *argv[])
{
	GtkWidget *menubar = NULL;
	GtkWidget *table = NULL;
	GtkWidget *label = NULL;
	GtkWidget *combo = NULL;
	GtkWidget *entry = NULL;
	
	int i=0;
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
	
	_init_pages();
	g_thread_init(NULL);
	gdk_threads_init();
	gdk_threads_enter();
	/* Initialize GTK */
	gtk_init (&argc, &argv);
	/* fill in all static info for pages */
	/* Make a window */
	main_window = gtk_dialog_new();
	g_signal_connect(G_OBJECT(main_window), "delete_event",
			 G_CALLBACK(_delete), NULL);
	gtk_window_set_title(GTK_WINDOW(main_window), "Sview");
	gtk_window_set_default_size(GTK_WINDOW(main_window), 600, 400);
	gtk_container_set_border_width(
		GTK_CONTAINER(GTK_DIALOG(main_window)->vbox), 1);
	/* Create the main notebook, place the position of the tabs */
	main_notebook = gtk_notebook_new();
	g_signal_connect(G_OBJECT(main_notebook), "switch_page",
			 G_CALLBACK(_page_switched),
			 NULL);
	table = gtk_table_new(1, 4, FALSE);
	gtk_table_set_homogeneous(GTK_TABLE(table), FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 1);	
	/* Create a menu */
	menubar = _get_menubar_menu(main_window, main_notebook);
	gtk_table_attach_defaults(GTK_TABLE(table), menubar, 0, 1, 0, 1);

	label = gtk_label_new("Search ");
	gtk_table_attach(GTK_TABLE(table), label, 1, 2, 0, 1,
			 GTK_SHRINK, GTK_EXPAND | GTK_FILL,
			 0, 0);
	
	combo = create_pulldown_combo(pulldown_display_data, PAGE_CNT);
	gtk_table_attach(GTK_TABLE(table), combo, 2, 3, 0, 1,
			 GTK_SHRINK, GTK_EXPAND | GTK_FILL,
			 0, 0);
	
	entry = gtk_entry_new();
	gtk_table_attach(GTK_TABLE(table), entry, 3, 4, 0, 1,
			 GTK_SHRINK, GTK_EXPAND | GTK_FILL,
			 0, 0);
	
	g_signal_connect(G_OBJECT(entry), "activate",
			 G_CALLBACK(_search_entry),
			 combo);
	  
	gtk_notebook_popup_enable(GTK_NOTEBOOK(main_notebook));
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(main_notebook), TRUE);
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(main_notebook), GTK_POS_TOP);
	
	main_statusbar = gtk_statusbar_new();
	gtk_statusbar_set_has_resize_grip(GTK_STATUSBAR(main_statusbar), 
					  FALSE);
	/* Pack it all together */
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(main_window)->vbox),
			   table, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(main_window)->vbox), 
			   main_notebook, TRUE, TRUE, 0);	
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
	gtk_widget_show_all (main_window);

	/* Finished! */
	gtk_main ();
	gdk_threads_leave();

	return 0;
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

