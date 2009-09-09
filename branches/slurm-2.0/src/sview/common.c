/*****************************************************************************\
 *  common.c - common functions used by tabs in sview
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
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
#include "src/common/parse_time.h"

typedef struct {
	GtkTreeModel *model;
	GtkTreeIter iter;
} treedata_t;

static void _handle_response(GtkDialog *dialog, gint response_id,
			     popup_info_t *popup_win)
{
	switch(response_id) {
	case GTK_RESPONSE_OK: //refresh
		(popup_win->display_data->refresh)(NULL, popup_win);
		break;
	case GTK_RESPONSE_DELETE_EVENT: // exit
	case GTK_RESPONSE_CLOSE: // close
		delete_popup(NULL, NULL, popup_win->spec_info->title);
		break;
	default:
		g_print("handle unknown response %d\n", response_id);
		break;
	}
	return;
}

static int _sort_iter_compare_func_char(GtkTreeModel *model,
					GtkTreeIter  *a,
					GtkTreeIter  *b,
					gpointer      userdata)
{
	int sortcol = GPOINTER_TO_INT(userdata);
	int ret = 0;
	int len1 = 0, len2 = 0;
	gchar *name1 = NULL, *name2 = NULL;
	
	gtk_tree_model_get(model, a, sortcol, &name1, -1);
	gtk_tree_model_get(model, b, sortcol, &name2, -1);
	
	if (name1 == NULL || name2 == NULL) {
		if (name1 == NULL && name2 == NULL)
			goto cleanup; /* both equal => ret = 0 */
		
		ret = (name1 == NULL) ? -1 : 1;
	} else {
		/* sort like a human would 
		   meaning snowflake2 would be greater than
		   snowflake12 */
		len1 = strlen(name1);
		len2 = strlen(name2);
		while((ret < len1) && (!g_ascii_isdigit(name1[ret]))) 
			ret++;
		if(ret < len1) {
			if(!g_ascii_strncasecmp(name1, name2, ret)) {
				if(len1 > len2)
					ret = 1;
				else if(len1 < len2)
					ret = -1;
				else {
					ret = g_ascii_strcasecmp(name1, name2);
				}
			} else {
				ret = g_ascii_strcasecmp(name1, name2);
			}
			
		} else {
			ret = g_ascii_strcasecmp(name1, name2);
		}
	}
cleanup:
	g_free(name1);
	g_free(name2);
	
	return ret;
}

static int _sort_iter_compare_func_int(GtkTreeModel *model,
				       GtkTreeIter  *a,
				       GtkTreeIter  *b,
				       gpointer      userdata)
{
	int sortcol = GPOINTER_TO_INT(userdata);
	int ret = 0;
	gint int1, int2;

	gtk_tree_model_get(model, a, sortcol, &int1, -1);
	gtk_tree_model_get(model, b, sortcol, &int2, -1);
	
	if (int1 != int2)
		ret = (int1 > int2) ? 1 : -1;
	
	return ret;
}

static int _sort_iter_compare_func_nodes(GtkTreeModel *model,
					 GtkTreeIter  *a,
					 GtkTreeIter  *b,
					 gpointer      userdata)
{
	int sortcol = GPOINTER_TO_INT(userdata);
	int ret = 0;
	gchar *name1 = NULL, *name2 = NULL;
	
	gtk_tree_model_get(model, a, sortcol, &name1, -1);
	gtk_tree_model_get(model, b, sortcol, &name2, -1);
	
	if (name1 == NULL || name2 == NULL) {
		if (name1 == NULL && name2 == NULL)
			goto cleanup; /* both equal => ret = 0 */
		
		ret = (name1 == NULL) ? -1 : 1;
	} else {
		uint64_t int1 = atoi(name1);
		uint64_t int2 = atoi(name2);
		if(strchr(name1, 'K')) {
			int1 *= 1024;
		} else if(strchr(name1, 'M')) {
			int1 *= 1048576;
		} else if(strchr(name1, 'G')) {
			int1 *= 1073741824;
		}

		if(strchr(name2, 'K')) {
			int2 *= 1024;
		} else if(strchr(name2, 'M')) {
			int2 *= 1048576;
		} else if(strchr(name2, 'G')) {
			int2 *= 1073741824;
		}

		if (int1 != int2)
			ret = (int1 > int2) ? 1 : -1;		
	}
cleanup:
	g_free(name1);
	g_free(name2);
	
	return ret;
}

/* Make a BlueGene node name into a numeric representation of 
 * its location. 
 * Value is low_coordinate * 1,000,000 + 
 *          high_coordinate * 1,000 + I/O node (999 if none)
 * (e.g. bg123[4] -> 123,123,004, bg[234x235] -> 234,235,999)
 */
