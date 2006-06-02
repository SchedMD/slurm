/*****************************************************************************\
 *  common.c - common functions used by tabs in sview
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

#include "src/sview/sview.h"

static int _sort_iter_compare_func(GtkTreeModel *model,
				   GtkTreeIter  *a,
				   GtkTreeIter  *b,
				   gpointer      userdata)
{
	int sortcol = GPOINTER_TO_INT(userdata);
	int ret = 0;
	gchar *name1 = NULL, *name2 = NULL;

	gtk_tree_model_get(model, a, sortcol, &name1, -1);
	gtk_tree_model_get(model, b, sortcol, &name2, -1);
	
	if (name1 == NULL || name2 == NULL)
	{
		if (name1 == NULL && name2 == NULL)
			goto cleanup; /* both equal => ret = 0 */
		
		ret = (name1 == NULL) ? -1 : 1;
	}
	else
	{
		ret = g_utf8_collate(name1,name2);
	}
cleanup:
	g_free(name1);
	g_free(name2);
	
	return ret;
}

static void _add_col_to_treeview(GtkTreeView *tree_view, 
				 display_data_t *display_data)
{
	GtkTreeViewColumn   *col;
	GtkCellRenderer     *renderer;
  
	renderer = gtk_cell_renderer_text_new();
	col = gtk_tree_view_column_new();	
	gtk_tree_view_column_pack_start (col, renderer, TRUE);
	gtk_tree_view_column_add_attribute (col, renderer, 
					    "text", display_data->id);
	gtk_tree_view_column_set_title (col, display_data->name);
	gtk_tree_view_append_column(tree_view, col);
	gtk_tree_view_column_set_sort_column_id(col, display_data->id);

}

static void _toggle_state_changed(GtkCheckMenuItem *menuitem, 
				  gpointer user_data)
{
	bool *checked = user_data;
	if(*checked)
		*checked = FALSE;
	else
		*checked = TRUE;
	toggled = TRUE;
	refresh_page(NULL, NULL);
}

extern void snprint_time(char *buf, size_t buf_size, time_t time)
{
	if (time == INFINITE) {
		snprintf(buf, buf_size, "UNLIMITED");
	} else {
		long days, hours, minutes, seconds;
		seconds = time % 60;
		minutes = (time / 60) % 60;
		hours = (time / 3600) % 24;
		days = time / 86400;

		if (days)
			snprintf(buf, buf_size,
				"%ld-%2.2ld:%2.2ld:%2.2ld",
				days, hours, minutes, seconds);
		else if (hours)
			snprintf(buf, buf_size,
				"%ld:%2.2ld:%2.2ld", 
				hours, minutes, seconds);
		else
			snprintf(buf, buf_size,
				"%ld:%2.2ld", minutes,seconds);
	}
}

extern int get_row_number(GtkTreeView *tree_view, GtkTreePath *path)
{
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	GtkTreeIter iter;
	int line = 0;
	
	if(!model) {
		g_error("error getting the model from the tree_view");
		return -1;
	}
	
	if (!gtk_tree_model_get_iter(model, &iter, path)) {
		g_error("error getting iter from model");
		return -1;
	}	
	gtk_tree_model_get(model, &iter, POS_LOC, &line, -1);
	return line;
}

extern GtkListStore *create_liststore(display_data_t *display_data, int count)
{
	GtkListStore *liststore = NULL;
	GType types[count];
	int i=0;
	/* for the position 'unseen' var */
	types[i] = G_TYPE_INT;
	for(i=1; i<count; i++)
		types[i] = G_TYPE_STRING;
	liststore = gtk_list_store_newv(count, types);
	if(!liststore)
		return NULL;
	for(i=1; i<count; i++) {
		if(display_data[i].show)
			gtk_tree_sortable_set_sort_func(
				GTK_TREE_SORTABLE(liststore), 
				i, 
				_sort_iter_compare_func,
				GINT_TO_POINTER(i), 
				NULL); 
	}
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(liststore), 
					     1, 
					     GTK_SORT_ASCENDING);
	return liststore;
}

extern void load_header(GtkTreeView *tree_view, display_data_t *display_data)
{
	while(display_data++) {
		if(display_data->id == -1)
			break;
		else if(!display_data->show) 
			continue;
		_add_col_to_treeview(tree_view, display_data);
	}
}

