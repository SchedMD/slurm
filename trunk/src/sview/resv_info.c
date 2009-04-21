/*****************************************************************************\
 *  resv_info.c - Functions related to advanced reservation display 
 *  mode of sview.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>
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

#include "src/common/uid.h"
#include "src/sview/sview.h"
#include "src/common/parse_time.h"
 
#define _DEBUG 0

/* These need to be in alpha order (except POS and CNT) */
enum { 
	SORTID_POS = POS_LOC,
	SORTID_ACCOUNTS,
	SORTID_END_TIME,
	SORTID_FEATURES,
	SORTID_FLAGS,
	SORTID_NAME,
	SORTID_NODE_CNT,
	SORTID_NODE_LIST,
	SORTID_PARTITION,
	SORTID_START_TIME,
	SORTID_USERS,
	SORTID_CNT
};

/* extra field here is for choosing the type of edit you that will
 * take place.  If you choose EDIT_MODEL (means only display a set of
 * known options) create it in function create_model_*.  
 */

static display_data_t display_data_resv[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, EDIT_NONE, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_ACCOUNTS,   "Accounts", FALSE, EDIT_TEXTBOX, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_END_TIME,   "EndTime", FALSE, EDIT_TEXTBOX, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_FEATURES,   "Features", FALSE, EDIT_TEXTBOX, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_FLAGS,      "Flags", FALSE, EDIT_NONE, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_NAME,       "Name", TRUE, EDIT_NONE, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_INT,    SORTID_NODE_CNT,   "Nodes", TRUE, EDIT_TEXTBOX, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_NODE_LIST,  "NodeList", TRUE, EDIT_TEXTBOX, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_PARTITION,  "Partition", FALSE, EDIT_TEXTBOX, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_START_TIME, "StartTime", FALSE, EDIT_TEXTBOX, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_USERS,      "Users", FALSE, EDIT_TEXTBOX, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_NONE, -1, NULL, FALSE, EDIT_NONE}
};

static display_data_t *local_display_data = NULL;

extern void refresh_resv(GtkAction *action, gpointer user_data)
{
	popup_info_t *popup_win = (popup_info_t *)user_data;
	xassert(popup_win != NULL);
	xassert(popup_win->spec_info != NULL);
	xassert(popup_win->spec_info->title != NULL);
	popup_win->force_refresh = 1;
	specific_info_resv(popup_win);
}

extern GtkListStore *create_model_resv(int type)
{
	return (GtkListStore *) NULL;
}

extern void admin_edit_resv(GtkCellRendererText *cell,
			    const char *path_string,
			    const char *new_text,
			    gpointer data)
{
}

extern void get_info_resv(GtkTable *table, display_data_t *display_data)
{
	int error_code = SLURM_SUCCESS;
	static int view = -1;
	static reserve_info_msg_t *resv_info_ptr = NULL;
	reserve_info_t *resv;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	List info_list = NULL;
	int i = 0, j = 0;
		
	if(display_data)
		local_display_data = display_data;
	if(!table) {
		display_data_resv->set_menu = local_display_data->set_menu;
		return;
	}
	if(display_widget && toggled) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
		goto display_it;
	}

	error_code = get_new_info_resv(&resv_info_ptr, force_refresh);
	if ((error_code != SLURM_SUCCESS) &&
	    (error_code != SLURM_NO_CHANGE_IN_DATA)) {
		if(view == ERROR_VIEW)
			goto end_it;
		if(display_widget)
			gtk_widget_destroy(display_widget);
		view = ERROR_VIEW;
		sprintf(error_char, "slurm_load_reservations: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(table, label, 0, 1, 0, 1); 
		gtk_widget_show(label);	
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
		goto end_it;
	}

display_it:
	for (i=0; i<resv_info_ptr->record_count; i++) {
		resv = &resv_info_ptr->reservation_array[i];
		j = 0;
		while (resv->node_inx[j] >= 0) {
//FIXME: Need to capure the color here?
			(void)        change_grid_color(grid_button_list,
							resv->node_inx[j],
							resv->node_inx[j+1],
							i);
			j += 2;
		}
	}

	if(view == ERROR_VIEW && display_widget) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
	}

	if(!display_widget) {
		tree_view = create_treeview(local_display_data);
				
		display_widget = gtk_widget_ref(GTK_WIDGET(tree_view));
		gtk_table_attach_defaults(GTK_TABLE(table), 
					  GTK_WIDGET(tree_view),
					  0, 1, 0, 1); 
		/* since this function sets the model of the tree_view 
		 * to the treestore we don't really care about 
		 * the return value */
		create_treestore(tree_view, display_data_resv, SORTID_CNT);
	}

	view = INFO_VIEW;
//	_update_info_job(info_list, GTK_TREE_VIEW(display_widget));

end_it:
	toggled = FALSE;
	force_refresh = FALSE;
	
	return;
}

extern int get_new_info_resv(reserve_info_msg_t **info_ptr, 
			     int force)
{
	static reserve_info_msg_t *resv_info_ptr = NULL, *new_resv_ptr = NULL;
	int error_code = SLURM_NO_CHANGE_IN_DATA;
	time_t now = time(NULL);
	static time_t last;
	static bool changed = 0;
		
	if(!force && ((now - last) < global_sleep_time)) {
		error_code = SLURM_NO_CHANGE_IN_DATA;
		*info_ptr = resv_info_ptr;
		if(changed) 
			return SLURM_SUCCESS;
		return error_code;
	}
	last = now;
	if (resv_info_ptr) {
		error_code = slurm_load_reservations(resv_info_ptr->last_update,
						     &new_resv_ptr);
		if (error_code == SLURM_SUCCESS) {
			slurm_free_reservation_info_msg(resv_info_ptr);
			changed = 1;
		} else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_resv_ptr = resv_info_ptr;
			changed = 0;
		}
	} else {
		error_code = slurm_load_reservations((time_t) NULL, 
						     &new_resv_ptr);
		changed = 1;
	}
	resv_info_ptr = new_resv_ptr;
	*info_ptr = new_resv_ptr;
	return error_code;
}

extern void specific_info_resv(popup_info_t *popup_win)
{
}

extern void set_menus_resv(void *arg, GtkTreePath *path, 
			   GtkMenu *menu, int type)
{
}

#if 0
static void _layout_job_record(GtkTreeView *treeview, 
			       sview_job_info_t *sview_job_info_ptr, 
			       int update)
{
	char *nodes = NULL, *reason = NULL, *uname = NULL;
	char tmp_char[50];
	time_t now_time = time(NULL);
	job_info_t *job_ptr = sview_job_info_ptr->job_ptr;
	struct group *group_info = NULL;
	uint16_t term_sig = 0;

	GtkTreeIter iter;
	GtkTreeStore *treestore = 
		GTK_TREE_STORE(gtk_tree_view_get_model(treeview));

	if(!treestore)
		return;

	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_job,
						 SORTID_NAME), 
				   job_ptr->name);
}

static void _update_job_record(sview_job_info_t *sview_job_info_ptr, 
			       GtkTreeStore *treestore,
			       GtkTreeIter *iter)
{

	gtk_tree_store_set(treestore, iter, SORTID_NAME, job_ptr->name, -1);
}

#endif