static int _bp_coordinate(const char *name)
{
	int i, io_val = 999, low_val = -1, high_val = -1;

	for (i=0; name[i]; i++) {
		if (name[i] == '[') {
			i++;
			if (low_val < 0) {
				char *end_ptr;
				low_val = strtol(name+i, &end_ptr, 10);
				if ((end_ptr[0] != '\0') &&
				    (isdigit(end_ptr[1])))
					high_val = atoi(end_ptr + 1);
				else
					high_val = low_val;
			} else
				io_val = atoi(name+i);
			break;
		} else if ((low_val < 0) && (isdigit(name[i])))
			low_val = high_val = atoi(name+i);
	}

	if (low_val < 0)
		return low_val;
	return ((low_val * 1000000) + (high_val * 1000) + io_val);
}

static int _sort_iter_compare_func_bp_list(GtkTreeModel *model,
					   GtkTreeIter  *a,
					   GtkTreeIter  *b,
					   gpointer      userdata)
{
	int sortcol = GPOINTER_TO_INT(userdata);
	int ret = 0;
	gchar *name1 = NULL, *name2 = NULL;
	
	gtk_tree_model_get(model, a, sortcol, &name1, -1);
	gtk_tree_model_get(model, b, sortcol, &name2, -1);
	
	if ((name1 == NULL) || (name2 == NULL)) {
		if ((name1 == NULL) && (name2 == NULL))
			goto cleanup; /* both equal => ret = 0 */
		
		ret = (name1 == NULL) ? -1 : 1;
	} else {
		/* Sort in numeric order based upon coordinates */
		ret = _bp_coordinate(name1) - _bp_coordinate(name2);
	}
cleanup:
	g_free(name1);
	g_free(name2);
	
	return ret;
}

static void _editing_started(GtkCellRenderer *cell,
			     GtkCellEditable *editable,
			     const gchar     *path,
			     gpointer         data)
{
	gdk_threads_leave();
	g_static_mutex_lock(&sview_mutex);
}

static void _editing_canceled(GtkCellRenderer *cell,
			       gpointer         data)
{
	g_static_mutex_unlock(&sview_mutex);
}

static void *_editing_thr(gpointer arg)
{
	int msg_id = 0;
	sleep(5);
	gdk_threads_enter();
	msg_id = GPOINTER_TO_INT(arg);
	gtk_statusbar_remove(GTK_STATUSBAR(main_statusbar), 
			     STATUS_ADMIN_EDIT, msg_id);
	gdk_flush();
	gdk_threads_leave();
	return NULL;	
}


static void _add_col_to_treeview(GtkTreeView *tree_view, 
				 display_data_t *display_data)
{
	GtkTreeViewColumn *col = gtk_tree_view_column_new();
	GtkListStore *model = (display_data->create_model)(display_data->id);
	GtkCellRenderer *renderer = NULL;
	if(model && display_data->extra != EDIT_NONE) {
		renderer = gtk_cell_renderer_combo_new();
		g_object_set(renderer,
			     "model", model,
			     "text-column", 0,
			     "has-entry", display_data->extra,
			     "editable", TRUE,
			     NULL);
	} else if(display_data->extra == EDIT_TEXTBOX) {
		renderer = gtk_cell_renderer_text_new();
		g_object_set(renderer,
			     "editable", TRUE,
			     NULL);
	} else
		renderer = gtk_cell_renderer_text_new();
	
	g_signal_connect(renderer, "editing-started",
			 G_CALLBACK(_editing_started), NULL);
	g_signal_connect(renderer, "editing-canceled",
			 G_CALLBACK(_editing_canceled), NULL);
	
	g_signal_connect(renderer, "edited",
			 G_CALLBACK(display_data->admin_edit), 
			 gtk_tree_view_get_model(tree_view));
	
	g_object_set_data(G_OBJECT(renderer), "column", 
			  GINT_TO_POINTER(display_data->id));
	
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, 
					   "text", display_data->id);
	
	gtk_tree_view_column_set_title(col, display_data->name);
	gtk_tree_view_column_set_reorderable(col, true);
	gtk_tree_view_column_set_resizable(col, true);
	gtk_tree_view_column_set_expand(col, true);
	gtk_tree_view_append_column(tree_view, col);
	//	gtk_tree_view_insert_column(tree_view, col, display_data->id);
	gtk_tree_view_column_set_sort_column_id(col, display_data->id);

}

static void _toggle_state_changed(GtkCheckMenuItem *menuitem, 
				  display_data_t *display_data)
{
	if(display_data->show)
		display_data->show = FALSE;
	else
		display_data->show = TRUE;
	toggled = TRUE;
	refresh_main(NULL, NULL);
}

