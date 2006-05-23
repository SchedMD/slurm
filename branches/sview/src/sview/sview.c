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
GtkNotebook *notebook = NULL;
sview_parameters_t params;
int frequency = 5;


static void _tab_pos(gpointer   callback_data,
		     guint      callback_action,
		     GtkWidget *menu_item )
{
	gtk_notebook_set_tab_pos (notebook, callback_action);
}

static void _next_page(gpointer   callback_data,
		       guint      callback_action,
		       GtkWidget *menu_item )
{
	gtk_notebook_next_page(notebook);
}

static void _prev_page(gpointer   callback_data,
		       guint      callback_action,
		       GtkWidget *menu_item )
{
	gtk_notebook_prev_page(notebook);
}

static void _set_freq(gpointer   callback_data,
		      guint      callback_action,
		      GtkWidget *menu_item )
{
	frequency++;
}

/* Our menu, an array of GtkItemFactoryEntry structures that defines each menu item */
static GtkItemFactoryEntry menu_items[] = {
	{ "/_File",         NULL,         NULL,           0, "<Branch>" },
	{ "/File/_New",     "<control>N", NULL,    0, "<StockItem>", GTK_STOCK_NEW },
	{ "/File/_Open",    "<control>O", NULL,    0, "<StockItem>", GTK_STOCK_OPEN },
	{ "/File/_Save",    "<control>S", NULL,    0, "<StockItem>", GTK_STOCK_SAVE },
	{ "/File/Save _As", NULL,         NULL,           0, "<Item>" },
	{ "/File/sep1",     NULL,         NULL,           0, "<Separator>" },
	{ "/File/_Quit",    "<CTRL>Q", gtk_main_quit, 0, "<StockItem>", GTK_STOCK_QUIT },
	{ "/_Options",      NULL,         NULL,           0, "<Branch>" },
	{ "/Options/TabPos", NULL,        NULL,          0, "<Branch>" },
	{ "/Options/TabPos/_Top",  "<control>T",    _tab_pos,      2, "<RadioItem>" },
	{ "/Options/TabPos/_Bottom",  "<control>B", _tab_pos,      3, "/Options/TabPos/Top" },
	{ "/Options/TabPos/_Left",  "<control>L",   _tab_pos,      4, "/Options/TabPos/Top" },
	{ "/Options/TabPos/_Right",  "<control>R",  _tab_pos,      1, "/Options/TabPos/Top" },
	{ "/Options/sep",   NULL,         NULL,           0, "<Separator>" },
	{ "/Options/_Frequency",  "<CTRL>F",    _set_freq, 0, "<Item>" },
	{ "/Options/sep",   NULL,         NULL,           0, "<Separator>" },
	{ "/Options/Ne_xtPage",  "<CTRL>X",    _next_page, 0, "<Item>" },
	{ "/Options/_PrevPage",  "<CTRL>P",    _prev_page, 0, "<Item>" },
	{ "/_Help",         NULL,         NULL,           0, "<LastBranch>" },
	{ "/_Help/About",   NULL,         NULL,           0, "<Item>" },
};

static gint nmenu_items = sizeof (menu_items) / sizeof (menu_items[0]);

/* Returns a menubar widget made from the above menu */
static GtkWidget *get_menubar_menu( GtkWidget  *window )
{
	GtkItemFactory *item_factory;
	GtkAccelGroup *accel_group;

	/* Make an accelerator group (shortcut keys) */
	accel_group = gtk_accel_group_new ();

	/* Make an ItemFactory (that makes a menubar) */
	item_factory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>",
					     accel_group);

	/* This function generates the menu items. Pass the item factory,
	   the number of items in the array, the array itself, and any
	   callback data for the the menu items. */
	gtk_item_factory_create_items (item_factory, nmenu_items, menu_items, NULL);

	/* Attach the new accelerator group to the window. */
	gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);

	/* Finally, return the actual menu bar created by the item factory. */
	return gtk_item_factory_get_widget (item_factory, "<main>");
}

/* Popup the menu when the popup button is pressed */
static gboolean popup_cb( GtkWidget *widget,
                          GdkEvent *event,
                          GtkWidget *menu )
{
	GdkEventButton *bevent = (GdkEventButton *)event;
  
	/* Only take button presses */
	if (event->type != GDK_BUTTON_PRESS)
		return FALSE;
  
	/* Show the menu */
	gtk_menu_popup (GTK_MENU(menu), NULL, NULL,
			NULL, NULL, bevent->button, bevent->time);
  
	return TRUE;
}

/* Same as with get_menubar_menu() but just return a button with a signal to
   call a popup menu */
GtkWidget *get_popup_menu( void )
{
	GtkItemFactory *item_factory;
	GtkWidget *button, *menu;
  
	/* Same as before but don't bother with the accelerators */
	item_factory = gtk_item_factory_new (GTK_TYPE_MENU, "<main>",
					     NULL);
	gtk_item_factory_create_items (item_factory, nmenu_items, menu_items, NULL);
	menu = gtk_item_factory_get_widget (item_factory, "<main>");
  
	/* Make a button to activate the popup menu */
	button = gtk_button_new_with_label ("Popup");
	/* Make the menu popup when clicked */
	g_signal_connect (G_OBJECT(button),
			  "event",
			  G_CALLBACK(popup_cb),
			  (gpointer) menu);

	return button;
}

