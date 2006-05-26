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
GtkWidget *notebook = NULL;
sview_parameters_t params;
int adding = 1;
int fini = 0;

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
	
	/* make sure we aren't adding the page, and really asking for info */
	if(adding)
		return;
	switch(page_num) {
	case 0:
		get_slurm_part(table);
		break;
	case 1:
		get_slurm_part(table);
		break;
	case 2:
		get_slurm_part(table);
		break;
	case 3:
		get_slurm_part(table);
		break;
	default:
		break;
	}
}

static void _tab_pos(GtkRadioAction *action,
		     GtkRadioAction *extra,
		     GtkWidget *menu_item)
{
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), 
				 gtk_radio_action_get_current_value(action));
}

static void _next_page(GtkAction *action,
		       GtkWidget *menu_item)
{
	gtk_notebook_next_page(GTK_NOTEBOOK(notebook));
}

static void _prev_page(GtkAction *action,
		       GtkWidget *menu_item)
{
	gtk_notebook_prev_page(GTK_NOTEBOOK(notebook));
}

static void _refresh(GtkRadioAction *action,
		     GtkRadioAction *extra,
		     GtkWidget *menu_item)
{
	int page = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
	if(page == -1)
		g_error("no pages in notebook for refresh\n");
	_page_switched(GTK_NOTEBOOK(notebook), NULL, page, NULL);
	
}

static void _create_page(char *name)
{
	GtkScrolledWindow *scrolled_window = NULL;
	GtkWidget *table = NULL;
	GtkWidget *label;
	int err;

	table = gtk_table_new(1, 1, FALSE);

	gtk_container_set_border_width(GTK_CONTAINER(table), 10);	

	scrolled_window = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(
						      NULL, NULL));	
	gtk_container_set_border_width(GTK_CONTAINER(scrolled_window), 10);
    
	gtk_scrolled_window_set_policy(scrolled_window,
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);
    
	gtk_scrolled_window_add_with_viewport(scrolled_window, table);

	label = gtk_label_new(name);
	if((err = gtk_notebook_append_page(GTK_NOTEBOOK(notebook), 
				 GTK_WIDGET(scrolled_window), 
					   label)) == -1) {
		g_error("Couldn't add page to notebook\n");
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
	{"NextPage", NULL, "Ne_xtPage", 
	 "<control>X", "Moves to next page", G_CALLBACK(_next_page)},
	{"PrevPage", NULL, "_PrevPage", 
	 "<control>P", "Moves to previous page", G_CALLBACK(_prev_page)},
	{"Refresh", NULL, "Refresh", 
	 "F5", "Refreshes page", G_CALLBACK(_refresh)},
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
static GtkWidget *_get_menubar_menu(GtkWidget *window)
{
	GtkActionGroup *action_group = NULL;
	GtkUIManager *ui_manager = NULL;
	GtkAccelGroup *accel_group = NULL;
	GError *error = NULL;
	
	/* Make an accelerator group (shortcut keys) */
	action_group = gtk_action_group_new ("MenuActions");
	gtk_action_group_add_actions(action_group, entries, 
				     G_N_ELEMENTS(entries), window);
	gtk_action_group_add_radio_actions(action_group, radio_entries, 
					   G_N_ELEMENTS(radio_entries), 
					   0, G_CALLBACK(_tab_pos), window);
	ui_manager = gtk_ui_manager_new ();
	gtk_ui_manager_insert_action_group (ui_manager, action_group, 0);

	accel_group = gtk_ui_manager_get_accel_group (ui_manager);
	gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);

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
	fini = 1;
	return FALSE;
}

/* You have to start somewhere */
int main( int argc,
          char *argv[] )
{
	GtkWidget *window;
	GtkWidget *menubar;
	
	/* Initialize GTK */
	gtk_init (&argc, &argv);
 
	/* Make a window */
	window = gtk_dialog_new();
	g_signal_connect(G_OBJECT(window), "delete_event",
			 G_CALLBACK(_delete), NULL);
	gtk_window_set_title(GTK_WINDOW(window), "Sview");
	gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);
	gtk_container_set_border_width(GTK_CONTAINER(GTK_DIALOG(window)->vbox),
				       1);
	
	/* Create a menu */
	menubar = _get_menubar_menu(window);
	/* Create a new notebook, place the position of the tabs */
	notebook = gtk_notebook_new();
	g_signal_connect(G_OBJECT(notebook), "switch_page",
			 G_CALLBACK(_page_switched),
			 NULL);
	
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
	
	/* Pack it all together */
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(window)->vbox),
			   menubar, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(GTK_DIALOG(window)->vbox), 
			 notebook, TRUE, TRUE, 0);	
	
	/* Partition info */

	_create_page("Partitions");
	/* Job info */
	_create_page("Jobs");	

	/* Node info */
	_create_page("Nodes");
	
	/* Admin */
	_create_page("Administration");
	/* tell signal we are done adding */
	adding = 0;
	
	gtk_widget_show_all (window);

	/* Finished! */
	gtk_main ();
 
	return 0;
}