static void _popup_state_changed(GtkCheckMenuItem *menuitem, 
				 display_data_t *display_data)
{
	popup_info_t *popup_win = (popup_info_t *) display_data->user_data;
	if(display_data->show)
		display_data->show = FALSE;
	else
		display_data->show = TRUE;
	popup_win->toggled = 1;
	(display_data->refresh)(NULL, display_data->user_data);
}

static void _selected_page(GtkMenuItem *menuitem, 
			   display_data_t *display_data)
{
	treedata_t *treedata = (treedata_t *)display_data->user_data;

	switch(display_data->extra) {
	case PART_PAGE:
		popup_all_part(treedata->model, &treedata->iter, 
			       display_data->id);
		break;
	case JOB_PAGE:
		popup_all_job(treedata->model, &treedata->iter, 
			      display_data->id);
		break;
	case NODE_PAGE:
		popup_all_node(treedata->model, &treedata->iter, 
			       display_data->id);
		break;
	case BLOCK_PAGE: 
		popup_all_block(treedata->model, &treedata->iter, 
				display_data->id);
		break;
	case RESV_PAGE: 
		popup_all_resv(treedata->model, &treedata->iter, 
			       display_data->id);
		break;
	case ADMIN_PAGE:
		switch(display_data->id) {
		case JOB_PAGE:
			admin_job(treedata->model, &treedata->iter, 
				  display_data->name);
			break;
		case PART_PAGE:
			admin_part(treedata->model, &treedata->iter, 
				  display_data->name);
			break;
		case BLOCK_PAGE:
			admin_block(treedata->model, &treedata->iter, 
				    display_data->name);
			break;
		case RESV_PAGE:
			admin_resv(treedata->model, &treedata->iter, 
				   display_data->name);
			break;
		case NODE_PAGE:
			admin_node(treedata->model, &treedata->iter, 
				   display_data->name);
			break;
		default:
			g_print("common admin got %d %d\n",
				display_data->extra,
				display_data->id);
		}
		break;
	default:
		g_print("common got %d %d\n", display_data->extra,
			display_data->id);
	}
	xfree(treedata);
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

extern int find_col(display_data_t *display_data, int type)
{
	int i = 0;

	while(display_data++) {
		if(display_data->id == -1)
			break;
		if(display_data->id == type)
			return i;
		i++;
	}
	return -1;
}

extern const char *find_col_name(display_data_t *display_data, int type)
{
	int i = 0;

	while(display_data++) {
		if(display_data->id == -1)
			break;
		if(display_data->id == type)
			return display_data->name;
		i++;
	}
	return NULL;
}

extern void *get_pointer(GtkTreeView *tree_view, GtkTreePath *path, int loc)
{
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	GtkTreeIter iter;
	void *ptr = NULL;
	
	if(!model) {
		g_error("error getting the model from the tree_view");
		return ptr;
	}
	
	if (!gtk_tree_model_get_iter(model, &iter, path)) {
		g_error("error getting iter from model");
		return ptr;
	}	
	gtk_tree_model_get(model, &iter, loc, &ptr, -1);
	return ptr;
}

extern void make_fields_menu(popup_info_t *popup_win, GtkMenu *menu,
			     display_data_t *display_data, int count)
{
	GtkWidget *menuitem = NULL;
	display_data_t *first_display_data = display_data;
	int i = 0;

	/* we don't want to display anything on the full info page */
	if(popup_win && popup_win->spec_info->type == INFO_PAGE)
		return;

	for(i=0; i<count; i++) {
		while(display_data++) {
			if(display_data->id == -1)
				break;
			if(!display_data->name)
				continue;
			if(display_data->id != i)
				continue;
		
			menuitem = gtk_check_menu_item_new_with_label(
				display_data->name); 
			
			gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(menuitem),
				display_data->show);
			if(popup_win) {
				display_data->user_data = popup_win;
				g_signal_connect(
					menuitem, "toggled",
					G_CALLBACK(_popup_state_changed), 
					display_data);
			} else {
				g_signal_connect(
					menuitem, "toggled",
					G_CALLBACK(_toggle_state_changed), 
					display_data);
			}
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
			break;
		}
		display_data = first_display_data;
	}
}

extern void make_options_menu(GtkTreeView *tree_view, GtkTreePath *path,
			      GtkMenu *menu, display_data_t *display_data)
{
	GtkWidget *menuitem = NULL;
	treedata_t *treedata = xmalloc(sizeof(treedata_t));
	treedata->model = gtk_tree_view_get_model(tree_view);
	if (!gtk_tree_model_get_iter(treedata->model, &treedata->iter, path)) {
		g_error("error getting iter from model\n");
		return;
	}	
	if(display_data->user_data)
		xfree(display_data->user_data);
		
	while(display_data++) {
		if(display_data->id == -1)
			break;
		if(!display_data->name)
			continue;
		
		display_data->user_data = treedata;
		menuitem = gtk_menu_item_new_with_label(display_data->name); 
		g_signal_connect(menuitem, "activate",
				 G_CALLBACK(_selected_page), 
				 display_data);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
	}
}