extern void make_fields_menu(GtkMenu *menu, display_data_t *display_data)
{
	GtkWidget *menuitem = NULL;
	
	while(display_data++) {
		if(display_data->id == -1)
			break;
		menuitem = gtk_check_menu_item_new_with_label(
			display_data->name); 
		
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
					       display_data->show);
		g_signal_connect(menuitem, "toggled",
				 G_CALLBACK(_toggle_state_changed), 
				 &display_data->show);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    
	}		

}

extern void create_page(GtkNotebook *notebook, display_data_t *display_data)
{
	GtkScrolledWindow *scrolled_window = NULL;
	GtkWidget *table = NULL;
	GtkWidget *event_box = NULL;
	GtkWidget *label = NULL;
	int err;
	GtkWidget *menu = gtk_menu_new();
	
	table = gtk_table_new(1, 1, FALSE);

	gtk_container_set_border_width(GTK_CONTAINER(table), 10);	

	scrolled_window = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(
						      NULL, NULL));	
	gtk_container_set_border_width(GTK_CONTAINER(scrolled_window), 10);
    
	gtk_scrolled_window_set_policy(scrolled_window,
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);
    
	gtk_scrolled_window_add_with_viewport(scrolled_window, table);
	
	event_box = gtk_event_box_new();
	gtk_event_box_set_above_child(GTK_EVENT_BOX(event_box), FALSE);
	g_signal_connect(G_OBJECT(event_box), "button-press-event",
			 G_CALLBACK(tab_pressed),
			 display_data);
	
	label = gtk_label_new(display_data->name);
	gtk_container_add(GTK_CONTAINER(event_box), label);
	gtk_widget_show(label);
	(display_data->set_fields)(GTK_MENU(menu));
	if((err = gtk_notebook_append_page(GTK_NOTEBOOK(notebook), 
					   GTK_WIDGET(scrolled_window), 
					   event_box)) == -1) {
		g_error("Couldn't add page to notebook\n");
	}
	
	display_data->extra = err;

}

extern void right_button_pressed(GtkWidget *widget,
				 GdkEventButton *event, 
				 const display_data_t *display_data)
{
	if(event->button == 3) {
		GtkMenu *menu = GTK_MENU(gtk_menu_new());
	
		(display_data->set_fields)(menu);
				
		gtk_widget_show_all(GTK_WIDGET(menu));
		gtk_menu_popup(menu, NULL, NULL, NULL, NULL,
			       (event != NULL) ? event->button : 0,
			       gdk_event_get_time((GdkEvent*)event));
	}
}

extern void button_pressed(GtkTreeView *tree_view, GdkEventButton *event, 
			   const display_data_t *display_data)
{
	GtkTreePath *path = NULL;
	GtkTreeSelection *selection = NULL;
	
        if(!gtk_tree_view_get_path_at_pos(tree_view,
					  (gint) event->x, 
					  (gint) event->y,
					  &path, NULL, NULL, NULL)) {
		return;
	}
	selection = gtk_tree_view_get_selection(tree_view);
	gtk_tree_selection_unselect_all(selection);
	gtk_tree_selection_select_path(selection, path);
             	
	/* single click with the right mouse button? */
	if(event->button == 3) {
		right_button_pressed(NULL, event, display_data);
	} else if(event->type==GDK_2BUTTON_PRESS ||
		  event->type==GDK_3BUTTON_PRESS) {
		(display_data->row_clicked)(tree_view, path, 
					    NULL, display_data->user_data);
	}
	gtk_tree_path_free(path);
}

extern void tab_focus(GtkNotebook *notebook, GdkEventFocus *event, 
		      const display_data_t *display_data)
{
	int page = gtk_notebook_get_current_page(notebook);
	g_print("page number is %d type %d, send_event %d, in %d\n",
		page, event->type, event->send_event, event->in);
	return;
	/* single click with the right mouse button? */
/* 	if(event->button == 3) { */
/* 		right_button_pressed(NULL, event, display_data); */
/* //view_popup_menu(treeview, event, userdata); */
/* 	} else if(event->type==GDK_2BUTTON_PRESS || */
/* 		  event->type==GDK_3BUTTON_PRESS) { */
/* 		/\* (display_data->row_clicked)(tree_view, path,  *\/ */
/* /\* 					    NULL, display_data->user_data); *\/ */
/* 	} */
	//gtk_tree_path_free(path);
}

extern GtkMenu *make_menu(const display_data_t *display_data)
{
	GtkMenu *menu = GTK_MENU(gtk_menu_new());
	
	(display_data->set_fields)(menu);
	return menu;
}