/* Same again but return an option menu */
GtkWidget *get_option_menu( void )
{
	GtkItemFactory *item_factory;
	GtkWidget *option_menu;
  
	/* Same again, not bothering with the accelerators */
	item_factory = gtk_item_factory_new (GTK_TYPE_OPTION_MENU, "<main>",
					     NULL);
	gtk_item_factory_create_items (item_factory, nmenu_items, menu_items, NULL);
	option_menu = gtk_item_factory_get_widget (item_factory, "<main>");

	return option_menu;
}

static gboolean delete( GtkWidget *widget,
                        GtkWidget *event,
                        gpointer   data )
{
	gtk_main_quit ();
	return FALSE;
}

/* You have to start somewhere */
int main( int argc,
          char *argv[] )
{
	GtkWidget *window;
	GtkWidget *diagwindow;
	GtkWidget *button;
	GtkWidget *menubar, *option_menu, *popup_button;
	GtkWidget *table;
	GtkWidget *scrolltable;
	GtkWidget *scrollwindow;
	GtkWidget *frame;
	GtkWidget *label;
	GtkWidget *checkbutton;
	GtkWidget *scrolled_window;
	int i, j;
	char bufferf[32];
	char bufferl[32];
    
	/* Initialize GTK */
	gtk_init (&argc, &argv);
 
	/* Make a window */
	window = gtk_dialog_new();
	g_signal_connect(G_OBJECT(window), "delete_event",
			 G_CALLBACK(delete), NULL);
	gtk_window_set_title(GTK_WINDOW(window), "Sview");
	
	gtk_container_set_border_width(GTK_CONTAINER(GTK_DIALOG(window)->vbox),
				       1);
	
	/* Get the three types of menu */
	menubar = get_menubar_menu(window);
	
	table = gtk_table_new(1, 6, FALSE);
	gtk_widget_set_size_request(GTK_WIDGET(table), 600, 400);

	/* Pack it all together */
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(window)->vbox),
			   menubar, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(GTK_DIALOG(window)->vbox), 
			 table, TRUE, TRUE, 0);	
	
	/* Create a new notebook, place the position of the tabs */
	notebook = gtk_notebook_new();
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
	gtk_table_attach_defaults(GTK_TABLE(table), notebook, 0, 6, 0, 1);
	gtk_widget_show(notebook);
  
	/* Partition info */
	scrolltable = gtk_table_new(10, 1, FALSE);

	gtk_container_set_border_width(GTK_CONTAINER(scrolltable), 10);
	

	scrolled_window = gtk_scrolled_window_new(NULL, NULL);
	
	for (j = 0; j < 10; j++) {
		sprintf (bufferl, "Partition (%d)\n", j);
		button = gtk_toggle_button_new_with_label (bufferl);
		gtk_table_attach_defaults (GTK_TABLE(scrolltable), 
					   button,
					   0, 1, j, j+1);
		gtk_widget_show (button);
	}
	/* g_signal_connect (G_OBJECT(scrolled_window), "focused", */
/* 			  G_CALLBACK(get_slurm_part),  */
/* 			  (gpointer)scrolled_window); */
	gtk_container_set_border_width(GTK_CONTAINER(scrolled_window), 10);
    
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);
    
	gtk_widget_show(scrolled_window);
	gtk_scrolled_window_add_with_viewport (
		GTK_SCROLLED_WINDOW (scrolled_window), scrolltable);
	gtk_widget_show(scrolltable);
	

	label = gtk_label_new("Partitions");
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), 
				 scrolled_window, label);
	
	/* Job info */
	scrolltable = gtk_table_new(10, 1, FALSE);

	gtk_container_set_border_width(GTK_CONTAINER(scrolltable), 10);
	

	scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    
	gtk_container_set_border_width(GTK_CONTAINER(scrolled_window), 10);
    
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);
    
	gtk_widget_show(scrolled_window);
	gtk_scrolled_window_add_with_viewport (
		GTK_SCROLLED_WINDOW (scrolled_window), scrolltable);
	gtk_widget_show(scrolltable);
	

	for (j = 0; j < 10; j++) {
		sprintf (bufferl, "Job (%d)\n", j);
		button = gtk_toggle_button_new_with_label (bufferl);
		gtk_table_attach_defaults (GTK_TABLE (scrolltable), 
					   button,
					   0, 1, j, j+1);
		gtk_widget_show (button);
	}

	label = gtk_label_new("Jobs");
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), 
				 scrolled_window, label);
	/* Node info */

	frame = gtk_frame_new ("Node info");
	gtk_container_set_border_width (GTK_CONTAINER (frame), 10);
	gtk_widget_show (frame);
	
	label = gtk_label_new ("Node info here");
	gtk_container_add (GTK_CONTAINER (frame), label);
	gtk_widget_show (label);
	
	label = gtk_label_new ("Nodes");
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), frame, label);
	
	/* Admin */
	frame = gtk_frame_new ("Administration");
	gtk_container_set_border_width (GTK_CONTAINER (frame), 10);
	gtk_widget_show (frame);
	
	label = gtk_label_new ("Admin info here");
	gtk_container_add (GTK_CONTAINER (frame), label);
	gtk_widget_show (label);
	
	label = gtk_label_new ("Admin");
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), frame, label);
	
    
	/* Set what page to start at (page 4) */
	gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 0);

	gtk_widget_show (table);
	gtk_widget_show_all (window);

	/* Finished! */
	gtk_main ();
 
	return 0;
}