extern GtkScrolledWindow *create_scrolled_window()
{
	GtkScrolledWindow *scrolled_window = NULL;
	GtkWidget *table = NULL;
	table = gtk_table_new(1, 1, FALSE);

	gtk_container_set_border_width(GTK_CONTAINER(table), 10);	

	scrolled_window = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(
						      NULL, NULL));	
	gtk_container_set_border_width(GTK_CONTAINER(scrolled_window), 10);
    
	gtk_scrolled_window_set_policy(scrolled_window,
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);
    
	gtk_scrolled_window_add_with_viewport(scrolled_window, table);
	
	return scrolled_window;
}

extern GtkWidget *create_entry()
{
	GtkWidget *entry = gtk_entry_new();
	
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);

	return entry;
}

extern void create_page(GtkNotebook *notebook, display_data_t *display_data)
{
	GtkScrolledWindow *scrolled_window = create_scrolled_window();
	GtkWidget *event_box = NULL;
	GtkWidget *label = NULL;
	int err;
		
	event_box = gtk_event_box_new();
	gtk_event_box_set_above_child(GTK_EVENT_BOX(event_box), FALSE);
	g_signal_connect(G_OBJECT(event_box), "button-press-event",
			 G_CALLBACK(tab_pressed),
			 display_data);
	
	label = gtk_label_new(display_data->name);
	gtk_container_add(GTK_CONTAINER(event_box), label);
	gtk_widget_show(label);
	//(display_data->set_fields)(GTK_MENU(menu));
	if((err = gtk_notebook_append_page(GTK_NOTEBOOK(notebook), 
					   GTK_WIDGET(scrolled_window), 
					   event_box)) == -1) {
		g_error("Couldn't add page to notebook\n");
	}
	
	display_data->extra = err;

}

extern GtkTreeView *create_treeview(display_data_t *local)
{
	GtkTreeView *tree_view = GTK_TREE_VIEW(gtk_tree_view_new());

	local->user_data = NULL;
	g_signal_connect(G_OBJECT(tree_view), "button-press-event",
			 G_CALLBACK(row_clicked),
			 local);
	gtk_widget_show(GTK_WIDGET(tree_view));
	
	return tree_view;

}

extern GtkTreeView *create_treeview_2cols_attach_to_table(GtkTable *table)
{
	GtkTreeView *tree_view = GTK_TREE_VIEW(gtk_tree_view_new());
	GtkTreeStore *treestore = 
		gtk_tree_store_new(3, GTK_TYPE_STRING, 
				   GTK_TYPE_STRING, GTK_TYPE_STRING);
	GtkTreeViewColumn *col = gtk_tree_view_column_new();
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();

	gtk_table_attach_defaults(table,
				  GTK_WIDGET(tree_view),
				  0, 1, 0, 1);
	
	gtk_tree_view_set_model(tree_view, GTK_TREE_MODEL(treestore));

	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, 
					   "text", DISPLAY_NAME);
	gtk_tree_view_column_add_attribute(col, renderer, 
					   "font", DISPLAY_FONT);
	gtk_tree_view_column_set_title(col, "Name");
	gtk_tree_view_column_set_resizable(col, true);
	gtk_tree_view_column_set_expand(col, true);
	gtk_tree_view_append_column(tree_view, col);

	col = gtk_tree_view_column_new();
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, 
					   "text", DISPLAY_VALUE);
	gtk_tree_view_column_add_attribute(col, renderer, 
					   "font", DISPLAY_FONT);
	gtk_tree_view_column_set_title(col, "Value");
	gtk_tree_view_column_set_resizable(col, true);
	gtk_tree_view_column_set_expand(col, true);
	gtk_tree_view_append_column(tree_view, col);

	col = gtk_tree_view_column_new();
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_set_visible(col, false);
	gtk_tree_view_column_add_attribute(col, renderer, 
					   "text", DISPLAY_FONT);
	gtk_tree_view_append_column(tree_view, col);

       	g_object_unref(treestore);
	return tree_view;
}

