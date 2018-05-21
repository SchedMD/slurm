/*****************************************************************************\
 *  admin_info.c - Functions related to admin display
 *  mode of sview.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/sview/sview.h"

#define _DEBUG 0

enum {
	SORTID_POS = POS_LOC,
	SORTID_PARTITION,
	SORTID_AVAIL,
	SORTID_TIMELIMIT,
	SORTID_NODES,
	SORTID_NODELIST,
	SORTID_CNT
};

static display_data_t display_data_admin[] = {
	{G_TYPE_INT, SORTID_POS, NULL, false, -1},
	{G_TYPE_STRING, SORTID_PARTITION, "PARTITION", true, -1},
	{G_TYPE_STRING, SORTID_AVAIL, "AVAIL", true, -1},
	{G_TYPE_STRING, SORTID_TIMELIMIT, "TIMELIMIT", true, -1},
	{G_TYPE_STRING, SORTID_NODES, "NODES", true, -1},
	{G_TYPE_STRING, SORTID_NODELIST, "NODELIST", true, -1},
	{G_TYPE_NONE, -1, NULL, false, -1}};

static display_data_t options_data_admin[] = {
	{G_TYPE_STRING, JOB_PAGE, "Jobs", true, -1},
	{G_TYPE_STRING, NODE_PAGE, "Nodes", true, -1},
	{G_TYPE_STRING, SUBMIT_PAGE, "Job Submit", true, -1},
	{G_TYPE_NONE, -1, NULL, false, -1}
};

static display_data_t *local_display_data = NULL;

extern void get_info_admin(GtkTable *table, display_data_t *display_data)
{
	local_display_data = display_data;
}


extern void set_menus_admin(void *arg, GtkTreePath *path,
			    GtkMenu *menu, int type)
{
	GtkTreeView *tree_view = (GtkTreeView *)arg;
	popup_info_t *popup_win = (popup_info_t *)arg;
	switch(type) {
	case TAB_CLICKED:
		make_fields_menu(NULL, menu, display_data_admin, SORTID_CNT);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_admin);
		break;
	case POPUP_CLICKED:
		make_fields_menu(popup_win, menu,
				 popup_win->display_data, SORTID_CNT);
		break;
	default:
		g_error("UNKNOWN type %d given to set_fields\n", type);
	}
}

extern void row_clicked_admin(GtkTreeView *tree_view,
			      GtkTreePath *path,
			      GtkTreeViewColumn *column,
			      gpointer user_data)
{
	int line = get_row_number(tree_view, path);
	GtkWidget *popup = NULL;
	GtkWidget *label = NULL;
	char *info = NULL;
	if (line == -1) {
		g_error("problem getting line number");
		return;
	}

	popup = gtk_dialog_new();

	gtk_window_set_type_hint(GTK_WINDOW(popup),
				 GDK_WINDOW_TYPE_HINT_NORMAL);
	label = gtk_label_new(info);
	gtk_box_pack_end(GTK_BOX(GTK_DIALOG(popup)->vbox),
			 label, true, true, 0);
	xfree(info);
	gtk_widget_show(label);

	gtk_widget_show(popup);

}
