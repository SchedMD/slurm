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
/* globals */
sview_parameters_t params;
int adding = 1;
int fini = 0;
bool toggled = FALSE;
List popup_list;
	
GtkWidget *main_notebook = NULL;
display_data_t main_display_data[] = {
	{G_TYPE_NONE, PART_PAGE, "Partitions", TRUE, -1, 
	 refresh_main, get_info_part, set_menus_part, row_clicked_part, NULL},
	{G_TYPE_NONE, JOB_PAGE, "Jobs", TRUE, -1,
	 refresh_main, get_info_job, set_menus_job, row_clicked_job, NULL},
	{G_TYPE_NONE, NODE_PAGE, "Nodes", TRUE, -1,
	 refresh_main, get_info_node, set_menus_node, row_clicked_node, NULL},
#ifdef HAVE_BG
	{G_TYPE_NONE, BLOCK_PAGE, "BG Blocks", TRUE, -1,
#else
	 {G_TYPE_NONE, BLOCK_PAGE, "BG Blocks", FALSE, -1,
#endif
	  refresh_main, get_info_block, set_menus_block, 
	  row_clicked_block, NULL},
	 {G_TYPE_NONE, SUBMIT_PAGE, "Submit Job", TRUE, -1,
	  refresh_main, get_info_submit, set_menus_submit, 
	  row_clicked_submit, NULL},
	 {G_TYPE_NONE, ADMIN_PAGE, "Admin", TRUE, -1,
	  refresh_main, get_info_admin, set_menus_admin, 
	  row_clicked_admin, NULL},
	 {G_TYPE_NONE, -1, NULL, FALSE, -1, NULL, NULL, NULL, NULL, NULL}
	};
	
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
	/* make sure we aren't adding the page, and really asking for info */
	if(adding)
		return;
	
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
	(main_display_data[i].get_info)(table, &main_display_data[i]);
}

static void _tab_pos(GtkRadioAction *action,
		     GtkRadioAction *extra,
		     GtkNotebook *notebook)
{
	gtk_notebook_set_tab_pos(notebook, 
				 gtk_radio_action_get_current_value(action));
}

static void _next_page(GtkAction *action,
		       GtkNotebook *notebook)
{
	int page = gtk_notebook_get_current_page(notebook);
	int cnt = gtk_notebook_get_n_pages(notebook);
	
	cnt--;
	
	if(page < cnt)
		gtk_notebook_next_page(notebook);
	else
		gtk_notebook_set_current_page(notebook, 0);
}

static void _prev_page(GtkAction *action,
		       GtkNotebook *notebook)
{
	int page = gtk_notebook_get_current_page(notebook);
	int cnt = gtk_notebook_get_n_pages(notebook);

	cnt--;

	if(page != 0)
		gtk_notebook_prev_page(notebook);
	else
		gtk_notebook_set_current_page(notebook, cnt);
	//gtk_notebook_prev_page(notebook);
}

static void _init_pages()
{
	int i;
	for(i=1; i<PAGE_CNT; i++) {
		(main_display_data[i].get_info)(NULL, &main_display_data[i]);
	}
}

/* Our menu*/
static const char *ui_description =
"<ui>"
"  <menubar name='MainMenu'>"
"    <menu action='Options'>"
"      <menuitem action='Refresh'/>"
"      <separator/>"
"      <menu action='Tab Pos'>"
"        <menuitem action='Top'/>"
"        <menuitem action='Bottom'/>"
"        <menuitem action='Left'/>"
"        <menuitem action='Right'/>"
"      </menu>"
"      <separator/>"
"      <menuitem action='NextPage'/>"
"      <menuitem action='PrevPage'/>"
"    </menu>"
"    <menu action='Help'>"
"      <menuitem action='About'/>"
"    </menu>"
"  </menubar>"
"</ui>";

static GtkActionEntry entries[] = {
	{"Options", NULL, "_Options"},
	{"Tab Pos", NULL, "_Tab Pos"},
	{"NextPage", NULL, "_NextPage", 
	 "<control>N", "Moves to next page", G_CALLBACK(_next_page)},
	{"PrevPage", NULL, "_PrevPage", 
	 "<control>P", "Moves to previous page", G_CALLBACK(_prev_page)},
	{"Refresh", NULL, "Refresh", 
	 "F5", "Refreshes page", G_CALLBACK(refresh_main)},
	{"Help", NULL, "_Help"},
	{"About", NULL, "_About"}
};

static GtkRadioActionEntry radio_entries[] = {
	{"Top", NULL, "_Top", 
	 "<control>T", "Move tabs to top", 2},
	{"Bottom", NULL, "_Bottom", 
	 "<control>B", "Move tabs to the bottom", 3},
	{"Left", NULL, "_Left", 
	 "<control>L", "Move tabs to the Left", 4},
	{"Right", NULL, "_Right", 
	 "<control>R", "Move tabs to the Right", 1}
};

/* Returns a menubar widget made from the above menu */
static GtkWidget *_get_menubar_menu(GtkWidget *window, GtkWidget *notebook)
{
	GtkActionGroup *action_group = NULL;
	GtkUIManager *ui_manager = NULL;
	GtkAccelGroup *accel_group = NULL;
	GError *error = NULL;
	
	/* Make an accelerator group (shortcut keys) */
	action_group = gtk_action_group_new ("MenuActions");
	gtk_action_group_add_actions(action_group, entries, 
				     G_N_ELEMENTS(entries), notebook);
	gtk_action_group_add_radio_actions(action_group, radio_entries, 
					   G_N_ELEMENTS(radio_entries), 
					   0, G_CALLBACK(_tab_pos), notebook);
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

static gboolean _delete(GtkWidget *widget,
                        GtkWidget *event,
                        gpointer data)
{
	gtk_main_quit ();
	list_destroy(popup_list);
	fini = 1;
	return FALSE;
}

int main(int argc, char *argv[])
{
	GtkWidget *window;
	GtkWidget *menubar;
	int i=0;
	
	/* Initialize GTK */
	gtk_init (&argc, &argv);
	/* fill in all static info for pages */
	_init_pages();
	/* Make a window */
	window = gtk_dialog_new();
	g_signal_connect(G_OBJECT(window), "delete_event",
			 G_CALLBACK(_delete), NULL);
	gtk_window_set_title(GTK_WINDOW(window), "Sview");
	gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);
	gtk_container_set_border_width(GTK_CONTAINER(GTK_DIALOG(window)->vbox),
				       1);
	/* Create the main notebook, place the position of the tabs */
	main_notebook = gtk_notebook_new();
	g_signal_connect(G_OBJECT(main_notebook), "switch_page",
			 G_CALLBACK(_page_switched),
			 NULL);
	
	/* Create a menu */
	menubar = _get_menubar_menu(window, main_notebook);
	
	gtk_notebook_popup_enable(GTK_NOTEBOOK(main_notebook));
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(main_notebook), TRUE);
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(main_notebook), GTK_POS_TOP);
	
	/* Pack it all together */
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(window)->vbox),
			   menubar, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(GTK_DIALOG(window)->vbox), 
			 main_notebook, TRUE, TRUE, 0);	
	
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
	gtk_widget_show_all (window);

	/* Finished! */
	gtk_main ();
 
	return 0;
}

extern void refresh_main(GtkAction *action, gpointer user_data)
{
	int page = gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook));
	if(page == -1)
		g_error("no pages in notebook for refresh\n");
	_page_switched(GTK_NOTEBOOK(main_notebook), NULL, page, NULL);
	toggled = FALSE;
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