extern GtkTreeStore *create_treestore(GtkTreeView *tree_view, 
				      display_data_t *display_data,
				      int count)
{
	GtkTreeStore *treestore = NULL;
	GType types[count];
	int i=0;
	
	/*set up the types defined in the display_data_t */
	for(i=0; i<count; i++) {
		types[display_data[i].id] = display_data[i].type;
	}
	
	treestore = gtk_tree_store_newv(count, types);
	if(!treestore) {
		g_print("Can't create treestore.\n");
		return NULL;
	}
	
	gtk_tree_view_set_model(tree_view, GTK_TREE_MODEL(treestore));
	for(i=1; i<count; i++) {
		if(!display_data[i].show) 
			continue;
		
		_add_col_to_treeview(tree_view, &display_data[i]);
		switch(display_data[i].type) {
		case G_TYPE_INT:
			gtk_tree_sortable_set_sort_func(
				GTK_TREE_SORTABLE(treestore), 
				display_data[i].id, 
				_sort_iter_compare_func_int,
				GINT_TO_POINTER(display_data[i].id), 
				NULL); 
			
			break;
		case G_TYPE_STRING:
			if(!strcasecmp(display_data[i].name, "Nodes")
			   || !strcasecmp(display_data[i].name, "Real Memory")
			   || !strcasecmp(display_data[i].name, "Tmp Disk")) {
				gtk_tree_sortable_set_sort_func(
					GTK_TREE_SORTABLE(treestore), 
					display_data[i].id, 
					_sort_iter_compare_func_nodes,
					GINT_TO_POINTER(display_data[i].id), 
					NULL); 
				break;
			} else if(!strcasecmp(display_data[i].name,
					      "BP List")) {
				gtk_tree_sortable_set_sort_func(
					GTK_TREE_SORTABLE(treestore), 
					display_data[i].id, 
					_sort_iter_compare_func_bp_list,
					GINT_TO_POINTER(display_data[i].id), 
					NULL); 
				break;
			} else {
				gtk_tree_sortable_set_sort_func(
					GTK_TREE_SORTABLE(treestore), 
					display_data[i].id, 
					_sort_iter_compare_func_char,
					GINT_TO_POINTER(display_data[i].id), 
					NULL); 
				break;
			}
		default:
			g_print("unknown type %d",
				(int)display_data[i].type);
		}
	}
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(treestore), 
					     1, 
					     GTK_SORT_ASCENDING);
	
	g_object_unref(treestore);

	return treestore;
}

extern void right_button_pressed(GtkTreeView *tree_view, 
				 GtkTreePath *path,
				 GdkEventButton *event, 
				 const display_data_t *display_data,
				 int type)
{
	if(event->button == 3) {
		GtkMenu *menu = GTK_MENU(gtk_menu_new());
	
		(display_data->set_menu)(tree_view, path, menu, type);
				
		gtk_widget_show_all(GTK_WIDGET(menu));
		gtk_menu_popup(menu, NULL, NULL, NULL, NULL,
			       (event != NULL) ? event->button : 0,
			       gdk_event_get_time((GdkEvent*)event));
	}
}

extern gboolean row_clicked(GtkTreeView *tree_view, GdkEventButton *event, 
			    const display_data_t *display_data)
{
	GtkTreePath *path = NULL;
	GtkTreeSelection *selection = NULL;
	gboolean did_something = FALSE;
	/*  right click? */
	if(!gtk_tree_view_get_path_at_pos(tree_view,
					  (gint) event->x, 
					  (gint) event->y,
					  &path, NULL, NULL, NULL)) {
		return did_something;
	}
	selection = gtk_tree_view_get_selection(tree_view);
	gtk_tree_selection_unselect_all(selection);
	gtk_tree_selection_select_path(selection, path);
	
	if(event->x <= 2) {	
		/* When you try to resize a column this event happens
		   for some reason.  Resizing always happens in the
		   first 2 of x so if that happens just return and
		   continue. */
		did_something = FALSE;
	} else if(event->x <= 20) {
		/* This should also be included with above since there
		   is no reason for us to handle this here since it is
		   already handled automatically. Just to make sure
		   we will keep it this way until 2.1 just so we
		   don't break anything. */
		if(!gtk_tree_view_expand_row(tree_view, path, FALSE))
			gtk_tree_view_collapse_row(tree_view, path);
		did_something = TRUE;
	} else if(event->button == 3) {
		right_button_pressed(tree_view, path, event, 
				     display_data, ROW_CLICKED);
		did_something = TRUE;
	} else if(!admin_mode)
		did_something = TRUE;
	gtk_tree_path_free(path);
	
	return did_something;
}

extern popup_info_t *create_popup_info(int type, int dest_type, char *title)
{
	GtkScrolledWindow *window = NULL;
	GtkBin *bin = NULL;
	GtkViewport *view = NULL;
	GtkWidget *popup = NULL;
	GtkWidget *label = NULL;
	GtkWidget *table = NULL;
	popup_info_t *popup_win = xmalloc(sizeof(popup_info_t));

	list_push(popup_list, popup_win);
	
	popup_win->spec_info = xmalloc(sizeof(specific_info_t));
	popup_win->spec_info->search_info =
		xmalloc(sizeof(sview_search_info_t));
	popup_win->spec_info->search_info->search_type = 0;
	popup_win->spec_info->search_info->gchar_data = NULL;
	popup_win->spec_info->search_info->int_data = NO_VAL;
	popup_win->spec_info->search_info->int_data2 = NO_VAL;

	popup_win->spec_info->type = type;
	popup_win->spec_info->title = xstrdup(title);
	popup_win->popup = gtk_dialog_new_with_buttons(
		title,
		GTK_WINDOW(main_window),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_REFRESH,
		GTK_RESPONSE_OK,
		GTK_STOCK_CLOSE,
		GTK_RESPONSE_CLOSE,
		NULL);

	popup_win->show_grid = 1;
	popup_win->toggled = 0;
	popup_win->force_refresh = 0;
	popup_win->type = dest_type;
	popup_win->not_found = false;
	gtk_window_set_default_size(GTK_WINDOW(popup_win->popup), 
				    600, 400);
	gtk_window_set_transient_for(GTK_WINDOW(popup_win->popup), NULL);
	popup = popup_win->popup;

	popup_win->event_box = gtk_event_box_new();
	label = gtk_label_new(popup_win->spec_info->title);
	gtk_container_add(GTK_CONTAINER(popup_win->event_box), label);
	
	g_signal_connect(G_OBJECT(popup_win->event_box),
			 "button-press-event",
			 G_CALLBACK(redo_popup),
			 popup_win);
	
	gtk_event_box_set_above_child(
		GTK_EVENT_BOX(popup_win->event_box), 
		FALSE);
		
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			   popup_win->event_box, FALSE, FALSE, 0);

	window = create_scrolled_window();
	gtk_scrolled_window_set_policy(window,
				       GTK_POLICY_NEVER,
				       GTK_POLICY_AUTOMATIC);
	bin = GTK_BIN(&window->container);
	view = GTK_VIEWPORT(bin->child);
	bin = GTK_BIN(&view->bin);
	popup_win->grid_table = GTK_TABLE(bin->child);
	popup_win->grid_button_list = NULL;

	table = gtk_table_new(1, 2, FALSE);
	
	gtk_table_attach(GTK_TABLE(table), GTK_WIDGET(window), 0, 1, 0, 1,
			 GTK_SHRINK, GTK_EXPAND | GTK_FILL,
			 0, 0);
	
	window = create_scrolled_window();
	bin = GTK_BIN(&window->container);
	view = GTK_VIEWPORT(bin->child);
	bin = GTK_BIN(&view->bin);
	popup_win->table = GTK_TABLE(bin->child);

	gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(window), 
				  1, 2, 0, 1);
	
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			   table, TRUE, TRUE, 0);
	
	g_signal_connect(G_OBJECT(popup_win->popup), "delete_event",
			 G_CALLBACK(delete_popup), 
			 popup_win->spec_info->title);
	g_signal_connect(G_OBJECT(popup_win->popup), "response",
			 G_CALLBACK(_handle_response), 
			 popup_win);
	
	gtk_widget_show_all(popup_win->popup);
	return popup_win;
}

extern void setup_popup_info(popup_info_t *popup_win, 
			     display_data_t *display_data, 
			     int cnt)
{
	int i = 0;
	
	popup_win->display_data = xmalloc(sizeof(display_data_t)*(cnt+2));
	for(i=0; i<cnt+1; i++) {
		memcpy(&popup_win->display_data[i], 
		       &display_data[i], 
		       sizeof(display_data_t));
	}
}

extern void redo_popup(GtkWidget *widget, GdkEventButton *event, 
		       popup_info_t *popup_win)
{
	if(event->button == 3) {
		GtkMenu *menu = GTK_MENU(gtk_menu_new());
		
		(popup_win->display_data->set_menu)(popup_win, 
						    NULL, 
						    menu, POPUP_CLICKED);
		
		gtk_widget_show_all(GTK_WIDGET(menu));
		gtk_menu_popup(menu, NULL, NULL, NULL, NULL,
			       (event != NULL) ? event->button : 0,
			       gdk_event_get_time((GdkEvent*)event));
	}
}

extern void destroy_search_info(void *arg)
{
	sview_search_info_t *search_info = (sview_search_info_t *)arg;
	if(search_info) {
		if(search_info->gchar_data)
			g_free(search_info->gchar_data);
		search_info->gchar_data = NULL;
		xfree(search_info);
		search_info = NULL;
	}
}

extern void destroy_specific_info(void *arg)
{
	specific_info_t *spec_info = (specific_info_t *)arg;
	if(spec_info) {
		xfree(spec_info->title);

		destroy_search_info(spec_info->search_info); 

		if(spec_info->display_widget) {
			gtk_widget_destroy(spec_info->display_widget);
			spec_info->display_widget = NULL;
		}
		xfree(spec_info);
	}
}

extern void destroy_popup_info(void *arg)
{
	popup_info_t *popup_win = (popup_info_t *)arg;
	
	if(popup_win) {
		*popup_win->running = 0;
		//g_print("locking destroy_popup_info\n");
		g_static_mutex_lock(&sview_mutex);
		//g_print("locked\n");
		/* these are all childern of each other so must 
		   be freed in this order */
		if(popup_win->grid_button_list) {
			list_destroy(popup_win->grid_button_list);
			popup_win->grid_button_list = NULL;
		}
		if(popup_win->table) {
			gtk_widget_destroy(GTK_WIDGET(popup_win->table));
			popup_win->table = NULL;
		}
		if(popup_win->grid_table) {
			gtk_widget_destroy(GTK_WIDGET(popup_win->grid_table));
			popup_win->grid_table = NULL;
		}
		if(popup_win->event_box) {
			gtk_widget_destroy(popup_win->event_box);
			popup_win->event_box = NULL;
		}
		if(popup_win->popup) {
			gtk_widget_destroy(popup_win->popup);
			popup_win->popup = NULL;
		}
		
		destroy_specific_info(popup_win->spec_info);
		xfree(popup_win->display_data);
		xfree(popup_win);
		g_static_mutex_unlock(&sview_mutex);
	}
	
}

extern gboolean delete_popup(GtkWidget *widget, GtkWidget *event, char *title)
{
	ListIterator itr = list_iterator_create(popup_list);
	popup_info_t *popup_win = NULL;
	
	while((popup_win = list_next(itr))) {
		if(popup_win->spec_info) {
			if(!strcmp(popup_win->spec_info->title, title)) {
				//g_print("removing %s\n", title);
				list_remove(itr);
				destroy_popup_info(popup_win);
				break;
			}
		}
	}
	list_iterator_destroy(itr);
	

	return FALSE;
}

extern void *popup_thr(popup_info_t *popup_win)
{
	void (*specifc_info) (popup_info_t *popup_win) = NULL;
	int running = 1;
	switch(popup_win->type) {
	case PART_PAGE:
		specifc_info = specific_info_part;
		break;
	case JOB_PAGE:
		specifc_info = specific_info_job;
		break;
	case NODE_PAGE:
		specifc_info = specific_info_node;
		break;
	case BLOCK_PAGE: 
		specifc_info = specific_info_block;
		break;
	case RESV_PAGE: 
		specifc_info = specific_info_resv;
		break;
	case SUBMIT_PAGE: 
	default:
		g_print("thread got unknown type %d\n", popup_win->type);
		return NULL;
	}
	/* this will switch to 0 when popup is closed. */
	popup_win->running = &running;
	/* when popup is killed toggled will be set to -1 */
	while(running) {
		//g_print("locking popup_thr\n");
		g_static_mutex_lock(&sview_mutex);
		//g_print("locked popup_thr\n");
		gdk_threads_enter();
		(specifc_info)(popup_win);
		gdk_flush();
		gdk_threads_leave();
		g_static_mutex_unlock(&sview_mutex);
		//g_print("done popup_thr\n");
		sleep(global_sleep_time);
	}	
	return NULL;
}

extern void remove_old(GtkTreeModel *model, int updated)
{
	GtkTreePath *path = gtk_tree_path_new_first();
	GtkTreeIter iter;
	int i;
	
	/* remove all old partitions */
	if (gtk_tree_model_get_iter(model, &iter, path)) {
		while(1) {
			gtk_tree_model_get(model, &iter, updated, &i, -1);
			if(!i) {
				if(!gtk_tree_store_remove(
					   GTK_TREE_STORE(model), 
					   &iter))
					break;
				else
					continue;
			}
			if(!gtk_tree_model_iter_next(model, &iter)) {
				break;
			}
		}
	}
	gtk_tree_path_free(path);
}

extern GtkWidget *create_pulldown_combo(display_data_t *display_data,
					int count)
{
	GtkListStore *store = NULL;
	GtkWidget *combo = NULL;
	GtkTreeIter iter;
	GtkCellRenderer *renderer = NULL;
	int i=0;
	
	store = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);
	for(i=0; i<count; i++) {
		if(display_data[i].id == -1)
			break;
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, display_data[i].id,
				   1, display_data[i].name, -1);
	}
	combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
       
	g_object_unref(store);	
	renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), renderer, TRUE);
	gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(combo), renderer,
				      "text", 1);
	
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
	return combo;
}

/*
 * str_tolower - convert string to all lower case
 * upper_str IN - upper case input string
 * RET - lower case version of upper_str, caller must be xfree
 */ 
extern char *str_tolower(char *upper_str)
{
	int i = strlen(upper_str) + 1;
	char *lower_str = xmalloc(i);

	for (i=0; upper_str[i]; i++)
		lower_str[i] = tolower((int) upper_str[i]);

	return lower_str;
}

extern char *get_reason()
{
	char *reason_str = NULL;
	int len = 0;
	GtkWidget *table = gtk_table_new(1, 2, FALSE);
	GtkWidget *label = gtk_label_new("Reason ");
	GtkWidget *entry = gtk_entry_new();
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"State change reason",
		GTK_WINDOW(main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_OK,
		GTK_RESPONSE_OK,
		GTK_STOCK_CANCEL,
		GTK_RESPONSE_CANCEL,
		NULL);
	int response = 0;
	char *user_name = NULL;
	char time_buf[64], time_str[32];
	time_t now = time(NULL);
			
	gtk_container_set_border_width(GTK_CONTAINER(table), 10);
	
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			   table, FALSE, FALSE, 0);
	
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);	
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, 0, 1);
	
	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK)
	{
		reason_str = xstrdup(gtk_entry_get_text(GTK_ENTRY(entry)));
		len = strlen(reason_str) - 1;
		if(len == -1) {
			xfree(reason_str);
			reason_str = NULL;
			goto end_it;
		}
		/* Append user, date and time */
		xstrcat(reason_str, " [");
		user_name = getlogin();
		if (user_name)
			xstrcat(reason_str, user_name);
		else {
			sprintf(time_buf, "%d", getuid());
			xstrcat(reason_str, time_buf);
		}
		slurm_make_time_str(&now, time_str, sizeof(time_str));
		snprintf(time_buf, sizeof(time_buf), "@%s]", time_str); 
		xstrcat(reason_str, time_buf);
	} else 
		reason_str = xstrdup("cancelled");
end_it:
	gtk_widget_destroy(popup);	
	
	return reason_str;
}

extern void display_edit_note(char *edit_note)
{
	GError *error = NULL;
	int msg_id = 0;
	gtk_statusbar_pop(GTK_STATUSBAR(main_statusbar), STATUS_ADMIN_EDIT);
	msg_id = gtk_statusbar_push(GTK_STATUSBAR(main_statusbar), 
				    STATUS_ADMIN_EDIT,
				    edit_note);
	if (!g_thread_create(_editing_thr, GINT_TO_POINTER(msg_id),
			     FALSE, &error))
	{
		g_printerr ("Failed to create edit thread: %s\n",
			    error->message);
	}
	return;
}

extern void add_display_treestore_line(int update,
				       GtkTreeStore *treestore,
				       GtkTreeIter *iter,
				       const char *name, char *value)
{
	if(!name) {
		g_print("error, name = %s and value = %s\n",
			name, value);
		return;
	}
	if(update) {
		char *display_name = NULL;
		GtkTreePath *path = gtk_tree_path_new_first();
		gtk_tree_model_get_iter(GTK_TREE_MODEL(treestore), iter, path);
	
		while(1) {
			/* search for the jobid and check to see if 
			   it is in the list */
			gtk_tree_model_get(GTK_TREE_MODEL(treestore), iter,
					   DISPLAY_NAME, 
					   &display_name, -1);
			if(!strcmp(display_name, name)) {
				/* update with new info */
				g_free(display_name);
				goto found;
			}
			g_free(display_name);
				
			if(!gtk_tree_model_iter_next(GTK_TREE_MODEL(treestore),
						     iter)) {
				return;
			}
		}
		
	} else {
		gtk_tree_store_append(treestore, iter, NULL);
	}
found:
	gtk_tree_store_set(treestore, iter,
			   DISPLAY_NAME, name, 
			   DISPLAY_VALUE, value,
			   -1);

	return;
}

extern void add_display_treestore_line_with_font(
	int update,
	GtkTreeStore *treestore,
	GtkTreeIter *iter,
	const char *name, char *value,
	char *font)
{
	if(!name) {
		g_print("error, name = %s and value = %s\n",
			name, value);
		return;
	}
	if(update) {
		char *display_name = NULL;
		GtkTreePath *path = gtk_tree_path_new_first();
		gtk_tree_model_get_iter(GTK_TREE_MODEL(treestore), iter, path);
	
		while(1) {
			/* search for the jobid and check to see if 
			   it is in the list */
			gtk_tree_model_get(GTK_TREE_MODEL(treestore), iter,
					   DISPLAY_NAME, 
					   &display_name, -1);
			if(!strcmp(display_name, name)) {
				/* update with new info */
				g_free(display_name);
				goto found;
			}
			g_free(display_name);
				
			if(!gtk_tree_model_iter_next(GTK_TREE_MODEL(treestore),
						     iter)) {
				return;
			}
		}
		
	} else {
		gtk_tree_store_append(treestore, iter, NULL);
	}
found:
	gtk_tree_store_set(treestore, iter,
			   DISPLAY_NAME, name, 
			   DISPLAY_VALUE, value,
			   DISPLAY_FONT, font,
			   -1);

	return;
}
